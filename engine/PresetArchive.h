#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "engine/Preset.h"

namespace dw {

using Bytes = std::vector<std::uint8_t>;
using FileMap = std::map<std::string, Bytes>;  // archive path -> file content

// Limits when reading an archive (protection against damaged or hostile files).
constexpr std::size_t kMaxManifestBytes = 4u << 20;
constexpr std::size_t kMaxExtraBytes = 1u << 20;
constexpr std::size_t kMaxSampleBytes = 512u << 20;
constexpr std::size_t kMaxExtraFiles = 16;

// A .dwpreset (spec Part 11): a ZIP with `manifest.json` (the preset, Deflate) and the sample files
// under `samples/` (stored without compression). `extras` are further top-level files such as
// the plugin state's `instance.json`; they are written and read as they are.
struct PresetArchive {
    Preset preset;
    FileMap samples;  // keyed by the slot's `file`, e.g. "samples/kick.wav"
    FileMap extras;   // not "manifest.json", not below "samples/"
};

struct ArchiveWriteResult {
    Bytes bytes;
    std::string error;  // set when writing failed
    bool ok() const { return error.empty(); }
};

// Fails when a slot's file is missing, or its content does not match the slot's SHA-256.
ArchiveWriteResult writePresetArchive(const PresetArchive& archive);

struct ArchiveReadResult {
    std::optional<PresetArchive> archive;
    std::string error;  // set when `archive` is empty
};

// Reads the manifest and exactly the samples it references, each checked against its SHA-256.
ArchiveReadResult readPresetArchive(const Bytes& zip);

}  // namespace dw
