#include "gc.h"
#include "gainternal.h"
#include <string.h>
#include "array.h"
#include "table.h"
#include "gaobj.h"

enum _GCflagsPriv // upper 16 bits
{
    _GCF_GC_ALLOCATED     = (1 << 16), // internally set when object was actually allocated via GC
    _GCF_BLACK            = (1 << 17), // Object marking step has started
    _GCF_GREY             = (1 << 18), // Object was entered into the grey list
};

enum
{
    GC_PHASE_IDLE,    // GC not running
    GC_PHASE_PREMARK, // GC early mark phase (start setting up greylist)
    GC_PHASE_MARK,    // GC object traversal phase (until greylist empty)
    GC_PHASE_SPLICE,  // Separate reachable and unreachable (ie. dead) objects
};

enum Costs
{
    COST_FIN = 10,
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

static void makegrey(GC& gc, GCobj *o)
{
    u32 f = o->gcTypeAndFlags;
    if(f & _GCF_GREY)
        return;

    o->gcTypeAndFlags = f | _GCF_GREY;

    GCprefix *p = prefixof(o);
    p->hdr.gcnext = gc.grey;
    gc.grey = p;
}

static int markval(ga_RT& rt, ValU v, int steps)
{
    assert(v.type < PRIMTYPE_ANY);

    if(v.type >= _PRIMTYPE_FIRST_OBJ)
    {
        makegrey(rt.gc, v.u.obj);
    }
    else switch(v.type)
    {
        case PRIMTYPE_ERROR:
            assert(false); // ??? FIXME
        case PRIMTYPE_STRING:
            rt.sp.mark(v.u.str);
            break;

        case PRIMTYPE_TYPE:
            //rt.tr.mark(v.u.t);
            break;

        default:
           //rt.tr.mark(v.type); // FIXME: basic types should be pinned; the rest is done via object marking
           break;
    }

    return steps - 1;
}

/*static void weakobj(ga_RT& rt, const GCobj *obj)
{
    // TODO: rememeber for later, must remove old elems before sweep
}*/

static int traverse_valarray_any(ga_RT& rt, const ValU *va, size_t N, int steps)
{
    for(tsize i = 0; i < N; ++i)
        steps = markval(rt, va[i], steps);
    return steps;
}

static int traverse_array(ga_RT& rt, const DArray *a, int steps)
{
    const tsize N = a->size();
    if(!N)
        return steps;

    /*if(obj->gcTypeAndFlags & GCF_WEAK)
    {
        weakobj(rt, obj);
        return steps;
    }*/

    if(a->t >= PRIMTYPE_ANY)
        traverse_valarray_any(rt, a->storage.vals, N, steps);
    else
    switch(a->t)
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
        case PRIMTYPE_FUNC:
        if(GCobj *objs = a->storage.objs)
            for(tsize i = 0; i < N; ++i)
                makegrey(rt.gc, &objs[i]);
        break;
    }

    return steps;
}

static int traverse_table(ga_RT& rt, Table *t, int steps)
{
    traverse_array(rt, &t->values(), steps);

    /*if(obj->gcTypeAndFlags & GCF_WEAK)
    {
        weakobj(rt, obj);
        return steps;

    }*/

    // mark table keys

    /*if(TKey *ks = t->keys)
    {
        switch(t->keytype.id)
        {
            case PRIMTYPE_ANY:


            case PRIMTYPE_TYPE:
            if(const Type *ts = a->storage.ts)
                for(tsize i = 0; i < N; ++i)
                    rt.tr.mark(ks[i].u.t);
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
            case PRIMTYPE_FUNC:
            if(GCobj *objs = a->storage.objs)
                for(tsize i = 0; i < N; ++i)
                    makegrey(rt.gc, &objs[i]);
            break;
        }
    }*/

    // FIXME: This might be slow. use faster code with direct keys access, but make sure any unused keys[] is some kind of nil
    const tsize N = t->size();
    for(tsize i = 0; i < N; ++i)
        markval(rt, t->keyat(i), steps);

    return steps;
}

static int traverse_dobj(ga_RT& rt, DObj *d, int steps)
{
    //if(d->dfields)
    //    makegrey(rt.gc, d->dfields);

    Val *v = d->memberArray();
    const tsize n = d->nmembers;
    return traverse_valarray_any(rt, v, n, steps);

}

/*
static int traverse_func(ga_RT& rt, DFunc *obj, int steps)
{
    // TODO
}
*/

// Mark object; delay marking of children
static int traverse_obj(ga_RT& rt, GCobj *obj, int steps)
{
    makegrey(rt.gc, obj->dtype);

    GCprefix *pre = prefixof(obj);
    u32 f = obj->gcTypeAndFlags;
    obj->gcTypeAndFlags = f | _GCF_BLACK;

    PrimType prim = PrimType(f & 0xff);

    assert(prim < PRIMTYPE_ANY);

    switch(prim)
    {
        case PRIMTYPE_ARRAY:  return traverse_array(rt, static_cast<DArray*>(obj), steps);
        case PRIMTYPE_TABLE:  return traverse_table(rt, static_cast<Table*>(obj), steps);
        case PRIMTYPE_OBJECT: return traverse_dobj(rt, static_cast<DObj*>(obj), steps);
        //case PRIMTYPE_FUNC:   return traverse_func(rt, static_cast<DFunc*>(obj), steps);
            // TODO
        default: ;
    }

    assert(false);
    return steps;
}


static void _gc_freeobj(GC& gc, GCprefix *o)
{
    --gc.info.live_objs;
    gc.info.used -= o->gcsize;
    gc.alloc(gc.gcud, o, o->gcsize, 0);

}

