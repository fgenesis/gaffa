#include "typing.h"
#include "table.h"
#include "array.h"
#include <algorithm>

size_t TDesc_AllocSize(size_t numFieldsAndBits)
{
	return sizeof(TDesc) + numFieldsAndBits * sizeof(Type) + numFieldsAndBits * sizeof(unsigned);
}

TDesc *TDesc_New(const GaAlloc& ga, size_t numFieldsAndBits)
{
	size_t sz = TDesc_AllocSize(numFieldsAndBits);
	TDesc *td = (TDesc*)ga.alloc(ga.ud, NULL, 0, sz);
	if(td)
		td->bits = numFieldsAndBits;
	return td;
}

void TDesc_Delete(const GaAlloc& ga, TDesc *td)
{
	size_t sz = TDesc_AllocSize(td->bits);
	ga.alloc(ga.ud, td, sz, 0);
}

TypeRegistry::TypeRegistry(const GaAlloc& ga)
	: _ga(ga)
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

TDesc* TypeRegistry::construct(const Table& t)
{
	std::vector<TypeAndName> tn;
	tn.reserve(t._pairs.size());

	for(size_t i = 0; i < t._pairs.size(); ++i)
	{
		const ValU& k = t._pairs[i].k;
		const ValU& v = t._pairs[i].v;
		if(k.type.id != PRIMTYPE_STRING)
			return NULL;
		if(v.type.id != PRIMTYPE_TYPE) // TODO: support initializers
			return NULL;
		TypeAndName t;
		t.name = k.type.id;
		t.t = v.u.t;
		tn.push_back(t);
	}

	std::sort(tn.begin(), tn.end(), _sortfields);
	size_t bits = t._pairs.size();

	TDesc *td = TDesc_New(_ga, bits);
	for(size_t i = 0; i < tn.size(); ++i)
	{
		td->names()[i] = tn[i].name;
		td->types()[i] = tn[i].t;
	}

	return _store(td);
}

TDesc* TypeRegistry::construct(const Array& t)
{
	const size_t n = t.n;
	TDesc *td = TDesc_New(_ga, n);
	if(t.t.id == PRIMTYPE_TYPE)
		for(size_t i = 0; i < n; ++i)
		{
			td->names()[i] = 0;
			td->types()[i] = t.storage.ts[i];
		}
	else
		for(size_t i = 0; i < n; ++i)
		{
			Val e = t.dynamicLookup(i);
			if(e.type.id != PRIMTYPE_TYPE)
			{
				TDesc_Delete(_ga, td);
				return NULL;
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

TDesc *TypeRegistry::_store(TDesc *td)
{
	return td;
}
