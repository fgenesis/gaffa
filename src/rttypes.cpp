#include "rttypes.h"
#include "typing.h"
#include "gaobj.h"
#include "symtable.h"
#include "strings.h"
#include "gavm.h"
#include "runtime.h"
#include "lex.h"
#include "rtops.h"
#include "table.h"
#include <assert.h>
#include <limits>


struct RTReg;

struct ClassReg
{
    ClassReg(RTReg& r, DType *cls) : r(r), cls(cls) {}

    template<typename T>
    void symbol(const char *name, const T& obj)
    {
        sref s = r.sp.put(name).id;
        r.syms.addToNamespace(r.gc, cls->tid, s, Val(obj));
    }

    template<size_t NP, size_t NR>
    inline DFunc *method(const char *name, LeafFunc lfunc, const Type (&params)[NP], const Type (&rets)[NR], FuncInfo::Flags extraflags = FuncInfo::None)
    {
        return this->method(name, lfunc, params, NP, rets, NR, extraflags);
    }

    DFunc *method(const char * name, LeafFunc lfunc, const Type * params, size_t nparams, const Type * rets, size_t nrets, FuncInfo::Flags extraflags);
    DFunc *op(OperatorId op, LeafFunc lfunc, Type t, unsigned arity);

    // Make an operator function an existing method, eg. [] = getopt
    void opAlias(OperatorId op, DFunc *df)
    {
        const char *name = Lexer::GetOperatorName(op);
        assert(name);
        symbol(name, df);
    }

private:
    RTReg& r;
    DType *cls;
};


struct RTReg
{
    GC& gc;
    StringPool& sp;
    TypeRegistry& tr;
    SymTable& syms;

    struct
    {
        DType *type;
        DType *func;
        DType *sint;
        DType *uint;
        DType *flt;
        DType *str;
        DType *array;
        DType *table;
        DType *symtab;
        DType *any;
        DType *noreturn;
        DType *voidt;
    } types;

    struct
    {
        Type empty;
    } tids;

    sref str(const char *s)
    {
       return sp.put(s).id;
    }

    template<typename T>
    inline void symbol(const char *name, const T& obj)
    {
        syms.addSymbol(gc, str(name), Val(obj));
    }

    ClassReg regclass(const char *name, DType *t)
    {
        symbol(name, t);
        return ClassReg(*this, t);
    }


    DFunc *regfunc(DType *ns, const char *name, LeafFunc lfunc, const Type *params, size_t nparams, const Type *rets, size_t nrets, FuncInfo::Flags extraflags)
    {
        Type tp = tr.mklistDesc(params, nparams)->id();
        Type tret = tr.mklistDesc(rets, nrets)->id();
        Type tf = tr.mkfuncDesc(tp, tret)->id();
        sref sname = str(name);

        DFunc *df = DFunc::GCNew(gc);
        df->info.paramtype = tp;
        df->info.rettype = tret;
        df->info.functype = tf;
        df->info.nargs = nparams;
        df->info.nrets = nrets;
        df->info.flags = FuncInfo::LFunc | extraflags;
        df->info.nupvals = 0;
        df->u.lfunc = lfunc;
        df->dbg.lineend = -1;
        df->dbg.linestart = -1;
        df->dbg.name = sname;
        df->dtype = types.func;
        df->opdef = NULL;
        df->tsig = NULL;

        if(ns)
            syms.addToNamespace(gc, ns->tid, sname, Val(df));
        else
            syms.addSymbol(gc, sname, Val(df));

        return df;
    }


    template<size_t NP, size_t NR>
    inline DFunc *regfunc(DType *ns, const char *name, LeafFunc lfunc, const Type (&params)[NP], const Type (&rets)[NR], FuncInfo::Flags extraflags = FuncInfo::None)
    {
        return regfunc(ns, name, lfunc, params, NP, rets, NR, extraflags);
    }
};


DFunc *ClassReg::method(const char * name, LeafFunc lfunc, const Type * params, size_t nparams, const Type * rets, size_t nrets, FuncInfo::Flags extraflags)
{
    return r.regfunc(cls, name, lfunc, params, nparams, rets, nrets, extraflags);
}

DFunc *ClassReg::op(OperatorId op, LeafFunc lfunc, Type t, unsigned arity)
{
    assert(arity <= 2);
    const char *name = Lexer::GetOperatorName(op);
    assert(name);
    Type ta[] = { t, t };
    return this->method(name, lfunc, ta, arity, ta, 1, FuncInfo::Pure);
}

