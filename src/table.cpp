#include "table.h"
#include "hashfunc.h"


static const ValU dead = { {PRIMTYPE_NIL}, 1 };

static inline bool isdead(ValU k)
{
	return k.type.id == PRIMTYPE_NIL && k.u.ui;
}




Table::Table(Type valtype)
	: vals(valtype), keys(NULL), backrefs(NULL), idxmask(0)
{
}

ValU* Table::_resizekeys(GC& gc, size_t n)
{
	// TODO: check that n is power of 2
	const size_t kcap = idxmask + 1;
	ValU *newk = (ValU*)gc_alloc_unmanaged(gc, keys, kcap * sizeof(ValU), n * sizeof(ValU));
	size_t *newbk = (size_t*)gc_alloc_unmanaged(gc, backrefs, kcap * sizeof(size_t), n * sizeof(size_t));

	if(!(newk && newbk))
	{
		gc_alloc_unmanaged(gc, newk,  kcap * sizeof(ValU), 0);
		gc_alloc_unmanaged(gc, newbk, kcap * sizeof(size_t), 0);
		return NULL;
	}

	idxmask = n - 1; // ok if this underflows
	keys = newk;
	backrefs = newbk;
	return newk;
}

size_t Table::_getidx(ValU v) const
{
	return size_t();
}

Table* Table::GCNew(GC& gc, Type kt, Type vt)
{
    void *pa = gc_new(gc, sizeof(Table));
    if(!pa)
        return NULL;

    return GA_PLACEMENT_NEW(pa) Table(vt);
}

void Table::dealloc(GC& gc)
{
	vals.dealloc(gc);

}

void Table::clear()
{
	vals.clear();
	// TODO: fast way to clear keys
}

ValU Table::get(const ValU& k) const
{
	return Val(_Nil());
}

void Table::set(const ValU& k, const ValU& v)
{

}
