#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>

inline bool is_power_of_two(uint64_t x) { return ~(x & (x - 1)); }

inline uintptr_t forward_align(uintptr_t base, size_t align) {
    assert(is_power_of_two(align));
    size_t padding = align - base & (align - 1);
    return base + padding;
}

//============================== ARENA ==============================//
struct Arena {
    void* m_memory;
    size_t m_offset;
    size_t m_capacity;

    void* alloc_aligned(size_t bytes, size_t align) {
        if (bytes == 0) return nullptr;

        uintptr_t base_addr = (uintptr_t)m_memory + m_offset;
        uintptr_t aligned_addr = forward_align(base_addr, align);
        size_t aligned_offset = aligned_addr - (uintptr_t)m_memory;
        size_t next_offset = aligned_offset + bytes;
        if (next_offset > m_capacity) { return nullptr; }

        m_offset = next_offset;
        return std::memset((void*)aligned_addr, 0, bytes);
    }

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

TEST test_arena() {
    size_t arena_size = 8;
    char memory[arena_size];
    Arena arena = { .m_memory = &memory, .m_capacity = arena_size };

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
    
    *(uint8_t*)arena.alloc_aligned(8, 8) = ~0; // fill with all 1s
    arena.reset();
    TEST_ASSERT(*(uint8_t*)(arena.alloc_aligned(8, 8)) == 0); // ensure mem zeroed
    arena.reset();

    TEST_END
}

///////////////////////////////////////////////////////////////////////////
//============================== END TESTS ==============================//
///////////////////////////////////////////////////////////////////////////
int main() {
    RUN_TEST("arena", test_arena);
    return 0;
}