template<typename T>
static void _reg_numeric(ClassReg& xr)
{
    xr.symbol("zero", T(0));
    xr.symbol("minval", std::numeric_limits<T>::min());
    xr.symbol("maxval", std::numeric_limits<T>::max());
}



// This function solves the hen-and-egg problem:
// The type 'type' has itself as the underlying type. This is normally not possible to create.
void reg_type_type(RTReg& r)
{
    TDesc *desc = const_cast<TDesc*>(r.tr.mkprimDesc(PRIMTYPE_TYPE)); // HACK

    // HACK: This is to init everything properly
    union
    {
        byte tmp[sizeof(DType)] = {0};
        Type ta[sizeof(DType) / sizeof(Type)];
    } u;

    // Pretend that DType::tid is set to PRIMTYPE_TYPE (yes, this is totally a valid type, trust me bro)
    enum { OffsBytes = offsetof(DType, tid), Offs = OffsBytes / sizeof(Type) };
    u.ta[Offs] = desc->h.tid;

    DType *dummy = (DType*)&u.tmp[0];
    dummy->tdesc = desc;

    DType *d = DType::GCNew(r.gc, desc, dummy);
    d->dtype = d;
    desc->h.dtype = d;
    assert(d->dtype->tdesc); // This must be present for the type system to be sound

    r.types.type = d;

    ClassReg xtype = r.regclass("type", d);
}

static void reg_type_func(RTReg& r)
{
    DType *d = r.tr.mkprim(PRIMTYPE_FUNC);
    r.types.func = d;
    ClassReg xfunc = r.regclass("anyfunc", d);
}



static void reg_type_void(RTReg& r)
{
    const TDesc *d  = r.tr.mklistDesc(NULL, 0);
    r.tids.empty = d->id();
    r.types.voidt = r.tr.asObj(d);
    ClassReg xvoid = r.regclass("void", r.types.voidt);
}

static void reg_type_bool(RTReg& r)
{
    r.regclass("bool", r.tr.mkprim(PRIMTYPE_BOOL));
}

static void reg_type_uint(RTReg& r)
{
    DType *d = r.tr.mkprim(PRIMTYPE_UINT);
    r.types.uint = d;
    ClassReg xuint = r.regclass("uint", d);
    _reg_numeric<uint>(xuint);
}


static int mth_int_abs(VM *, Val *v)
{
    assert(v->type == PRIMTYPE_SINT);
    const sint i = v->u.si;
    v->u.si = i < 0 ? -i : i; // TODO degenerate cases
    return 1;
}



static void reg_type_sint(RTReg& r)
{
    DType *d = r.tr.mkprim(PRIMTYPE_SINT);
    r.types.sint = d;
    ClassReg xint = r.regclass("int", d);
    _reg_numeric<sint>(xint);

    {
        const Type sint1[] = { PRIMTYPE_SINT };
        const Type uint1[] = { PRIMTYPE_UINT };
        xint.method("abs", mth_int_abs, sint1, uint1);
        xint.method("iabs", mth_int_abs, sint1, sint1);
    }
}


static int mth_float_abs(VM *, Val *v)
{
    assert(v->type == PRIMTYPE_FLOAT);
    float f = v->u.f;
    v->u.f = f < 0 ? -f : f;
    return 1;
}

static void reg_type_float(RTReg& r)
{
    DType *d = r.tr.mkprim(PRIMTYPE_FLOAT);
    r.types.flt = d;
    ClassReg xfloat = r.regclass("float", d);
    _reg_numeric<real>(xfloat);

    {
        const Type float1[] = { PRIMTYPE_FLOAT };
        xfloat.method("abs", mth_float_abs, float1, float1);
    }
}

/*
VMFUNC_IMM(strlen, Imm_2xu32)
{
    Val *v = LOCAL(imm->a);
    assert(s->type == PRIMTYPE_STRING);
    sref s = v->u.str;
    vm->
    NEXT();
}
*/

static int mth_string_len(VM *vm, Val *v)
{
    assert(v->type == PRIMTYPE_STRING);
    const sref s = v->u.str;
    v->u.str = vm->rt->sp.lookup(s).len;
    v->type = PRIMTYPE_UINT;
    return 1;
}

// __iter1(uint i) -> uint (the byte at i), __iter2(uint i) -> uint i, uint byte; no values once i is past the end
static int stringIter(VM *vm, Val *v, int n)
{
    if(v[0].type != PRIMTYPE_STRING || v[1].type != PRIMTYPE_UINT)
        return RTE_INCOMPATIBLE_TYPE;
    const Strp str = vm->rt->sp.lookup(v[0].u.str);
    const uint i = v[1].u.ui;
    if(i >= str.len)
        return 0; // end of iteration
    const Val c = Val(uint((unsigned char)str.s[i]));
    if(n == 2)
    {
        v[0] = Val(i);
        v[1] = c;
    }
    else
        v[0] = c;
    return n;
}

