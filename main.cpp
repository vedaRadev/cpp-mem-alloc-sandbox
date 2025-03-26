#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>

inline bool is_power_of_two(uint64_t x) { return ~(x & (x - 1)); }

//============================== ARENA ==============================//

// Get the next address >= the `base` address aligned to `align` boundary.
inline uintptr_t forward_align(uintptr_t base, size_t align) {
    assert(is_power_of_two(align));
    size_t padding = align - base & (align - 1);
    return base + padding;
}

struct Arena {
    unsigned char* m_memory;
    size_t m_prev_offset;
    size_t m_offset;
    size_t m_capacity;

    // Try to allocate some amount of memory with the given alignment.
    void* alloc_aligned(size_t bytes, size_t align) {
        if (bytes == 0) return nullptr;

        uintptr_t base_addr = (uintptr_t)m_memory + m_offset;
        uintptr_t aligned_addr = forward_align(base_addr, align);
        size_t aligned_offset = aligned_addr - (uintptr_t)m_memory;
        size_t next_offset = aligned_offset + bytes;
        if (next_offset > m_capacity) { return nullptr; }

        m_prev_offset = m_offset;
        m_offset = next_offset;
        return std::memset((void*)aligned_addr, 0, bytes);
    }

    // Given an older allocation from the arena, attempt to resize it.
    // NOTE: This does NOT support changing the _alignment_ of an allocation.
    void* resize_aligned(void* old_allocation, size_t old_size, size_t new_size, size_t align) {
        assert(is_power_of_two(align));
        unsigned char* old_alloc = (unsigned char*)old_allocation;
        if (old_alloc == nullptr || old_size == 0) return nullptr;
        if (old_alloc < m_memory || old_alloc > m_memory + m_capacity) return nullptr;

        // Was this the last thing we allocated from the arena?
        if (m_memory + m_prev_offset == old_alloc) {
            m_offset = m_prev_offset + new_size;
            if (new_size > old_size) {
                // Zero new memory
                std::memset(m_memory + m_offset, 0, new_size - old_size);
            }
            return old_alloc;
        } else {
            // If not, allocate new mem from the arena and copy the old data to it.
            void* new_alloc = this->alloc_aligned(new_size, align);
            if (new_alloc == nullptr) return nullptr;
            size_t copy_size = old_size < new_size ? old_size : new_size;
            std::memmove(new_alloc, old_alloc, copy_size);
            return new_alloc;
        }
    }

    void reset() { m_offset = 0; }
};

//============================== STACK ==============================//

// This is what our stack memory block looks like.
// We ensure that there's enough padding between allocations to do two things:
// 1) Properly align the next allocation to a user-supplied alignment (power of two).
// 2) Store a header within the padding between allocations.
// The header stores information that allows us to set our offset back to the start
// of a previous allocation, in effect freeing memory of the most recent allocations.
// +----------------+---------+------+----------------+------+
// | Old Allocation | Padding |Header| New Allocation | Free |
// +----------------+---------+------+----------------+------+
//                  ↑                                 ↑
//          Previous Offset                    Current Offset

// Headers placed within the padding used to align data in our stack.
struct StackAllocationHeader {
    // Num bytes we need to put before the header such that a new allocation can
    // be properly aligned.
    size_t padding;
    size_t prev_offset;
    StackAllocationHeader *prev_header;
    StackAllocationHeader *next_header;
};

// Max alignment bytes = 2^(8 * sizeof(padding) - 1)
constexpr size_t STACK_MAX_ALIGN = (size_t)1 << (8 * sizeof(StackAllocationHeader::padding) - 1);

// Calculate the amount of padding we need to both
// A) align our pointer to an `align` byte boundary, and
// B) fit a header of size `header_size` bytes in the padding.
size_t calc_padding_with_header(uintptr_t base, size_t align, size_t header_size) {
    assert(is_power_of_two(align));
    size_t padding = align - base & (align - 1);

    // If we can't fit our header into the padding we need to bump our
    // padding up to the next aligned boundary that _can_ fit our header.
    if (header_size > padding) {
        size_t space_needed = header_size - padding;
        // Is the additional space we need to store our header a multiple
        // of our desired alignment?
        if ((space_needed & (align - 1)) != 0) {
            padding += align * (1 + space_needed / align);
        } else {
            padding += space_needed;
        }
    }

    return padding;
}

struct Stack {
    unsigned char* m_memory;
    size_t m_capacity;
    size_t m_offset;
    size_t m_prev_offset;
    StackAllocationHeader *m_prev_header;

