#pragma once

#include "gainternal.h"
#include "lex.h"

enum Comparison
{
    JUMP_ALWAYS,
    JUMP_EQ,
    JUMP_NEQ,
    JUMP_LT,
    JUMP_LTE,
    JUMP_GTE
};

enum IdentFlags
{
    IDENTFLAGS_NONE     = 0x00,
    IDENTFLAGS_OPTIONAL = 0x01
};

enum FuncFlags
{
    FUNCFLAGS_VAR_ARG = 0x01,
    FUNCFLAGS_VAR_RET = 0x02,
    FUNCFLAGS_PURE    = 0x04,
    FUNCFLAGS_DEDUCE_RET = 0x08,
};


enum HLNodeType
{
    HLNODE_NONE,
    HLNODE_CONSTANT_VALUE,
    HLNODE_UNARY,
    HLNODE_BINARY,
    HLNODE_TERNARY,
    HLNODE_CONDITIONAL,
    HLNODE_LIST,
    HLNODE_FORLOOP,
    HLNODE_WHILELOOP,
    HLNODE_ASSIGNMENT,
    HLNODE_VARDECLASSIGN,
    HLNODE_AUTODECL,
    HLNODE_VARDEF,
    HLNODE_DECLLIST,
    HLNODE_RETURN,
    HLNODE_CALL,
    HLNODE_MTHCALL,
    HLNODE_IDENT,
    HLNODE_TABLECONS,
    HLNODE_ARRAYCONS,
    HLNODE_RANGE,
    HLNODE_ITER_DECLLIST,
    HLNODE_ITER_EXPRLIST,
    HLNODE_INDEX,
    HLNODE_FUNCTION,
    HLNODE_FUNCTIONHDR,
};

enum HLTypeFlags
{
    HLTF_NONE     = 0x00,
    HLTF_OPTIONAL = 0x01
};

struct HLNode;

struct HLConstantValue
{
    enum { EnumType = HLNODE_CONSTANT_VALUE };
    ValU val;
};

struct HLUnary
{
    enum { EnumType = HLNODE_UNARY };
    Lexer::TokenType tok;
    HLNode *rhs;
};

struct HLBinary
{
    enum { EnumType = HLNODE_BINARY };
    Lexer::TokenType tok;
    HLNode *lhs;
    HLNode *rhs;
};

struct HLTernary
{
    enum { EnumType = HLNODE_TERNARY };
    Lexer::TokenType tok;
    HLNode *a;
    HLNode *b;
    HLNode *c;
};

struct HLConditional
{
    enum { EnumType = HLNODE_CONDITIONAL };
    HLNode *condition;
    HLNode *ifblock;
    HLNode *elseblock;
};

struct HLForLoop
{
    enum { EnumType = HLNODE_FORLOOP };
    HLNode *iter;
    HLNode *body;
};

struct HLWhileLoop
{
    enum { EnumType = HLNODE_WHILELOOP };
    HLNode *cond;
    HLNode *body;
};

struct HLList
{
    enum { EnumType = HLNODE_LIST };
    size_t used;
    size_t cap;
    HLNode **list;

    HLNode *add(HLNode *node, const GaAlloc& ga); // returns node, unless memory allocation fails
};

struct HLVarDef
{
    enum { EnumType = HLNODE_VARDEF };
    HLNode *ident;
    HLNode *type;
};

struct HLAutoDecl
{
    enum { EnumType = HLNODE_AUTODECL };
    HLNode *ident;
    HLNode *value;
    HLNode *type;
};

struct HLVarDeclList
{
    enum { EnumType = HLNODE_VARDECLASSIGN };
    HLNode *decllist;
    HLNode *vallist;
    bool mut;
};

struct HLAssignment
{
    enum { EnumType = HLNODE_ASSIGNMENT };
    HLNode *dstlist;
    HLNode *vallist;
};

struct HLReturn
{
    enum { EnumType = HLNODE_RETURN };
    HLNode *what;
};

struct HLBranchAlways
{
    enum { EnumType = HLNODE_NONE };
    HLNode *target;
};

struct HLFnCall
{
    enum { EnumType = HLNODE_CALL };
    HLNode *callee;
    HLNode *paramlist;
};

