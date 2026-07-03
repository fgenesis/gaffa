#pragma once

#include "strings.h"
#include "rttypes.h"
#include "gc.h"


struct CommonStrings
{
    bool init(StringPool &sp);
    sref operatorNames[_OP_MAX];
    sref primtypeNames[PRIMTYPE_MAX];
};

struct Runtime
{
    Runtime();
    ~Runtime();
    bool init(Galloc alloc);

    GC gc;
    StringPool sp;
    TypeRegistry tr;
    CommonStrings cs;

};
