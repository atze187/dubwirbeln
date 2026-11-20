#pragma once

// Counts heap allocations made on the current thread while a guard is alive. The counting global
// operator new lives in AllocGuard.cpp, which every test executable that uses the guard links in.
// Keep the guarded region small and run the checks (CHECK, REQUIRE) after the guard is gone: the
// test framework allocates too.
namespace dw::test {

class AllocGuard {
public:
    AllocGuard();
    ~AllocGuard();
    AllocGuard(const AllocGuard&) = delete;
    AllocGuard& operator=(const AllocGuard&) = delete;

    // Allocations since the guard was created.
    long allocations() const;

private:
    long start_;
};

}  // namespace dw::test
