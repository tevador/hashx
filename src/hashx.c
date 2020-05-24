/* Copyright (c) 2020 tevador <tevador@gmail.com> */
/* See LICENSE for licensing information */

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <hashx.h>
#include "blake2.h"
#include "endian.h"
#include "program.h"
#include "context.h"
#include "compiler.h"

#if HASHX_SIZE > 32
#error HASHX_SIZE cannot be more than 32
#endif

#ifndef HASHX_BLOCK_MODE
#define HASHX_INPUT_ARGS input
#else
#define HASHX_INPUT_ARGS input, size
#endif

static int initialize_program(hashx_ctx* ctx, hashx_program* program, 
	siphash_state keys[2]) {

	if (!hashx_program_generate(&keys[0], program)) {
		return 0;
	}
#ifndef HASHX_BLOCK_MODE
	memcpy(&ctx->keys, &keys[1], 32);
#else
	memcpy(&ctx->params.salt, &keys[1], 32);
#endif
#ifndef NDEBUG
	ctx->has_program = true;
#endif
	return 1;
}

int hashx_make(hashx_ctx* ctx, const void* seed, size_t size) {
	assert(ctx != NULL && ctx != HASHX_NOTSUPP);
	assert(seed != NULL || size == 0);	
	siphash_state keys[2];
	blake2b_state hash_state;
	hashx_blake2b_init_param(&hash_state, &hashx_blake2_params);
	hashx_blake2b_update(&hash_state, seed, size);
	hashx_blake2b_final(&hash_state, &keys, sizeof(keys));
	if (ctx->type & HASHX_COMPILED) {
		hashx_program program;
		if (!initialize_program(ctx, &program, keys)) {
			return 0;
		}
		hashx_compile(&program, ctx->code);
		return 1;
	}
	return initialize_program(ctx, ctx->program, keys);
}

void hashx_exec(hashx_ctx* ctx, HASHX_INPUT, void* output) {
	assert(ctx != NULL && ctx != HASHX_NOTSUPP);
	assert(output != NULL);
	assert(ctx->has_program);
	uint64_t r[8];
#ifndef HASHX_BLOCK_MODE
	hashx_siphash24_ctr_state512(&ctx->keys, input, r);
#else
	hashx_blake2b_4r(&ctx->params, input, size, r);
#endif

	if (ctx->type & HASHX_COMPILED) {
		ctx->func(r);
	}
	else {
		hashx_program_execute(ctx->program, r);
	}

	/* Hash finalization with 1 SipRound per 4 registers.
	 * This is required to pass SMHasher.
	 * Adding more SipRounds doesn't seem to have any effects on the
	 * quality of the hash. TODO: Evaluate from a security standpoint. */
	SIPROUND(r[0], r[1], r[2], r[3]);
	SIPROUND(r[4], r[5], r[6], r[7]);

#if HASHX_SIZE != 32
	uint8_t temp_hash[32];
#else
	uint8_t* temp_hash = (uint8_t*)output;
#endif

	store64(temp_hash +  0, r[0] ^ r[4]);
	store64(temp_hash +  8, r[1] ^ r[5]);
	store64(temp_hash + 16, r[2] ^ r[6]);
	store64(temp_hash + 24, r[3] ^ r[7]);

#if HASHX_SIZE != 32
	memcpy(output, temp_hash, HASHX_SIZE);
#endif
}
