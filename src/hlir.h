// High-level intermediate representation
// aka AST.
// This is produced by the parser.

#pragma once

#include "gainternal.h"
#include "lex.h"

struct GC;

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
    IDENTFLAGS_NONE         = 0x00,
    IDENTFLAGS_OPTIONALTYPE = 0x01,
    IDENTFLAGS_DUCKYTYPE    = 0x02,
};

enum FuncFlags
{
    FUNCFLAGS_VAR_ARG = 0x01,
    FUNCFLAGS_VAR_RET = 0x02,
    FUNCFLAGS_PURE    = 0x04, // calling function has no side effects
    FUNCFLAGS_DEDUCE_RET = 0x08, // must deduce return value of function
    FUNCFLAGS_METHOD_SUGAR    = 0x10, // implicit first parameter is 'this'
};

enum DeclFlags
{
    DECLFLAG_DEFAULT = 0x00,
    DECLFLAG_MUTABLE = 0x01
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
    HLNODE_BLOCK, // list of statements
    HLNODE_FORLOOP,
    HLNODE_WHILELOOP,
    HLNODE_ASSIGNMENT,
    HLNODE_VARDECLASSIGN,
    HLNODE_AUTODECL,
    HLNODE_FUNCDECL,
    HLNODE_VARDEF,
    HLNODE_DECLLIST,
    HLNODE_RETURN,
    HLNODE_CALL,
    HLNODE_MTHCALL,
    HLNODE_IDENT,
    HLNODE_NAME,
    HLNODE_TABLECONS,
    HLNODE_ARRAYCONS,
    HLNODE_RANGE,
    HLNODE_ITER_DECLLIST,
    HLNODE_ITER_EXPRLIST,
    HLNODE_INDEX,
    HLNODE_FUNCTION,
    HLNODE_FUNCTIONHDR,
    HLNODE_SINK
};

enum HLTypeFlags
{
    HLTF_NONE     = 0x00,
    HLTF_OPTIONAL = 0x01
};

struct HLNode;
struct HLNodeBase {};

// Important: Any must be the FIRST struct members!

struct HLConstantValue : HLNodeBase
{
    enum { EnumType = HLNODE_CONSTANT_VALUE, Children = 0 };
    ValU val;
};

struct HLUnary : HLNodeBase
{
    enum { EnumType = HLNODE_UNARY, Children = 1 };
    HLNode *rhs;
};

struct HLBinary : HLNodeBase
{
    enum { EnumType = HLNODE_BINARY, Children = 2 };
    HLNode *lhs;
    HLNode *rhs;
};

struct HLTernary : HLNodeBase
{
    enum { EnumType = HLNODE_TERNARY, Children = 3 };
    HLNode *a;
    HLNode *b;
    HLNode *c;
};

struct HLConditional : HLNodeBase
{
    enum { EnumType = HLNODE_CONDITIONAL, Children = 3 };
    HLNode *condition;
    HLNode *ifblock;
    HLNode *elseblock;
};

struct HLForLoop : HLNodeBase
{
    enum { EnumType = HLNODE_FORLOOP, Children = 2 };
    HLNode *iter;
    HLNode *body;
};

struct HLWhileLoop : HLNodeBase
{
    enum { EnumType = HLNODE_WHILELOOP, Children = 2 };
    HLNode *cond;
    HLNode *body;
};

struct HLList : HLNodeBase
{
    enum { EnumType = HLNODE_LIST, Children = 0xff };
    size_t used;
    size_t cap;
    HLNode **list;

    HLNode *add(HLNode *node, GC& gc); // returns node, unless memory allocation fails
};

struct HLVarDef : HLNodeBase
{
    enum { EnumType = HLNODE_VARDEF, Children = 2 };
    HLNode *ident;
    HLNode *type;
};

struct HLAutoDecl : HLNodeBase
{
    enum { EnumType = HLNODE_AUTODECL, Children = 3 };
    HLNode *ident;
    HLNode *value;
    HLNode *type;
};

struct HLFuncDecl : HLNodeBase
{
    enum { EnumType = HLNODE_FUNCDECL, Children = 3 };
    HLNode *ident;
    HLNode *value;
    HLNode *namespac;
};

struct HLVarDeclList : HLNodeBase
{
    enum { EnumType = HLNODE_VARDECLASSIGN, Children = 2 };
    HLNode *decllist; // list of HLVarDef
    HLNode *vallist; // list of HLNode
};

struct HLAssignment : HLNodeBase
{
    enum { EnumType = HLNODE_ASSIGNMENT, Children = 2 };
    HLNode *dstlist;
    HLNode *vallist;
};

