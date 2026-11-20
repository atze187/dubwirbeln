#include <cstring>
#include <string>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <miniz.h>
#include <nlohmann/json.hpp>

#include "engine/PresetArchive.h"
#include "engine/Sha256.h"

using Catch::Approx;
using nlohmann::json;
using namespace dw;

namespace {

Bytes pseudoBytes(size_t n, std::uint32_t seed) {
    Bytes b(n);
    std::uint32_t x = seed;
    for (auto& v : b) {
        x = x * 1664525u + 1013904223u;
        v = static_cast<std::uint8_t>(x >> 24);
    }
    return b;
}

Bytes text(const std::string& s) { return Bytes(s.begin(), s.end()); }

SampleSlot slotFor(const std::string& name, const Bytes& content, const std::string& ext = "wav") {
    SampleSlot s;
    s.name = name + "." + ext;
    s.sha256 = sha256Hex(content);
    s.file = "samples/" + name + "." + ext;
    s.tuneSemitones = 2.0;
    s.start = 0.1;
    s.end = 0.9;
    s.levelDb = -3.0;
    return s;
}

struct Fixture {
    Bytes kick = pseudoBytes(5000, 1);
    Bytes snare = pseudoBytes(7000, 2);
    PresetArchive archive;
    Fixture() {
        archive.preset = defaultPreset(321);
        archive.preset.name = "Container test";
        archive.preset.rollType = RollType::Rush;
        archive.preset.sampleA = slotFor("kick", kick);
        archive.preset.sampleB = slotFor("snare", snare, "flac");
        archive.samples["samples/kick.wav"] = kick;
        archive.samples["samples/snare.flac"] = snare;
    }
};

// Builds a ZIP with miniz directly, to craft broken archives.
Bytes makeZip(const FileMap& files) {
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    REQUIRE(mz_zip_writer_init_heap(&zip, 0, 0));
    for (const auto& [name, data] : files)
        REQUIRE(mz_zip_writer_add_mem(&zip, name.c_str(), data.data(), data.size(), MZ_DEFAULT_LEVEL));
    void* buf = nullptr;
    size_t size = 0;
    REQUIRE(mz_zip_writer_finalize_heap_archive(&zip, &buf, &size));
    Bytes out(static_cast<const std::uint8_t*>(buf), static_cast<const std::uint8_t*>(buf) + size);
    mz_zip_writer_end(&zip);
    mz_free(buf);
    return out;
}

struct ZipView {
    mz_zip_archive zip;
    explicit ZipView(const Bytes& b) {
        std::memset(&zip, 0, sizeof(zip));
        REQUIRE(mz_zip_reader_init_mem(&zip, b.data(), b.size(), 0));
    }
    ~ZipView() { mz_zip_reader_end(&zip); }
    // -1 when the file is not in the archive, else its compression method (0 = stored, 8 = deflate)
    int method(const char* name) {
        const int index = mz_zip_reader_locate_file(&zip, name, nullptr, 0);
        if (index < 0) return -1;
        mz_zip_archive_file_stat st;
        REQUIRE(mz_zip_reader_file_stat(&zip, static_cast<mz_uint>(index), &st));
        return static_cast<int>(st.m_method);
    }
};

Bytes manifestOf(const Preset& p) { return text(presetToJson(p)); }

}  // namespace

TEST_CASE("an archive round-trips the preset and its samples", "[archive]") {
    Fixture f;
    const auto written = writePresetArchive(f.archive);
    INFO(written.error);
    REQUIRE(written.ok());
    const auto r = readPresetArchive(written.bytes);
    INFO(r.error);
    REQUIRE(r.archive.has_value());
    CHECK(json::parse(presetToJson(r.archive->preset)) == json::parse(presetToJson(f.archive.preset)));
    CHECK(r.archive->preset.name == "Container test");
    CHECK(r.archive->preset.humanizeSeed == 321u);
    REQUIRE(r.archive->samples.size() == 2);
    CHECK(r.archive->samples.at("samples/kick.wav") == f.kick);
    CHECK(r.archive->samples.at("samples/snare.flac") == f.snare);
}

TEST_CASE("a preset without samples round-trips", "[archive]") {
    PresetArchive a;
    a.preset = defaultPreset(5);
    const auto written = writePresetArchive(a);
    REQUIRE(written.ok());
    const auto r = readPresetArchive(written.bytes);
    REQUIRE(r.archive.has_value());
    CHECK(r.archive->samples.empty());
    CHECK(r.archive->preset.humanizeSeed == 5u);
}

TEST_CASE("the manifest is deflated and the samples are stored", "[archive][layout]") {
    Fixture f;
    f.archive.preset.metadataJson = R"({"note":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"})";
    const auto written = writePresetArchive(f.archive);
    REQUIRE(written.ok());
    ZipView view(written.bytes);
    CHECK(view.method("manifest.json") == MZ_DEFLATED);
    CHECK(view.method("samples/kick.wav") == 0);
    CHECK(view.method("samples/snare.flac") == 0);
}

