#include "table.h"

Table::Table()
{
}

Table::~Table()
{
}

Table* Table::New(GaAlloc& ga)
{
	void *mem = ga.alloc(ga.ud, NULL, NULL, sizeof(Table));
	return mem ? new (mem) Table() : NULL;
}

void Table::destroy(GaAlloc& ga)
{
	this->~Table();
	ga.alloc(ga.ud, this, sizeof(*this), 0);
}

ValU Table::get(const ValU& k) const
{
	for(size_t i = 0; i < _pairs.size(); ++i)
	{
		if(_pairs[i].k == k)
			return _pairs[i].v;
	}
	return Val(_Nil());
}

void Table::set(const ValU& k, const ValU& v)
{
	for(size_t i = 0; i < _pairs.size(); ++i)
	{
		if(_pairs[i].k == k)
		{
			_pairs[i].v = v;
			return;
		}
	}
	KV kv = {k, v};
	_pairs.push_back(kv);
}
