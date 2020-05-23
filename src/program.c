/* Copyright (c) 2020 tevador <tevador@gmail.com> */
/* See LICENSE for licensing information */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "program.h"
#include "unreachable.h"
#include "siphash_rng.h"

/* instructions are generated until this CPU cycle */
#define TARGET_CYCLE 170

/* requirements for the program to be acceptable */
#define REQUIREMENT_SIZE 510
#define REQUIREMENT_MUL_COUNT 170
#define REQUIREMENT_LATENCY 173

/* R5 (x86 = r13) register cannot be used as the destination of INSTR_ADD_RS */
#define REGISTER_NEEDS_DISPLACEMENT 5

#define PORT_MAP_SIZE (TARGET_CYCLE + 4)
#define NUM_PORTS 3
#define MAX_RETRIES 1

#define TRACE false
#define TRACE_PRINT(...) do { if (TRACE) printf(__VA_ARGS__); } while (false)

#define MAX(a,b) ((a) > (b) ? (a) : (b))

/* If the instruction is a multiplication.  */
static inline bool is_mul(instr_type type) {
	return type <= INSTR_MUL_R;
}

/* If the instruction is a 64x64->128 bit multiplication.  */
static inline bool is_wide_mul(instr_type type) {
	return type < INSTR_MUL_R;
}

/* Ivy Bridge integer execution ports: P0, P1, P5 */
typedef enum execution_port {
	PORT_NONE = 0,
	PORT_P0 = 1,
	PORT_P1 = 2,
	PORT_P5 = 4,
	PORT_P01 = PORT_P0 | PORT_P1,
	PORT_P05 = PORT_P0 | PORT_P5,
	PORT_P015 = PORT_P0 | PORT_P1 | PORT_P5
} execution_port;

typedef struct instr_template {
	instr_type type;          /* instruction type */
	const char* x86_asm;      /* x86 assembly */
	int x86_size;             /* x86 code size */
	int latency;              /* latency in cycles */
	execution_port uop1;      /* ex. ports for the 1st uop */
	execution_port uop2;      /* ex. ports for the 2nd uop */
	uint32_t immediate_mask;  /* mask for imm32 */
	instr_type group;         /* instruction group */
	bool imm_can_be_0;        /* if imm32 can be zero */
	bool distinct_dst;        /* if dst and src must be distinct */
	bool op_par_src;          /* operation parameter is equal to src */
	bool has_src;             /* if the instruction has a source operand */
} instr_template;

typedef struct register_info {
	int latency;              /* cycle when the register value will be ready */
	instr_type last_op;       /* last op applied to the register */
	int last_op_par;          /* parameter of the last op (-1 = constant) */
} register_info;

typedef struct generator_ctx {
	int cycle;
	int mul_count;
	bool chain_mul;
	int latency;
	siphash_rng gen;
	register_info registers[8];
	execution_port ports[PORT_MAP_SIZE][NUM_PORTS];
} generator_ctx;

const static instr_template tpl_umulh_r = {
	.type = INSTR_UMULH_R,
	.x86_asm = "mul r",
	.x86_size = 9, /* mov, mul, mov */
	.latency = 4,
	.uop1 = PORT_P1,
	.uop2 = PORT_P5,
	.immediate_mask = 0,
	.group = INSTR_UMULH_R,
	.imm_can_be_0 = false,
	.distinct_dst = false,
	.op_par_src = false,
	.has_src = true
};

const static instr_template tpl_smulh_r = {
	.type = INSTR_SMULH_R,
	.x86_asm = "imul r",
	.x86_size = 9, /* mov, mul, mov */
	.latency = 4,
	.uop1 = PORT_P1,
	.uop2 = PORT_P5,
	.immediate_mask = 0,
	.group = INSTR_SMULH_R,
	.imm_can_be_0 = false,
	.distinct_dst = false,
	.op_par_src = false,
	.has_src = true
};