static int mth_string_iter1(VM *vm, Val *v) { return stringIter(vm, v, 1); }
static int mth_string_iter2(VM *vm, Val *v) { return stringIter(vm, v, 2); }

static void reg_type_string(RTReg& r)
{
    DType *d = r.tr.mkprim(PRIMTYPE_STRING);
    r.types.str = d;
    ClassReg xstr = r.regclass("string", d);

    {
        const Type str1[] = { PRIMTYPE_STRING };
        const Type uint1[] = { PRIMTYPE_UINT };
        xstr.method("len", mth_string_len, str1, uint1, FuncInfo::Pure);
        const Type str_uint[] = { PRIMTYPE_STRING, PRIMTYPE_UINT };
        const Type uint2[] = { PRIMTYPE_UINT, PRIMTYPE_UINT };
        xstr.method("__iter1", mth_string_iter1, str_uint, uint1, FuncInfo::Pure); // bytes
        xstr.method("__iter2", mth_string_iter2, str_uint, uint2, FuncInfo::Pure); // index, byte
    }
}

// --- array, Array(T): __getindex(uint i) -> T (out of bounds is an error), __getindexopt(uint i) -> T? (nil then),
// __setindex(uint i, T v) (i must be in bounds), __iter1(uint i) -> T, __iter2(uint i) -> uint, T (no values once
// i is past the end: that ends a loop). The same functions serve all array types: a specialized type without
// its own methods falls back to these (the compiler's type family lookup), with the signature from DFunc::tsig.
// [] is __getindex or __getindexopt depending on whether T is optional, []= is __setindex.

static DArray *argArray(const Val& v)
{
    return static_cast<DArray*>(v.asObj(PRIMTYPE_ARRAY));
}

static int arrayGet(Val *v, bool opt)
{
    const DArray *a = argArray(v[0]);
    if(!a || v[1].type != PRIMTYPE_UINT)
        return RTE_INCOMPATIBLE_TYPE;
    if(v[1].u.ui >= a->sz)
    {
        if(!opt)
            return RTE_OUT_OF_BOUNDS;
        v[0] = _Nil();
        return 1;
    }
    v[0] = a->dynamicLookup((tsize)v[1].u.ui);
    return 1;
}

static int mth_array_getindex(VM *vm, Val *v)    { return arrayGet(v, false); }
static int mth_array_getindexopt(VM *vm, Val *v) { return arrayGet(v, true); }

// __iter1(uint i) -> element, __iter2(uint i) -> i, element; no values at the end
static int arrayIter(Val *v, int n)
{
    const DArray *a = argArray(v[0]);
    if(!a || v[1].type != PRIMTYPE_UINT)
        return RTE_INCOMPATIBLE_TYPE;
    const uint i = v[1].u.ui;
    if(i >= a->sz)
        return 0; // end of iteration
    const Val e = a->dynamicLookup((tsize)i);
    if(n == 2)
    {
        v[0] = Val(i);
        v[1] = e;
    }
    else
        v[0] = e;
    return n;
}

static int mth_array_iter1(VM *vm, Val *v) { return arrayIter(v, 1); }
static int mth_array_iter2(VM *vm, Val *v) { return arrayIter(v, 2); }

static int mth_array_setindex(VM *vm, Val *v)
{
    DArray *a = argArray(v[0]);
    if(!a || v[1].type != PRIMTYPE_UINT)
        return RTE_INCOMPATIBLE_TYPE;
    if(v[1].u.ui >= a->sz)
        return RTE_OUT_OF_BOUNDS;
    if(a->t < PRIMTYPE_ANY && v[2].type != a->t) // typed storage
        return RTE_INCOMPATIBLE_TYPE;
    a->dynamicSet((tsize)v[1].u.ui, v[2]); // TODO GC: write barrier
    return 0;
}

// --- Signatures of the generic container methods for a specialized type (DFunc::tsig) ---

static Type sigOf(TypeRegistry& tr, const Type *params, size_t np, const Type *rets, size_t nr)
{
    const Type tp = tr.mklistDesc(params, np)->id();
    const Type tr_ = tr.mklistDesc(rets, nr)->id();
    return tr.mkfuncDesc(tp, tr_)->id();
}

static Type constOf(TypeRegistry& tr, Type t) // reading methods take Const(self), so a Const container can be read
{
    DType *d = tr.asObj(t);
    DType *c = d ? tr.mkconst(d) : NULL;
    return c ? c->tid : t;
}

