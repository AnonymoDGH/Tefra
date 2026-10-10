#include "tefra/core/memory.h"
#include "tefra/core/assert.h"

#include <atomic>
#include <cstdlib>

namespace tefra {
namespace {
std::atomic<i64> g_tracked[static_cast<usize>(MemTag::Count)];
} // namespace

void mem_track(MemTag tag, i64 delta_bytes) {
    g_tracked[static_cast<usize>(tag)].fetch_add(delta_bytes, std::memory_order_relaxed);
}

i64 mem_tracked(MemTag tag) { return g_tracked[static_cast<usize>(tag)].load(std::memory_order_relaxed); }

i64 mem_tracked_total() {
    i64 total = 0;
    for (const auto& t : g_tracked) total += t.load(std::memory_order_relaxed);
    return total;
}

const char* mem_tag_name(MemTag tag) {
    switch (tag) {
    case MemTag::General: return "general";
    case MemTag::Render: return "render";
    case MemTag::World: return "world";
    case MemTag::Physics: return "physics";
    case MemTag::Audio: return "audio";
    case MemTag::Count: break;
    }
    return "?";
}

// ---- LinearArena ----------------------------------------------------------------------------------

LinearArena::LinearArena(usize capacity, MemTag tag) : capacity_(capacity), tag_(tag) {
    base_ = static_cast<u8*>(std::malloc(capacity));
    TEFRA_VERIFY(base_ != nullptr);
    mem_track(tag_, static_cast<i64>(capacity_));
}

LinearArena::~LinearArena() {
    if (base_) {
        std::free(base_);
        mem_track(tag_, -static_cast<i64>(capacity_));
    }
}

void* LinearArena::alloc(usize size, usize align) {
    TEFRA_ASSERT(is_pow2(align));
    const usize start = align_up(reinterpret_cast<usize>(base_) + offset_, align) - reinterpret_cast<usize>(base_);
    if (start + size > capacity_) return nullptr;
    offset_ = start + size;
    if (offset_ > high_water_) high_water_ = offset_;
    return base_ + start;
}

// ---- PoolAllocator --------------------------------------------------------------------------------

PoolAllocator::PoolAllocator(usize block_size, usize block_count, MemTag tag)
    : block_size_(align_up(block_size < sizeof(void*) ? sizeof(void*) : block_size, alignof(std::max_align_t))),
      block_count_(block_count), tag_(tag) {
    base_ = static_cast<u8*>(std::malloc(block_size_ * block_count_));
    TEFRA_VERIFY(base_ != nullptr);
    mem_track(tag_, static_cast<i64>(block_size_ * block_count_));
    // Thread the free list through the blocks, lowest address first.
    for (usize i = block_count_; i-- > 0;) {
        void* block = base_ + i * block_size_;
        *static_cast<void**>(block) = free_list_;
        free_list_ = block;
    }
}

PoolAllocator::~PoolAllocator() {
    if (base_) {
        std::free(base_);
        mem_track(tag_, -static_cast<i64>(block_size_ * block_count_));
    }
}

void* PoolAllocator::alloc() {
    if (!free_list_) return nullptr;
    void* block = free_list_;
    free_list_ = *static_cast<void**>(block);
    ++in_use_;
    return block;
}

void PoolAllocator::free(void* p) {
    if (!p) return;
    TEFRA_ASSERT(owns(p));
    *static_cast<void**>(p) = free_list_;
    free_list_ = p;
    --in_use_;
}

bool PoolAllocator::owns(const void* p) const {
    const u8* b = static_cast<const u8*>(p);
    return b >= base_ && b < base_ + block_size_ * block_count_ && static_cast<usize>(b - base_) % block_size_ == 0;
}

} // namespace tefra
