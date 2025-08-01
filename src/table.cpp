#include "table.h"
#include "hashfunc.h"
#include "util.h"
#include "gc.h"
#include <string.h>

/*
Implementation notes:
- Internally, keys are stored via open addressing, using linear probing and tombstones.
- Tombstones are cleaned up when erasing keys, if no probe sequences are broken.
- Keys and values are stored separately to maximize cache line occupancy.
- Hashes are not stored. Computing a hash from any key is trivially O(1) and not worth storing.
- Reading material / inspired by: https://craftinginterpreters.com/hash-tables.html
*/

enum { KEY_ALIGNMENT = sizeof(((ValU*)NULL)->u) };

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
    k.u.ui = 1;
}

static inline void makempty(TKey& k)
{
    k.type.id = PRIMTYPE_NIL;
    k.u.ui = 0;
}

#define KCHECK(idx) assert(backrefs[keys[idx].validx] == idx);

static bool isValidCap(tsize x)
{
    // Aka. must be 0, or power of 2. Falsely rejects 1 but this is not a problem because 1 never appears as capacity.
    return !((x - 1) & x);
}

Table::Table(Type keytype, Type valtype)
    : vals(valtype), keys(NULL), keytype(keytype), idxmask(-1), backrefs(NULL)
{
}

// never returns NULL
TKey *Table::_getkey(ValU findkey, tsize mask) const
{
    assert(keys);

    TKey *tombstone = NULL;
    tsize hk = (tsize)hashvalue(HASH_SEED, findkey);
    // This can't be an infinite loop since keys[] is resized when it gets too full,
    // ensuring there are always some empty slots.
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
    void *pa = gc_new(gc, sizeof(Table), PRIMTYPE_TABLE);
    if(!pa)
        return NULL;

    return GA_PLACEMENT_NEW(pa) Table(kt, vt);
}

void Table::dealloc(GC& gc)
{
    const tsize cap = idxmask + 1; // Possible overflowing back to zero is intended here
    if(cap)
    {
        vals.dealloc(gc);
        _resize(gc, 0);
    }

}

void Table::clear()
{
    const size_t N = vals.sz;
    if(N)
    {
        vals.clear();
        for(size_t i = 0; i < N; ++i)
        {
            const tsize idx = backrefs[i];
            KCHECK(idx);
            maketombstone(keys[idx]);
            _cleanupforward(idx);
        }
    }
}

// Starting from idx, go forward. If we only find tombstones along the way and then hit empty,
// clear backwards.
// Use case: We just made keys[idx] a tombstone -> clear trailing tombstones
// Ie.: (V = valid value, T = tombstone, E = empty)
// V E T V T T T E E V
//           ^ We just set this to a tombstone
// Then we check if we can hit empty and only pass tombstones, and if so, clear backwards:
//           ~~>|
// V E T V E E E E E V
//        |<~~~~|
void Table::_cleanupforward(tsize idx)
{
    const tsize mask = idxmask;
    for(;;)
    {
        ++idx;
        idx &= mask;

        TKey& k = keys[idx];
        if(k.type.id != PRIMTYPE_NIL)
            return; // definitely not a tombstone, get out

        if(!k.u.opaque) // empty?
            break; // time to go backwards
    }

    for(;;)
    {
        --idx;
        idx &= mask;

        TKey& k = keys[idx];
        if(!(k.type.id == PRIMTYPE_NIL && k.u.opaque))
            break; // not a tombstone, get out

        k.u.opaque = 0; // was a tombstone, clear it
    }
}

tsize Table::_resize(GC& gc, tsize newsize)
{
    assert(isValidCap(newsize));
    const tsize oldcap = idxmask + 1;
    TKey *newk = gc_alloc_unmanaged_T<TKey>(gc, keys, oldcap, newsize);
    tsize *newbk = gc_alloc_unmanaged_T<tsize>(gc, backrefs, oldcap, newsize);

    if(newsize && !(newk && newbk))
    {
        if(newk)
            gc_alloc_unmanaged_T<TKey>(gc, newk, oldcap, 0);
        if(newbk)
            gc_alloc_unmanaged_T<tsize>(gc, newbk, oldcap, 0);
        return 0;
    }

    if(newsize > oldcap)
    {
        // Clear new keys to empty
        memset(newk + oldcap, 0, sizeof(*newk) * (newsize - oldcap));
    }

    keys = newk;
    backrefs = newbk;
    idxmask = newsize - 1; // ok if this underflows

    if(oldcap && newsize)
        _rehash(oldcap, newsize - 1);

    return newsize;
}