static Type optOf(TypeRegistry& tr, Type t)
{
    DType *d = tr.asObj(t);
    DType *o = d ? tr.mkoptional(d) : NULL;
    return o ? o->tid : t;
}

// __getindex(Const(Array(T)), uint) -> T, __getindexopt(...) -> T?, __setindex(Array(T), uint, T),
// __iter1(Const(Array(T)), uint) -> T, __iter2(...) -> uint, T
static Type sig_array_getindex(TypeRegistry& tr, Type self)
{
    const Type et = tr.arrayElem(self);
    if(et == PRIMTYPE_NOTYPE)
        return PRIMTYPE_AUTO;
    const Type ps[] = { constOf(tr, self), PRIMTYPE_UINT };
    return sigOf(tr, ps, 2, &et, 1);
}
static Type sig_array_getindexopt(TypeRegistry& tr, Type self)
{
    const Type et = tr.arrayElem(self);
    if(et == PRIMTYPE_NOTYPE)
        return PRIMTYPE_AUTO;
    const Type ps[] = { constOf(tr, self), PRIMTYPE_UINT };
    const Type rt = optOf(tr, et);
    return sigOf(tr, ps, 2, &rt, 1);
}
static Type sig_array_setindex(TypeRegistry& tr, Type self)
{
    const Type et = tr.arrayElem(self);
    if(et == PRIMTYPE_NOTYPE)
        return PRIMTYPE_AUTO;
    const Type ps[] = { self, PRIMTYPE_UINT, et };
    return sigOf(tr, ps, 3, NULL, 0);
}
static Type sig_array_iter1(TypeRegistry& tr, Type self)
{
    return sig_array_getindex(tr, self); // same types
}
static Type sig_array_iter2(TypeRegistry& tr, Type self)
{
    const Type et = tr.arrayElem(self);
    if(et == PRIMTYPE_NOTYPE)
        return PRIMTYPE_AUTO;
    const Type ps[] = { constOf(tr, self), PRIMTYPE_UINT };
    const Type rs[] = { PRIMTYPE_UINT, et };
    return sigOf(tr, ps, 2, rs, 2);
}

// __getindex(Const(Table(K, V)), K) -> V, __getindexopt(...) -> V?, __setindex(Table(K, V), K, V),
// __iter1(Const(Table(K, V)), uint) -> V, __iter2(...) -> K, V, __iter3(...) -> uint, K, V
static Type sig_table_getindex(TypeRegistry& tr, Type self)
{
    Type k, v;
    if(!tr.tableKV(self, &k, &v))
        return PRIMTYPE_AUTO;
    const Type ps[] = { constOf(tr, self), k };
    return sigOf(tr, ps, 2, &v, 1);
}
static Type sig_table_getindexopt(TypeRegistry& tr, Type self)
{
    Type k, v;
    if(!tr.tableKV(self, &k, &v))
        return PRIMTYPE_AUTO;
    const Type ps[] = { constOf(tr, self), k };
    const Type rt = optOf(tr, v);
    return sigOf(tr, ps, 2, &rt, 1);
}
static Type sig_table_setindex(TypeRegistry& tr, Type self)
{
    Type k, v;
    if(!tr.tableKV(self, &k, &v))
        return PRIMTYPE_AUTO;
    const Type ps[] = { self, k, v };
    return sigOf(tr, ps, 3, NULL, 0);
}
static Type sig_table_iter1(TypeRegistry& tr, Type self)
{
    Type k, v;
    if(!tr.tableKV(self, &k, &v))
        return PRIMTYPE_AUTO;
    const Type ps[] = { constOf(tr, self), PRIMTYPE_UINT };
    return sigOf(tr, ps, 2, &v, 1);
}
static Type sig_table_iter2(TypeRegistry& tr, Type self)
{
    Type k, v;
    if(!tr.tableKV(self, &k, &v))
        return PRIMTYPE_AUTO;
    const Type ps[] = { constOf(tr, self), PRIMTYPE_UINT };
    const Type rs[] = { k, v };
    return sigOf(tr, ps, 2, rs, 2);
}
static Type sig_table_iter3(TypeRegistry& tr, Type self)
{
    Type k, v;
    if(!tr.tableKV(self, &k, &v))
        return PRIMTYPE_AUTO;
    const Type ps[] = { constOf(tr, self), PRIMTYPE_UINT };
    const Type rs[] = { PRIMTYPE_UINT, k, v };
    return sigOf(tr, ps, 2, rs, 3);
}

