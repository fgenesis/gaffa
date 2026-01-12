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



// C leaf function; fastest to call but has some restrictions:
// - Non-variadic, max(#parameters, #retvals) must be <= MINSTACK
// - Read args from inout[0..], write return values to inout[0..]
// - Must NOT call back into the VM (no call frame is pushed)
// - Can not reallocate the VM stack
// - You need to know the number of parameters and return values,
//   and the function must be registered correctly so the VM
//   and type system know this too.
// - Stack space is limited; up to MINSTACK usable slots total
// - To throw a runtime error, return any of RTError != 0
typedef RTError (*LeafFunc)(VM *vm, Val *inout);

struct StackCRef;
struct StackRef;

// Full-fledged C function; slower to call
// - May or may not be variadic (params, return values, or both)
// - Calling back into the VM is allowed
// - Can grow the stack
// ---- C function call protocol: ----
// - Parameters are in args[0..nargs)
// - If normal return: Write return values to ret[0..n), then return n
// - If variadic return: Write regular return values to ret[0..n),
//   push extra variadic ones onto the stack, then return (n + #variadic)
// - To throw a runtime error, return a RTError value < 0
typedef int (*CFunc)(VM *vm, size_t nargs, StackCRef *ret,
    const StackCRef *args, StackRef *stack);


struct FuncInfo
{
    Type rettype; // type list of return values
    Type paramtype; // type list of parameter values
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
        NoError = 1 << 5, // Will not set vm->err (sligthly more efficient to call)
    };
    u32 flags;
    u32 nlocals; // # of local slots that are needed to run the function. Always >= nargs.
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

struct DFunc : public GCobj
{
    static DFunc *GCNew(GC& gc);

    inline bool isPure() const { return info.flags & FuncInfo::Pure; }

    FuncInfo info;
    // TODO: (DType: Func(Args, Ret))
    union
    {
        CFunc cfunc;
        LeafFunc lfunc;
        struct
        {
            HLNode *node; // Cloned from original parse tree as a single block of memory
            size_t bytesToFree;
        } proto;
        struct
        {
            InstChunk *chunk;
        } gfunc;
    } u;

    const OpDef *opdef; // Infos for codegen, if this function can be implemented as VM opcode directly

    DebugInfo *dbg; // this is part of the vmcode but forwarded here for easier reference

    // Slow path for compile-time eval and such
    void call(VM *vm, Val *a) const;
};

