#include "gc.h"

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

static void markobj(GCobj *o, unsigned n)
{

}

static unsigned marksome(GC* root, unsigned n)
{
    //GCobj *o = root->curcolor;
    while(n--)
    {
    }
    return 0;
}

static void _gc_free(GC& gc, GCobj *o)
{
    gc.alloc(gc.gcud, o, o->gcsize, 0);
}


void gc_step(GC& gc, size_t n)
{
    const unsigned col = gc.curcolor;
    GCobj * const loopbegin = gc.curobj;
    if(!loopbegin)
        return;
    GCobj *it = loopbegin;
    GCobj *last = loopbegin;
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
                GCobj *o = it;
                it = it->gcnext;
                if(o->gccolor != col)
                    _gc_free(gc, o);
                else
                {
                    last->gcnext = o;
                    last = o;
                }
            }

        }
        gc.gcstep = STEP_MARK;
    }

    gc.curobj = last;
}

void *gc_new(GC& gc, size_t bytes)
{
    bytes += sizeof(GCobj);
    GCobj *p = (GCobj*)gc.alloc(gc.gcud, NULL, 0, bytes);
    if(p)
    {
        p->gccolor = 0;
        p->gcsize = bytes;
        p->gcnext = gc.curobj;
        gc.curobj = p;
        ++p;
    }
    return p;
}

void* gc_alloc_unmanaged(GC& gc, void* p, size_t oldsize, size_t newsize)
{
    return gc.alloc(gc.gcud, p, oldsize, newsize);
}
