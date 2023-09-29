#pragma once

#include <vector>

#include "defs.h"

enum TokenType
{
    TT_INVALID,
    TT_LITERAL,
    TT_DECL,
    TT_UNOP,
    TT_BINOP,
    TT_VARREF,
    TT_INDEX,
    TT_FNCALL,
    TT_MTHCALL,
};

enum UnOpType
{
    UOP_POS,
    UOP_NEG,
    UOP_BIN_COMPL,
    UOP_TRY,
    UOP_UNWRAP,
};

enum BinOpType
{
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_INTDIV,
    OP_BIN_AND,
    OP_BIN_OR,
    OP_BIN_XOR,
    OP_SHL,
    OP_SHR,
    OP_C_EQ,
    OP_C_NEQ,
    OP_C_LT,
    OP_C_GT,
    OP_C_LTE,
    OP_C_GTE,
    OP_C_AND,
    OP_C_OR,
    OP_EVAL_AND,
    OP_EVAL_OR,
    OP_CONCAT,
};

enum ASTNodeFlags
{
    NF_NONE,
    NF_MUTABLE = 0x01,
};


class ASTNode
{
public:
    ASTNode() : tt(TT_INVALID) {}
    ASTNode(TokenType t) : tt(t), flags(NF_NONE), n(0) {}
    ASTNode(const Val& v) : tt(TT_LITERAL), flags(NF_NONE), n(0) { value = v; }
    ASTNode(TokenType t, const Val& v, size_t n = 0) : tt(t), flags(NF_NONE), n(n) { value = v; }
    TokenType tt;
    unsigned flags;
    Val value;
    size_t n;
};
