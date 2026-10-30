#ifndef LINUX_APP_BPF_INSN_H
#define LINUX_APP_BPF_INSN_H

#include <linux/bpf.h>
#include <stdint.h>

#define BPF_INSN(CODE, DST, SRC, OFF, IMM) \
    ((struct bpf_insn){                    \
        .code = (CODE),                    \
        .dst_reg = (DST),                  \
        .src_reg = (SRC),                  \
        .off = (OFF),                      \
        .imm = (IMM),                      \
    })

#define BPF_MOV64_IMM(DST, IMM) \
    BPF_INSN(BPF_ALU64 | BPF_MOV | BPF_K, DST, 0, 0, IMM)
#define BPF_MOV64_REG(DST, SRC) \
    BPF_INSN(BPF_ALU64 | BPF_MOV | BPF_X, DST, SRC, 0, 0)
#define BPF_ALU64_IMM(OP, DST, IMM) \
    BPF_INSN(BPF_ALU64 | (OP) | BPF_K, DST, 0, 0, IMM)
#define BPF_ALU64_REG(OP, DST, SRC) \
    BPF_INSN(BPF_ALU64 | (OP) | BPF_X, DST, SRC, 0, 0)
#define BPF_LDX_MEM(SIZE, DST, SRC, OFF) \
    BPF_INSN(BPF_LDX | BPF_MEM | (SIZE), DST, SRC, OFF, 0)
#define BPF_STX_MEM(SIZE, DST, SRC, OFF) \
    BPF_INSN(BPF_STX | BPF_MEM | (SIZE), DST, SRC, OFF, 0)
#define BPF_ST_MEM(SIZE, DST, OFF, IMM) \
    BPF_INSN(BPF_ST | BPF_MEM | (SIZE), DST, 0, OFF, IMM)
#define BPF_STX_XADD(SIZE, DST, SRC, OFF) \
    BPF_INSN(BPF_STX | BPF_XADD | (SIZE), DST, SRC, OFF, 0)
#define BPF_JMP_IMM(OP, DST, IMM, OFF) \
    BPF_INSN(BPF_JMP | (OP) | BPF_K, DST, 0, OFF, IMM)
#define BPF_CALL_FUNC(FUNC) BPF_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, FUNC)
#define BPF_EXIT_INSN() BPF_INSN(BPF_JMP | BPF_EXIT, 0, 0, 0, 0)

static inline void
emit_ld_imm64(struct bpf_insn *insns, int *count, int dst, uint64_t value)
{
    insns[(*count)++] = BPF_INSN(BPF_LD | BPF_DW | BPF_IMM, dst, 0, 0,
                                 (int32_t)value);
    insns[(*count)++] = BPF_INSN(0, 0, 0, 0, (int32_t)(value >> 32));
}

static inline void
emit_ld_map_fd(struct bpf_insn *insns, int *count, int dst, int map_fd)
{
    insns[(*count)++] = BPF_INSN(BPF_LD | BPF_DW | BPF_IMM, dst,
                                 BPF_PSEUDO_MAP_FD, 0, map_fd);
    insns[(*count)++] = BPF_INSN(0, 0, 0, 0, 0);
}

#endif
