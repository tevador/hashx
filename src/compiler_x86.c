/* Copyright (c) 2020 tevador <tevador@gmail.com> */
/* See LICENSE for licensing information */

#include <string.h>

#include "compiler.h"
#include "program.h"
#include "virtual_memory.h"
#include "unreachable.h"

#if defined(_WIN32) || defined(__CYGWIN__)
#define WINABI
#endif

#define EMIT(p,x) do {           \
		memcpy(p, x, sizeof(x)); \
		p += sizeof(x);          \
	} while (0)
#define EMIT_BYTE(p,x) *((p)++) = x
#define EMIT_U16(p,x) *((uint16_t*)(p)) = x; p += sizeof(uint16_t)
#define EMIT_U32(p,x) *((uint32_t*)(p)) = x; p += sizeof(uint32_t)
#define EMIT_U64(p,x) *((uint64_t*)(p)) = x; p += sizeof(uint64_t)

#define GEN_SIB(scale, index, base) ((scale << 6) | (index << 3) | base)

static const uint8_t x86_prologue[] = {
#ifndef WINABI
	0x48, 0x89, 0xF9,             /* mov rcx, rdi */
	0x4C, 0x89, 0xE6,             /* mov rsi, r12 */
	0x4C, 0x89, 0xEF,             /* mov rdi, r13 */
	0x41, 0x56,                   /* push r14 */
	0x41, 0x57,                   /* push r15 */
#else
	0x4C, 0x89, 0x64, 0x24, 0x08, /* mov qword ptr [rsp+8], r12 */
	0x4C, 0x89, 0x6C, 0x24, 0x10, /* mov qword ptr [rsp+16], r13 */
	0x4C, 0x89, 0x74, 0x24, 0x18, /* mov qword ptr [rsp+24], r14 */
	0x4C, 0x89, 0x7C, 0x24, 0x20, /* mov qword ptr [rsp+32], r15 */
#endif
	0x4C, 0x8B, 0x01,             /* mov r8, qword ptr [rcx+0] */
	0x4C, 0x8B, 0x49, 0x08,       /* mov r9, qword ptr [rcx+8] */
	0x4C, 0x8B, 0x51, 0x10,       /* mov r10, qword ptr [rcx+16] */
	0x4C, 0x8B, 0x59, 0x18,       /* mov r11, qword ptr [rcx+24] */
	0x4C, 0x8B, 0x61, 0x20,       /* mov r12, qword ptr [rcx+32] */
	0x4C, 0x8B, 0x69, 0x28,       /* mov r13, qword ptr [rcx+40] */
	0x4C, 0x8B, 0x71, 0x30,       /* mov r14, qword ptr [rcx+48] */
	0x4C, 0x8B, 0x79, 0x38        /* mov r15, qword ptr [rcx+56] */
};

static const uint8_t x86_epilogue[] = {
	0x4C, 0x89, 0x01,             /* mov qword ptr [rcx+0], r8 */
	0x4C, 0x89, 0x49, 0x08,       /* mov qword ptr [rcx+8], r9 */
	0x4C, 0x89, 0x51, 0x10,       /* mov qword ptr [rcx+16], r10 */
	0x4C, 0x89, 0x59, 0x18,       /* mov qword ptr [rcx+24], r11 */
	0x4C, 0x89, 0x61, 0x20,       /* mov qword ptr [rcx+32], r12 */
	0x4C, 0x89, 0x69, 0x28,       /* mov qword ptr [rcx+40], r13 */
	0x4C, 0x89, 0x71, 0x30,       /* mov qword ptr [rcx+48], r14 */
	0x4C, 0x89, 0x79, 0x38,       /* mov qword ptr [rcx+56], r15 */
#ifndef WINABI
	0x41, 0x5F,                   /* pop r15 */
	0x41, 0x5E,                   /* pop r14 */
	0x49, 0x89, 0xFD,             /* mov r13, rdi */
	0x49, 0x89, 0xF4,             /* mov r12, rsi */
#else
	0x4C, 0x8B, 0x64, 0x24, 0x08, /* mov r12, qword ptr [rsp+8] */
	0x4C, 0x8B, 0x6C, 0x24, 0x10, /* mov r13, qword ptr [rsp+16] */
	0x4C, 0x8B, 0x74, 0x24, 0x18, /* mov r14, qword ptr [rsp+24] */
	0x4C, 0x8B, 0x7C, 0x24, 0x20, /* mov r15, qword ptr [rsp+32] */
#endif
	0xC3                          /* ret */
};

void hashx_compile_x86(const hashx_program* program, uint8_t* code) {
	hashx_vm_rw(code, COMP_CODE_SIZE);
	uint8_t* pos = code;
	EMIT(pos, x86_prologue);
	for (int i = 0; i < program->code_size; ++i) {
		const instruction* instr = &program->code[i];
		switch (instr->opcode)
		{
		case INSTR_UMULH_R:
			EMIT_U64(pos, 0x8b4ce0f749c08b49 |
				(((uint64_t)instr->src) << 40) |
				(instr->dst << 16));
			EMIT_BYTE(pos, 0xc2 + 8 * instr->dst);
			break;
		case INSTR_SMULH_R:
			EMIT_U64(pos, 0x8b4ce8f749c08b49 |
				(((uint64_t)instr->src) << 40) |
				(instr->dst << 16));
			EMIT_BYTE(pos, 0xc2 + 8 * instr->dst);
			break;
		case INSTR_MUL_R:
			EMIT_U32(pos, 0xc0af0f4d | (instr->dst << 27) | (instr->src << 24));
			break;
		case INSTR_SUB_R:
			EMIT_U16(pos, 0x2b4d);
			EMIT_BYTE(pos, 0xc0 | (instr->dst << 3) | instr->src);
			break;
		case INSTR_NEG:
			EMIT_U16(pos, 0xf749);
			EMIT_BYTE(pos, 0xd8 | instr->dst);
			break;
		case INSTR_XOR_R:
			EMIT_U16(pos, 0x334d);
			EMIT_BYTE(pos, 0xc0 | (instr->dst << 3) | instr->src);
			break;
		case INSTR_ADD_RS:
			EMIT_U32(pos, 0x00048d4f |
				(instr->dst << 19) |
				GEN_SIB(instr->imm32, instr->src, instr->dst) << 24);
			break;
		case INSTR_ROR_C:
			EMIT_U32(pos, 0x00c8c149 | (instr->dst << 16) | (instr->imm32 << 24));
			break;
		case INSTR_ADD_C:
			EMIT_U16(pos, 0x8149);
			EMIT_BYTE(pos, 0xc0 | instr->dst);
			EMIT_U32(pos, instr->imm32);
			break;
		case INSTR_XOR_C:
			EMIT_U16(pos, 0x8149);
			EMIT_BYTE(pos, 0xf0 | instr->dst);
			EMIT_U32(pos, instr->imm32);
			break;
		default:
			UNREACHABLE;
		}
	}
	EMIT(pos, x86_epilogue);
	hashx_vm_rx(code, COMP_CODE_SIZE);
}
