#include "string.h"

void* memcpy(void* dest, const void* src, unsigned int count) {
    char* d = (char*)dest;
    const char* s = (const char*)src;
    while (count--) *d++ = *s++;
    return dest;
}

void* memset(void* dest, int val, unsigned int count) {
    char* d = (char*)dest;
    while (count--) *d++ = (char)val;
    return dest;
}

int strlen(const char* str) {
    int len = 0;
    while (str[len]) len++;
    return len;
}

int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}