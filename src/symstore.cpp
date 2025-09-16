#include "symstore.h"

Symstore::Symstore()
{
}

void Symstore::push(ScopeType boundary)
{
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

    if(f.boundary != SCOPE_FUNCTION)
    {
        const size_t fsz = f.symids.size();
        // Return locals from this scope to enclosing function
        Frame& ff = funcframe();
        for(size_t i = 0; i < fsz; ++i)
        {
            int slot = getsym(f.symids[i])->slot;
            assert(slot >= 0);
            ff.localids.putback(slot);
        }
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


Symstore::Lookup Symstore::lookup(unsigned strid, const Lexer::Token& tok, SymbolRefContext referencedHow, bool createExternal)
{
    unsigned usage = SYMUSE_USED;

    Lookup res = { NULL, SCOPEREF_LOCAL };
    for(size_t k = frames.size(); k --> 0; )
    {
        Frame& f = frames[k];
        if(Sym *s = findinframe(f.symids.data(), f.symids.size(), strid))
        {
            if(s->referencedHow & SYMREF_VISIBLE) // Intentionally skip invisible symbols
            {
                s->referencedHow |= referencedHow;
                s->usage |= usage;
                if(!s->firstuse.line)
                    s->firstuse = tok;
                res.sym = s;
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
        s->slot = -(int)missing.size();
    }
    res.sym = s;
    return res;
}

Symstore::Decl Symstore::decl(unsigned strid, const Lexer::Token& tok, SymbolRefContext referencedHow)
{
    assert(frames.size());
    Frame *ff;
    Decl res = {};
    Sym *s;
    for(size_t k = frames.size(); k --> 0; )
    {
        ff = &frames[k];
        s = findinframe(ff->symids.data(), ff->symids.size(), strid);
        if(s)
        {
            if(s->referencedHow & SYMREF_DEFERRED)
            {
                s->referencedHow &= ~SYMREF_DEFERRED;
                goto redecl;
            }
            res.clashing = s;
            return res;
        }

        if(ff->boundary == SCOPE_FUNCTION)
            break;
    }

    s = newsym();
    s->tok = tok;
    s->nameStrId = strid;
    s->slot = ff->localids.pick();
    frames.back().symids.push_back(getuid(s));
redecl:
    // In case it's re-declared, don't lose the flags that may have accumulated until now
    s->referencedHow |= referencedHow;
    res.sym = s;
    return res;
}

Symstore::Sym *Symstore::newsym()
{
    allsyms.push_back(Sym());
    Sym *s = &allsyms.back();
    s->referencedHow = SYMREF_HIDDEN;
    s->firstuse.line = 0;
    s->usage = SYMUSE_UNUSED;
    s->forgetValue();
    return s;
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
    referencedHow = (SymbolRefContext)(referencedHow | SYMREF_VISIBLE);
}

void Symstore::Sym::makeMutable()
{
    referencedHow = (SymbolRefContext)(referencedHow | SYMREF_MUTABLE);
}

void Symstore::Sym::makeDeferred()
{
    referencedHow = (SymbolRefContext)(referencedHow | SYMREF_DEFERRED);
}

void Symstore::Sym::setValue(const Val& val)
{
    assert(!valtype() || valtype()->id == val.type.id);
    referencedHow = (SymbolRefContext)(referencedHow | SYMREF_KNOWN_VALUE);
    this->val = val;
}

void Symstore::Sym::forgetValue()
{
    referencedHow = (SymbolRefContext)(referencedHow & ~SYMREF_KNOWN_VALUE);
    this->val = _Xnil();
}

void Symstore::Sym::setType(Type t)
{
    assert(!value() || value()->type.id == t.id);
    referencedHow = (SymbolRefContext)(referencedHow | SYMREF_KNOWN_TYPE);
    this->val.type = t;
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