static void reg_type_array(RTReg& r)
{
    DType *d = r.tr.mkprim(PRIMTYPE_ARRAY);
    r.types.array = d;
    ClassReg xarr = r.regclass("array", d);

    // The generic array is Array(any?)
    const Type anyopt = PRIMTYPE_ANY | TYPEBIT_OPTIONAL;
    const Type self_idx[] = { PRIMTYPE_ARRAY, PRIMTYPE_UINT };
    const Type self_idx_val[] = { PRIMTYPE_ARRAY, PRIMTYPE_UINT, anyopt };
    const Type val[] = { anyopt };
    const Type idx_val[] = { PRIMTYPE_UINT, anyopt };
    DFunc *get = xarr.method("__getindex", mth_array_getindex, self_idx, val);
    DFunc *getopt = xarr.method("__getindexopt", mth_array_getindexopt, self_idx, val);
    DFunc *set = xarr.method("__setindex", mth_array_setindex, self_idx_val, 2 + 1, NULL, 0, FuncInfo::None);
    DFunc *it1 = xarr.method("__iter1", mth_array_iter1, self_idx, val);
    DFunc *it2 = xarr.method("__iter2", mth_array_iter2, self_idx, idx_val);
    get->tsig = sig_array_getindex; // these serve every Array(T)
    getopt->tsig = sig_array_getindexopt;
    set->tsig = sig_array_setindex;
    it1->tsig = sig_array_iter1;
    it2->tsig = sig_array_iter2;
    xarr.opAlias(OP_GETINDEX, getopt); // any? is optional
    xarr.opAlias(OP_SETINDEX, set);
}

// --- table, Table(K, V): __getindex(K k) -> V (a missing key is an error), __getindexopt(K k) -> V? (nil then),
// __setindex(K k, V v), __iter1(uint i) -> V, __iter2(uint i) -> K, V, __iter3(uint i) -> uint, K, V (i is the
// position in the table's value array, insertion order unless keys were removed; no values at the end). nil keys aren't allowed; nil is a regular value.
// [] and []= like for arrays.

static Table *argTable(const Val& v)
{
    return static_cast<Table*>(v.asObj(PRIMTYPE_TABLE));
}

static int tableGet(Val *v, bool opt)
{
    const Table *t = argTable(v[0]);
    if(!t)
        return RTE_INCOMPATIBLE_TYPE;
    Val out;
    if(!t->find(v[1], out))
    {
        if(!opt)
            return RTE_KEY_NOT_FOUND;
        out = _Nil();
    }
    v[0] = out;
    return 1;
}

static int mth_table_getindex(VM *vm, Val *v)    { return tableGet(v, false); }
static int mth_table_getindexopt(VM *vm, Val *v) { return tableGet(v, true); }

// __iter1(uint i) -> value, __iter2(uint i) -> key, value, __iter3(uint i) -> i, key, value; no values at the end.
// The result area has room for 3 values (it holds max(params, results)).
static int tableIter(Val *v, int n)
{
    const Table *t = argTable(v[0]);
    if(!t || v[1].type != PRIMTYPE_UINT)
        return RTE_INCOMPATIBLE_TYPE;
    const uint i = v[1].u.ui;
    if(i >= t->size())
        return 0; // end of iteration
    const KV kv = t->index((tsize)i);
    if(n == 3)
    {
        v[0] = Val(i);
        v[1] = kv.k;
        v[2] = kv.v;
    }
    else if(n == 2)
    {
        v[0] = kv.k;
        v[1] = kv.v;
    }
    else
        v[0] = kv.v;
    return n;
}

static int mth_table_iter1(VM *vm, Val *v) { return tableIter(v, 1); }
static int mth_table_iter2(VM *vm, Val *v) { return tableIter(v, 2); }
static int mth_table_iter3(VM *vm, Val *v) { return tableIter(v, 3); }

static int mth_table_setindex(VM *vm, Val *v)
{
    Table *t = argTable(v[0]);
    if(!t || v[1].type == PRIMTYPE_NIL)
        return RTE_INCOMPATIBLE_TYPE;
    if((t->keytype < PRIMTYPE_ANY && v[1].type != t->keytype)
        || (t->values().t < PRIMTYPE_ANY && v[2].type != t->values().t)) // typed storage
        return RTE_INCOMPATIBLE_TYPE;
    t->set(vm->rt->gc, v[1], v[2]); // TODO GC: write barrier
    return 0;
}

