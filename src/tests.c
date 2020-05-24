/* Copyright (c) 2020 tevador <tevador@gmail.com> */
/* See LICENSE for licensing information */

#ifdef NDEBUG
#undef NDEBUG
#endif

#include <assert.h>
#include <stdio.h>
#include <hashx.h>
#include <stdbool.h>
#include <string.h>

typedef bool test_func();

static int test_no = 0;

static hashx_ctx* ctx_int = NULL;
static hashx_ctx* ctx_cmp = NULL;

static const char seed1[] = "This is a test";
static const char seed2[] = "Lorem ipsum dolor sit amet";

static const uint64_t counter1 = 0;
static const uint64_t counter2 = 123456;
static const uint64_t counter3 = 987654321123456789;

static const unsigned char long_input[] = {
	0x0b, 0x0b, 0x98, 0xbe, 0xa7, 0xe8, 0x05, 0xe0, 0x01, 0x0a, 0x21, 0x26,
	0xd2, 0x87, 0xa2, 0xa0, 0xcc, 0x83, 0x3d, 0x31, 0x2c, 0xb7, 0x86, 0x38,
	0x5a, 0x7c, 0x2f, 0x9d, 0xe6, 0x9d, 0x25, 0x53, 0x7f, 0x58, 0x4a, 0x9b,
	0xc9, 0x97, 0x7b, 0x00, 0x00, 0x00, 0x00, 0x66, 0x6f, 0xd8, 0x75, 0x3b,
	0xf6, 0x1a, 0x86, 0x31, 0xf1, 0x29, 0x84, 0xe3, 0xfd, 0x44, 0xf4, 0x01,
	0x4e, 0xca, 0x62, 0x92, 0x76, 0x81, 0x7b, 0x56, 0xf3, 0x2e, 0x9b, 0x68,
	0xbd, 0x82, 0xf4, 0x16
};

#define RUN_TEST(x) run_test(#x, &x)

static void run_test(const char* name, test_func* func) {
	printf("[%2i] %-40s ... ", ++test_no, name);
	printf(func() ? "PASSED\n" : "SKIPPED\n");
}

static inline char parse_nibble(char hex) {
	hex &= ~0x20;
	return (hex & 0x40) ? hex - ('A' - 10) : hex & 0xf;
}

static void hex2bin(const char* in, int length, char* out) {
	for (int i = 0; i < length; i += 2) {
		char nibble1 = parse_nibble(*in++);
		char nibble2 = parse_nibble(*in++);
		*out++ = nibble1 << 4 | nibble2;
	}
}

static void output_hex(const char* data, int length) {
	for (unsigned i = 0; i < length; ++i)
		printf("%02x", data[i] & 0xff);
}

static bool hashes_equal(char* a, char* b) {
	return memcmp(a, b, HASHX_SIZE) == 0;
}

static bool equals_hex(const void* hash, const char* hex) {
	char reference[HASHX_SIZE];
	hex2bin(hex, 2 * HASHX_SIZE, reference);
	return memcmp(hash, reference, sizeof(reference)) == 0;
}

static bool test_alloc() {
	ctx_int = hashx_alloc(HASHX_INTERPRETED);
	assert(ctx_int != NULL && ctx_int != HASHX_NOTSUPP);
	return true;
}

static bool test_free() {
	hashx_free(ctx_int);
	hashx_free(ctx_cmp);
	return true;
}

static bool test_make1() {
	int result = hashx_make(ctx_int, seed1, sizeof(seed1));
	assert(result == 1);
	return true;
}

static bool test_hash_ctr1() {
#ifndef HASHX_BLOCK_MODE
	char hash[HASHX_SIZE];
	hashx_exec(ctx_int, counter2, hash);
	/* printf("\n");
	output_hex(hash, HASHX_SIZE);
	printf("\n"); */
	assert(equals_hex(hash, "aa0a9294e37de61561a6f67c6eb5cf7de7ffc83928d140b72cc27a00f398f889"));
	return true;
#else
	return false;
#endif
}

