#include "runtime.h"

Runtime::Runtime()
    : sp(gc)
    , tr(gc)
{
}

Runtime::~Runtime()
{
}

bool Runtime::init(Galloc alloc)
{
    gc.alloc = alloc;

    return sp.init() && tr.init() && cs.init(sp);
}

bool CommonStrings::init(StringPool& sp)
{
    for(size_t i = 0; i < PRIMTYPE_MAX; ++i)
        primtypeNames[i] = sp.put(GetPrimTypeName(i)).id;
    for(size_t i = 0; i < _OP_MAX; ++i)
        operatorNames[i] = sp.put(GetOperatorName((OperatorId)i)).id;
    return true;
}
