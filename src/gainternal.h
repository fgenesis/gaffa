#pragma once

/*#ifdef _MSC_VER
#pragma warning(ignore: C26812) // Unscoped enum
#endif*/

#include "gaffa.h"
#include "defs.h"

struct GaAlloc
{
	GaffaAllocFunc alloc;
	void *ud;
};
