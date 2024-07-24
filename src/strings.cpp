#include "strings.h"
#include <string.h>

static const Str None = {0, 0};


StringPool::StringPool()
{
    _pool.push_back("");
}

Str StringPool::put(const char* s)
{
    if(!s)
        return None;

    return put(s, strlen(s));
}

Str StringPool::put(const char* s, size_t n)
{
    if(!s)
        return None;

    Str ret = get(s, n);
    if(!ret.id)
    {
        ret.id = _pool.size();
        _pool.push_back(std::string(s, s+n));
        ret.len = n;
    }
    return ret;

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
    if(!s)
        return None;

    const size_t N = _pool.size();
    for(size_t i = 1; i < N; ++i)
    {
        const std::string& x = _pool[i];
        if(x.size() == n && !memcmp(s, x.c_str(), n))
        {
            Str ret;
            ret.id = i;
            ret.len = n;
            return ret;
        }
    }

    return None;
}

Str StringPool::get(const std::string& s) const
{
    return get(s.c_str(), s.size());
}

const std::string& StringPool::lookup(size_t id) const
{
    return _pool[id];
}
