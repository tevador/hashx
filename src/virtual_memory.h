/* Copyright (c) 2020 tevador <tevador@gmail.com> */
/* See LICENSE for licensing information */

#ifndef VIRTUAL_MEMORY_H
#define VIRTUAL_MEMORY_H

#include <stdint.h>
#include <stddef.h>

#define ALIGN_SIZE(pos, align) ((((pos) - 1) / (align) + 1) * (align))

void* hashx_vm_alloc(size_t size);
void hashx_vm_rw(void* ptr, size_t size);
void hashx_vm_rx(void* ptr, size_t size);
void* hashx_vm_alloc_huge(size_t size);
void hashx_vm_free(void* ptr, size_t size);

#endif
