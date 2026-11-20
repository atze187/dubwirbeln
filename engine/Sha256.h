#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dw {

// SHA-256 as lowercase hex (64 characters). Samples are identified by this hash (spec Parts 11, 12).
std::string sha256Hex(const std::uint8_t* data, std::size_t size);

inline std::string sha256Hex(const std::vector<std::uint8_t>& bytes) { return sha256Hex(bytes.data(), bytes.size()); }

}  // namespace dw
