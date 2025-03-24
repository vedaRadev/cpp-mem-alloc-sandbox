#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstdio>

inline bool is_power_of_two(uint64_t x) { return ~(x & (x - 1)); }

//============================== ARENA ==============================//
struct Arena {
    void* m_memory;
    size_t m_offset;
    size_t m_capacity;

    void* alloc_aligned(size_t bytes, size_t align) {
        if (bytes == 0) return nullptr;

        uintptr_t base_addr = (uintptr_t)m_memory + m_offset;
        assert(is_power_of_two(align));
        size_t padding = align - base_addr & (align - 1);
        if (m_offset + bytes + padding > m_capacity) { return nullptr; }

        m_offset += bytes + padding;
        return (void*)(base_addr + padding);
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
    const size_t arena_size = 8;
    Arena arena = { .m_memory = malloc(arena_size), .m_capacity = arena_size };

    TEST_ASSERT(arena.alloc_aligned(4, 4) != nullptr);
    TEST_ASSERT(arena.alloc_aligned(1, 1) != nullptr);
    TEST_ASSERT(arena.alloc_aligned(4, 4) == nullptr);
    arena.reset();
    TEST_ASSERT(arena.alloc_aligned(4, 1) != nullptr);
    TEST_ASSERT(arena.alloc_aligned(4, 16) == nullptr);

    TEST_END
}

///////////////////////////////////////////////////////////////////////////
//============================== END TESTS ==============================//
///////////////////////////////////////////////////////////////////////////
int main() {
    RUN_TEST("arena", test_arena);
    return 0;
}
