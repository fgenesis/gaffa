#include "table.h"
#include "hashfunc.h"
#include "util.h"

enum { KEY_ALIGNMENT = sizeof(((ValU*)NULL)->u) };

// There's no ValU in here due to possible alignment and padding issues with that type.
// Can't seem to convince the compiler to place validx in the otherwise unused padding section...
struct TKey : public ValU
{
    _AnyValU u; // corresponds to ValU::u
    Type type;  // corresponds to ValU::type
    tsize validx1; // index of value + 1. invalid if 0.
};

static inline bool isdead(const TKey& k)
{
	return k.type.id == PRIMTYPE_NIL && k.u.ui;
}
static inline bool reallyempty(const TKey& k)
{
	return k.type.id == PRIMTYPE_NIL && !k.u.ui;
}
static inline bool isfree(ValU k)
{
	return k.type.id == PRIMTYPE_NIL;
}

static inline bool issame(const TKey& k, ValU v)
{
    return k.type.id == v.type.id && k.u.opaque == v.u.opaque;
}

static inline void maketombstone(TKey& k)
{
    k.type.id = PRIMTYPE_NIL;
    k.u.opaque = 1;
    k.validx1 = 0;
}

static inline void makempty(TKey& k)
{
    k.type.id = PRIMTYPE_NIL;
    k.u.opaque = 0;
    k.validx1 = 0;
}





Table::Table(Type valtype)
	: vals(valtype), keys(NULL), backrefs(NULL), idxmask(0)
{
}

TKey* Table::_resizekeys(GC& gc, size_t n)
{
	// TODO: check that n is power of 2
	const size_t kcap = idxmask + 1;
	TKey *newk = (ValU*)gc_alloc_unmanaged(gc, keys, kcap * sizeof(TKey), n * sizeof(TKey));
	tsize *newbk = (tsize*)gc_alloc_unmanaged(gc, backrefs, kcap * sizeof(tsize), n * sizeof(tsize));

	if(!(newk && newbk))
	{
        if(newk)
		    gc_alloc_unmanaged(gc, newk,  kcap * sizeof(ValU), 0);
        if(newbk)
		    gc_alloc_unmanaged(gc, newbk, kcap * sizeof(tsize), 0);
		return NULL;
	}

	idxmask = n - 1; // ok if this underflows
	keys = newk;
	backrefs = newbk;
	return newk;
}

// never returns NULL
TKey *Table::_getkey(ValU findkey) const
{
    TKey *tombstone = NULL;
    const tsize mask = idxmask;
	tsize hk = (tsize)hashvalue(HASH_SEED, findkey);
    for(;;)
    {
        const tsize kidx = hk++ & mask;
        TKey& k = keys[kidx];
        if(issame(k, findkey))
            return &k; // found matching key; end iteration
        if(k.type.id == PRIMTYPE_NIL) // empty or tombstone?
        {
            if(!k.u.opaque) // empty?
                return tombstone ? tombstone : &k; // empty entry ends the iteration, but prefer tombstone if we saw one
            else // tombstone. Remember and continue iterating.
                if(!tombstone)
                    tombstone = &k;
        }
    }
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

ValU Table::get(ValU k) const
{
	const TKey *tk = _getkey(k);
    const size_t idx = tk->validx1 - 1;
    return vals.dynamicLookup(idx);
}

void Table::set(ValU k, ValU v)
{
    TKey *tk = _getkey(k);
    const size_t idx = vals.sz;
    v
}
