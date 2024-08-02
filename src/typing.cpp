#include "typing.h"
#include "table.h"

TDesc *TDesc_New(const GaAlloc& ga, size_t numFieldsAndBits)
{
	size_t sz = sizeof(TDesc) + numFieldsAndBits * sizeof(Type) + numFieldsAndBits * sizeof(unsigned);
	TDesc *td = (TDesc*)ga.alloc(ga.ud, NULL, 0,
}

TypeRegistry::TypeRegistry(const GaAlloc& ga)
	: _ga(ga)
{
}

TypeRegistry::~TypeRegistry()
{
}

TDesc* TypeRegistry::construct(const Table& t)
{
	TDesc *
	for(size_t i = 0; i < t._pairs.size(); ++i)
}
