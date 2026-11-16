#ifndef HEAP_H
#define HEAP_H

#define NULL ((void*)0)

/* Initialise the heap from the Multiboot v1 memory information. */
int heap_init(unsigned int flags, unsigned int mem_upper,
              unsigned int mmap_addr, unsigned int mmap_length);
void* malloc(unsigned int size);
void free(void* ptr);
unsigned int heap_available(void);

#endif
