#pragma once

#include "tefra/core/types.h"

namespace tefra {

// Coarse memory accounting by subsystem, reported by the bench.
enum class MemTag : u8 { General, Render, World, Physics, Audio, Count };

void mem_track(MemTag tag, i64 delta_bytes);
i64 mem_tracked(MemTag tag);
i64 mem_tracked_total();
const char* mem_tag_name(MemTag tag);

inline usize align_up(usize v, usize align) { return (v + align - 1) & ~(align - 1); }
inline bool is_pow2(usize v) { return v && !(v & (v - 1)); }

// Bump allocator over a fixed block. reset() frees everything at once (per-frame scratch).
class LinearArena {
  public:
    LinearArena() = default;
    explicit LinearArena(usize capacity, MemTag tag = MemTag::General);
    ~LinearArena();
    LinearArena(const LinearArena&) = delete;
    LinearArena& operator=(const LinearArena&) = delete;

    // Returns nullptr when out of space.
    void* alloc(usize size, usize align = 16);
    template <typename T> T* alloc_array(usize count) { return static_cast<T*>(alloc(sizeof(T) * count, alignof(T))); }
    void reset() { offset_ = 0; }
    usize used() const { return offset_; }
    usize capacity() const { return capacity_; }
    usize high_water() const { return high_water_; }

  private:
    u8* base_ = nullptr;
    usize capacity_ = 0;
    usize offset_ = 0;
    usize high_water_ = 0;
    MemTag tag_ = MemTag::General;
};

// Fixed-size block pool with an intrusive free list. O(1) alloc/free, no fragmentation.
class PoolAllocator {
  public:
    PoolAllocator() = default;
    PoolAllocator(usize block_size, usize block_count, MemTag tag = MemTag::General);
    ~PoolAllocator();
    PoolAllocator(const PoolAllocator&) = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;

    void* alloc();
    void free(void* p);
    usize block_size() const { return block_size_; }
    usize capacity() const { return block_count_; }
    usize in_use() const { return in_use_; }
    bool owns(const void* p) const;

  private:
    u8* base_ = nullptr;
    void* free_list_ = nullptr;
    usize block_size_ = 0;
    usize block_count_ = 0;
    usize in_use_ = 0;
    MemTag tag_ = MemTag::General;
};

} // namespace tefra
