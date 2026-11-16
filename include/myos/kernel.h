#ifndef MYOS_KERNEL_H
#define MYOS_KERNEL_H

#include "myos/multiboot.h"

#define MYOS_KERNEL_NAME "MyOS"
#define MYOS_KERNEL_VERSION "0.3.0-arch-alpha"
#define MYOS_KERNEL_ARCH "x86"
#define MYOS_KERNEL_BITS 32u

void kernel_main(multiboot_info_t* mbi);

#endif /* MYOS_KERNEL_H */
