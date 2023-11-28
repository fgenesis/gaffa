#pragma once

#include <stdlib.h>

typedef void* (*Galloc)(void *ud, void *ptr, size_t osize, size_t nsize);

struct GCobj
{
    GCobj *gcnext;
    unsigned gccolor;
    unsigned _reserved_;
    size_t gcsize;
};

struct GCroot
{
    GCobj *curobj;
    unsigned curcolor;
    unsigned gcstep;
    Galloc alloc;
    void *gcud;
};

void gc_step(GCroot *root, size_t n);
void *gc_new(GCroot *root, size_t bytes);
