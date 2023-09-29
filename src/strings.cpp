#include "strings.h"
#include <string.h>

unsigned StringPool::put(const char* s)
{
    if(!s)
        return 0;

    return put(s, strlen(s));
}

unsigned StringPool::put(const char* s, size_t n)
{
    if(!s)
        return 0;

    unsigned id = get(s, n);
    if(id)
        return id;

    _pool.push_back(std::string(s, s+n));
    return _pool.size();

}

unsigned StringPool::put(const std::string& s)
{
    return put(s.c_str(), s.size());
}

unsigned StringPool::get(const char* s) const
{
    return s ? get(s, strlen(s)) : 0;
}

unsigned StringPool::get(const char* s, size_t n) const
{
    if(!s)
        return 0;

    const size_t N = _pool.size();
    for(size_t i = 0; i < N; ++i)
    {
        const std::string& x = _pool[i];
        if(x.size() == n && !memcmp(s, x.c_str(), n))
            return i+1;
    }

    return 0;
}

unsigned StringPool::get(const std::string& s) const
{
    return get(s.c_str(), s.size());
}
