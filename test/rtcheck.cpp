// Runtime checks of builtin C functions (leaf functions registered by rtinit), called directly.
// Exists until running scripts works end to end; then these should become script tests.
// No arguments; prints failed checks, exits 0 iff all passed.

#include "runtime.h"
#include "gainternal.h"
#include "symtable.h"
#include "strings.h"
#include "gavm.h"
#include "gaobj.h"
#include "table.h"
#include "array.h"
#include "lex.h"
#include "rttypes.h"
#include "typing.h"
#include "gaimpdbg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *alloc(void *ud, void *ptr, size_t osz, size_t nsz)
{
    (void)ud; (void)osz;
    if(!nsz)
    {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, nsz);
}

static int fails = 0;
static int checks = 0;
#define CHECK(c) do { ++checks; if(!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while(0)

struct Ctx
{
    Runtime rt;
    SymTable *env;
    VM vm;
};

static const DFunc *method(Ctx& c, Type t, const char *name)
{
    const Val *v = c.env->lookupInNamespace(t, c.rt.sp.put(name).id);
    return v ? v->asFunc() : NULL;
}

static const DFunc *opfunc(Ctx& c, Type t, OperatorId op)
{
    return method(c, t, Lexer::GetOperatorName(op));
}

// The objects here are made by hand, without a DType, so Val(GCobj*) can't be used
static Val objval(GCobj *o, PrimType t)
{
    Val v = _Nil();
    v.type = t;
    v.u.obj = o;
    return v;
}

// Call with up to 3 args; the result area holds max(args, results) = 3 values
static int call(Ctx& c, const DFunc *f, Val *v, Val a0, Val a1, Val a2 = _Nil())
{
    v[0] = a0;
    v[1] = a1;
    v[2] = a2;
    return f->call(&c.vm, v);
}

// The signature of a family method for a specialized type, as a string
static bool sigIs(Ctx& c, const DFunc *f, Type self, const char *expect)
{
    if(!f || !f->tsig)
        return false;
    const std::string s = getTypeName(c.rt, f->tsig(c.rt.tr, self));
    if(s == expect)
        return true;
    printf("  signature is '%s', expected '%s'\n", s.c_str(), expect);
    return false;
}

static void checkArray(Ctx& c)
{
    const DFunc *get = method(c, PRIMTYPE_ARRAY, "__getindex");
    const DFunc *getopt = method(c, PRIMTYPE_ARRAY, "__getindexopt");
    const DFunc *set = method(c, PRIMTYPE_ARRAY, "__setindex");
    const DFunc *it1 = method(c, PRIMTYPE_ARRAY, "__iter1");
    const DFunc *it2 = method(c, PRIMTYPE_ARRAY, "__iter2");
    CHECK(get && getopt && set && it1 && it2);
    if(!(get && getopt && set && it1 && it2))
        return;
    CHECK(opfunc(c, PRIMTYPE_ARRAY, OP_GETINDEX) == getopt); // [] of the generic array (any?) is __getindexopt
    CHECK(opfunc(c, PRIMTYPE_ARRAY, OP_SETINDEX) == set);

    // Signatures for a specialized type
    const Type af = c.rt.tr.mkarray(PRIMTYPE_FLOAT);
    CHECK(sigIs(c, get, af, "func(Const(Array(float)), uint) -> float"));
    CHECK(sigIs(c, getopt, af, "func(Const(Array(float)), uint) -> float?"));
    CHECK(sigIs(c, set, af, "func(Array(float), uint, float)")); // no return values
    CHECK(set->tsig(c.rt.tr, c.rt.tr.mktable(PRIMTYPE_STRING, PRIMTYPE_UINT)) == PRIMTYPE_AUTO); // not for tables
    CHECK(sigIs(c, it1, af, "func(Const(Array(float)), uint) -> float"));
    CHECK(sigIs(c, it2, af, "func(Const(Array(float)), uint) -> uint, float"));
    CHECK(get->tsig(c.rt.tr, PRIMTYPE_TABLE) == PRIMTYPE_AUTO); // not an array

    DArray *a = DArray::GCNew(c.rt.gc, 4, PRIMTYPE_UINT); // typed storage
    a->dynamicAppend(c.rt.gc, Val(uint(10)));
    a->dynamicAppend(c.rt.gc, Val(uint(20)));
    const Val av = objval(a, PRIMTYPE_ARRAY);
    Val v[3];

    CHECK(call(c, get, v, av, Val(uint(1))) == 1 && v[0].type == PRIMTYPE_UINT && v[0].u.ui == 20);
    CHECK(call(c, get, v, av, Val(uint(5))) == RTE_OUT_OF_BOUNDS);
    CHECK(call(c, getopt, v, av, Val(uint(5))) == 1 && v[0].type == PRIMTYPE_NIL);
    CHECK(call(c, set, v, av, Val(uint(0)), Val(uint(7))) == 0);
    CHECK(call(c, get, v, av, Val(uint(0))) == 1 && v[0].u.ui == 7);
    CHECK(call(c, set, v, av, Val(uint(2)), Val(uint(7))) == RTE_OUT_OF_BOUNDS);
    CHECK(call(c, set, v, av, Val(uint(0)), Val(real(1.5f))) == RTE_INCOMPATIBLE_TYPE); // typed uint storage
    CHECK(call(c, get, v, av, Val(+1)) == RTE_INCOMPATIBLE_TYPE); // index must be a uint

    // Iteration: values until the index is past the end, then no values
    CHECK(call(c, it1, v, av, Val(uint(1))) == 1 && v[0].u.ui == 20);
    CHECK(call(c, it2, v, av, Val(uint(1))) == 2 && v[0].type == PRIMTYPE_UINT && v[0].u.ui == 1 && v[1].u.ui == 20);
    CHECK(call(c, it1, v, av, Val(uint(2))) == 0);
    CHECK(call(c, it2, v, av, Val(uint(2))) == 0);
}

