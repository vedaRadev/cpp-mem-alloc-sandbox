#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstdio>

struct Arena {
    void* m_memory;
    size_t m_offset;
    size_t m_capacity;

    void* alloc_aligned(size_t bytes, size_t align) {
        if (bytes == 0) return nullptr;

        uintptr_t base_addr = (uintptr_t)m_memory + m_offset;
        size_t padding = (align - (base_addr % align)) % align;
        if (m_offset + bytes + padding > m_capacity) { return nullptr; }

        m_offset += bytes + padding;
        return (void*)(base_addr + padding);
    }

    void reset() { m_offset = 0; }
};

int main() {
    const int arena_size = 1 << 8;
    Arena arena = { .m_memory = malloc(arena_size), .m_capacity = arena_size };

    void* result;

    result = arena.alloc_aligned(250, 8);
    if (result == nullptr) {
        printf("First allocation failed but shouldn't have\n");
        return 1;
    }

    result = arena.alloc_aligned(8, 4);
    if (result != nullptr) {
        printf("Second allocation didn't fail but should have\n");
        return 1;
    }

    printf("success");

    return 0;
}
