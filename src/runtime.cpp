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

	return sp.init() && tr.init();
}