const static instr_template tpl_mul_r = {
	.type = INSTR_MUL_R,
	.x86_asm = "imul r,r",
	.x86_size = 4,
	.latency = 3,
	.uop1 = PORT_P1,
	.uop2 = PORT_NONE,
	.immediate_mask = 0,
	.group = INSTR_MUL_R,
	.imm_can_be_0 = false,
	.distinct_dst = true,
	.op_par_src = true,
	.has_src = true
};

const static instr_template tpl_sub_r = {
	.type = INSTR_SUB_R,
	.x86_asm = "sub r,r",
	.x86_size = 3,
	.latency = 1,
	.uop1 = PORT_P015,
	.uop2 = PORT_NONE,
	.immediate_mask = 0,
	.group = INSTR_ADD_RS,
	.imm_can_be_0 = false,
	.distinct_dst = true,
	.op_par_src = true,
	.has_src = true
};

const static instr_template tpl_neg = {
	.type = INSTR_NEG,
	.x86_asm = "neg r",
	.x86_size = 3,
	.latency = 1,
	.uop1 = PORT_P015,
	.uop2 = PORT_NONE,
	.immediate_mask = 0,
	.group = INSTR_ADD_C,  /* two's complement negation is basically: */
	.imm_can_be_0 = false, /*    xor r, -1  */
	.distinct_dst = true,  /*    add r, 1   */
	.op_par_src = false,
	.has_src = false
};

const static instr_template tpl_xor_r = {
	.type = INSTR_XOR_R,
	.x86_asm = "xor r,r",
	.x86_size = 3,
	.latency = 1,
	.uop1 = PORT_P015,
	.uop2 = PORT_NONE,
	.immediate_mask = 0,
	.group = INSTR_XOR_R,
	.imm_can_be_0 = false,
	.distinct_dst = true,
	.op_par_src = true,
	.has_src = true
};

const static instr_template tpl_add_rs = {
	.type = INSTR_ADD_RS,
	.x86_asm = "lea r,r+r*s",
	.x86_size = 4,
	.latency = 1,
	.uop1 = PORT_P01,
	.uop2 = PORT_NONE,
	.immediate_mask = 3,
	.group = INSTR_ADD_RS,
	.imm_can_be_0 = true,
	.distinct_dst = true,
	.op_par_src = true,
	.has_src = true
};

const static instr_template tpl_ror_c = {
	.type = INSTR_ROR_C,
	.x86_asm = "ror r,i",
	.x86_size = 4,
	.latency = 1,
	.uop1 = PORT_P05,
	.uop2 = PORT_NONE,
	.immediate_mask = 63,
	.group = INSTR_ROR_C,
	.imm_can_be_0 = false,
	.distinct_dst = true,
	.op_par_src = false,
	.has_src = false
};

const static instr_template tpl_add_c = {
	.type = INSTR_ADD_C,
	.x86_asm = "add r,i",
	.x86_size = 7,
	.latency = 1,
	.uop1 = PORT_P015,
	.uop2 = PORT_NONE,
	.immediate_mask = UINT32_MAX,
	.group = INSTR_ADD_C,
	.imm_can_be_0 = false,
	.distinct_dst = true,
	.op_par_src = false,
	.has_src = false
};

const static instr_template tpl_xor_c = {
	.type = INSTR_XOR_C,
	.x86_asm = "xor r,i",
	.x86_size = 7,
	.latency = 1,
	.uop1 = PORT_P015,
	.uop2 = PORT_NONE,
	.immediate_mask = UINT32_MAX,
	.group = INSTR_XOR_C,
	.imm_can_be_0 = false,
	.distinct_dst = true,
	.op_par_src = false,
	.has_src = false
};

const static instr_template* instr_lookup[] = {	
	&tpl_ror_c,
	&tpl_neg,
	&tpl_xor_c,
	&tpl_add_c,
	&tpl_ror_c,
	&tpl_sub_r,
	&tpl_xor_r,
	&tpl_add_rs,
};

