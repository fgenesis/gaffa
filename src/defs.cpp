#include "defs.h"

#include "lex.h"
#include "util.h"

#include <string.h>

struct UintPair
{
    unsigned from, to;
};

static const UintPair lut_tok2unop[] =
{
    { Lexer::TOK_EXCL,     UOP_NOT         },
    { Lexer::TOK_PLUS,     UOP_POS         },
    { Lexer::TOK_MINUS,    UOP_NEG         },
    { Lexer::TOK_TILDE,    UOP_BIN_COMPL   },
    { Lexer::TOK_QQM,      UOP_TRY         },
    { Lexer::TOK_FATARROW, UOP_UNWRAP      },
};

static const UintPair lut_tok2binop[] =
{
    { Lexer::TOK_PLUS,   OP_ADD,      },
    { Lexer::TOK_MINUS,  OP_SUB       },
    { Lexer::TOK_STAR ,  OP_MUL       },
    { Lexer::TOK_SLASH,  OP_DIV       },
    { Lexer::TOK_SLASH2X,OP_INTDIV    },
    { Lexer::TOK_PERC,   OP_MOD       },
    { Lexer::TOK_BITAND, OP_BIN_AND   },
    { Lexer::TOK_BITOR,  OP_BIN_OR    },
    { Lexer::TOK_HAT,    OP_BIN_XOR   },
    { Lexer::TOK_SHL,    OP_SHL       },
    { Lexer::TOK_SHR,    OP_SHR       },
    { Lexer::TOK_EQ,     OP_C_EQ      },
    { Lexer::TOK_NEQ,    OP_C_NEQ     },
    { Lexer::TOK_LT,     OP_C_LT      },
    { Lexer::TOK_GT,     OP_C_GT      },
    { Lexer::TOK_LTE,    OP_C_LTE     },
    { Lexer::TOK_GTE,    OP_C_GTE     },
    { Lexer::TOK_LOGAND, OP_C_AND     },
    { Lexer::TOK_LOGOR,  OP_C_OR      },
    { Lexer::TOK_AND,    OP_EVAL_AND  },
    { Lexer::TOK_OR,     OP_EVAL_OR   },
    { Lexer::TOK_CONCAT, OP_CONCAT    },
};

static unsigned lookup(const UintPair a[], size_t N, unsigned val, unsigned def)
{
    for(size_t i = 0; i < N; ++i)
        if(val == a[i].from)
            return a[i].to;
    return def;
}

BinOpType BinOp_TokenToOp(unsigned tok)
{
    return (BinOpType)lookup(&lut_tok2binop[0], Countof(lut_tok2binop), tok, OP_INVALID);
}

UnOpType UnOp_TokenToOp(unsigned tok)
{
    return (UnOpType)lookup(&lut_tok2unop[0], Countof(lut_tok2unop), tok, UOP_INVALID);
}

static const unsigned char TypeElementSizes[] =
{
    /* PRIMTYPE_NIL    */ sizeof(unsigned char),
    /* PRIMTYPE_BOOL   */ sizeof(bool),
    /* PRIMTYPE_UINT   */ sizeof(uint),
    /* PRIMTYPE_SINT   */ sizeof(sint),
    /* PRIMTYPE_FLOAT  */ sizeof(real),
    /* PRIMTYPE_STRING */ sizeof(unsigned),
    /* PRIMTYPE_TYPE   */ sizeof(Type),
    /* PRIMTYPE_TABLE  */ sizeof(void*), // table and array are always dynamically allocated
    /* PRIMTYPE_ARRAY  */ sizeof(void*),
    /* PRIMTYPE_ANY    */ sizeof(ValU),
    /* PRIMTYPE_URANGE */ sizeof(Range<uint>),
    /* PRIMTYPE_SRANGE */ sizeof(Range<sint>),
    /* PRIMTYPE_FRANGE */ sizeof(Range<real>),
};
static_assert(sizeof(TypeElementSizes) == PRIMTYPE_MAX, "size mismatch");

size_t GetPrimTypeStorageSize(unsigned t)
{
    return t < Countof(TypeElementSizes) ? TypeElementSizes[t] : sizeof(ValU);
}

void ValU::_init(unsigned tyid)
{
    static_assert(sizeof(_AnyValU) == sizeof(((_AnyValU*)NULL)->opaque), "oops");
    u.opaque = 0;
    type.id = tyid;
}

bool ValU::operator==(const ValU& o) const
{
    return u.t.id == o.u.t.id && u.opaque == o.u.opaque;
}