    // Try to allocate some amount of memory with the given alignment.
    void* alloc_aligned(size_t alloc_size, size_t align) {
        assert(is_power_of_two(align));
        // Max align bytes = 2^(8 * sizeof(padding) - 1)
        // For one byte:
        if (align > STACK_MAX_ALIGN) { align = STACK_MAX_ALIGN; }
        uintptr_t base_addr = (uintptr_t)m_memory + m_offset;
        size_t padding = calc_padding_with_header(base_addr, align, sizeof(StackAllocationHeader));
        // Check if we're out of memory
        if (m_offset + alloc_size + padding > m_capacity) { return nullptr; }
        m_prev_offset = m_offset;
        m_offset += padding;

        uintptr_t next_aligned_addr = base_addr + padding;
        StackAllocationHeader* header = (StackAllocationHeader*)(next_aligned_addr - sizeof(StackAllocationHeader));
        header->padding = (uint8_t)padding;
        header->prev_header = m_prev_header;
        header->prev_offset = m_prev_offset;
        if (m_prev_header != nullptr) {
            m_prev_header->next_header = header;
        }
        m_prev_header = header;
        m_offset += alloc_size;

        return std::memset((void*)next_aligned_addr, 0, alloc_size);
    }

    // Given an allocation, free to the start of the previous allocation.
    // Returns whether the operation was successful.
    bool free(void* alloc) {
        if (alloc == nullptr) return false;

        uintptr_t start = (uintptr_t)m_memory;
        uintptr_t end = start + m_capacity;
        uintptr_t curr_addr = (uintptr_t)alloc;
        // Ensure that we're in the bounds of our memory.
        // Should probably assert here.
        if (curr_addr < start || curr_addr > end) { return false; }
        // Allow double-frees
        if (curr_addr >= start + m_offset) { return false; }

        StackAllocationHeader* header = (StackAllocationHeader*)(curr_addr - sizeof(StackAllocationHeader));
        // size_t prev_offset = (size_t)(curr_addr - (uintptr_t)header->padding - start);
        // Protect against out-of-order frees
        if (m_prev_offset != header->prev_offset) { return false; }

        m_offset = m_prev_offset;
        if (header->prev_header != nullptr) {
            m_prev_offset = header->prev_header->prev_offset;
        } else {
            m_prev_offset = 0;
        }

        return true;
    }

    // TODO update to work with new header
    // TODO enforce LIFO?
    // NOTE: This does NOT support changing the _alignment_ of an allocation.
    /*
    void* resize_aligned(void* old_allocation, size_t old_size, size_t new_size, size_t align) {
        if (old_allocation == nullptr) { return this->alloc_aligned(new_size, align); }
        if (new_size == 0) {
            this->free(old_allocation);
            return nullptr;
        }

        uintptr_t old_alloc = (uintptr_t)old_allocation;
        uintptr_t start = (uintptr_t)m_memory;
        uintptr_t end = start + m_capacity;
        if (old_alloc < start || old_alloc > end) { return nullptr; }
        if (old_alloc >= start + m_offset) { return nullptr; }

        StackAllocationHeader* header = (StackAllocationHeader*)(old_alloc - sizeof(StackAllocationHeader));
        // Was this the most recent thing we allocated?
        if (header->prev_offset == m_prev_offset) {
            if (new_size > old_size) {
                std::memset((void*)(old_alloc + old_size), 0, new_size);
            }
            m_offset = (old_alloc - start) + new_size;
            return old_allocation;
        } else {
            void* resized_alloc = this->alloc_aligned(new_size, align);
            size_t min_size = old_size < new_size ? old_size : new_size;
            std::memmove(resized_alloc, old_allocation, min_size);
            return resized_alloc;
        }
    }
    */

    void reset() { m_offset = 0; }
};

///////////////////////////////////////////////////////////////////////
//============================== TESTS ==============================//
///////////////////////////////////////////////////////////////////////

// Test results
struct TestResult {
    bool passed;
    int failure_line;
    const char* failure_expr;
};

#define TEST TestResult

#define RUN_TEST(label, test) { \
    printf("test: %s... ", label); \
    TestResult result = test(); \
    if (result.passed) { \
        printf("ok\n"); \
    } else { \
        printf("failed (line %d): %s\n", result.failure_line, result.failure_expr); \
    } \
};

#define TEST_ASSERT(expr) if (!(expr)) { \
    return TestResult { \
        .passed = false, \
        .failure_line = __LINE__, \
        .failure_expr = #expr \
    }; \
}

#define TEST_END return { .passed = true };

TEST test_forward_align() {
    TEST_ASSERT(forward_align(3, 1) == 3);
    TEST_ASSERT(forward_align(1, 4) == 4);
    TEST_ASSERT(forward_align(29, 8) == 32);
    TEST_ASSERT(forward_align(17, 16) == 32);
    TEST_ASSERT(forward_align(129, 256) == 256);
    
    TEST_END
}