static const instr_template* select_template(generator_ctx* ctx, instr_type last_instr, int attempt) {
	if (ctx->mul_count < ctx->cycle + 1) {
		if (ctx->mul_count % 4 == 0) /* 25% of multiplications are 64x64->128-bit */
			return (hashx_siphash_rng_u8(&ctx->gen) % 2) ? &tpl_smulh_r : &tpl_umulh_r;
		return &tpl_mul_r;
	}
	const instr_template* tpl;
	do {
		/* if the previous attempt failed, try instructions that don't need a src register */
		tpl = instr_lookup[hashx_siphash_rng_u8(&ctx->gen) % (attempt > 0 ? 4 : 8)];
	} while (tpl->group == last_instr);
	return tpl;
}

static void instr_from_template(const instr_template* tpl, siphash_rng* gen, instruction* instr) {
	instr->opcode = tpl->type;
	if (tpl->immediate_mask) {
		do {
			instr->imm32 = hashx_siphash_rng_u32(gen) & tpl->immediate_mask;
		} while (instr->imm32 == 0 && !tpl->imm_can_be_0);
	}
	if (!tpl->op_par_src) {
		if (tpl->distinct_dst) {
			instr->op_par = UINT32_MAX;
		}
		else {
			instr->op_par = hashx_siphash_rng_u32(gen);
		}
	}
	if (!tpl->has_src) {
		instr->src = -1;
	}
}

static bool select_register(int available_regs[8], int regs_count, siphash_rng* gen, int* reg_out) {
	if (regs_count == 0)
		return false;

	int index;

	if (regs_count > 1) {
		index = hashx_siphash_rng_u32(gen) % regs_count;
	}
	else {
		index = 0;
	}
	*reg_out = available_regs[index];
	return true;
}

static bool select_destination(const instr_template* tpl, instruction* instr, generator_ctx* ctx, int cycle) {
	int available_regs[8];
	int regs_count = 0;
	/* Conditions for the destination register:
	// * value must be ready at the required cycle
	// * cannot be the same as the source register unless the instruction allows it
	//   - this avoids optimizable instructions such as "xor r, r" or "sub r, r"
	// * register cannot be multiplied twice in a row unless allowChainedMul is true 
	//   - this avoids accumulation of trailing zeroes in registers due to excessive multiplication
	//   - allowChainedMul is set to true if an attempt to find source/destination registers failed (this is quite rare, but prevents a catastrophic failure of the generator)
	// * either the last instruction applied to the register or its source must be different than this instruction
	//   - this avoids optimizable instruction sequences such as "xor r1, r2; xor r1, r2" or "ror r, C1; ror r, C2" or "add r, C1; add r, C2"
	// * register r5 cannot be the destination of the IADD_RS instruction (limitation of the x86 lea instruction) */
	for (int i = 0; i < 8; ++i) {
		bool available = ctx->registers[i].latency <= cycle;
		available &= ((!tpl->distinct_dst) | (i != instr->src));
		available &= (ctx->chain_mul | (tpl->group != INSTR_MUL_R) | (ctx->registers[i].last_op != INSTR_MUL_R));
		available &= ((ctx->registers[i].last_op != tpl->group) | (ctx->registers[i].last_op_par != instr->op_par));
		available &= ((instr->opcode != INSTR_ADD_RS) | (i != REGISTER_NEEDS_DISPLACEMENT));
		//if (registers[i].latency <= cycle && (canReuse_ || i != src_) && (allowChainedMul || opGroup_ != SuperscalarInstructionType::IMUL_R || registers[i].lastOpGroup != SuperscalarInstructionType::IMUL_R) && (registers[i].lastOpGroup != opGroup_ || registers[i].lastOpPar != opGroupPar_) && (info_->getType() != SuperscalarInstructionType::IADD_RS || i != REGISTER_NEEDS_DISPLACEMENT))
		if (available)
			available_regs[regs_count++] = i;
	}
	return select_register(available_regs, regs_count, &ctx->gen, &instr->dst);
}