static void checkTable(Ctx& c)
{
    const DFunc *get = method(c, PRIMTYPE_TABLE, "__getindex");
    const DFunc *getopt = method(c, PRIMTYPE_TABLE, "__getindexopt");
    const DFunc *set = method(c, PRIMTYPE_TABLE, "__setindex");
    const DFunc *it1 = method(c, PRIMTYPE_TABLE, "__iter1");
    const DFunc *it2 = method(c, PRIMTYPE_TABLE, "__iter2");
    const DFunc *it3 = method(c, PRIMTYPE_TABLE, "__iter3");
    CHECK(get && getopt && set && it1 && it2 && it3);
    if(!(get && getopt && set && it1 && it2 && it3))
        return;
    CHECK(opfunc(c, PRIMTYPE_TABLE, OP_GETINDEX) == getopt);
    CHECK(opfunc(c, PRIMTYPE_TABLE, OP_SETINDEX) == set);

    const Type ts = c.rt.tr.mktable(PRIMTYPE_STRING, PRIMTYPE_UINT);
    CHECK(sigIs(c, get, ts, "func(Const(Table(string, uint)), string) -> uint"));
    CHECK(sigIs(c, getopt, ts, "func(Const(Table(string, uint)), string) -> uint?"));
    CHECK(sigIs(c, set, ts, "func(Table(string, uint), string, uint)"));
    CHECK(sigIs(c, it1, ts, "func(Const(Table(string, uint)), uint) -> uint"));
    CHECK(sigIs(c, it2, ts, "func(Const(Table(string, uint)), uint) -> string, uint"));
    CHECK(sigIs(c, it3, ts, "func(Const(Table(string, uint)), uint) -> uint, string, uint"));

    Table *t = Table::GCNew(c.rt.gc, PRIMTYPE_STRING, PRIMTYPE_UINT); // typed keys and values
    const Val tv = objval(t, PRIMTYPE_TABLE);
    const Val kx = Val(_Str(c.rt.sp.put("x").id));
    const Val ky = Val(_Str(c.rt.sp.put("y").id));
    Val v[3];

    CHECK(call(c, set, v, tv, kx, Val(uint(3))) == 0);
    CHECK(call(c, get, v, tv, kx) == 1 && v[0].type == PRIMTYPE_UINT && v[0].u.ui == 3);
    CHECK(call(c, get, v, tv, ky) == RTE_KEY_NOT_FOUND);
    CHECK(call(c, getopt, v, tv, ky) == 1 && v[0].type == PRIMTYPE_NIL);
    CHECK(call(c, set, v, tv, Val(uint(1)), Val(uint(3))) == RTE_INCOMPATIBLE_TYPE); // key type
    CHECK(call(c, set, v, tv, _Nil(), Val(uint(3))) == RTE_INCOMPATIBLE_TYPE);       // nil key
    CHECK(call(c, set, v, tv, ky, Val(uint(4))) == 0);

    // Iteration in insertion order: x=3, y=4
    CHECK(call(c, it1, v, tv, Val(uint(1))) == 1 && v[0].u.ui == 4);
    CHECK(call(c, it2, v, tv, Val(uint(0))) == 2 && v[0].type == PRIMTYPE_STRING && v[0].u.str == kx.u.str && v[1].u.ui == 3);
    CHECK(call(c, it3, v, tv, Val(uint(1))) == 3 && v[0].u.ui == 1 && v[1].u.str == ky.u.str && v[2].u.ui == 4);
    CHECK(call(c, it1, v, tv, Val(uint(2))) == 0);
    CHECK(call(c, it3, v, tv, Val(uint(2))) == 0);

    // Generic table: nil is a regular value
    Table *g = Table::GCNew(c.rt.gc, PRIMTYPE_ANY, PRIMTYPE_ANY);
    const Val gv = objval(g, PRIMTYPE_TABLE);
    CHECK(call(c, set, v, gv, kx, _Nil()) == 0);
    CHECK(call(c, get, v, gv, kx) == 1 && v[0].type == PRIMTYPE_NIL); // present, holding nil
}