static void reg_type_table(RTReg& r)
{
    DType *d = r.tr.mkprim(PRIMTYPE_TABLE);
    r.types.table = d;
    ClassReg xtab = r.regclass("table", d);

    // The generic table is Table(any, any?)
    const Type anyopt = PRIMTYPE_ANY | TYPEBIT_OPTIONAL;
    const Type self_key[] = { PRIMTYPE_TABLE, PRIMTYPE_ANY };
    const Type self_key_val[] = { PRIMTYPE_TABLE, PRIMTYPE_ANY, anyopt };
    const Type val[] = { anyopt };
    const Type self_idx[] = { PRIMTYPE_TABLE, PRIMTYPE_UINT };
    const Type key_val[] = { PRIMTYPE_ANY, anyopt };
    DFunc *get = xtab.method("__getindex", mth_table_getindex, self_key, val);
    DFunc *getopt = xtab.method("__getindexopt", mth_table_getindexopt, self_key, val);
    DFunc *set = xtab.method("__setindex", mth_table_setindex, self_key_val, 2 + 1, NULL, 0, FuncInfo::None);
    DFunc *it1 = xtab.method("__iter1", mth_table_iter1, self_idx, val);
    DFunc *it2 = xtab.method("__iter2", mth_table_iter2, self_idx, key_val);
    const Type idx_key_val[] = { PRIMTYPE_UINT, PRIMTYPE_ANY, anyopt };
    DFunc *it3 = xtab.method("__iter3", mth_table_iter3, self_idx, idx_key_val);
    get->tsig = sig_table_getindex; // these serve every Table(K, V)
    getopt->tsig = sig_table_getindexopt;
    set->tsig = sig_table_setindex;
    it1->tsig = sig_table_iter1;
    it2->tsig = sig_table_iter2;
    it3->tsig = sig_table_iter3;
    xtab.opAlias(OP_GETINDEX, getopt); // any? is optional
    xtab.opAlias(OP_SETINDEX, set);
}

static void reg_type_symtab(RTReg& r)
{
    DType *d = r.tr.mkprim(PRIMTYPE_SYMTAB);
    r.types.symtab = d;
    ClassReg xsymt = r.regclass("symtable", d);
}

/*
static void op_any_plus(VM *vm, Val *v)
{
    const PrimType t0 = v[0].type;
    if(t0 == v[1].type)
    {
        switch(t0)
        {
            case PRIMTYPE_UINT: v[0].u.ui += v[1].u.ui; return; // TODO: check overflow
            case PRIMTYPE_SINT: v[0].u.si += v[1].u.si; return;
            case PRIMTYPE_FLOAT: v[0].u.f += v[1].u.f; return;
        }
    }

    if(t0 == PRIMTYPE_OBJECT)
    {
        DObj *obj0 = v[0].asDObj();
        const Type tt0 = obj0->dynamicType();
        // TODO: need to pass current env (aka symtab) somehow
    }
}
*/

static void reg_type_any(RTReg& r)
{
    DType *d = r.tr.mkprim(PRIMTYPE_ANY);
    r.types.any = d;
    ClassReg xany = r.regclass("any", d);

    /*{
        const Type any1[] = { PRIMTYPE_ANY };
        xany.op(Lexer::TOK_PLUS, op_any_plus, PRIMTYPE_ANY, 2);
    }*/
}

static void reg_types_special(RTReg& r)
{
    DType *noret = r.tr.mkprim(PRIMTYPE_NORETURN);
    r.types.noreturn = noret;
    r.regclass("noreturn", noret);

    // The types of nil and of error values, needed as value types (eg. a function returning an error value).
    // Not named: there's no reason to use them as types in scripts.
    r.tr.mkprim(PRIMTYPE_NIL);
    r.tr.mkprim(PRIMTYPE_ERROR);
}

static void reg_constants(RTReg& r)
{
    // nil is kinda special. It exists as a keyword for the parser (in particular, '-> nil' for void function returns),
    // but is also a "constant predefined identifier", in case that ever matters.
    r.symbol("nil", _Nil());

    r.symbol("true", true);
    r.symbol("false", false);
}

#include <time.h>
static int u_clock(VM *vm, Val *v)
{
    *v = Val((uint)clock());
    return 1;
}
static int u_time(VM *vm, Val *v)
{
    *v = Val((uint)time(NULL));
    return 1;
}

static int u_const(VM *vm, Val *v)
{
    DType *t = v->asDType();
    if(!t)
        return RTE_INCOMPATIBLE_TYPE;

    DType *tc = vm->rt->tr.mkconst(t);
    if(!tc)
        return RTE_INCOMPATIBLE_TYPE;

    *v = Val(tc);
    return 1;
}

static int u_optional(VM *vm, Val *v)
{
    DType *t = v->asDType();
    if(!t)
        return RTE_INCOMPATIBLE_TYPE;

    DType *to = vm->rt->tr.mkoptional(t);
    if(!to)
        return RTE_INCOMPATIBLE_TYPE;

    *v = Val(to);
    return 1;
}

