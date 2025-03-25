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

// This stack implementation only encodes padding.
// The padding is the # bytes we need to put before the header such that a new
// allocation can be properly aligned.
// NOTE: Storing the padding as a byte limits the max alignment that can be used
// with this particular stack allocator to 128 bytes.
// Max alignment bytes = 2^(8 * sizeof(padding) - 1)
struct StackAllocationHeader {
    uint8_t padding;
};

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

    // Try to allocate some amount of memory with the given alignment.
    void* alloc_aligned(size_t size, size_t align) {
        return nullptr;
    }
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

    TEST_END
}

TEST test_stack() {
    printf("TODO ");

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
