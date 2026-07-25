#pragma once

#include "defs.h"
#include "table.h"
#include "typing.h"
#include "gavm.h"



/* Idea:
Use a butterfly construction:
<obj header stuff>
--- obj ptr points here ---
<any number of bytes follows>

That way write<T> to obj memory can be the same for all objs -> obj+offs (userdata or not)
Only need to ensure that we always know if a pointer is headered or not (user pointers won't be)
*/

struct HLNode;
class DType;
struct VM;
struct HLNode;
struct OpDef;
struct FuncProto;

// Base for any object instance.
// Beware, variable sized! Do NOT derive from this class!
class DObj : public GCobj
{
private:
    DObj(DType *dty);
public:
    static DObj *GCNew(GC& gc, DType *dty);

    //Table *dfields; // Extra fields, usually NULL
    usize nmembers;

    inline       Val *memberArray()        { return reinterpret_cast<      Val*>(this + 1); }
    inline const Val *memberArray() const  { return reinterpret_cast<const Val*>(this + 1); }
    inline Type dynamicType() const;

    Val *member(const Val& key);
    tsize memberOffset(const Val *pmember) const; // Returns offset for memberAtOffset()

    FORCEINLINE Val *memberAtOffset(tsize offs)
    {
        return (Val*)((char*)this + offs);
    }

    // additional extra storage space for members follows
};

// generated from TDesc
class DType : public GCobj
{
private:
    DType(TDesc *desc, DType *typeType);
public:
    static DType *GCNew(GC& gc, TDesc *desc, DType *typeType);
    const Type tid;
    TDesc *tdesc;
    Table fieldIndices;
    tsize numfields() const { return tdesc->size(); }
};

inline Type DObj::dynamicType() const { return dtype->tid; }


struct FuncInfo
{
    Type rettype; // type list of return values
    Type paramtype; // type list of parameter values
    // TODO: param struct type? with names and everything
    Type functype; // All combined: func(argtype) -> rettype
    u32 nargs; // Minimal number. If variadic, there may be more.
    u32 nrets;
    enum Flags
    {
        None = 0,

        FuncTypeMask = 3 << 0, // lowest 2 bits:
        LFunc = 0, // light/leaf C function (much more efficient to call)
        CFunc = 1, // regular/variadic C function
        GFunc = 2, // bytecode function
        Proto = 3, // HLNode, not yet folded

        VarArgs = 1 << 2, // set if variadic
        VarRets = 1 << 3,
        Pure    = 1 << 4, // Can run at compile time
        NoError = 1 << 5, // Will not set vm->state (sligthly more efficient to call)
    };
    u32 flags;
    u32 nupvals;
};
inline FuncInfo::Flags operator|(FuncInfo::Flags a, FuncInfo::Flags b) { return FuncInfo::Flags((unsigned)a | (unsigned)b); }

struct DebugInfo
{
    u32 linestart;
    u32 lineend;
    sref name;
     // TODO
};

// Owned by DFunc
struct InstChunk
{
    u32 allocsize;
    u32 ninst;
    SymTable *env;
    Inst *begin() { return (Inst*)(this + 1); }

    // Inst[] follows

    // Optional: Debug info follows
};

// Callable function representation. May have upvalues.
struct DFunc : public GCobj
{
    static DFunc *GCNew(GC& gc);

    inline bool isPure() const { return info.flags & FuncInfo::Pure; }

    union
    {
        LeafFunc lfunc;
        struct
        {
            CFunc f;

        } cfunc;
        FuncProto *proto; // Cloned from original parse tree as a single block of memory
        struct
        {
            InstChunk *chunk;
            u32 maxstack;
        } gfunc;
    } u;

    Val *upvals;

    FuncInfo info;
    // TODO: (DType: Func(Args, Ret))

    const OpDef *opdef; // Infos for codegen, if this function can be implemented as VM opcode directly

    DebugInfo *dbg; // this is part of the vmcode but forwarded here for easier reference

    // Slow path for compile-time eval and such
    int call(VM *vm, Val *a) const;
};

//
struct DClosure : public GCobj
{
    static DClosure *GCNew(GC& gc);

    DFunc *func;
};

