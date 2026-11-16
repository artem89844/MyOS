#include "heap.h"
#include <stdint.h>

#define MULTIBOOT_INFO_MEMORY       (1u << 0)
#define MULTIBOOT_INFO_MMAP         (1u << 6)
#define MULTIBOOT_MEMORY_AVAILABLE  1u
#define HEAP_ALIGNMENT              8u
#define HEAP_ADDRESS_LIMIT          0xFFFFF000ULL
#define HEAP_MINIMUM_BYTES          (64u * 1024u)

typedef struct block_header {
    uint32_t size;       /* payload size */
    uint32_t is_free;    /* 1 = free, 0 = allocated */
} block_header_t;

/* Multiboot v1 memory-map records: size excludes the size field itself. */
typedef struct __attribute__((packed)) multiboot_mmap_entry {
    uint32_t size;
    uint64_t addr;
    uint64_t len;
    uint32_t type;
} multiboot_mmap_entry_t;

extern unsigned char kernel_end;

static uint32_t heap_start;
static uint32_t heap_current;
static uint32_t heap_limit;

static uint32_t align_up_8(uint32_t value) {
    if (value > 0xFFFFFFF7u) return 0;
    return (value + 7u) & ~7u;
}

static uint64_t find_mmap_heap_end(uint32_t mmap_addr, uint32_t mmap_length,
                                  uint32_t start) {
    uint32_t offset = 0;
    uint64_t best_end = 0;

    if (!mmap_addr || mmap_length < sizeof(uint32_t)) return 0;

    while (offset <= mmap_length - sizeof(uint32_t)) {
        const multiboot_mmap_entry_t* entry =
            (const multiboot_mmap_entry_t*)(uintptr_t)(mmap_addr + offset);
        uint32_t payload_size = entry->size;
        uint32_t record_size;
        uint64_t region_end;

        if (payload_size < 20u) break;
        if (payload_size > mmap_length - offset - sizeof(uint32_t)) break;
        record_size = payload_size + sizeof(uint32_t);

        region_end = entry->addr + entry->len;
        if (region_end >= entry->addr && entry->type == MULTIBOOT_MEMORY_AVAILABLE &&
            entry->addr <= (uint64_t)start && region_end > (uint64_t)start) {
            if (region_end > best_end) best_end = region_end;
        }
        offset += record_size;
    }

    return best_end;
}

int heap_init(unsigned int flags, unsigned int mem_upper,
              unsigned int mmap_addr, unsigned int mmap_length) {
    uint32_t start = align_up_8((uint32_t)(uintptr_t)&kernel_end);
    uint64_t end = 0;

    heap_start = 0;
    heap_current = 0;
    heap_limit = 0;
    if (!start) return 0;

    if (flags & MULTIBOOT_INFO_MMAP) {
        end = find_mmap_heap_end(mmap_addr, mmap_length, start);
    } else if (flags & MULTIBOOT_INFO_MEMORY) {
        /* mem_upper is the number of usable KiB above physical 1 MiB. */
        end = 0x100000ULL + (uint64_t)mem_upper * 1024ULL;
        if (end <= (uint64_t)start) end = 0;
    }

    /* The kernel is 32-bit and has no PAE, so keep allocations below 4 GiB. */
    if (end > HEAP_ADDRESS_LIMIT) end = HEAP_ADDRESS_LIMIT;
    if (end <= (uint64_t)start || end - (uint64_t)start < HEAP_MINIMUM_BYTES) {
        return 0;
    }

    heap_start = start;
    heap_current = start;
    heap_limit = (uint32_t)end;
    return 1;
}

void* malloc(unsigned int size) {
    uint32_t aligned_size;
    uint32_t current;
    uint32_t total_size;

    if (size == 0 || heap_start == 0 || heap_limit <= heap_start) return NULL;
    if (size > 0xFFFFFFF7u) return NULL;
    aligned_size = align_up_8(size);
    if (!aligned_size) return NULL;

    current = heap_start;
    while (current < heap_current) {
        block_header_t* block;
        uint32_t remaining;

        if (heap_current - current < sizeof(block_header_t)) return NULL;
        block = (block_header_t*)(uintptr_t)current;
        remaining = heap_current - current - (uint32_t)sizeof(block_header_t);
        if (block->size > remaining) return NULL; /* corrupted heap metadata */

        if (block->is_free && block->size >= aligned_size) {
            block->is_free = 0;
            return (void*)(uintptr_t)(current + sizeof(block_header_t));
        }
        current += (uint32_t)sizeof(block_header_t) + block->size;
    }

    if (aligned_size > heap_limit - heap_current ||
        sizeof(block_header_t) > heap_limit - heap_current - aligned_size) {
        return NULL;
    }
    total_size = (uint32_t)sizeof(block_header_t) + aligned_size;

    {
        block_header_t* block = (block_header_t*)(uintptr_t)heap_current;
        void* result = (void*)(uintptr_t)(heap_current + sizeof(block_header_t));
        block->size = aligned_size;
        block->is_free = 0;
        heap_current += total_size;
        return result;
    }
}

void free(void* ptr) {
    uint32_t address;
    uint32_t current;

    if (!ptr || !heap_start || !heap_current) return;
    address = (uint32_t)(uintptr_t)ptr;
    if (address < heap_start + sizeof(block_header_t) || address >= heap_current) return;

    /* Accept only the exact payload pointer of a currently allocated block. */
    current = heap_start;
    while (current < heap_current) {
        block_header_t* block;
        uint32_t next;
        uint32_t remaining;

        if (heap_current - current < sizeof(block_header_t)) return;
        block = (block_header_t*)(uintptr_t)current;
        remaining = heap_current - current - (uint32_t)sizeof(block_header_t);
        if (block->size > remaining) return;
        next = current + (uint32_t)sizeof(block_header_t) + block->size;

        if (address == current + sizeof(block_header_t)) {
            if (block->is_free) return; /* ignore double free */
            block->is_free = 1;
            break;
        }
        current = next;
    }
    if (current >= heap_current) return;

    /* Merge consecutive free blocks. */
    current = heap_start;
    while (current < heap_current) {
        block_header_t* block = (block_header_t*)(uintptr_t)current;
        uint32_t next = current + (uint32_t)sizeof(block_header_t) + block->size;
        if (next >= heap_current) break;

        {
            block_header_t* next_block = (block_header_t*)(uintptr_t)next;
            if (block->is_free && next_block->is_free) {
                block->size += (uint32_t)sizeof(block_header_t) + next_block->size;
            } else {
                current = next;
            }
        }
    }
}

unsigned int heap_available(void) {
    if (!heap_limit || heap_current >= heap_limit) return 0;
    return heap_limit - heap_current;
}