static bool select_source(const instr_template* tpl, instruction* instr, generator_ctx* ctx, int cycle) {
	int available_regs[8];
	int regs_count = 0;
	/* all registers that are ready at the cycle */
	for (int i = 0; i < 8; ++i) {
		if (ctx->registers[i].latency <= cycle)
			available_regs[regs_count++] = i;
	}
	/* if there are only 2 available registers for ADD_RS and one of them is r5, select it as the source because it cannot be the destination */
	if (regs_count == 2 && instr->opcode == INSTR_ADD_RS) {
		if (available_regs[0] == REGISTER_NEEDS_DISPLACEMENT || available_regs[1] == REGISTER_NEEDS_DISPLACEMENT) {
			instr->op_par = instr->src = REGISTER_NEEDS_DISPLACEMENT;
			return true;
		}
	}
	if (select_register(available_regs, regs_count, &ctx->gen, &instr->src)) {
		if (tpl->op_par_src)
			instr->op_par = instr->src;
		return true;
	}
	return false;
}

static int schedule_uop(execution_port uop, generator_ctx* ctx, int cycle, bool commit) {
	/* The scheduling here is done optimistically by checking port availability in order P5 -> P0 -> P1 to not overload
	   port P1 (multiplication) by instructions that can go to any port. */
	for (; cycle < PORT_MAP_SIZE; ++cycle) {
		if ((uop & PORT_P5) && !ctx->ports[cycle][2]) {
			if (commit) {
				ctx->ports[cycle][2] = uop;
			}
			return cycle;
		}
		if ((uop & PORT_P0) && !ctx->ports[cycle][0]) {
			if (commit) {
				ctx->ports[cycle][0] = uop;
			}
			return cycle;
		}
		if ((uop & PORT_P1) != 0 && !ctx->ports[cycle][1]) {
			if (commit) {
				ctx->ports[cycle][1] = uop;
			}
			return cycle;
		}
	}
	return -1;
}

static int schedule_instr(const instr_template* tpl, generator_ctx* ctx, bool commit) {
	if (tpl->uop2 == PORT_NONE) {
		/* this instruction has only one uOP */
		return schedule_uop(tpl->uop1, ctx, ctx->cycle, commit);
	}
	else {
		/* instructions with 2 uOPs are scheduled conservatively by requiring both uOPs to execute in the same cycle */
		for (int cycle = ctx->cycle; cycle < PORT_MAP_SIZE; ++cycle) {

			int cycle1 = schedule_uop(tpl->uop1, ctx, cycle, false);
			int cycle2 = schedule_uop(tpl->uop2, ctx, cycle, false);

			if (cycle1 >= 0 && cycle1 == cycle2) {
				if (commit) {
					schedule_uop(tpl->uop1, ctx, cycle, true);
					schedule_uop(tpl->uop2, ctx, cycle, false);
				}
				return cycle1;
			}
		}
	}

	return -1;
}

static void print_registers(const generator_ctx* ctx) {
	for (int i = 0; i < 8; ++i) {
		printf("   R%i = %i\n", i, ctx->registers[i].latency);
	}
}

