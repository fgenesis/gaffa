#pragma once

#include "gaobj.h"

struct VM;
struct Runtime;

struct DCoro : public GCobj
{
	static DCoro *GCNew(Runtime *rt, DFunc *func);

	struct Result
	{
		int status;
		const Val *rets;
	};

	// Non-yieldable call. Returns number of return values written to a if >= 0, RTError otherwise
	int callNoYield(Val *a, size_t nargs, size_t maxret);
	int callYield(Val *a, size_t nargs, size_t maxret);
	Result callEx(Val *a, size_t nargs);

	VM vm;
};
