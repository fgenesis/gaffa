#include "gc.h"
#include "gainternal.h"
#include <string.h>
#include "array.h"
#include "table.h"

enum
{
    GC_UNK,
    GC_COL_A,
    GC_COL_B
};

enum
{
    STEP_MARK,
    STEP_SWEEP,
    STEP_END
};

struct GCprefix;

// This is the hidden, GC-only part in front of a GCobj.
struct GCheader
{
    GCprefix *gcnext;
};

// This struct overlays GCheader with the beginning of a GCobj
struct GCprefix
{
    GCheader hdr;
    // --------
    // This is overlaid in memory. The following fields are the same as in GCobj.
    u32 gcTypeAndFlags;
    u32 gcsize;

    enum { HDR_SIZE = sizeof(GCheader) };

    inline GCobj *obj() { return reinterpret_cast<GCobj*>(((char*)this) + HDR_SIZE); }
};

static GCprefix *prefixof(GCobj *obj)
{
    assert(obj->gcTypeAndFlags & _GCF_GC_ALLOCATED);
    return reinterpret_cast<GCprefix*>(((char*)obj) - GCprefix::HDR_SIZE);
}

static size_t gcMarkObj(ga_RT& rt, GCobj *pre, size_t steps);

static size_t gcMarkVal(ga_RT& rt, ValU v, size_t steps)
{
    assert(v.type.id != PRIMTYPE_ANY);

    switch(v.type.id)
    {
        case PRIMTYPE_ERROR:
        case PRIMTYPE_STRING:
            rt.sp.mark(v.u.str);
            return steps;

        case PRIMTYPE_TYPE:
            rt.tr.mark(v.u.t);
            return steps;

        default:
           rt.tr.mark(v.type);
        case PRIMTYPE_ARRAY:
        case PRIMTYPE_TABLE:
        case PRIMTYPE_OBJECT:
            return gcMarkObj(rt, v.u.obj, steps);
    }
}

static void weakobj(ga_RT& rt, const GCobj *obj)
{
    // TODO: rememeber for later, must remove old elems before sweep
}

static size_t gcMarkCh_Array(ga_RT& rt, const GCobj *obj, size_t steps)
{
    const DArray *a = static_cast<const DArray*>(obj);
    const tsize N = a->size();
    if(!N)
        return steps;

    if(obj->gcTypeAndFlags & GCF_WEAK)
    {
        weakobj(rt, obj);
        return steps;
    }

    if(a->t.id >= PRIMTYPE_ANY)
    {
        ValU *p = a->storage.vals;

        for(tsize i = 0; i < N; ++i)
            gcMarkVal(rt, p[i], steps);

        return steps;
    }

    switch(a->t.id)
    {
        case PRIMTYPE_TYPE:
        if(const Type *ts = a->storage.ts)
            for(tsize i = 0; i < N; ++i)
                rt.tr.mark(ts[i]);
        break;

        case PRIMTYPE_STRING:
        case PRIMTYPE_ERROR:
        if(const sref *ss = a->storage.s)
            for(tsize i = 0; i < N; ++i)
                rt.sp.mark(ss[i]);
        break;

        case PRIMTYPE_ARRAY:
        case PRIMTYPE_TABLE:
        case PRIMTYPE_OBJECT:
        if(GCobj *objs = a->storage.objs)
            for(tsize i = 0; i < N; ++i)
                gcMarkObj(rt, objs, steps);
        break;
    }

    return steps;
}

static size_t gcMarkCh_Table(ga_RT& rt, GCobj *obj, size_t steps)
{
    Table *t = static_cast<Table*>(obj);
    gcMarkCh_Array(rt, &t->values(), steps);

    if(obj->gcTypeAndFlags & GCF_WEAK)
    {
        weakobj(rt, obj);
        return steps;

    }

    // mark table keys
    const tsize N = t->size();
    for(tsize i = 0; i < N; ++i)
        gcMarkVal(rt, t->keyat(i), steps);

    return steps;
}

static size_t gcMarkCh_Obj(ga_RT& rt, GCobj *obj, size_t steps)
{
    // TODO: mark members & type
}

static size_t gcMarkCh_Func(ga_RT& rt, GCobj *obj, size_t steps)
{
    // TODO
}

// Mark object; delay marking of children
static size_t gcMarkObj(ga_RT& rt, GCobj *obj, size_t steps)
{
    GCprefix *pre = prefixof(obj);
    obj->gcTypeAndFlags |= _GCF_COLOR_1; // FIXME

    PrimType prim = PrimType(pre->gcTypeAndFlags & 0xff);

    assert(prim < PRIMTYPE_ANY);


    if(obj->gcTypeAndFlags & GCF_RECURSIVE)
    {
        switch(prim)
        {
            case PRIMTYPE_ARRAY:  return gcMarkCh_Array(rt, obj, steps);
            case PRIMTYPE_TABLE:  return gcMarkCh_Table(rt, obj, steps);
            case PRIMTYPE_OBJECT: return gcMarkCh_Obj(rt, obj, steps);
            case PRIMTYPE_FUNC:   return gcMarkCh_Func(rt, obj, steps);
                // TODO
        }
    }

}


static void _gc_freeobj(GC& gc, GCprefix *o)
{
    --gc.info.live_objs;
    gc.info.used -= o->gcsize;
    gc.alloc(gc.gcud, o, o->gcsize, 0);

}


void gc_step(GC& gc, size_t n)
{
    const u32 col = gc.curcolor;
    GCprefix * const loopbegin = gc.curobj;
    if(!loopbegin)
        return;
    GCprefix *it = loopbegin;
    GCprefix *last = loopbegin;
    switch(gc.gcstep)
    {
        case STEP_MARK:
        {
        }
        // fall through
        gc.gcstep = STEP_SWEEP;
        case STEP_SWEEP:
        {
            while(n--)
            {
                GCprefix *o = it;
                it = it->hdr.gcnext;
                if((o->gcTypeAndFlags & _GCF_COLOR_MASK) != col)
                    _gc_freeobj(gc, o);
                else
                {
                    last->hdr.gcnext = o;
                    last = o;
                }
            }

        }
        gc.gcstep = STEP_MARK;
    }

    gc.curobj = last;
}

GCobj *gc_new(GC& gc, size_t bytes, PrimType gctype)
{
    STATIC_ASSERT(PRIMTYPE_ANY < 0xff);

    assert(gctype < PRIMTYPE_ANY);

    bytes += sizeof(GCprefix::HDR_SIZE);
    GCprefix *p = (GCprefix*)gc.alloc(gc.gcud, NULL, 0, bytes);
    if(!p)
        return NULL;

    gc.info.used += bytes;
    ++gc.info.live_objs;

    p->gcTypeAndFlags = _GCF_GC_ALLOCATED | gctype | gc.curcolor;
    p->gcsize = bytes;
    p->hdr.gcnext = gc.curobj;
    gc.curobj = p;
    return p->obj();
}

void* gc_alloc_unmanaged(GC& gc, void* p, size_t oldsize, size_t newsize)
{
    void *ret = gc.alloc(gc.gcud, p, oldsize, newsize);
    if(ret || !newsize)
        gc.info.used += (newsize - oldsize);
    return ret;
}

void* gc_alloc_unmanaged_zero(GC& gc, size_t size)
{
    if(!size)
        return NULL;
    void *p = gc.alloc(gc.gcud, NULL, 0, size);
    if(p)
    {
        gc.info.used += size;
        memset(p, 0, size);
    }
    return p;
}