struct HLMthCall
{
    enum { EnumType = HLNODE_MTHCALL };
    HLNode *obj;
    HLNode *mthname;
    HLNode *paramlist;
};


struct HLIdent
{
    enum { EnumType = HLNODE_IDENT };
    unsigned nameStrId;
    size_t len;
    IdentFlags flags;
};

struct HLRange
{
    enum { EnumType = HLNODE_RANGE };
    HLNode *begin;
    HLNode *end;
    HLNode *step;
};

struct HLIndex
{
    enum { EnumType = HLNODE_INDEX };
    HLNode *lhs;
    HLNode *expr;       // for []
    unsigned nameStrId; // for .
};

struct HLFunctionHdr
{
     enum { EnumType = HLNODE_FUNCTIONHDR };
    HLNode *rettypes;
    HLNode *paramlist; // list of HLVarDecl
    FuncFlags flags;
};

struct HLFunction
{
    enum { EnumType = HLNODE_FUNCTION };
    HLNode *decl;
    HLNode *body;
};

// All of the node types intentionally occupy the same memory.
// This is so that a node type can be easily mutated into another,
// while keeping pointers intact.
// This is to make node-based optimization easier.
struct HLNode
{
    HLNodeType type;
    union
    {
        HLConstantValue constant;
        HLUnary unary;
        HLBinary binary;
        HLTernary ternary;
        HLConditional conditional;
        HLList list;
        HLIdent ident;
        HLAssignment assignment;
        HLVarDeclList vardecllist;
        HLAutoDecl autodecl;
        HLVarDef vardef;
        HLForLoop forloop;
        HLWhileLoop whileloop;
        HLReturn retn;
        HLBranchAlways branch;
        HLFnCall fncall;
        HLMthCall mthcall;
        HLRange range;
        HLIndex index;
        HLFunction func;
        HLFunctionHdr fhdr;
    } u;
};


class HLIRBuilder
{
public:
    HLIRBuilder(const GaAlloc& a);
    ~HLIRBuilder();

    inline HLNode *constantValue() { return allocT<HLConstantValue>(); }
    inline HLNode *unary()         { return allocT<HLUnary>();         }
    inline HLNode *binary()        { return allocT<HLBinary>();        }
    inline HLNode *ternary()       { return allocT<HLTernary>();       }
    inline HLNode *conditional()   { return allocT<HLConditional>();   }
    inline HLNode *list()          { return allocT<HLList>();          }
    inline HLNode *forloop()       { return allocT<HLForLoop>();       }
    inline HLNode *whileloop()     { return allocT<HLWhileLoop>();     }
    inline HLNode *assignment()    { return allocT<HLAssignment>();    }
    inline HLNode *vardecllist()   { return allocT<HLVarDeclList>();   }
    inline HLNode *autodecl()      { return allocT<HLAutoDecl>();      }
    inline HLNode *vardef()        { return allocT<HLVarDef>();        }
    inline HLNode *retn()          { return allocT<HLReturn>();        }
    inline HLNode *continu()       { return allocT<HLBranchAlways>();  }
    inline HLNode *brk()           { return allocT<HLBranchAlways>();  }
    inline HLNode *fncall()        { return allocT<HLFnCall>();        }
    inline HLNode *mthcall()       { return allocT<HLMthCall>();        }
    inline HLNode *ident()         { return allocT<HLIdent>();         }
    //inline HLNode *range()         { return allocT<HLRange>();         }
    inline HLNode *index()         { return allocT<HLIndex>();         }
    inline HLNode *func()          { return allocT<HLFunction>();         }
    inline HLNode *fhdr()          { return allocT<HLFunctionHdr>();         }

private:
    struct Block
    {
        Block *prev;
        size_t used;
        size_t cap;
        // payload follows
        HLNode *alloc();
    };
    template<typename T> inline HLNode *allocT(HLNodeType ty = HLNodeType(T::EnumType))
    {
        return alloc(ty);
    }
    HLNode *alloc(HLNodeType ty);
    void clear();
    Block *allocBlock(size_t sz);
    GaAlloc galloc;
    Block *b;
};
