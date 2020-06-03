/* Copyright (c) 2020 tevador <tevador@gmail.com> */
/* See LICENSE for licensing information */

#include "test_utils.h"
#include <time.h>
#include <limits.h>
#include <inttypes.h>

int main(int argc, char** argv) {
	int nonces, seeds, start, diff;
	bool interpret;
	read_int_option("--diff", argc, argv, &diff, INT_MAX);
	read_int_option("--start", argc, argv, &start, 0);
	read_int_option("--seeds", argc, argv, &seeds, 500);
	read_int_option("--nonces", argc, argv, &nonces, 65536);
	read_option("--interpret", argc, argv, &interpret);
	hashx_type flags = HASHX_INTERPRETED;
	if (!interpret) {
		flags = HASHX_COMPILED;
	}
	hashx_ctx* ctx = hashx_alloc(flags);
	if (ctx == NULL) {
		printf("Error: memory allocation failure\n");
		return 1;
	}
	if (ctx == HASHX_NOTSUPP) {
		printf("Error: not supported. Try with --interpret\n");
		return 1;
	}
	uint64_t best_hash = UINT64_MAX;
	uint64_t diff_ex = (uint64_t)diff * 1000ULL;
	uint64_t threshold = UINT64_MAX / diff_ex;
	int seeds_end = seeds + start;
	printf("Interpret: %i, Target diff.: %" PRIu64 "\n", interpret, diff_ex);
	printf("Testing seeds %i-%i with %i nonces each ...\n", start, seeds_end - 1, nonces);
	clock_t clock_start, clock_end;
	clock_start = clock();
	int64_t total_hashes = 0;
	for (int seed = start; seed < seeds_end; ++seed) {
		if (!hashx_make(ctx, &seed, sizeof(seed))) {
			continue;
		}
		for (int nonce = 0; nonce < nonces; ++nonce) {
			uint64_t hash[4] = { 0 };
#ifndef HASHX_BLOCK_MODE
			hashx_exec(ctx, nonce, hash);
#else
			hashx_exec(ctx, &nonce, sizeof(nonce), hash);
#endif
			if (hash[0] < best_hash) {
				best_hash = hash[0];
			}
			if (hash[0] < threshold) {
				printf("* Hash (%5i, %5i) below threshold: ...", seed, nonce);
				output_hex((char*)hash, 8);
				printf("\n");
			}
		}
		total_hashes += nonces;
	}
	clock_end = clock();
	double elapsed = (clock_end - clock_start) / (double)CLOCKS_PER_SEC;
	printf("Total hashes: %" PRIi64 "\n", total_hashes);
	printf("%f hashes/sec.\n", total_hashes / elapsed);
	printf("%f seeds/sec.\n", seeds / elapsed);
	printf("Best hash: ...");
	output_hex((char*)&best_hash, sizeof(best_hash));
	printf(" (diff: %" PRIu64 ")\n", UINT64_MAX / best_hash);
	return 0;
}
