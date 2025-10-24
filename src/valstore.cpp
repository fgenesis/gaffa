#include "valstore.h"
#include "serialio.h"
#include "strings.h"
#include <assert.h>

enum
{
    INITIAL_VALS = 3
};

ValStore::ValStore(GC& gc)
    : _gc(gc)
{
    vals.push_back(gc, Val());
    vals.push_back(gc, Val(false));
    vals.push_back(gc, Val(true));
    assert(vals.size() == INITIAL_VALS);
}

ValStore::~ValStore()
{
    vals.dealloc(_gc);
}

u32 ValStore::put(ValU v)
{
    const size_t N = vals.size();
    for(size_t i = 0; i < N; ++i) // FIXME: Make this smarter than a dumb linear search
    {
        if(vals[i] == v)
            return i;
    }

    ValU *p = vals.push_back(_gc, v);
    assert(p); // TODO: handle OOM
    return N;
}

static const byte s_typeLUT[] =
{
    PRIMTYPE_UINT,
    PRIMTYPE_SINT,
    PRIMTYPE_FLOAT
    // anthing with a higher id is a string
};


void ValStore::serialize(BufSink* sk, const StringPool& sp) const
{
    byte buf[16];
    const size_t N = vals.size();
    u32 n = vu128enc(&buf[1], N);
    sk->Write(sk, &buf, n);

    Strp s;
    s.len = 0;
    for(size_t i = INITIAL_VALS; i < N; ++i)
    {
        const ValU& v = vals[i];

        switch(v.type)
        {
            case PRIMTYPE_UINT:
            case PRIMTYPE_SINT:
                buf[0] = v.type - PRIMTYPE_UINT;
                n = 1 + vu128enc(&buf[1], zigzagenc(v.u.ui));
                break;
            case PRIMTYPE_FLOAT:
                buf[0] = 4;
                n = 1 + vu128enc(&buf[1], zigzagenc(v.u.f_as_u));
                break;
            case PRIMTYPE_STRING:
                s = sp.lookup(v.u.str);
                n = vu128enc(&buf[0], Countof(s_typeLUT) + s.len);
                break;

            default:
                assert(false);
        }

        sk->Write(sk, &buf, 1);
        if(s.len)
        {
            sk->Write(sk, s.s, s.len);
            s.len = 0;
        }

    }
}