static bool test_hash_ctr2() {
#ifndef HASHX_BLOCK_MODE
	char hash[HASHX_SIZE];
	hashx_exec(ctx_int, counter1, hash);
	assert(equals_hex(hash, "ebb08958003246d82bcdb3bde7b067e087e19b20583139b95a5e2e19673f741e"));
	return true;
#else
	return false;
#endif
}

static bool test_make2() {
	int result = hashx_make(ctx_int, seed2, sizeof(seed2));
	assert(result == 1);
	return true;
}

static bool test_hash_ctr3() {
#ifndef HASHX_BLOCK_MODE
	char hash[HASHX_SIZE];
	hashx_exec(ctx_int, counter2, hash);
	assert(equals_hex(hash, "408fe2f609bf743d7401b469f4c4da72b12deef846069f75edafe7dcc1aae9ef"));
	return true;
#else
	return false;
#endif
}

static bool test_hash_ctr4() {
#ifndef HASHX_BLOCK_MODE
	char hash[HASHX_SIZE];
	hashx_exec(ctx_int, counter3, hash);
	assert(equals_hex(hash, "e6a38a783dba1153a94babe97ee84c04348148e5440ac23859b80f37cf208e8f"));
	return true;
#else
	return false;
#endif
}

static bool test_hash_block1() {
#ifndef HASHX_BLOCK_MODE
	return false;
#else
	char hash[HASHX_SIZE];
	hashx_exec(ctx_int, long_input, sizeof(long_input), hash);
	assert(equals_hex(hash, "bcf8c222c9530e6bed3af1472b90258033a24bb4b31aa71db037b1b5d8cb11c4"));
	return true;
#endif
}

static bool test_alloc_compiler() {
	ctx_cmp = hashx_alloc(HASHX_COMPILED);
	assert(ctx_cmp != NULL);
	return ctx_cmp != HASHX_NOTSUPP;
}

static bool test_make3() {
	if (ctx_cmp == HASHX_NOTSUPP)
		return false;

	int result = hashx_make(ctx_cmp, seed2, sizeof(seed2));
	assert(result == 1);
	return true;
}

static bool test_compiler_ctr1() {
	if (ctx_cmp == HASHX_NOTSUPP)
		return false;

#ifndef HASHX_BLOCK_MODE
	char hash1[HASHX_SIZE];
	char hash2[HASHX_SIZE];
	hashx_exec(ctx_int, counter2, hash1);
	hashx_exec(ctx_cmp, counter2, hash2);
	assert(hashes_equal(hash1, hash2));
	return true;
#else
	return false;
#endif
}

static bool test_compiler_ctr2() {
	if (ctx_cmp == HASHX_NOTSUPP)
		return false;

#ifndef HASHX_BLOCK_MODE
	char hash1[HASHX_SIZE];
	char hash2[HASHX_SIZE];
	hashx_exec(ctx_int, counter3, hash1);
	hashx_exec(ctx_cmp, counter3, hash2);
	assert(hashes_equal(hash1, hash2));
	return true;
#else
	return false;
#endif
}

static bool test_compiler_block1() {
	if (ctx_cmp == HASHX_NOTSUPP)
		return false;
#ifndef HASHX_BLOCK_MODE
	return false;
#else
	char hash1[HASHX_SIZE];
	char hash2[HASHX_SIZE];
	hashx_exec(ctx_int, long_input, sizeof(long_input), hash1);
	hashx_exec(ctx_cmp, long_input, sizeof(long_input), hash2);
	assert(hashes_equal(hash1, hash2));
	return true;
#endif
}

int main() {
	RUN_TEST(test_alloc);
	RUN_TEST(test_make1);
	RUN_TEST(test_hash_ctr1);
	RUN_TEST(test_hash_ctr2);
	RUN_TEST(test_make2);
	RUN_TEST(test_hash_ctr3);
	RUN_TEST(test_hash_ctr4);
	RUN_TEST(test_alloc_compiler);
	RUN_TEST(test_make3);
	RUN_TEST(test_compiler_ctr1);
	RUN_TEST(test_compiler_ctr2);
	RUN_TEST(test_hash_block1);
	RUN_TEST(test_compiler_block1);
	RUN_TEST(test_free);
	
	printf("\nAll tests were successful\n");
	return 0;
}
