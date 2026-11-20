#include "engine/PresetArchive.h"

#include <cstring>
#include <set>

#include <miniz.h>

#include "engine/Sha256.h"

namespace dw {

namespace {

constexpr const char* kManifest = "manifest.json";
constexpr const char* kSamplesDir = "samples/";

bool startsWith(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

std::vector<const SampleSlot*> slotsOf(const Preset& p) {
    std::vector<const SampleSlot*> out;
    if (p.sampleA) out.push_back(&*p.sampleA);
    if (p.sampleB) out.push_back(&*p.sampleB);
    return out;
}

bool validExtraName(const std::string& name) {
    return !name.empty() && name != kManifest && !startsWith(name, kSamplesDir) && !startsWith(name, "/") &&
           name.find('\\') == std::string::npos && name.find("..") == std::string::npos;
}

// Owns a miniz writer for the duration of a write.
struct Writer {
    mz_zip_archive zip;
    bool open = false;
    Writer() {
        std::memset(&zip, 0, sizeof(zip));
        open = mz_zip_writer_init_heap(&zip, 0, 0) != 0;
    }
    ~Writer() {
        if (open) mz_zip_writer_end(&zip);
    }
    bool add(const std::string& name, const Bytes& data, mz_uint level) {
        return mz_zip_writer_add_mem(&zip, name.c_str(), data.data(), data.size(), level) != 0;
    }
};

struct Reader {
    mz_zip_archive zip;
    bool open = false;
    explicit Reader(const Bytes& bytes) {
        std::memset(&zip, 0, sizeof(zip));
        open = !bytes.empty() && mz_zip_reader_init_mem(&zip, bytes.data(), bytes.size(), 0) != 0;
    }
    ~Reader() {
        if (open) mz_zip_reader_end(&zip);
    }

    // Reads one file. Returns false and sets `error` when it is missing, too large or damaged.
    bool read(const std::string& name, std::size_t limit, Bytes& out, std::string& error) {
        const int index = mz_zip_reader_locate_file(&zip, name.c_str(), nullptr, 0);
        if (index < 0) {
            error = "file '" + name + "' is missing in the archive";
            return false;
        }
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, static_cast<mz_uint>(index), &stat)) {
            error = "cannot read the entry '" + name + "'";
            return false;
        }
        if (stat.m_uncomp_size > limit) {
            error = "file '" + name + "' is too large";
            return false;
        }
        out.assign(static_cast<std::size_t>(stat.m_uncomp_size), 0);
        if (!out.empty() &&
            !mz_zip_reader_extract_to_mem(&zip, static_cast<mz_uint>(index), out.data(), out.size(), 0)) {
            error = "file '" + name + "' is damaged";
            return false;
        }
        return true;
    }
};

}  // namespace

ArchiveWriteResult writePresetArchive(const PresetArchive& archive) {
    for (const auto& entry : archive.extras)
        if (!validExtraName(entry.first)) return {{}, "extra file name not allowed: '" + entry.first + "'"};

    // Every sample the preset uses must be present and match its hash.
    std::set<std::string> used;
    for (const SampleSlot* slot : slotsOf(archive.preset)) {
        const auto it = archive.samples.find(slot->file);
        if (it == archive.samples.end()) return {{}, "sample file '" + slot->file + "' is missing"};
        if (sha256Hex(it->second) != slot->sha256)
            return {{}, "sample '" + slot->file + "' does not match its SHA-256"};
        used.insert(slot->file);
    }

    Writer w;
    if (!w.open) return {{}, "cannot create the archive"};
    const std::string manifest = presetToJson(archive.preset);
    if (!w.add(kManifest, Bytes(manifest.begin(), manifest.end()), MZ_DEFAULT_LEVEL))
        return {{}, "cannot write the manifest"};
    for (const auto& file : used)  // samples are stored without compression
        if (!w.add(file, archive.samples.at(file), MZ_NO_COMPRESSION)) return {{}, "cannot write '" + file + "'"};
    for (const auto& [name, data] : archive.extras)
        if (!w.add(name, data, MZ_DEFAULT_LEVEL)) return {{}, "cannot write '" + name + "'"};

    void* buffer = nullptr;
    std::size_t size = 0;
    if (!mz_zip_writer_finalize_heap_archive(&w.zip, &buffer, &size)) return {{}, "cannot finish the archive"};
    Bytes out(static_cast<const std::uint8_t*>(buffer), static_cast<const std::uint8_t*>(buffer) + size);
    mz_free(buffer);
    return {std::move(out), {}};
}

ArchiveReadResult readPresetArchive(const Bytes& zip) {
    Reader r(zip);
    if (!r.open) return {std::nullopt, "not a valid ZIP archive"};

    std::string error;
    Bytes manifest;
    if (!r.read(kManifest, kMaxManifestBytes, manifest, error)) return {std::nullopt, error};
    auto parsed = presetFromJson(std::string(manifest.begin(), manifest.end()));
    if (!parsed.preset) return {std::nullopt, "manifest.json: " + parsed.error};

    PresetArchive archive;
    archive.preset = std::move(*parsed.preset);

    for (const SampleSlot* slot : slotsOf(archive.preset)) {
        auto it = archive.samples.find(slot->file);
        if (it == archive.samples.end()) {
            Bytes data;
            if (!r.read(slot->file, kMaxSampleBytes, data, error)) return {std::nullopt, error};
            it = archive.samples.emplace(slot->file, std::move(data)).first;
        }
        if (sha256Hex(it->second) != slot->sha256)
            return {std::nullopt, "sample '" + slot->file + "' does not match its SHA-256"};
    }

    // Top-level files besides the manifest (the plugin state's instance.json, for example).
    const mz_uint count = mz_zip_reader_get_num_files(&r.zip);
    for (mz_uint i = 0; i < count && archive.extras.size() < kMaxExtraFiles; ++i) {
        if (mz_zip_reader_is_file_a_directory(&r.zip, i)) continue;
        char name[256];
        if (mz_zip_reader_get_filename(&r.zip, i, name, sizeof(name)) == 0) continue;
        const std::string file = name;
        if (file == kManifest || file.find('/') != std::string::npos) continue;
        Bytes data;
        if (!r.read(file, kMaxExtraBytes, data, error)) return {std::nullopt, error};
        archive.extras.emplace(file, std::move(data));
    }
    return {std::move(archive), {}};
}

}  // namespace dw
