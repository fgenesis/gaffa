#include "gavm.h"
#include "util.h"



static FORCEINLINE GaOpcode fetchop(VM *vm, size_t pc)
{
    return vm->code[pc];
}

static FORCEINLINE GaOpcode fetchop(VM *vm)
{
    return vm->code[vm->pc++];
}

static FORCEINLINE Reg *reg(VM *vm, size_t reg)
{
    return &vm->regs[reg];
}

template<typename T>
struct RegAccess {}; // TODO: anything else

template<>
struct RegAccess<uint>
{
    static FORCEINLINE uint& reg(Reg *r)
    {
        return r->u.ui;
    }
};

template<>
struct RegAccess<sint>
{
    static FORCEINLINE sint& reg(Reg *r)
    {
        return r->u.si;
    }
};


template<>
struct RegAccess<real>
{
    static FORCEINLINE real& reg(Reg *r)
    {
        return r->u.f;
    }
};



template<typename T, unsigned Typemask>
struct ArithSubVM
{
    typedef RegAccess<T> RA;

    static T& Val(VM *vm, size_t idx)
    {
        return T();
    };

    GaOpcode Run(VM *vm, GaOpcode op)
    {
        size_t pc = vm->pc;
        Reg * const r = vm->regs;
#define R(k) RA::reg(r[k])
        goto entry;

        do
        {
            ++pc;
entry:
            size_t dst = (op >> 8) & 0xff;
            size_t src1 = (op >> 16) & 0xff;
            size_t src2 = (op >> 24) & 0xff;
            switch(op & GA_OPMASK_ARITH_OP)
            {
            case GA_SUBOP_ADD: R(dst) = R(src1) + R(src2);
            }

            op = fetchop(vm, pc);
        }
        while( (op & (GA_OPMASK_ARITH_MASK | GA_OPMASK_ARITH_TYPEMASK)) == (Typemask | GA_OPMASK_ARITH_ID) );
        vm->pc = pc;
        return op;
    }
    
};