static void checkString(Ctx& c)
{
    const DFunc *it1 = method(c, PRIMTYPE_STRING, "__iter1");
    const DFunc *it2 = method(c, PRIMTYPE_STRING, "__iter2");
    CHECK(it1 && it2);
    if(!(it1 && it2))
        return;
    const char bytes[] = { 'a', (char)0xff };
    const Val sv = Val(_Str(c.rt.sp.put(bytes, sizeof(bytes)).id)); // bytes 0x61, 0xff
    Val v[3];
    CHECK(call(c, it1, v, sv, Val(uint(0))) == 1 && v[0].type == PRIMTYPE_UINT && v[0].u.ui == 0x61);
    CHECK(call(c, it1, v, sv, Val(uint(1))) == 1 && v[0].u.ui == 0xff); // unsigned bytes
    CHECK(call(c, it2, v, sv, Val(uint(1))) == 2 && v[0].u.ui == 1 && v[1].u.ui == 0xff);
    CHECK(call(c, it1, v, sv, Val(uint(2))) == 0); // end
    CHECK(call(c, it2, v, sv, Val(uint(2))) == 0);
    const Val empty = Val(_Str(c.rt.sp.put("").id));
    CHECK(call(c, it1, v, empty, Val(uint(0))) == 0);
}

// Struct types (TDesc with names and defaults) and objects (DObj)
static void checkStruct(Ctx& c)
{
    // {x=float, n=5, s="a"}: x without a default
    const sref nx = c.rt.sp.put("x").id, nn = c.rt.sp.put("n").id, ns = c.rt.sp.put("s").id;
    StructMember m[3];
    m[0].t = PRIMTYPE_FLOAT; m[0].name = nx; m[0].defaultval = _Xnil();
    m[1].t = PRIMTYPE_UINT; m[1].name = nn; m[1].defaultval = Val(uint(5));
    m[2].t = PRIMTYPE_STRING; m[2].name = ns; m[2].defaultval = Val(_Str(c.rt.sp.put("a").id));
    const TDesc *td = c.rt.tr.mkstructDesc(m, 3, 2);
    CHECK(td && td->kind == TKIND_STRUCT && td->n == 3 && td->numdefaults == 2);
    if(!td)
        return;
    CHECK(c.rt.tr.mkstructDesc(m, 3, 2) == td); // dedup'd: same structure, same type
    CHECK(getTypeName(c.rt, td->id()) == "{x=float, n=5, s=\"a\"}");

    DType *dt = c.rt.tr.asObj(td);
    CHECK(dt && dt->fieldIndices.size() == 3);
    if(!dt)
        return;

    DObj *o = DObj::GCNew(c.rt.gc, dt);
    CHECK(o && o->nmembers == 3);
    if(!o)
        return;
    const Val *mem = o->memberArray();
    CHECK(mem[0].type == PRIMTYPE_NIL); // no default
    CHECK(mem[1].type == PRIMTYPE_UINT && mem[1].u.ui == 5);
    CHECK(mem[2].type == PRIMTYPE_STRING && mem[2].u.str == c.rt.sp.put("a").id);
    CHECK(o->member(Val(_Str(nn))) == &o->memberArray()[1]); // field name -> member via DType::fieldIndices
    CHECK(o->member(Val(_Str(ns))) == &o->memberArray()[2]);
}

