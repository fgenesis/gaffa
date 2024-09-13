#include "typing.h"
#include "table.h"
#include "array.h"
#include "gc.h"
#include <algorithm>

static const Type NilType = {PRIMTYPE_NIL};

size_t TDesc_AllocSize(size_t numFieldsAndBits)
{
	return sizeof(TDesc) + numFieldsAndBits * sizeof(Type) + numFieldsAndBits * sizeof(sref);
}

TDesc *TDesc_New(GC& gc, tsize numFieldsAndBits)
{
	size_t sz = TDesc_AllocSize(numFieldsAndBits);
	TDesc *td = (TDesc*)gc_alloc_unmanaged(gc, NULL, 0, sz);
	if(td)
		td->bits = numFieldsAndBits;
	return td;
}

void TDesc_Delete(GC& gc, TDesc *td)
{
	size_t sz = TDesc_AllocSize(td->bits);
	gc_alloc_unmanaged(gc, td, sz, 0);
}

TypeRegistry::TypeRegistry(GC& gc)
	: _gc(gc)
{
}

TypeRegistry::~TypeRegistry()
{
}

static bool _sortfields(const TypeAndName& a, const TypeAndName& b)
{
	return a.t.id < b.t.id;
}

static uint hashfields(const TypeAndName *fields, size_t N)
{
	uint ret = 0;
	for(size_t i = 0; i < N; ++i) // FIXME: do something sane
	{
		ret += (ret >> 3) ^ ret;
	}
	return ret;
}

Type TypeRegistry::construct(const Table& t)
{
	PodArray<TypeAndName> tn;
	const tsize N = t.size();
	if(!tn.resize(_gc, N))
		return NilType;

	for(tsize i = 0; i < N; ++i)
	{
		const KV e = t.index(i);
		if(e.k.type.id != PRIMTYPE_STRING)
			return NilType;
		if(e.v.type.id != PRIMTYPE_TYPE) // TODO: support initializers
			return NilType;
		tn[i].name = e.k.type.id;
		tn[i].t = e.v.u.t;
	}

	std::sort(tn.data(), tn.data() + N, _sortfields);
	tsize bits = N;

	TDesc *td = TDesc_New(_gc, bits);
	for(size_t i = 0; i < tn.size(); ++i)
	{
		td->names()[i] = tn[i].name;
		td->types()[i] = tn[i].t;
	}

	return _store(td);
}

Type TypeRegistry::construct(const DArray& t)
{
	const tsize N = t.size();
	TDesc *td = TDesc_New(_gc, N);
	if(t.t.id == PRIMTYPE_TYPE)
		for(size_t i = 0; i < N; ++i)
		{
			td->names()[i] = 0;
			td->types()[i] = t.storage.ts[i];
		}
	else
		for(size_t i = 0; i < N; ++i)
		{
			Val e = t.dynamicLookup(i);
			if(e.type.id != PRIMTYPE_TYPE)
			{
				TDesc_Delete(_gc, td);
				return NilType;
			}
			td->names()[i] = 0;
			td->types()[i] = e.u.t;
		}

	return _store(td);
}

unsigned TypeRegistry::lookup(const TypeAndName* tn, size_t n)
{
	const uint hash = hashfields(tn, n);

	return 0;
}

Type TypeRegistry::_store(TDesc *td)
{
	return NilType; // FIXME
}
