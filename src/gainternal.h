#pragma once

#include "gaffa.h"
#include "defs.h"

struct GaAlloc
{
	GaffaAllocFunc alloc;
	void *ud;
};
