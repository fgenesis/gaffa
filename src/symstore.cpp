#include "symstore.h"

Symstore::Symstore()
    : _indexbase(1)
{
}

void Symstore::push(ScopeType boundary)
{
    if(!frames.empty())
        _indexbase += frames.back().symids.size();
    Frame f;
    f.boundary = boundary;
    frames.push_back(f);
}

Symstore::~Symstore()
{
    assert(frames.empty());
}

void Symstore::pop(Frame& f)
{
    f = frames.back();
    frames.pop_back();
    const size_t fsz = f.symids.size();
    _indexbase -= fsz;

    if(f.boundary != SCOPE_FUNCTION)
    {
        // Return locals from this scope to enclosing function
        Frame& ff = funcframe();
        for(size_t i = 0; i < fsz; ++i)
            ff.localids.putback(getsym(f.symids[i])->localslot);
    }
}

Symstore::Frame& Symstore::funcframe()
{
    for(size_t i = frames.size(); i --> 0; )
    {
        if(frames[i].boundary == SCOPE_FUNCTION)
            return frames[i];
    }
    assert(false);
    return frames[0];
}

const Symstore::Frame& Symstore::peek() const
{
    return frames.back();
}

Symstore::Sym *Symstore::findinframe(const unsigned *uids, size_t n, sref strid)
{
    for(size_t i = 0; i < n; ++i)
    {
        Symstore::Sym *s = getsym(uids[i]);
        if(s->nameStrId == strid)
            return s;
    }
    return NULL;
}

unsigned Symstore::indexinframe(const unsigned *uids, size_t n, const Sym *sym)
{
    const unsigned uid = getuid(sym);
    for(size_t i = 0; i < n; ++i)
        if(uids[i] == uid)
            return i;
    assert(false);
    return -1;
}


Symstore::Lookup Symstore::lookup(unsigned strid, unsigned line, SymbolRefContext referencedHow)
{
    unsigned usage = SYMUSE_USED;

    Lookup res = { NULL, SCOPEREF_LOCAL, 0 };
    for(size_t k = frames.size(); k --> 0; )
    {
        Frame& f = frames[k];
        if(Sym *s = findinframe(f.symids.data(), f.symids.size(), strid))
        {
            if(!(s->referencedHow & SYMREF_NOTAVAIL)) // Intentionally skip symbols flagged as such
            {
                s->referencedHow |= referencedHow;
                s->usage |= usage;
                if(!s->lineused)
                    s->lineused = line;
                res.sym = s;
                res.symindex = _indexbase + indexinframe(f.symids.data(), f.symids.size(), s);
                return res;
            }
        }

        // symbol not found in this scope
        if(f.boundary == SCOPE_FUNCTION && res.where == SCOPEREF_LOCAL)
        {
            res.where = SCOPEREF_UPVAL;
            usage |= SYMUSE_UPVAL;
        }
    }
    // Symbol not found, must be supplied as external (global) symbol
    res.where = SCOPEREF_EXTERNAL;
    usage = SYMUSE_USED;

    // Record as missing
    Sym *s = findinframe(missing.data(), missing.size(), strid);
    if(s)
        s->referencedHow |= referencedHow;
    else
    {
        s = newsym();
        s->nameStrId = strid;
        s->linedefined = 0;
        s->lineused = line;
        s->referencedHow = referencedHow;
        s->usage = usage;
        missing.push_back(getuid(s));
    }
    res.symindex = -1 - (int)indexinframe(missing.data(), missing.size(), s);
    res.sym = s;
    return res;
}

Symstore::Decl Symstore::decl(unsigned strid, unsigned line, SymbolRefContext referencedHow)
{
    Frame *ff;
    Decl res = {};
    for(size_t k = frames.size(); k --> 0; )
    {
        ff = &frames[k];
        if(Sym *s = findinframe(ff->symids.data(), ff->symids.size(), strid))
        {
            res.clashing = s;
            return res;
        }

        if(ff->boundary == SCOPE_FUNCTION)
            break;
    }

    Sym *s = newsym();
    s->linedefined = line;
    s->nameStrId = strid;
    s->lineused = 0;
    s->referencedHow = referencedHow;
    s->usage = SYMUSE_UNUSED;
    s->localslot = ff->localids.pick();
    frames.back().symids.push_back(getuid(s));
    res.sym = s;

    return res;
}

Symstore::Sym *Symstore::newsym()
{
    allsyms.push_back(Sym());
    return &allsyms.back();
}

Symstore::Sym *Symstore::getsym(unsigned uid)
{
    return &allsyms[uid];
}

unsigned Symstore::getuid(const Sym *sym)
{
    size_t pos = sym - allsyms.data();
    assert(pos < allsyms.size());
    return (unsigned)pos;
}

Symstore::Sym& Symstore::makeUsable(unsigned uid)
{
    Sym *sym = getsym(uid);
    assert(sym);
    assert(sym->referencedHow & SYMREF_NOTAVAIL);
    sym->referencedHow = (SymbolRefContext)(sym->referencedHow & ~SYMREF_NOTAVAIL);
    return *sym;
}

Symstore::Sym& Symstore::makeMutable(unsigned uid)
{
    Sym *sym = getsym(uid);
    assert(sym);
    assert(sym->referencedHow & SYMREF_NOTAVAIL);
    sym->referencedHow = (SymbolRefContext)(sym->referencedHow | SYMREF_MUTABLE);
    return *sym;
}

u32 SlotDistrib::pick()
{
    u32 slot;
    if(!avail.empty())
    {
        slot = avail.back();
        avail.pop_back();
    }
    else
    {
        slot = _used;
        _used = slot + 1;
    }
    return slot;
}

void SlotDistrib::putback(u32 slot)
{
    assert(slot < _used);
    avail.push_back(slot);
}

const char* Symstore::Lookup::namewhere() const
{
    switch(where)
    {
        case SCOPEREF_LOCAL:    return "local";
        case SCOPEREF_UPVAL:    return "upvalue";
        case SCOPEREF_EXTERNAL: return "extern";
    }

    assert(false);
    return NULL;
}
