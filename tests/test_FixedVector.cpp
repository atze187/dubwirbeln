#include <catch2/catch_test_macros.hpp>

#include "engine/FixedVector.h"

using dw::FixedVector;

TEST_CASE("a fixed vector starts empty", "[fixedvector]") {
    FixedVector<int, 4> v;
    CHECK(v.empty());
    CHECK(v.size() == 0);
    CHECK(v.capacity() == 4);
    CHECK_FALSE(v.full());
    CHECK(v.begin() == v.end());
}

TEST_CASE("push_back stores in order and drops what does not fit", "[fixedvector]") {
    FixedVector<int, 3> v;
    CHECK(v.push_back(1));
    CHECK(v.push_back(2));
    CHECK(v.push_back(3));
    CHECK(v.full());
    CHECK_FALSE(v.push_back(4));
    REQUIRE(v.size() == 3);
    CHECK(v[0] == 1);
    CHECK(v.front() == 1);
    CHECK(v.back() == 3);
    int sum = 0;
    for (int x : v) sum += x;
    CHECK(sum == 6);
}

TEST_CASE("an initializer list fills the vector up to its capacity", "[fixedvector]") {
    FixedVector<int, 2> v{1, 2, 3};
    REQUIRE(v.size() == 2);
    CHECK(v[1] == 2);
}

TEST_CASE("clear, truncate and resize change the size", "[fixedvector]") {
    FixedVector<int, 5> v{1, 2, 3, 4};
    v.truncate(2);
    CHECK(v.size() == 2);
    v.truncate(9);
    CHECK(v.size() == 2);
    v.resize(4, 7);
    REQUIRE(v.size() == 4);
    CHECK(v[1] == 2);
    CHECK(v[3] == 7);
    v.resize(99);
    CHECK(v.size() == 5);
    v.clear();
    CHECK(v.empty());
}

TEST_CASE("fixed vectors compare by content", "[fixedvector]") {
    FixedVector<int, 4> a{1, 2};
    FixedVector<int, 4> b{1, 2};
    CHECK(a == b);
    b.push_back(3);
    CHECK_FALSE(a == b);
}