bool hashx_program_generate(const siphash_state* key, hashx_program* program) {
	generator_ctx ctx = {
		.cycle = 0,
		.mul_count = 0,
		.chain_mul = false,
		.latency = 0,
		.ports = { 0 }
	};
	hashx_siphash_rng_init(&ctx.gen, key);
	for (int i = 0; i < 8; ++i) {
		ctx.registers[i].last_op = -1;
		ctx.registers[i].latency = 0;
		ctx.registers[i].last_op_par = -1;
	}
	program->code_size = 0;
	int sub_cycle = 0; /* 3 sub-cycles = 1 CPU cycle
						  this assumes that the CPU can decode 3 instructions
						  per cycle on average */
	int attempt = 0;
	instr_type last_instr = -1;
#ifdef HASHX_PROGRAM_STATS
	program->x86_size = 0;
#endif

	while (program->code_size < HASHX_PROGRAM_MAX_SIZE) {
		instruction* instr = &program->code[program->code_size];
		TRACE_PRINT("CYCLE: %i\n", ctx.cycle);

		/* select an instruction template */
		const instr_template* tpl = select_template(&ctx, last_instr, attempt);
		last_instr = tpl->group;

		TRACE_PRINT("Template: %s\n", tpl->x86_asm);

		instr_from_template(tpl, &ctx.gen, instr);

		/* calculate the earliest cycle when this instruction (all of its uOPs) can be scheduled for execution */
		int scheduleCycle = schedule_instr(tpl, &ctx, false);
		if (scheduleCycle < 0) {
			TRACE_PRINT("Unable to map operation '%s' to execution port (cycle %i)\n", tpl->x86_asm, ctx.cycle);
			/* __debugbreak(); */
			break;
		}

		ctx.chain_mul = attempt > 0;

		/* find a source register (if applicable) that will be ready when this instruction executes */
		if (tpl->has_src) {
			if (!select_source(tpl, instr, &ctx, scheduleCycle)) {
				TRACE_PRINT("; src STALL (attempt %i)\n", attempt);
				if (attempt++ < MAX_RETRIES) {
					continue;
				}
				if (TRACE) {
					printf("; select_source FAILED at cycle %i\n", ctx.cycle);
					print_registers(&ctx);
					/* __debugbreak(); */
				}
				sub_cycle += 3;
				ctx.cycle = sub_cycle / 3;
				attempt = 0;
				continue;
			}
			TRACE_PRINT("; src = r%i\n", instr->src);
		}

		/* find a destination register that will be ready when this instruction executes */
		{
			if (!select_destination(tpl, instr, &ctx, scheduleCycle)) {
				TRACE_PRINT("; dst STALL (attempt %i)\n", attempt);
				if (attempt++ < MAX_RETRIES) {
					continue;
				}
				if (TRACE) {
					printf("; select_destination FAILED at cycle %i\n", ctx.cycle);
					print_registers(&ctx);
					/* __debugbreak(); */
				}
				sub_cycle += 3;
				ctx.cycle = sub_cycle / 3;
				attempt = 0;
				continue;
			}
			TRACE_PRINT("; dst = r%i\n", instr->dst);
		}
		attempt = 0;

		/* recalculate when the instruction can be scheduled for execution based on operand availability */
		scheduleCycle = schedule_instr(tpl, &ctx, true);

		if (scheduleCycle < 0) {
			TRACE_PRINT("Unable to map operation '%s' to execution port (cycle %i)\n", tpl->x86_asm, ctx.cycle);
			break;
		}

		TRACE_PRINT("Scheduled at cycle %i\n", scheduleCycle);

		/* terminating condition */
		if (scheduleCycle >= TARGET_CYCLE) {
			break;
		}

		register_info* ri = &ctx.registers[instr->dst];
		int retireCycle = scheduleCycle + tpl->latency;
		ri->latency = retireCycle;
		ri->last_op = tpl->group;
		ri->last_op_par = instr->op_par;
		ctx.latency = MAX(retireCycle, ctx.latency);
		TRACE_PRINT("; RETIRED at cycle %i\n", retireCycle);

		program->code_size++;
#ifdef HASHX_PROGRAM_STATS
		program->x86_size += tpl->x86_size;
#endif

		ctx.mul_count += is_mul(instr->opcode);

		++sub_cycle;
		ctx.cycle = sub_cycle / 3;
	}

#ifdef HASHX_PROGRAM_STATS
	memset(program->asic_latencies, 0, sizeof(program->asic_latencies));

	program->counter = ctx.gen.counter;
	program->wide_mul_count = 0;
	program->mul_count = ctx.mul_count;

	/* Calculate ASIC latency:
	   Assumes 1 cycle latency for all operations and unlimited parallelization. */
	for (int i = 0; i < program->code_size; ++i) {
		instruction* instr = &program->code[i];
		int last_dst = program->asic_latencies[instr->dst] + 1;
		int lat_src = instr->dst != instr->src ? program->asic_latencies[instr->src] + 1 : 0;
		program->asic_latencies[instr->dst] = MAX(last_dst, lat_src);
		program->wide_mul_count += is_wide_mul(instr->opcode);
	}

	program->asic_latency = 0;
	program->cpu_latency = 0;
	for (int i = 0; i < 8; ++i) {
		program->asic_latency = MAX(program->asic_latency, program->asic_latencies[i]);
		program->cpu_latencies[i] = ctx.registers[i].latency;
		program->cpu_latency = MAX(program->cpu_latency, program->cpu_latencies[i]);
	}

	program->ipc = program->code_size / (double)program->cpu_latency;

	if (TRACE) {
		printf("; ALU port utilization:\n");
		printf("; (* = in use, _ = idle)\n");
		for (int i = 0; i < PORT_MAP_SIZE; ++i) {
			printf("; %3i ", i);
			for (int j = 0; j < NUM_PORTS; ++j) {
				printf("%c", (ctx.ports[i][j] ? '*' : '_'));
			}
			printf("\n");
		}
	}
#endif

	/* reject programs that don't meet the uniform complexity requirements */
	/* this doesn't happen in practice */
	return
		(program->code_size == REQUIREMENT_SIZE) &
		(ctx.mul_count == REQUIREMENT_MUL_COUNT) &
		(ctx.latency == REQUIREMENT_LATENCY - 1); /* cycles are numbered from 0 */
}

