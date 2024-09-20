#include "strings.h"
#include <string.h>


static const Str None = {0, 0};


StringPool::StringPool(GC& gc)
    : Dedup(gc, true)
{
}

FORCEINLINE static Str mkstr(sref ref, size_t len)
{
    Str s;
    s.len = len;
    s.id = ref;
    return s;
}


Str StringPool::put(const char* s)
{
    if(!s)
        return None;
    size_t len = strlen(s);
    return mkstr(Dedup::putCopy(s, len), len);
}

Str StringPool::put(const char* s, size_t n)
{
    return mkstr(Dedup::putCopy(s, n), n);
}

Str StringPool::put(const std::string& s)
{
    return put(s.c_str(), s.size());
}

Str StringPool::get(const char* s) const
{
    return s ? get(s, strlen(s)) : None;
}

Str StringPool::get(const char* s, size_t n) const
{
    sref ref = Dedup::find(s, n);
    return ref ? mkstr(ref, n) : None;
}

Str StringPool::get(const std::string& s) const
{
    return get(s.c_str(), s.size());
}

Strp StringPool::lookup(size_t id) const
{
    const MemBlock mb = Dedup::get(id);
    const Strp sp = { mb.p, mb.n };
    return sp;
}

Str StringPool::importFrom(const StringPool& other, size_t idInOther)
{
    // FIXME: make a function in Dedup to do this quickly
    const Strp s = other.lookup(idInOther);
    return put(s.s, s.len);
}
