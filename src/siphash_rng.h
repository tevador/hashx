/* Copyright (c) 2020 tevador <tevador@gmail.com> */
/* See LICENSE for licensing information */

#ifndef SIPHASH_GENERATOR_H
#define SIPHASH_GENERATOR_H

#include "siphash.h"
#include <stdint.h>

typedef struct siphash_rng {
	siphash_state keys;
	uint64_t counter;
	uint64_t buffer8, buffer32;
	unsigned count8, count32;
} siphash_rng;

#ifdef __cplusplus
extern "C" {
#endif

void hashx_siphash_rng_init(siphash_rng* gen, const siphash_state* state);
uint32_t hashx_siphash_rng_u32(siphash_rng* gen);
uint8_t hashx_siphash_rng_u8(siphash_rng* gen);

#ifdef __cplusplus
}
#endif

#endif
