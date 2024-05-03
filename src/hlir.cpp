#include "hlir.h"
#include <string.h>


HLIRBuilder::HLIRBuilder(const GaAlloc& ga)
	: galloc(ga), b(NULL)
{

}

HLIRBuilder::~HLIRBuilder()
{
	clear();
}

HLNode* HLIRBuilder::alloc(size_t sz, HLNode::Type ty)
{
	for(;;)
	{
		if(b)
		{
			HLNode *p = (HLNode*)b->alloc(sz);
			if(p)
			{
				p->type = ty;
				memset((char*)p + sizeof(p->type), 0, sz - sizeof(p->type));
				return p;
			}
		}
		Block *newb = allocBlock(b ? b->cap * 2 : 64 * sizeof(void*));
		if(newb)
		{
			newb->prev = b;
			b = newb;
		}
		else
			break;
	}
	return NULL;
}

void HLIRBuilder::clear()
{
	Block *b = this->b;
	while(b)
	{
		Block * const prev = b->prev;
		galloc.alloc(galloc.ud, b, b->cap, 0);
		b = prev;
	}
	this->b = NULL;
}

HLIRBuilder::Block* HLIRBuilder::allocBlock(size_t sz)
{
	sz += sizeof(Block);
	Block *newb = (Block*)galloc.alloc(galloc.ud, NULL, 0, sz);
	if(newb)
	{
		newb->prev = NULL;
		newb->used = sizeof(Block);
		newb->cap = sz;
	}
	return newb;
}

void* HLIRBuilder::Block::alloc(size_t sz)
{
	size_t avail = cap - used;
	if(avail < sz)
		return NULL;
	void *p = (char*)this + used;
	used += sz;
	return p;
}