// Array(T)
static int u_array(VM *vm, Val *v)
{
    DType *t = v->asDType();
    const Type at = t ? vm->rt->tr.mkarray(t->tid) : PRIMTYPE_AUTO;
    DType *d = at != PRIMTYPE_AUTO ? vm->rt->tr.asObj(at) : NULL;
    if(!d)
        return RTE_INCOMPATIBLE_TYPE;
    *v = Val(d);
    return 1;
}

// Table(K, V)
static int u_table(VM *vm, Val *v)
{
    DType *k = v[0].asDType();
    DType *t = v[1].asDType();
    const Type tt = k && t ? vm->rt->tr.mktable(k->tid, t->tid) : PRIMTYPE_AUTO;
    DType *d = tt != PRIMTYPE_AUTO ? vm->rt->tr.asObj(tt) : NULL;
    if(!d)
        return RTE_INCOMPATIBLE_TYPE;
    *v = Val(d);
    return 1;
}

// --- Constructors. Calling a value that isn't a function calls the () operator of its type with the value as self,
// so calling a type value T calls type::()(T, args). That dispatches without any symbol lookup (there's no symbol
// table at runtime): type -> type::__new, struct types -> built from a table. A type's own constructor T::__new
// (no self) is looked up by the compiler, which then calls it directly.

// type::__new(table template) -> type: a struct type from a template table {x=float, y=0.0}: a type value is a
// field's type, any other value its default (nil isn't allowed, use T?). Same rules as the compiler's templates.
static int mth_type_new(VM *vm, Val *v)
{
    const Table *t = argTable(v[0]);
    if(!t)
        return RTE_INCOMPATIBLE_TYPE;
    const TDesc *td = vm->rt->tr.mkstructDesc(*t);
    DType *d = td ? vm->rt->tr.asObj(td) : NULL;
    if(!d)
        return RTE_INCOMPATIBLE_TYPE;
    v[0] = Val(d);
    return 1;
}

// A struct object of type st from a table: every key must be a field and its value fit the field's type (no
// conversions at runtime); fields not given get their default, or nil if optional, else it's an error.
static int constructStruct(VM *vm, DType *st, const Table *t, Val *out)
{
    TypeRegistry& tr = vm->rt->tr;
    const TDesc *td = st->tdesc;
    const tsize n = t->size();
    for(tsize i = 0; i < n; ++i)
    {
        const KV e = t->index(i);
        const Val idx = e.k.type == PRIMTYPE_STRING ? st->fieldIndices.get(e.k) : Val(_Nil());
        if(idx.type != PRIMTYPE_UINT)
            return RTE_KEY_NOT_FOUND; // not a field
        if(!tr.isCompatible(e.v.getRtType(), td->types()[idx.u.ui]))
            return RTE_INCOMPATIBLE_TYPE;
    }
    DObj *o = DObj::GCNew(vm->rt->gc, st); // defaults, nil
    if(!o)
        return RTE_ALLOC_FAIL;
    Val *m = o->memberArray();
    for(tsize i = 0; i < n; ++i)
    {
        const KV e = t->index(i);
        m[st->fieldIndices.get(e.k).u.ui] = e.v; // TODO GC: write barrier
    }
    for(tsize f = 0; f < td->n; ++f)
        if(m[f].type == PRIMTYPE_NIL && !(td->types()[f] & TYPEBIT_OPTIONAL))
            return RTE_KEY_NOT_FOUND; // a required field is missing. TODO GC: o is garbage now
    *out = Val(o);
    return 1;
}

// type::()(type T, table t) -> T: calling a type value
static int mth_type_call(VM *vm, Val *v)
{
    DType *self = v[0].asDType();
    if(!self)
        return RTE_INCOMPATIBLE_TYPE;
    if(self->tid == PRIMTYPE_TYPE) // type(t)
    {
        v[0] = v[1];
        return mth_type_new(vm, v);
    }
    if(self->tdesc->kind == TKIND_STRUCT && self->isBase())
    {
        const Table *t = argTable(v[1]);
        return t ? constructStruct(vm, self, t, &v[0]) : RTE_INCOMPATIBLE_TYPE;
    }
    return RTE_NOT_CALLABLE; // other types need their own T::__new, which the compiler looks up
}

// error("msg"): an error value, the error state of an optional. Same payload as the string.
static int u_error(VM *vm, Val *v)
{
    if(v->type != PRIMTYPE_STRING)
        return RTE_INCOMPATIBLE_TYPE;
    v->type = PRIMTYPE_ERROR;
    return 1;
}

