// High-level intermediate representation
// aka AST.
// This is produced by the parser.

#pragma once

#include "gainternal.h"
#include "lex.h"
#include "gaalloc.h"
#include "symtable.h"
#include <vector>


struct GC;
struct HLFoldTracker;
class Symstore;


// FIXME: These must fit in a byte! Make per-type flags again?
enum HLFlags
{
    HLFLAG_NONE             = 0x00,
    HLFLAG_VARIADIC         = 0x01,

    IDENTFLAGS_OPTIONALTYPE = 0x02,
    //IDENTFLAGS_DUCKYTYPE    = 0x04,
    IDENTFLAGS_LHS          = 0x08, // Identifier is being declared or assigned to
    IDENTFLAGS_RHS          = 0x10, // Identifier is in a context of being evaluated
    // Neither LHS/RHS set: Identifier without a specific role

    HLFLAG_KNOWNTYPE        = 0x40, // Runtyime type has been deducted or is known somehow
    HLFLAG_NOEXTMEM         = 0x80, // Node doesn't use any external memory, don't try to free on deletion
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
    HLNODE_FUNCDECL,
    HLNODE_VARDEF,
    HLNODE_DECLLIST,
    HLNODE_RETURNYIELD,
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
    HLNODE_SINK,
    HLNODE_EXPORT,

    // Intermediates; not produced by the parser
    HLNODE_FUNC_PROTO
};

enum HLTypeFlags
{
    HLTF_NONE     = 0x00,
    HLTF_OPTIONAL = 0x01
};

struct HLNode;
struct HLNodeBase
{
    // Since there is nothing that can only result in nil, use that to indicate "no actual value type"
    enum { DefaultValType = PRIMTYPE_NIL };
};

// Important: Any children must be the FIRST struct members!

struct HLDummy : HLNodeBase
{
    enum { EnumType = HLNODE_NONE, Children = 0 };
};

struct HLConstantValue : HLNodeBase
{
    enum { EnumType = HLNODE_CONSTANT_VALUE, Children = 0, DefaultValType = PRIMTYPE_ANY };
    ValU val;
};

struct HLUnary : HLNodeBase
{
    enum { EnumType = HLNODE_UNARY, Children = 1, DefaultValType = PRIMTYPE_ANY };
    HLNode *a;
};

struct HLBinary : HLNodeBase
{
    enum { EnumType = HLNODE_BINARY, Children = 2, DefaultValType = PRIMTYPE_ANY };
    HLNode *a;
    HLNode *b;
};

struct HLTernary : HLNodeBase
{
    enum { EnumType = HLNODE_TERNARY, Children = 3, DefaultValType = PRIMTYPE_ANY };
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

struct HLVarDef : HLNodeBase // appears only as child of HLVarDeclList
{
    enum { EnumType = HLNODE_VARDEF, Children = 2 };
    HLNode *ident;
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

struct HLReturnYield : HLNodeBase
{
    enum { EnumType = HLNODE_RETURNYIELD, Children = 1, DefaultValType = PRIMTYPE_ANY };
    HLNode *what;
};

struct HLBranchAlways : HLNodeBase
{
    enum { EnumType = HLNODE_NONE, Children = 1 };
    HLNode *target;
};

struct HLFnCall : HLNodeBase
{
    enum { EnumType = HLNODE_CALL, Children = 2, DefaultValType = PRIMTYPE_ANY };
    HLNode *callee;
    HLNode *paramlist;
};

struct HLMthCall : HLNodeBase
{
    enum { EnumType = HLNODE_MTHCALL, Children = 3, DefaultValType = PRIMTYPE_ANY };
    HLNode *obj;
    HLNode *mth; // name or expr
    HLNode *paramlist; // HLList
};

struct HLIdent : HLNodeBase
{
    enum { EnumType = HLNODE_IDENT, Children = 0, DefaultValType = PRIMTYPE_ANY };
    sref nameStrId;
    sref symid;
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
    enum { EnumType = HLNODE_INDEX, Children = 2, DefaultValType = PRIMTYPE_ANY };
    HLNode *lhs;
    HLNode *idx; // name or expr
};

struct HLFunctionHdr : HLNodeBase
{
    enum { EnumType = HLNODE_FUNCTIONHDR, Children = 2 };
    HLNode *paramlist; // list of HLVarDef // TODO: make this HLVarDeclList in the future when there are function default args?
    HLNode *rettypes; // OPTIONAL list of HLNode

    // # of args / return values
    // if negative: variadic, and abs()-1 of that is the minimal number
    // The get the number of non-variadic elements, compute abs(n) - (n < 0)
    int nargs() const;
    int nrets() const;
};

struct HLFunction : HLNodeBase
{
    enum { EnumType = HLNODE_FUNCTION, Children = 2, DefaultValType = PRIMTYPE_FUNC };
    HLNode *hdr; // HLFunctionHdr
    HLNode *body;
};

struct HLExport : HLNodeBase
{
    enum { EnumType = HLNODE_EXPORT, Children = 1 };
    HLNode *what;
    HLNode *name; // any expr
};

struct FuncProto
{
    FuncProto *New(GC& gc, size_t nodemem);
    HLNode *node; // points to the AST behind the struct
    DType *paramtype;
    DType *rettype;
    size_t refcount;
    size_t memsize;