// not sure if this is necessary
void makePinnedGrey(GC& gc)
{
    GCprefix *o = gc.pinned;
    if(!o) // TODO: there will always be some pinned objects later on, then this check can be removed
        return;
    GCprefix *greyhead = gc.grey;
    GCprefix *whitehead = gc.normallywhite;

    do
    {
        GCprefix *const next = o->hdr.gcnext;

        if(o->gcTypeAndFlags & _GCF_PINNED) // Keep pinned object pinned, and make it grey
        {
            o->gcTypeAndFlags |= _GCF_BLACK;
            o->hdr.gcnext = greyhead;
            greyhead = o;
        }
        else // Object got unpinned, unlink it from the pinned list into the regular list
        {
            o->hdr.gcnext = whitehead;
            whitehead = o;
        }
        o = next;
    }
    while(o);

    gc.pinned = NULL;
    gc.normallywhite = whitehead;
    gc.grey = greyhead;
}

static size_t markstep(GC& gc, int n)
{
    assert(false); // TODO
    return n;
}

static void runfinalizer(ga_RT& gc, GCprefix *o)
{
    // TODO
    GCobj *obj = o->obj();
}


// Go through grey list, sort out white (unreachable/dead) objects and make black objects white
int splicestep(GC& gc, int n)
{
    GCprefix *o = gc.tosplice;
    if(!o)
        return n;

    GCprefix *whitehead = gc.normallywhite;
    GCprefix *deadhead = gc.dead;

    do
    {
        GCprefix * const next = o->hdr.gcnext;
        u32 f = o->gcTypeAndFlags;

        if(f & _GCF_BLACK)
        {
            // Object is still reachable. Clear marks.

            f &= ~(_GCF_BLACK | _GCF_GREY);

            o->gcTypeAndFlags = f;

            if(!(f & _GCF_PINNED)) // Regular objects go back into the regular list;
            {                      // which may have some new objects allocated in the meantime.
                o->hdr.gcnext = whitehead;
                whitehead = o;
            }
            else // Pinned objects go back into the pinned list
            {
                o->hdr.gcnext = gc.pinned;
                gc.pinned = o;
            }
        }
        else
        {
            // Object is unreachable
            o->hdr.gcnext = deadhead;
            deadhead = o;
        }

        o = next;
    }
    while(o && --n > 0);

    gc.tosplice = o;
    gc.normallywhite = whitehead;
    gc.dead = deadhead;
    return n;
}

static bool gc_canstart(GC& gc)
{
    return true; // TODO
}

static void freesomedead(ga_RT& rt)
{
    GCprefix *o = rt.gc.dead;
    if(!o)
        return;

    size_t remain = 2; // TODO: make this a gc setting
    do
    {
        GCprefix * const next = o->hdr.gcnext;

        u32 f = o->gcTypeAndFlags;

        // Finalizer?
        if(f & _GCF_FINALIZER)
        {
            // Resurrect (make white), but don't run the finalizer again
            f &= ~(_GCF_FINALIZER | _GCF_GREY | _GCF_BLACK);
            o->gcTypeAndFlags = f;
            o->hdr.gcnext = NULL;

            // This may or may not store o somewhere so that it's reachable again
            runfinalizer(rt, o);

            // If the object is in a GC list at this point, then the GC has picked it up again.
            // It it wasn't picked up, make it white because it may or may not have been resurrected.
            if(!o->hdr.gcnext)
            {
                o->hdr.gcnext = rt.gc.normallywhite;
                rt.gc.normallywhite = o;
            }
        }
        else
        {
            // Free for good
            _gc_freeobj(rt.gc, o);
        }

        o = next;
    }
    while(o && --remain);

    rt.gc.dead = o;
}

void gc_step(ga_RT& rt, size_t n)
{
    GC& gc = rt.gc;

    freesomedead(rt);

    switch(gc.phase)
    {
        case GC_PHASE_IDLE:
            if(!gc_canstart(gc))
                break;
            gc.phase = GC_PHASE_PREMARK;
        case GC_PHASE_PREMARK:
            assert(!gc.grey);
            assert(!gc.tosplice);
            gc.tosplice = gc.normallywhite;
            // Objects created from now on end up in the new white list, and are not touched in this GC cycle.
            gc.normallywhite = NULL;
            gc.grey = gc.pinned;
            gc.pinned = NULL;
            gc.phase = GC_PHASE_MARK;
        case GC_PHASE_MARK:
            n = markstep(gc, n);
            if(!n)
                return;
            gc.phase = GC_PHASE_SPLICE;
        case GC_PHASE_SPLICE:
            n = splicestep(gc, n);
            if(!n || gc.tosplice)
                return;
            assert(!gc.tosplice);
            gc.phase = GC_PHASE_IDLE;
            break;
    }
}

GCobj *gc_new(GC& gc, size_t bytes, PrimType gctype)
{
    STATIC_ASSERT(PRIMTYPE_ANY < 0xff);

    assert(gctype < PRIMTYPE_ANY);

    bytes += GCprefix::HDR_SIZE;
    GCprefix *p = (GCprefix*)gc.alloc(gc.gcud, NULL, 0, bytes);
    if(!p)
        return NULL;

    gc.info.used += bytes;
    ++gc.info.live_objs;

    p->gcTypeAndFlags = _GCF_GC_ALLOCATED | gctype;
    p->gcsize = bytes;

    // Link object into the gc list. If a collection is in progress,
    p->hdr.gcnext = gc.normallywhite;
    gc.normallywhite = p;

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
