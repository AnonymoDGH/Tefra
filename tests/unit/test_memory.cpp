#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest.h>

#include "tefra/core/memory.h"

#include <cstdint>
#include <set>

using namespace tefra;

TEST_CASE("LinearArena alignment, exhaustion and reset") {
    const i64 before = mem_tracked(MemTag::World);
    {
        LinearArena arena(1024, MemTag::World);
        CHECK(mem_tracked(MemTag::World) == before + 1024);
        void* a = arena.alloc(3, 1);
        void* b = arena.alloc(16, 64);
        REQUIRE(a != nullptr);
        REQUIRE(b != nullptr);
        CHECK(reinterpret_cast<std::uintptr_t>(b) % 64 == 0);
        CHECK(arena.alloc(2048) == nullptr);
        const usize used = arena.used();
        arena.reset();
        CHECK(arena.used() == 0);
        CHECK(arena.high_water() == used);
        CHECK(arena.alloc_array<u32>(256) != nullptr);
        CHECK(arena.alloc(1) == nullptr);
    }
    CHECK(mem_tracked(MemTag::World) == before);
}

TEST_CASE("PoolAllocator reuse and ownership") {
    PoolAllocator pool(24, 8);
    std::set<void*> seen;
    void* blocks[8];
    for (auto& b : blocks) {
        b = pool.alloc();
        REQUIRE(b != nullptr);
        CHECK(pool.owns(b));
        seen.insert(b);
    }
    CHECK(seen.size() == 8);
    CHECK(pool.alloc() == nullptr);
    CHECK(pool.in_use() == 8);
    pool.free(blocks[3]);
    CHECK(pool.alloc() == blocks[3]);
    int local = 0;
    CHECK_FALSE(pool.owns(&local));
}
