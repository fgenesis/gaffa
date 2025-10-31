#pragma once

#include "strings.h"
#include "rttypes.h"
#include "gc.h"


struct Runtime
{
	Runtime();
	~Runtime();
	bool init(Galloc alloc);

	GC gc;
	StringPool sp;
	TypeRegistry tr;
};
