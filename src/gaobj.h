#pragma once

#include "defs.h"
#include "table.h"
#include "typing.h"

// generated from TDesc
class DStructDef : public GCobj
{
    Type t;
    Table name2idx;
    PodArray<ValU> fields; // type is type of field, value is default value for field
};

class DObj : public GCobj
{
    DStructDef *def;
    PodArray<ValU> fields;
};