TEST_CASE("extra files are written and read back", "[archive]") {
    Fixture f;
    f.archive.extras["instance.json"] = text(R"({"hello":"world"})");
    const auto written = writePresetArchive(f.archive);
    REQUIRE(written.ok());
    const auto r = readPresetArchive(written.bytes);
    REQUIRE(r.archive.has_value());
    REQUIRE(r.archive->extras.size() == 1);
    CHECK(r.archive->extras.at("instance.json") == text(R"({"hello":"world"})"));
}

TEST_CASE("samples the preset does not reference are not written", "[archive]") {
    Fixture f;
    f.archive.samples["samples/unused.wav"] = pseudoBytes(100, 7);
    const auto written = writePresetArchive(f.archive);
    REQUIRE(written.ok());
    ZipView view(written.bytes);
    CHECK(view.method("samples/unused.wav") == -1);
    CHECK(view.method("samples/kick.wav") == 0);
}

TEST_CASE("both slots may use the same sample file", "[archive]") {
    Fixture f;
    f.archive.preset.sampleB = f.archive.preset.sampleA;
    f.archive.samples.erase("samples/snare.flac");
    const auto written = writePresetArchive(f.archive);
    REQUIRE(written.ok());
    const auto r = readPresetArchive(written.bytes);
    REQUIRE(r.archive.has_value());
    CHECK(r.archive->samples.size() == 1);
}

TEST_CASE("writing fails when a sample is missing or does not match its hash", "[archive][errors]") {
    Fixture f;
    auto missing = f.archive;
    missing.samples.erase("samples/kick.wav");
    const auto a = writePresetArchive(missing);
    CHECK_FALSE(a.ok());
    CHECK(a.bytes.empty());

    auto changed = f.archive;
    changed.samples["samples/kick.wav"][10] ^= 0x55;
    CHECK_FALSE(writePresetArchive(changed).ok());
}

TEST_CASE("writing refuses extra files that would clash with the layout", "[archive][errors]") {
    Fixture f;
    auto a = f.archive;
    a.extras["manifest.json"] = text("{}");
    CHECK_FALSE(writePresetArchive(a).ok());
    a = f.archive;
    a.extras["samples/other.wav"] = text("x");
    CHECK_FALSE(writePresetArchive(a).ok());
    a = f.archive;
    a.extras["../evil.json"] = text("x");
    CHECK_FALSE(writePresetArchive(a).ok());
}

TEST_CASE("input that is not a valid archive is rejected", "[archive][errors]") {
    CHECK_FALSE(readPresetArchive({}).archive.has_value());
    CHECK_FALSE(readPresetArchive(pseudoBytes(1000, 3)).archive.has_value());
    const auto r = readPresetArchive(text("PK-but-not-really"));
    CHECK_FALSE(r.archive.has_value());
    CHECK_FALSE(r.error.empty());
}

TEST_CASE("an archive without a manifest is rejected", "[archive][errors]") {
    CHECK_FALSE(readPresetArchive(makeZip({{"samples/kick.wav", pseudoBytes(10, 1)}})).archive.has_value());
}

TEST_CASE("an invalid manifest is rejected with its message", "[archive][errors]") {
    const auto r = readPresetArchive(makeZip({{"manifest.json", text("{\"name\":\"x\"}")}}));
    CHECK_FALSE(r.archive.has_value());
    CHECK_FALSE(r.error.empty());
    CHECK_FALSE(readPresetArchive(makeZip({{"manifest.json", text("not json")}})).archive.has_value());
}

TEST_CASE("a referenced sample that is missing is rejected", "[archive][errors]") {
    Fixture f;
    const auto r = readPresetArchive(makeZip({{"manifest.json", manifestOf(f.archive.preset)},
                                              {"samples/kick.wav", f.kick}}));  // snare.flac missing
    CHECK_FALSE(r.archive.has_value());
    CHECK_FALSE(r.error.empty());
}

TEST_CASE("a sample that does not match its hash is rejected", "[archive][errors]") {
    Fixture f;
    Bytes tampered = f.kick;
    tampered[100] ^= 0xff;
    const auto r = readPresetArchive(makeZip({{"manifest.json", manifestOf(f.archive.preset)},
                                              {"samples/kick.wav", tampered},
                                              {"samples/snare.flac", f.snare}}));
    CHECK_FALSE(r.archive.has_value());
    CHECK_FALSE(r.error.empty());
}

TEST_CASE("reserved folders and unknown entries are ignored", "[archive]") {
    Fixture f;
    const auto r = readPresetArchive(makeZip({{"manifest.json", manifestOf(f.archive.preset)},
                                              {"samples/kick.wav", f.kick},
                                              {"samples/snare.flac", f.snare},
                                              {"preview/audio.ogg", pseudoBytes(50, 5)}}));
    INFO(r.error);
    REQUIRE(r.archive.has_value());
    CHECK(r.archive->samples.size() == 2);
}

TEST_CASE("an oversized manifest is rejected", "[archive][errors]") {
    Fixture f;
    Bytes big(kMaxManifestBytes + 1, ' ');
    const auto text0 = manifestOf(f.archive.preset);
    std::copy(text0.begin(), text0.end(), big.begin());
    CHECK_FALSE(readPresetArchive(makeZip({{"manifest.json", big}})).archive.has_value());
}
