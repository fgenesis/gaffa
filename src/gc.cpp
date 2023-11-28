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

static unsigned marksome(GCroot* root, unsigned n)
{
    //GCobj *o = root->curcolor;
    while(n--)
    {
    }
}

void gc_step(GCroot *root, size_t n)
{
    const unsigned col = root->curcolor;
    GCobj * const loopbegin = root->curobj;
    if(!loopbegin)
        return;
    GCobj *it = loopbegin;
    GCobj *last = loopbegin;
    switch(root->gcstep)
    {
        case STEP_MARK:
        {
        }
        // fall through
        root->gcstep = STEP_SWEEP;
        case STEP_SWEEP:
        {
            while(n--)
            {
                GCobj *o = it;
                it = it->gcnext;
                if(o->gccolor != col)
                    root->alloc(root->gcud, o, o->gcsize + sizeof(GCobj), 0);
                else
                {
                    last->gcnext = o;
                    last = o;
                }
            }
            
        }
        root->gcstep = STEP_MARK;
    }
    
    root->curobj = last;
}

void *gc_new(GCroot* root, size_t bytes)
{
    GCobj *p = (GCobj*)root->alloc(root->gcud, NULL, 0, bytes + sizeof(GCobj));
    if(p)
    {
        p->gccolor = 0;
        p->gcsize = bytes;
        GCobj *cur = root->curobj;
        p->gcnext = cur->gcnext;
        cur->gcnext = p;
        ++p;
    }
    return p;
}