static const char* x86_reg_map[] = { "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15" };

void hashx_program_asm_x86(const hashx_program* program) {
	for (unsigned i = 0; i < program->code_size; ++i) {
		const instruction* instr = &program->code[i];
		switch (instr->opcode)
		{
		case INSTR_SUB_R:
			printf("sub %s, %s\n", x86_reg_map[instr->dst], x86_reg_map[instr->src]);
			break;
		case INSTR_XOR_R:
			printf("xor %s, %s\n", x86_reg_map[instr->dst], x86_reg_map[instr->src]);
			break;
		case INSTR_ADD_RS:
			printf("lea %s, [%s+%s*%u]\n", x86_reg_map[instr->dst], x86_reg_map[instr->dst], x86_reg_map[instr->src], 1 << instr->imm32);
			break;
		case INSTR_MUL_R:
			printf("imul %s, %s\n", x86_reg_map[instr->dst], x86_reg_map[instr->src]);
			break;
		case INSTR_ROR_C:
			printf("ror %s, %u\n", x86_reg_map[instr->dst], instr->imm32);
			break;
		case INSTR_ADD_C:
			printf("add %s, %i\n", x86_reg_map[instr->dst], instr->imm32);
			break;
		case INSTR_XOR_C:
			printf("xor %s, %i\n", x86_reg_map[instr->dst], instr->imm32);
			break;
		case INSTR_UMULH_R:
			printf("mov rax, %s\n", x86_reg_map[instr->dst]);
			printf("mul %s\n", x86_reg_map[instr->src]);
			printf("mov %s, rdx\n", x86_reg_map[instr->dst]);
			break;
		case INSTR_SMULH_R:
			printf("mov rax, %s\n", x86_reg_map[instr->dst]);
			printf("imul %s\n", x86_reg_map[instr->src]);
			printf("mov %s, rdx\n", x86_reg_map[instr->dst]);
			break;
		case INSTR_NEG:
			printf("neg %s\n", x86_reg_map[instr->dst]);
			break;
		default:
			UNREACHABLE;
		}
	}
}
