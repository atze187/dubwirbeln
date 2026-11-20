#pragma once

#include <cstdint>

namespace dw {

// PCG32 (XSH-RR). This is the engine's own, fully specified generator: the C++ <random>
// distributions are implementation-defined and would give different rolls on different
// platforms, which breaks the determinism required by the spec (Part 6, Part 10).
class Rng {
public:
    // Fixed stream used for every engine seed.
    static constexpr std::uint64_t kStream = 54u;

    explicit Rng(std::uint32_t seed) : Rng(static_cast<std::uint64_t>(seed), kStream) {}

    // Reference seeding (pcg32_srandom_r): initial state and stream selector.
    Rng(std::uint64_t initState, std::uint64_t initSeq) {
        state_ = 0u;
        inc_ = (initSeq << 1u) | 1u;
        nextU32();
        state_ += initState;
        nextU32();
    }

    std::uint32_t nextU32() {
        const std::uint64_t old = state_;
        state_ = old * 6364136223846793005ull + inc_;
        const auto xorShifted = static_cast<std::uint32_t>(((old >> 18u) ^ old) >> 27u);
        const auto rot = static_cast<std::uint32_t>(old >> 59u);
        return (xorShifted >> rot) | (xorShifted << ((~rot + 1u) & 31u));
    }

    // Uniform in [0, 1).
    double nextDouble() { return static_cast<double>(nextU32()) * (1.0 / 4294967296.0); }

    // Uniform in [0, bound) without modulo bias. bound == 0 yields 0.
    std::uint32_t nextBelow(std::uint32_t bound) {
        if (bound == 0u) return 0u;
        const std::uint32_t threshold = (~bound + 1u) % bound;
        for (;;) {
            const std::uint32_t r = nextU32();
            if (r >= threshold) return r % bound;
        }
    }

private:
    std::uint64_t state_ = 0u;
    std::uint64_t inc_ = 0u;
};

// Stateless hash of (seed, index): the first output of a generator started at `seed` on the
// stream `index`. Used where a value must depend only on a position (e.g. humanize per trigger).
inline std::uint32_t hashIndex(std::uint32_t seed, std::uint32_t index) {
    return Rng(static_cast<std::uint64_t>(seed), static_cast<std::uint64_t>(index)).nextU32();
}

}  // namespace dw
