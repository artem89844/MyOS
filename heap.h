#ifndef HEAP_H
#define HEAP_H

#define NULL ((void*)0)

void heap_init();
void* malloc(unsigned int size);
void free(void* ptr);

#endif