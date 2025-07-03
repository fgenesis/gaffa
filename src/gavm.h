#pragma once

#include "defs.h"
#include "typing.h"

struct VmIter;

// advance iterator; old value is in val and updated to new value
// continue iteration until this returns 0
typedef uint (*IterAdv)(ValU& val, VmIter& it);

struct VmIter
{
    IterAdv next;

    union
    {
        struct
        {
            _AnyValU end, step;
            ValU start;
        } numeric;

        struct
        {
            tsize i;
            GCobj *obj;
        } index;
    } u;
};
