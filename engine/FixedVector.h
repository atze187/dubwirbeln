#pragma once

#include <array>
#include <cstddef>
#include <initializer_list>

namespace dw {

// A vector with a fixed capacity and no heap allocation, for code that runs on the audio thread.
// push_back on a full vector drops the element and returns false (a documented limit, not an
// error). T must be default constructible.
template <typename T, std::size_t N>
class FixedVector {
public:
    using value_type = T;
    using iterator = T*;
    using const_iterator = const T*;

    FixedVector() = default;
    FixedVector(std::initializer_list<T> items) {
        for (const auto& item : items) push_back(item);
    }

    static constexpr std::size_t capacity() { return N; }
    std::size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    bool full() const { return size_ == N; }

    bool push_back(const T& value) {
        if (size_ == N) return false;
        items_[size_++] = value;
        return true;
    }
    void clear() { size_ = 0; }
    void resize(std::size_t n, const T& value = T()) {
        if (n > N) n = N;
        for (std::size_t i = size_; i < n; ++i) items_[i] = value;
        size_ = n;
    }
    // Keeps the first `n` elements.
    void truncate(std::size_t n) {
        if (n < size_) size_ = n;
    }

    T& operator[](std::size_t i) { return items_[i]; }
    const T& operator[](std::size_t i) const { return items_[i]; }
    T& front() { return items_[0]; }
    const T& front() const { return items_[0]; }
    T& back() { return items_[size_ - 1]; }
    const T& back() const { return items_[size_ - 1]; }
    T* data() { return items_.data(); }
    const T* data() const { return items_.data(); }
    iterator begin() { return items_.data(); }
    iterator end() { return items_.data() + size_; }
    const_iterator begin() const { return items_.data(); }
    const_iterator end() const { return items_.data() + size_; }

    bool operator==(const FixedVector& other) const {
        if (size_ != other.size_) return false;
        for (std::size_t i = 0; i < size_; ++i)
            if (!(items_[i] == other.items_[i])) return false;
        return true;
    }

private:
    std::array<T, N> items_{};
    std::size_t size_ = 0;
};

}  // namespace dw
