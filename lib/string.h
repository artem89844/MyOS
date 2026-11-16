#ifndef STRING_H
#define STRING_H

void* memcpy(void* dest, const void* src, unsigned int count);
void* memset(void* dest, int val, unsigned int count);
int strlen(const char* str);
int strcmp(const char* s1, const char* s2);

#endif