TEST test_arena() {
    size_t arena_size = 8;
    unsigned char memory[arena_size];
    Arena arena = { .m_memory = memory, .m_capacity = arena_size };
    void* alloc;

    // Test general allocations
    TEST_ASSERT(arena.alloc_aligned(4, 4) != nullptr);
    TEST_ASSERT(arena.alloc_aligned(1, 1) != nullptr);
    TEST_ASSERT(arena.alloc_aligned(4, 4) == nullptr);
    arena.reset();
    TEST_ASSERT(arena.alloc_aligned(4, 1) != nullptr);
    TEST_ASSERT(arena.alloc_aligned(5, 8) == nullptr);
    arena.reset();
    TEST_ASSERT(arena.alloc_aligned(8, 8) != nullptr);
    arena.reset();
    TEST_ASSERT(arena.alloc_aligned(16, 16) == nullptr);
    arena.reset();

    // Test that allocations are aligned properly.
    arena.alloc_aligned(3, 2);
    alloc = arena.alloc_aligned(4, 4);
    TEST_ASSERT(is_power_of_two((uint64_t)alloc));
    TEST_ASSERT((uintptr_t)alloc % 4 == 0);
    arena.reset();
    arena.alloc_aligned(4, 2);
    alloc = arena.alloc_aligned(4, 4);
    TEST_ASSERT(is_power_of_two((uint64_t)alloc));
    TEST_ASSERT((uintptr_t)alloc % 4 == 0);
    arena.reset();
    
    // Test that mem is zeroed
    *(uint8_t*)arena.alloc_aligned(8, 8) = ~0; // fill with all 1s
    arena.reset();
    TEST_ASSERT(*(uint8_t*)(arena.alloc_aligned(8, 8)) == 0); // ensure mem zeroed
    arena.reset();

    // Test resize of last allocation
    alloc = arena.alloc_aligned(4, 4);
    TEST_ASSERT(arena.resize_aligned(alloc, 4, 8, 4) != nullptr);
    TEST_ASSERT(arena.alloc_aligned(4, 4) == nullptr);
    arena.reset();

    // Test resize of second-to-last allocation but resize is too big
    alloc = arena.alloc_aligned(4, 4);
    TEST_ASSERT(arena.alloc_aligned(4, 4) != nullptr);
    // Should force new allocation but we're out of space
    TEST_ASSERT(arena.resize_aligned(alloc, 4, 8, 4) == nullptr);
    arena.reset();

    // Test resize of second-to-last allocation
    alloc = arena.alloc_aligned(2, 2);
    TEST_ASSERT(arena.alloc_aligned(2, 2) != nullptr);
    TEST_ASSERT(arena.resize_aligned(alloc, 2, 4, 2) != nullptr);
    arena.reset();

    TEST_END
}

TEST test_calc_padding_with_header() {
    TEST_ASSERT(calc_padding_with_header(0, 8, 1) == 8);
    TEST_ASSERT(calc_padding_with_header(0, 8, 7) == 8);
    TEST_ASSERT(calc_padding_with_header(1, 8, 1) == 7);
    TEST_ASSERT(calc_padding_with_header(15, 8, 0) == 1);
    TEST_ASSERT(calc_padding_with_header(1, 8, 14) == 15);
    TEST_ASSERT(calc_padding_with_header(1, 8, 32) == 39);

    TEST_END
}

TEST test_stack() {
    size_t stack_size = 256;
    unsigned char buf[stack_size];
    Stack stack = { .m_memory = buf, .m_capacity = stack_size };
    void *curr_alloc, *prev_alloc;

    // Test in-order free succeeds
    prev_alloc = stack.alloc_aligned(16, 16);
    curr_alloc = stack.alloc_aligned(32, 32);
    TEST_ASSERT(stack.free(curr_alloc));
    TEST_ASSERT(stack.free(prev_alloc));
    TEST_ASSERT(stack.m_prev_offset == 0);
    stack.reset();

    // Test out-of-order free fails
    curr_alloc = stack.alloc_aligned(32, 8);
    stack.alloc_aligned(32, 8);
    TEST_ASSERT(!stack.free(curr_alloc));
    stack.reset();

    TEST_END
}

///////////////////////////////////////////////////////////////////////////
//============================== END TESTS ==============================//
///////////////////////////////////////////////////////////////////////////
int main() {
    RUN_TEST("forward align", test_forward_align);
    RUN_TEST("arena", test_arena);
    RUN_TEST("calc padding with header", test_calc_padding_with_header);
    RUN_TEST("stack", test_stack);
    return 0;
}
