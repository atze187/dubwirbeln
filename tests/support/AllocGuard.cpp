#include "AllocGuard.h"

#include <cstdlib>
#include <new>

namespace {

thread_local int g_guardDepth = 0;
thread_local long g_allocations = 0;

void* counted(std::size_t size) {
    if (g_guardDepth > 0) ++g_allocations;
    if (size == 0) size = 1;
    return std::malloc(size);
}

}  // namespace

namespace dw::test {

AllocGuard::AllocGuard() : start_(g_allocations) { ++g_guardDepth; }
AllocGuard::~AllocGuard() { --g_guardDepth; }
long AllocGuard::allocations() const { return g_allocations - start_; }

}  // namespace dw::test

void* operator new(std::size_t size) {
    if (void* p = counted(size)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) {
    if (void* p = counted(size)) return p;
    throw std::bad_alloc();
}
void* operator new(std::size_t size, const std::nothrow_t&) noexcept { return counted(size); }
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept { return counted(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
