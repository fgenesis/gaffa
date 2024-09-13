#pragma once

/*#ifdef _MSC_VER
#pragma warning(ignore: C26812) // Unscoped enum
#endif*/

#include "gaffa.h"
#include "defs.h"
#include "gc.h"

#include <assert.h>

struct GaAlloc
{
	GaffaAllocFunc alloc;
	void *ud;
};

struct GaState
{
	GC gc;
};
