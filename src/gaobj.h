#pragma once

#include "defs.h"
#include "table.h"
#include "typing.h"



/* Idea:
Use a butterfly construction:
<obj header stuff>
--- obj ptr points here ---
<any number of bytes follows>

That way write<T> to obj memory can be the same for all objs -> obj+offs (userdata or not)
Only need to ensure that we always know if a pointer is headered or not (user pointers won't be)
*/


class DType;
struct VM;

class DObj : public GCobj
{
protected:
    DObj(DType *dty);
public:
    static DObj *GCNew(GC& gc, DType *dty);

    //Table *dfields; // Extra fields, usually NULL
    usize nmembers;

    inline       Val *memberArray()        { return reinterpret_cast<      Val*>(this + 1); }
    inline const Val *memberArray() const  { return reinterpret_cast<const Val*>(this + 1); }

    Val *member(const Val& key);
    tsize memberOffset(const Val *pmember) const; // Returns offset for memberAtOffset()

    FORCEINLINE Val *memberAtOffset(tsize offs)
    {
        return (Val*)((char*)this + offs);
    }

    // additional extra storage space for members follows
};

// generated from TDesc
class DType : public DObj
{
public:
    DType(Type tid, TDesc *desc, DType *typeType);
    const Type tid;
    TDesc *tdesc;
    Table fieldIndices;
    tsize numfields() const { return tdesc->size(); }
};



// C leaf function; fastest to call but has some restrictions:
// - Non-variadic, max(#parameters, #retvals) must be <= MINSTACK
// - Read args from inout[0..], write return values to inout[0..]
// - Must NOT call back into the VM (no call frame is pushed)
// - Can not reallocate the VM stack
// - You need to know the number of parameters and return values,
//   and the function must be registered correctly so the VM
//   and type system know this too.
// - Stack space is limited; up to MINSTACK usable slots total
// - Can't throw errors (but can return error values)
typedef void (*LeafFunc)(VM *vm, Val *inout);

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
// - To throw an error, write error to ret[0], then return -1
typedef int (*CFunc)(VM *vm, size_t nargs, StackCRef *ret,
    const StackCRef *args, StackRef *stack);


struct FuncInfo
{
    Type t;
    u32 nargs; // Minimal number. If variadic, there may be more.
    u32 nrets;
    enum Flags
    {
        FuncTypeMask = 3 << 0, // lowest 2 bits:
        LFunc = 0,
        CFunc = 1,
        Gfunc = 2,

        VarArgs = 1 << 2, // set if variadic
        VarRets = 1 << 3,
    };
    u32 flags;
};

struct DebugInfo
{
    u32 linestart;
    u32 lineend;
    sref name;
     // TODO
};

struct DFunc : public GCobj
{
    FuncInfo info;
    // TODO: (DType: Func(Args, Ret))
    union
    {
        CFunc cfunc;
        LeafFunc lfunc;
        struct
        {
            void *vmcode; // TODO
            DebugInfo *dbg; // this is part of the vmcode but forwarded here for easier reference
        }
        gfunc;
    } u;
};