// errstr(x) -> string?: the message if x is an error value, otherwise nil
static int u_errstr(VM *vm, Val *v)
{
    if(v->type == PRIMTYPE_ERROR)
        v->type = PRIMTYPE_STRING;
    else
        *v = _Nil();
    return 1;
}

static void reg_builtins(RTReg& r)
{
    const Type type1[] = { PRIMTYPE_TYPE };
    r.regfunc(NULL, "Const", u_const, type1, 1, type1, 1, FuncInfo::Pure);
    r.regfunc(NULL, "optional", u_optional, type1, 1, type1, 1, FuncInfo::Pure);
    const Type type2[] = { PRIMTYPE_TYPE, PRIMTYPE_TYPE };
    {
        // Constructors of 'type' (registered here: they use table, which is registered after 'type')
        const Type table1[] = { PRIMTYPE_TABLE };
        const Type type_table[] = { PRIMTYPE_TYPE, PRIMTYPE_TABLE };
        const Type any1[] = { PRIMTYPE_ANY };
        r.regfunc(r.types.type, "__new", mth_type_new, table1, 1, type1, 1, FuncInfo::Pure);
        r.regfunc(r.types.type, Lexer::GetOperatorName(OP_CALL), mth_type_call, type_table, 2, any1, 1, FuncInfo::Pure);
    }
    r.regfunc(NULL, "Array", u_array, type1, 1, type1, 1, FuncInfo::Pure);
    r.regfunc(NULL, "Table", u_table, type2, 2, type1, 1, FuncInfo::Pure);

    const Type str1[] = { PRIMTYPE_STRING };
    const Type err1[] = { PRIMTYPE_ERROR };
    const Type anyopt1[] = { PRIMTYPE_ANY | TYPEBIT_OPTIONAL };
    const Type stropt1[] = { PRIMTYPE_STRING | TYPEBIT_OPTIONAL };
    r.regfunc(NULL, "error", u_error, str1, 1, err1, 1, FuncInfo::Pure);
    r.regfunc(NULL, "errstr", u_errstr, anyopt1, 1, stropt1, 1, FuncInfo::Pure);
}

// For testing import until scripts can build symtabs:
// __testmod(n) -> symtab {tval = n, t<n> = n, string::tns = n}, n + 1
static int u_testmod(VM *vm, Val *v)
{
    if(v[0].type != PRIMTYPE_UINT)
        return RTE_INCOMPATIBLE_TYPE;
    const uint n = v[0].u.ui;
    Runtime& rt = *vm->rt;
    SymTable *st = SymTable::GCNew(rt.gc); // TODO GC: not rooted
    if(!st)
        return RTE_ALLOC_FAIL;
    char name[32];
    snprintf(name, sizeof(name), "t%u", (unsigned)n);
    st->addSymbol(rt.gc, rt.sp.put("tval").id, Val(n));
    st->addSymbol(rt.gc, rt.sp.put(name).id, Val(n));
    st->addToNamespace(rt.gc, PRIMTYPE_STRING, rt.sp.put("tns").id, Val(n));
    v[0] = Val(st);
    v[1] = Val(n + 1);
    return 2;
}

static void reg_test(RTReg& r)
{
    const Type uint1[] = { PRIMTYPE_UINT };
    r.regfunc(NULL, "clock", u_clock, NULL, 0, uint1, 1, FuncInfo::None);
    r.regfunc(NULL, "time", u_clock, NULL, 0, uint1, 1, FuncInfo::None);

    const Type symtab_uint[] = { PRIMTYPE_SYMTAB, PRIMTYPE_UINT };
    r.regfunc(NULL, "__testmod", u_testmod, uint1, 1, symtab_uint, 2, FuncInfo::Pure);
}

void rtinit(SymTable& syms, GC& gc, StringPool& sp, TypeRegistry& tr)
{
    RTReg r = { gc, sp, tr, syms };
    reg_type_type(r);
    reg_types_special(r);
    reg_type_void(r);
    reg_type_func(r);
    reg_type_bool(r);
    reg_type_sint(r);
    reg_type_uint(r);
    reg_type_float(r);
    reg_type_string(r);
    reg_type_any(r); // before the containers, their methods use any?
    reg_type_array(r);
    reg_type_table(r);
    reg_type_symtab(r);
    reg_constants(r);
    reg_builtins(r);
    reg_test(r);

    OpReg opr = { syms, gc, sp, tr };
    reg_bool_ops(opr);
    reg_uint_ops(opr);
    reg_sint_ops(opr);
    reg_float_ops(opr);
    reg_string_ops(opr);
}

