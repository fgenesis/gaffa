#include "symstore.h"

Symstore::Symstore()
    : _indexbase(1)
{
}

void Symstore::push(ScopeType boundary)
{
    if(!frames.empty())
        _indexbase += frames.back().syms.size();
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
    _indexbase -= f.syms.size();

    if(f.boundary != SCOPE_FUNCTION)
    {
        // Return locals from this scope to enclosing function
        Frame& ff = funcframe();
        for(size_t i = 0; i < f.syms.size(); ++i)
            ff.localids.putback(f.syms[i].localslot);
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

static Symstore::Sym *findinframe(std::vector<Symstore::Sym>& syms, unsigned strid)
{
    const size_t N = syms.size();
    for(size_t i = 0; i < N; ++i)
        if(syms[i].nameStrId == strid)
            return &syms[i];
    return NULL;
}

static unsigned indexinframe(std::vector<Symstore::Sym>& syms, const Symstore::Sym *sym)
{
    return sym - &syms[0];
}


Symstore::Lookup Symstore::lookup(unsigned strid, unsigned line, MLSymbolRefContext referencedHow)
{
    unsigned usage = SYMUSE_USED;

    Lookup res = { NULL, SCOPEREF_LOCAL, 0 };
    for(size_t k = frames.size(); k --> 0; )
    {
        Frame& f = frames[k];
        if(Sym *s = findinframe(f.syms, strid))
        {
            if(!(s->referencedHow & SYMREF_NOTAVAIL)) // Intentionally skip symbols flagged as such
            {
                s->referencedHow |= referencedHow;
                s->usage |= usage;
                if(!s->lineused)
                    s->lineused = line;
                res.sym = s;
                res.symindex = _indexbase + indexinframe(f.syms, s);
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
    Sym *s = findinframe(missing, strid);
    if(s)
        s->referencedHow |= referencedHow;
    else
    {
        Sym add = { strid, 0, line, referencedHow, usage };
        missing.push_back(add);
        s = &missing.back();
    }
    res.symindex = -1 - (int)indexinframe(missing, s);
    res.sym = s;
    return res;
}

Symstore::Decl Symstore::decl(unsigned strid, unsigned line, MLSymbolRefContext referencedHow)
{
    Frame *ff;
    Decl res = {};
    for(size_t k = frames.size(); k --> 0; )
    {
        ff = &frames[k];
        if(Sym *s = findinframe(ff->syms, strid))
        {
            res.clashing = s;
            return res;
        }

        if(ff->boundary == SCOPE_FUNCTION)
            break;
    }

    Sym s;
    s.linedefined = line;
    s.nameStrId = strid;
    s.lineused = 0;
    s.referencedHow = referencedHow;
    s.usage = SYMUSE_UNUSED;
    s.localslot = ff->localids.pick();
    frames.back().syms.push_back(s);
    res.sym = &frames.back().syms.back();

    return res;
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
