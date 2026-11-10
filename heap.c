#include "heap.h"
#include "string.h"

// Символ из linker.ld (адрес конца ядра)
extern unsigned int kernel_end;

// Заголовок каждого блока памяти
typedef struct block_header {
    unsigned int size;      // Размер полезной памяти (без учета заголовка)
    unsigned int is_free;   // 1 = свободен, 0 = занят
} block_header_t;

static unsigned int heap_start = 0;
static unsigned int heap_current = 0;
static unsigned int heap_limit = 0x08000000; // 128 МБ

// Выравнивание по 8 байт
#define ALIGN_8(x) (((x) + 7) & ~7)

void heap_init() {
    heap_start = (unsigned int)&kernel_end;
    heap_start = ALIGN_8(heap_start);
    heap_current = heap_start;
}

void* malloc(unsigned int size) {
    if (size == 0) return NULL;
    
    unsigned int aligned_size = ALIGN_8(size);

    // 1. Ищем уже существующий свободный блок
    unsigned int current = heap_start;
    while (current < heap_current) {
        block_header_t* block = (block_header_t*)current;
        
        if (block->is_free && block->size >= aligned_size) {
            block->is_free = 0; // Помечаем как занятый
            return (void*)(current + sizeof(block_header_t));
        }
        // Переходим к следующему блоку
        current += sizeof(block_header_t) + block->size;
    }

    // 2. Если свободного блока нет, выделяем новый в конце кучи
    unsigned int total_size = sizeof(block_header_t) + aligned_size;
    if (heap_current + total_size > heap_limit) {
        return NULL; // Out of memory
    }

    block_header_t* new_block = (block_header_t*)heap_current;
    new_block->size = aligned_size;
    new_block->is_free = 0;
    
    void* ptr = (void*)(heap_current + sizeof(block_header_t));
    heap_current += total_size;
    
    return ptr;
}

void free(void* ptr) {
    if (!ptr) return;
    
    // Получаем указатель на заголовок блока
    block_header_t* block = (block_header_t*)((unsigned int)ptr - sizeof(block_header_t));
    block->is_free = 1;

    // Слияние (Coalescing) соседних свободных блоков, чтобы избежать фрагментации
    unsigned int current = heap_start;
    while (current < heap_current) {
        block_header_t* curr_block = (block_header_t*)current;
        unsigned int next_addr = current + sizeof(block_header_t) + curr_block->size;
        
        if (next_addr >= heap_current) break; // Дошли до конца
        
        block_header_t* next_block = (block_header_t*)next_addr;
        if (curr_block->is_free && next_block->is_free) {
            // Объединяем два свободных блока в один
            curr_block->size += sizeof(block_header_t) + next_block->size;
        } else {
            current = next_addr;
        }
    }
}