    // cloned AST follows after the struct
};

// This intentionally has no automatic children, and serves as a recursion breaker.
// The FuncProto is heap-allocated and potentially shared between multiple nodes when cloning.
struct HLFuncProto : HLNodeBase
{
    enum { EnumType = HLNODE_FUNC_PROTO, Children = 0, DefaultValType = PRIMTYPE_FUNC };
    FuncProto *proto;
};

enum HLFoldStep
{
    FOLD_INITIAL,    // Initial folding step that cloned the AST and does some initial optimization
    FOLD_SPECIALIZE, // At this point, all external symbols need to be defined
};

enum HLFoldResult
{
    FOLD_FAILED, // Folding failed irrecoverably
    FOLD_OK,     // Fold successful
    FOLD_LATER,  // Retry folding later; can't finish now
};

// All of the node types intentionally occupy the same memory.
// This is so that a node type can be easily mutated into another,
// while keeping pointers intact.
// This is to make node-based optimization easier.
struct HLNode
{
    ~HLNode();

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
        HLFuncDecl funcdecl;
        HLVarDef vardef;
        HLForLoop forloop;
        HLWhileLoop whileloop;
        HLReturnYield retn;
        HLBranchAlways branch;
        HLFnCall fncall;
        HLMthCall mthcall;
        HLIndex index;
        HLFunction func;
        HLFunctionHdr fhdr;
        HLSink sink;
        HLExport exprt;
        HLFuncProto funcproto;

        HLNode *aslist[3];
    } u;
    u16 line;
    u16 column;
    byte type; // HLNodeType
    byte tok; // Lexer::TokenType
    byte flags; // any of the flags above, depends on type
    byte _nch; // number of child nodes
    Type mytype; // derived type (PRIMTYPE_* or custom),

    template<typename T> T *as()
    {
        assert(type == T::EnumType);
        return reinterpret_cast<T*>(&u);
    }

    template<typename T> const T *as() const
    {
        assert(type == T::EnumType);
        return reinterpret_cast<const T*>(&u);
    }

    HLList *aslist(HLNodeType ty)
    {
        assert(_nch == HLList::Children);
        assert(ty == type);
        return &u.list;
    }

    unsigned numchildren() const
    {
        return _nch == HLList::Children
            ? u.list.used
            : _nch;
    }

    HLNode **children()
    {
        return _nch == HLList::Children
            ? u.list.list
            : &u.aslist[0];
    }

    const HLNode * const *children() const
    {
        return _nch == HLList::Children
            ? u.list.list
            : &u.aslist[0];
    }

    bool isconst() const;
    bool iscall() const;

    bool isknowntype() const { return flags & HLFLAG_KNOWNTYPE; }
    void setknowntype(sref tid);

    HLFoldResult makeconst(GC& gc, const Val& val);
    void clear(GC& gc);

    template<typename T>
    inline HLNode *unsafemorph()
    {
        type = HLNodeType(T::EnumType);
        _nch = T::Children;
        mytype = T::DefaultValType;
        return this;
    }

    template<typename T>
    inline HLNode *morph(GC& gc)
    {
        this->clear(gc);
        return this->unsafemorph<T>();
    }

    HLNode *fold(HLFoldTracker &ft, HLFoldStep step);

    size_t memoryNeeded() const;
    HLNode *clone(GC& gc) const;

private:
    DType *getDType();
    HLFoldResult _foldfunc(HLFoldTracker &ft);
    HLFoldResult _tryfoldfunc(HLFoldTracker &ft);
    HLFoldResult _foldRec(HLFoldTracker &ft, HLFoldStep step);
    HLFoldResult _foldUnop(HLFoldTracker &ft);
    HLFoldResult _foldBinop(HLFoldTracker &ft);
    void _applyTypeFrom(HLFoldTracker& ft, HLNode *from);
    void _applyTypeFromList(HLFoldTracker& ft, HLNode *from);
    HLFoldResult propagateMyType(HLFoldTracker &ft, const HLNode *typesrc);

    HLNode *_clone(void *mem, size_t bytes, GC& gc) const;
    byte *_cloneRec(byte *m, HLNode *target, GC& gc) const;
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
    inline HLNode *funcdecl()      { return allocT<HLFuncDecl>();      }
    inline HLNode *vardef()        { return allocT<HLVarDef>();        }
    inline HLNode *retn()          { return allocT<HLReturnYield>();   }
    inline HLNode *continu()       { return allocT<HLBranchAlways>();  }
    inline HLNode *brk()           { return allocT<HLBranchAlways>();  } // FIXME: ???
    inline HLNode *fncall()        { return allocT<HLFnCall>();        }
    inline HLNode *mthcall()       { return allocT<HLMthCall>();       }
    inline HLNode *ident()         { return allocT<HLIdent>();         }
    inline HLNode *name()          { return allocT<HLName>();          }
    inline HLNode *index()         { return allocT<HLIndex>();         }
    inline HLNode *func()          { return allocT<HLFunction>();      }
    inline HLNode *fhdr()          { return allocT<HLFunctionHdr>();   }
    inline HLNode *sink()          { return allocT<HLSink>();          }
    inline HLNode *exprt()         { return allocT<HLExport>();        }
    inline HLNode *dummy()         { return allocT<HLDummy>();         }
    inline HLNode *funcproto()     { return allocT<HLFuncProto>();     }

private:

    BlockListAllocator bla;

    template<typename T> inline HLNode *allocT()
    {
        HLNode *node = (HLNode*)bla.alloc(sizeof(HLNode));
        return node ? node->unsafemorph<T>() : NULL;
    }
};

struct HLFoldTracker
{
    Runtime &rt;
    Symstore& syms;
    SymTable &env;
    std::vector<std::string> errors;

    void error(const HLNode *where, const char *msg);
};
