#include "typing.h"
#include "table.h"
#include <algorithm>

TDesc *TDesc_New(const GaAlloc& ga, size_t numFieldsAndBits)
{
	size_t sz = sizeof(TDesc) + numFieldsAndBits * sizeof(Type) + numFieldsAndBits * sizeof(unsigned);
	TDesc *td = (TDesc*)ga.alloc(ga.ud, NULL, 0);
	return td;
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

	TDesc *td = TDesc_New(_ga, t._pairs.size());
	for(size_t i = 0; i < tn.size(); ++i)
	{
		td->names()[i] = tn[i].name;
		td->types()[i] = tn[i].t;
	}

	return td;
}

TDesc* TypeRegistry::construct(const Array& t)
{
	return nullptr;
}

unsigned TypeRegistry::lookup(const TypeAndName* tn, size_t n)
{
	const uint hash = hashfields(tn, n);

	return 0;
}