void Table::_rehash(tsize oldsize, tsize newmask)
{
    for(tsize i = 0; i < oldsize; ++i)
    {
        const TKey kcopy = keys[i];
        makempty(keys[i]); // always clear; including tombstones
        if(kcopy.type.id == PRIMTYPE_NIL)
            continue;

        TKey *tk = _getkey(*(const ValU*)&kcopy, newmask); // HACK: dirty cast: is fine because the memory layout is the same
        const tsize keyoffs = tk - keys;
        *tk = kcopy;

        backrefs[kcopy.validx] = keyoffs;
    }
}

Val Table::get(Val k) const
{
    if(!keys)
        return _Nil();
    const TKey *tk = _getkey(k, idxmask);
    const size_t idx = tk->validx;
    return vals.dynamicLookup(idx);
}

// set new value, return old
Val Table::set(GC& gc, Val k, Val v)
{
    assert(keytype.id == PRIMTYPE_ANY || k.type.id == keytype.id);
    assert(vals.t.id == PRIMTYPE_ANY || v.type.id == vals.t.id);

    tsize newsize;
    TKey *tk;
    if(!keys) // When the table is fresh, keys are not allocated yet
    {
        newsize = 4; // Must be at least 4 to pass the load factor check below correctly
        goto doresize;
    }
    tk = _getkey(k, idxmask);
    if(tk->type.id != PRIMTYPE_NIL)
    {
        // The same key is already present -> just replace value
        return vals.dynamicSet(tk->validx, v); // returns old value
    }

    if(!tk->u.ui)
    {
        // Key is NOT a tombstone -> fresh key in use -> might have to enlarge keys
        if(vals.sz >= idxmask - (idxmask >> 2u)) // 75% load factor reached? enlarge
        {
            // Formula explanation:
            // Mask is known to be a power of 2 minus 1; or -1 (all FF) if the table is empty.
            // Before shift: xxxx11
            // After shift:  xxx110
            // Plus 1 makes it all-FF:
            //               xxx111
            // Another plus 1 and it's the next power of 2:
            //               xx1000
            newsize = (idxmask << 1u) + 2;
            assert(newsize); // TODO: newsize==0 here means the table can't hold more values
        doresize:
            newsize = _resize(gc, newsize); // keys got reallocated; must get new location
            assert(newsize); // TODO: handle OOM
            tk = _getkey(k, newsize - 1);
            // Key didn't exist, tombstones get cleared on resize, so the new key must be empty
            assert(tk->type.id == PRIMTYPE_NIL);
            assert(tk->u.ui == 0);
        }
    }

    tk->u = k.u;
    tk->type = k.type;
    tk->validx = vals.sz;

    backrefs[vals.sz] = tk - keys; // this is needed to quickly find a key that belongs to a value

    vals.dynamicAppend(gc, v); // TODO: handle OOM

    // Key didn't exist -> there's no prev. value
    return _Nil();
}

Val Table::pop(Val k)
{
    if(!vals.sz)
        return _Nil(); // table is empty

    TKey *tk = _getkey(k, idxmask);
    if(tk->type.id == PRIMTYPE_NIL) // tombstone or empty
        return _Nil(); // key is not in table

    // key exists, clear it
    maketombstone(*tk);
    const tsize keyoffs = tk - keys;
    _cleanupforward(keyoffs);

    // get wanted value out & move the last value in its place
    const tsize vidx = tk->validx;
    const ValU v = vals.removeAtAndMoveLast_Unsafe(vidx);

    // patch its key to point to the new location
    const tsize kidx = backrefs[vidx];
    keys[kidx].validx = vidx;
    backrefs[vidx] = keyoffs;

    KCHECK(kidx);

    // TODO: shrink if < 25% full

    return v;
}

Val Table::keyat(tsize idx) const
{
    KCHECK(idx);
    const TKey& tk = keys[backrefs[idx]];
    return Val(tk.u, tk.type);
}

KV Table::index(tsize idx) const
{
    KV e = { keyat(idx), vals.dynamicLookup(idx) };
    return e;
}

void Table::loadAll(const Table& o, GC& gc)
{
    // TODO: this could be optimized with a bunch of memcpy if this is empty

    const tsize n = o.size();
    for(tsize i = 0; i < n; ++i)
        set(gc, o.keyat(i), o.vals.dynamicLookup(i));
}
