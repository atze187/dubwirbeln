#include <string>

#include <catch2/catch_test_macros.hpp>

#include "engine/Sha256.h"

using namespace dw;

namespace {
std::string hashOf(const std::string& s) { return sha256Hex(reinterpret_cast<const std::uint8_t*>(s.data()), s.size()); }
}  // namespace

// Reference values from Python's hashlib.
TEST_CASE("SHA-256 of the empty input", "[sha256]") {
    CHECK(hashOf("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST_CASE("SHA-256 of abc", "[sha256]") {
    CHECK(hashOf("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE("SHA-256 of a two-block message", "[sha256]") {
    CHECK(hashOf("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST_CASE("SHA-256 around the padding boundaries", "[sha256]") {
    CHECK(hashOf(std::string(55, 'a')) == "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
    CHECK(hashOf(std::string(56, 'a')) == "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
    CHECK(hashOf(std::string(64, 'a')) == "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
}

TEST_CASE("SHA-256 of a million a", "[sha256]") {
    CHECK(hashOf(std::string(1000000, 'a')) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("the vector overload hashes the same bytes", "[sha256]") {
    const std::vector<std::uint8_t> bytes{'a', 'b', 'c'};
    CHECK(sha256Hex(bytes) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}
