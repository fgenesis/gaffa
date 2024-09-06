#pragma once

#include "typing.h"
#include "array.h"

struct TKey
{

};

class Table
{
public:
	Table *GCNew(GC& gc, Type kt, Type vt);
	void dealloc(GC& gc);
	void clear();
	ValU get(const ValU& k) const;
	void set(const ValU& k, const ValU& v);

private:
	Table(Type valtype);
	ValU *_resizekeys(GC& gc, size_t n);
	size_t _getidx(ValU v) const;

	ValU *keys;
	size_t *backrefs;
	size_t idxmask; // capacity = idxmask + 1
	Array vals;
};

/*
Preconditioned hashmap
Array part
[ a a a a b b b b b b...]
          ^- This is index 0 for the hashmap
  ^- This is index 0 for known object member lookup

-> Looking up fields for known keys gives negative indices (dynamic lookup)
   (Those are defined in the type and read during compilation or for dynamic lookup)
-> Can add fields at runtime but that will be slower (they go into the hashmap)
-> Hash key part does not need to be present in fully static operation


*/
