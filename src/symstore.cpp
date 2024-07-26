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


Symstore::Lookup Symstore::lookup(unsigned strid, unsigned line, unsigned usage)
{
    Lookup res = { NULL, SCOPEREF_LOCAL, 0 };
    for(size_t k = frames.size(); k --> 0; )
    {
        Frame& f = frames[k];
        if(Sym *s = findinframe(f.syms, strid))
        {
            s->usagemask |= usage;
            if(!s->lineused)
                s->lineused = line;
            res.sym = s;
            res.symindex = _indexbase + indexinframe(f.syms, s);
            return res;
        }

        // symbol not found in this scope
        if(f.boundary == SCOPE_FUNCTION && res.where == SCOPEREF_LOCAL)
            res.where = SCOPEREF_UPVAL;
    }
    // Symbol not found, must be supplied as external (global) symbol
    res.where = SCOPEREF_EXTERNAL;

    // Record as missing
    Sym *s = findinframe(missing, strid);
    if(s)
        s->usagemask |= usage;
    else
    {
        Sym add = { strid, 0, line, usage };
        missing.push_back(add);
        s = &missing.back();
    }
    res.symindex = -1 - (int)indexinframe(missing, s);
    res.sym = s;
    return res;
}

const Symstore::Sym *Symstore::decl(unsigned strid, unsigned line, unsigned usage)
{
    for(size_t k = frames.size(); k --> 0; )
    {
        Frame& f = frames[k];
        if(Sym *s = findinframe(f.syms, strid))
            return s;

        if(f.boundary >= SCOPE_VALBLOCK)
            break;
    }

    Sym s;
    s.linedefined = line;
    s.nameStrId = strid;
    s.lineused = 0;
    s.usagemask = usage;
    frames.back().syms.push_back(s);

    return NULL;
}

