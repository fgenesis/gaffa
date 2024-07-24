#pragma once

#include "defs.h"
#include "typing.h"

/*
General VM design:
- all instructions are 32bit wide
- 8-bit register indices (ie. addressible with single byte)
Instruction format:
- 8 bit opcodes, with different encodings

OToABC:
- 3 bytes target, src1, src2

oXXX:
-



Topost 2 bits: Category:
00: Misc
01: Arith



T - type bits (always 2)
     - 00: uint
     - 01: int
     - 10: float
     - 11: anything els
o - sub-opcode
A, B, C - register idx
Q - constants
*/

enum GaOpMasks
{
    GA_OPMASK_ARITH_OP       = 0x0f,        // 4 bits
    GA_OPMASK_ARITH_TYPEMASK = (0x03 << 4), // 2 bits, one of GaOpTypes

    // for checking that (op & GA_OPMASK_ARITH_IDMASK) == GA_OPMASK_ARITH_ID
    GA_OPMASK_ARITH_ID       = (0x01 << 6), // 2 bits,
    GA_OPMASK_ARITH_IDMASK    = (0x03 << 6), // 2 bits,
};

enum GaOpTypes
{
    GA_OP_T_UINT    = (0 << 4),
    GA_OP_T_INT     = (1 << 4),
    GA_OP_T_FLOAT   = (2 << 4),
    GA_OP_T_OTHER   = (3 << 4),
};


enum GaOpGroup
{
    GA_OP_ARITH,  // 01TToooo, dst(A), src1(B), src2(C)
    GA_OP_CMP,    // 00000ooo, compare regs A and B, pc += s8(C) if failed
    GA_OP_MOV,    // 00001000, A..A+u8(C) = B..B+u8(C)
    GA_OP_CAST,   // 00TT1001, A = type(A), where type = Q(u16(BC))



    GA_OP_CALL,   // 9

};

enum GaArithOp
{
    GA_ARITHOP_ADD,
    GA_ARITHOP_SUB,
    GA_ARITHOP_MUL,
    GA_ARITHOP_DIV,
    GA_ARITHOP_MOD,
    GA_ARITHOP_BIN_AND,
    GA_ARITHOP_BIN_OR,
    GA_ARITHOP_BIN_XOR,
    GA_ARITHOP_SHL,
    GA_ARITHOP_SHR,
};

enum GaCmpOp
{
    GA_CMPOP_C_EQ,
    GA_CMPOP_C_NEQ,
    GA_CMPOP_C_LT,
    GA_CMPOP_C_GT,
    GA_CMPOP_C_LTE,
    GA_CMPOP_C_GTE,
    GA_CMPOP_C_AND,
    GA_CMPOP_C_OR,
};

enum GaMiscOp
{
    GA_MISCOP_EVAL_AND,
    GA_MISCOP_EVAL_OR,
    GA_MISCOP_CONCAT,
};

typedef unsigned GaOpcode;


struct Reg
{
    union
    {
        uint ui;
        sint si;
        void *p;
        real f;
    } u;
    unsigned type;

};

class VMBlah
{
public:
    void run();

    size_t pc;

    GaOpcode *code;
    Reg *regs;
};