// Constructors: type::__new(table) -> type, type::()(T, table) -> T
static void checkConstructors(Ctx& c)
{
    const DFunc *tnew = method(c, PRIMTYPE_TYPE, "__new");
    const DFunc *tcall = opfunc(c, PRIMTYPE_TYPE, OP_CALL);
    CHECK(tnew && tcall && tnew->isPure() && tcall->isPure());
    if(!(tnew && tcall))
        return;
    const Val kx = Val(_Str(c.rt.sp.put("x").id));
    const Val kn = Val(_Str(c.rt.sp.put("n").id));
    const Val ko = Val(_Str(c.rt.sp.put("o").id));
    const Val ky = Val(_Str(c.rt.sp.put("y").id));
    const Val tfloat = Val(c.rt.tr.asObj(PRIMTYPE_FLOAT));
    const Val toptuint = Val(c.rt.tr.asObj(PRIMTYPE_UINT | TYPEBIT_OPTIONAL));
    Val v[3];

    // Template {x=float, n=5, o=uint?}
    Table *tpl = Table::GCNew(c.rt.gc, PRIMTYPE_ANY, PRIMTYPE_ANY);
    tpl->set(c.rt.gc, kx, tfloat);
    tpl->set(c.rt.gc, kn, Val(uint(5)));
    tpl->set(c.rt.gc, ko, toptuint);
    CHECK(call(c, tnew, v, objval(tpl, PRIMTYPE_TABLE), _Nil()) == 1 && v[0].type == PRIMTYPE_TYPE);
    DType *st = v[0].asDType();
    CHECK(st && getTypeName(c.rt, st->tid) == "{x=float, n=5, o=uint?}");
    if(!st)
        return;

    // type(t) via type::() is the same as type::__new
    CHECK(call(c, tcall, v, Val(c.rt.tr.asObj(PRIMTYPE_TYPE)), objval(tpl, PRIMTYPE_TABLE)) == 1 && v[0].asDType() == st);

    // A nil default gives no type
    Table *bad = Table::GCNew(c.rt.gc, PRIMTYPE_ANY, PRIMTYPE_ANY);
    bad->set(c.rt.gc, kx, _Nil());
    CHECK(call(c, tnew, v, objval(bad, PRIMTYPE_TABLE), _Nil()) == RTE_INCOMPATIBLE_TYPE);

    // Constructing the struct from a table: defaults, optional field nil
    Table *t = Table::GCNew(c.rt.gc, PRIMTYPE_ANY, PRIMTYPE_ANY);
    t->set(c.rt.gc, kx, Val(real(1.5f)));
    CHECK(call(c, tcall, v, Val(st), objval(t, PRIMTYPE_TABLE)) == 1 && v[0].type == PRIMTYPE_OBJECT);
    if(DObj *o = v[0].asDObj())
    {
        CHECK(o->dtype == st);
        CHECK(o->memberArray()[0].type == PRIMTYPE_FLOAT && o->memberArray()[0].u.f == 1.5f);
        CHECK(o->memberArray()[1].type == PRIMTYPE_UINT && o->memberArray()[1].u.ui == 5);
        CHECK(o->memberArray()[2].type == PRIMTYPE_NIL);
    }

    // Unknown field, wrong type (no conversion at runtime), missing required field
    Table *t2 = Table::GCNew(c.rt.gc, PRIMTYPE_ANY, PRIMTYPE_ANY);
    t2->set(c.rt.gc, kx, Val(real(1.5f)));
    t2->set(c.rt.gc, ky, Val(uint(1)));
    CHECK(call(c, tcall, v, Val(st), objval(t2, PRIMTYPE_TABLE)) == RTE_KEY_NOT_FOUND);
    Table *t3 = Table::GCNew(c.rt.gc, PRIMTYPE_ANY, PRIMTYPE_ANY);
    t3->set(c.rt.gc, kx, Val(uint(1)));
    CHECK(call(c, tcall, v, Val(st), objval(t3, PRIMTYPE_TABLE)) == RTE_INCOMPATIBLE_TYPE);
    Table *t4 = Table::GCNew(c.rt.gc, PRIMTYPE_ANY, PRIMTYPE_ANY);
    t4->set(c.rt.gc, kn, Val(uint(7)));
    CHECK(call(c, tcall, v, Val(st), objval(t4, PRIMTYPE_TABLE)) == RTE_KEY_NOT_FOUND);

    // Other types have no runtime constructor dispatch
    CHECK(call(c, tcall, v, Val(c.rt.tr.asObj(PRIMTYPE_UINT)), objval(t, PRIMTYPE_TABLE)) == RTE_NOT_CALLABLE);
}

int main()
{
    Ctx c;
    if(!c.rt.init(alloc))
        return 2;
    c.env = SymTable::GCNew(c.rt.gc);
    c.vm.rt = &c.rt;
    rtinit(*c.env, c.rt.gc, c.rt.sp, c.rt.tr);

    checkArray(c);
    checkTable(c);
    checkString(c);
    checkStruct(c);
    checkConstructors(c);

    printf("%s: %d checks, %d failed\n", fails ? "FAILED" : "OK", checks, fails);
    return fails ? 1 : 0;
}
