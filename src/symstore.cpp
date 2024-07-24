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
}

static Symstore::Sym *findinframe(std::vector<Symstore::Sym>& syms, unsigned strid)
{
    const size_t N = syms.size();
    for(size_t i = 0; i < N; ++i)
        if(syms[i].nameStrId == strid)
            return &syms[i];
    return NULL;
}


Symstore::Lookup Symstore::lookup(unsigned strid, unsigned line, unsigned usage)
{
    Lookup res = { NULL, SCOPEREF_LOCAL };
    for(size_t k = frames.size(); k --> 0; )
    {
        Frame& f = frames[k];
        if(Sym *s = findinframe(f.syms, strid))
        {
            s->usagemask |= usage;
            if(!s->lineused)
                s->lineused = line;
            res.sym = s;
            return res;
        }

        // symbol not found in this scope
        if(f.boundary == SCOPE_FUNCTION && res.where == SCOPEREF_LOCAL)
            res.where = SCOPEREF_UPVAL;
    }
    // Symbol not found, must be supplied as external (globaL) symbol
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
    s.usagemask = 0;
    frames.back().syms.push_back(s);

    return NULL;
}