struct HLReturn : HLNodeBase
{
    enum { EnumType = HLNODE_RETURN, Children = 1 };
    HLNode *what;
};

struct HLBranchAlways : HLNodeBase
{
    enum { EnumType = HLNODE_NONE, Children = 1 };
    HLNode *target;
};

struct HLFnCall : HLNodeBase
{
    enum { EnumType = HLNODE_CALL, Children = 2 };
    HLNode *callee;
    HLNode *paramlist;
};

struct HLMthCall : HLNodeBase
{
    enum { EnumType = HLNODE_MTHCALL, Children = 3 };
    HLNode *obj;
    HLNode *mth; // name or expr
    HLNode *paramlist; // HLList
};

struct HLIdent : HLNodeBase
{
    enum { EnumType = HLNODE_IDENT, Children = 0 };
    sref nameStrId;
};

struct HLName : HLNodeBase
{
    enum { EnumType = HLNODE_NAME, Children = 0 };
    sref nameStrId;
};

struct HLSink : HLNodeBase
{
    enum { EnumType = HLNODE_SINK, Children = 0 };
};

struct HLIndex : HLNodeBase
{
    enum { EnumType = HLNODE_INDEX, Children = 2 };
    HLNode *lhs;
    HLNode *idx; // name or expr
};

struct HLFunctionHdr : HLNodeBase
{
    enum { EnumType = HLNODE_FUNCTIONHDR, Children = 2 };
    HLNode *paramlist; // list of HLVarDef // TODO: make this HLVarDeclList in the future when there are function default args?
    HLNode *rettypes; // OPTIONAL list of HLNode
    //FuncFlags flags;
};

struct HLFunction : HLNodeBase
{
    enum { EnumType = HLNODE_FUNCTION, Children = 2 };
    HLNode *hdr; // HLFunctionHdr
    HLNode *body;
};

// All of the node types intentionally occupy the same memory.
// This is so that a node type can be easily mutated into another,
// while keeping pointers intact.
// This is to make node-based optimization easier.
struct HLNode
{
    union
    {
        HLConstantValue constant;
        HLUnary unary;
        HLBinary binary;
        HLTernary ternary;
        HLConditional conditional;
        HLList list;
        HLIdent ident;
        HLName name;
        HLAssignment assignment;
        HLVarDeclList vardecllist;
        HLAutoDecl autodecl;
        HLFuncDecl funcdecl;
        HLVarDef vardef;
        HLForLoop forloop;
        HLWhileLoop whileloop;
        HLReturn retn;
        HLBranchAlways branch;
        HLFnCall fncall;
        HLMthCall mthcall;
        HLIndex index;
        HLFunction func;
        HLFunctionHdr fhdr;
        HLSink sink;

        HLNode *aslist[3];
    } u;
    unsigned line;
    HLNodeType type;
    Lexer::TokenType tok;
    unsigned char flags; // any of the flags above, depends on type
    unsigned char _nch; // number of child nodes

    template<typename T> T *as()
    {
        assert(type == T::EnumType);
        return reinterpret_cast<T*>(&this->u);
    }

};


class HLIRBuilder
{
public:
    HLIRBuilder(GC& gc);
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
    inline HLNode *funcdecl()      { return allocT<HLFuncDecl>();      }
    inline HLNode *vardef()        { return allocT<HLVarDef>();        }
    inline HLNode *retn()          { return allocT<HLReturn>();        }
    inline HLNode *continu()       { return allocT<HLBranchAlways>();  }
    inline HLNode *brk()           { return allocT<HLBranchAlways>();  }
    inline HLNode *fncall()        { return allocT<HLFnCall>();        }
    inline HLNode *mthcall()       { return allocT<HLMthCall>();       }
    inline HLNode *ident()         { return allocT<HLIdent>();         }
    inline HLNode *name()          { return allocT<HLName>();          }
    inline HLNode *index()         { return allocT<HLIndex>();         }
    inline HLNode *func()          { return allocT<HLFunction>();      }
    inline HLNode *fhdr()          { return allocT<HLFunctionHdr>();   }
    inline HLNode *sink()          { return allocT<HLSink>();          }

private:
    struct Block
    {
        Block *prev;
        size_t used;
        size_t cap;
        // payload follows
        HLNode *alloc();
    };
    template<typename T> inline HLNode *allocT()
    {
        HLNode *node = alloc();
        if(node)
        {
            node->type = HLNodeType(T::EnumType);
            node->_nch = T::Children;
        }
        return node;
    }
    HLNode *alloc();
    void clear();
    Block *allocBlock(size_t sz);
    GC& gc;
    Block *b;
};

