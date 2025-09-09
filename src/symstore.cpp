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

void Symstore::dealloc(GC& gc)
{
    frames.clear();
    allsyms.clear();
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


Symstore::Lookup Symstore::lookup(unsigned strid, const Lexer::Token& tok, SymbolRefContext referencedHow, bool createExternal)
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
                if(!s->firstuse.line)
                    s->firstuse = tok;
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
    referencedHow = SymbolRefContext(referencedHow | SYMREF_EXTERNAL);

    // Record as missing
    Sym *s = findinframe(missing.data(), missing.size(), strid);
    if(s)
        s->referencedHow |= referencedHow;
    else
    {
        if(!createExternal)
            return res;
        s = newsym();
        s->nameStrId = strid;
        s->tok = tok;
        s->firstuse = tok;
        s->referencedHow = referencedHow;
        s->usage = usage;
        missing.push_back(getuid(s));
    }
    res.symindex = -1 - (int)indexinframe(missing.data(), missing.size(), s);
    res.sym = s;
    return res;
}

Symstore::Decl Symstore::decl(unsigned strid, const Lexer::Token& tok, SymbolRefContext referencedHow)
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
    s->tok = tok;
    s->nameStrId = strid;
    s->firstuse.line = 0;
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

const Symstore::Sym *Symstore::getsym(unsigned uid) const
{
    return &allsyms[uid];
}

unsigned Symstore::getuid(const Sym *sym)
{
    size_t pos = sym - allsyms.data();
    assert(pos < allsyms.size());
    return (unsigned)pos;
}

void Symstore::Sym::makeUsable()
{
    referencedHow = (SymbolRefContext)(referencedHow & ~SYMREF_NOTAVAIL);
}

void Symstore::Sym::makeMutable()
{
    referencedHow = (SymbolRefContext)(referencedHow | SYMREF_MUTABLE);
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
