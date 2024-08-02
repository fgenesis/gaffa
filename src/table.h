#pragma once

#include "typing.h"

#include <vector>

class Table
{
public:
	Table *New(GaAlloc& ga);
	void destroy(GaAlloc& ga);
	ValU get(const ValU& k) const;
	void set(const ValU& k, const ValU& v);


	// FIXME: This is the shittiest and most stupid implementation just to get this thing off the ground quickly.
	// Make this a proper data structure that doesn't suck!
	struct KV
	{
		ValU k, v;
	};
	std::vector<KV> _pairs;

private:
	Table();
	~Table();
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
