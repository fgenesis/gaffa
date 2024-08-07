#pragma once

#include "defs.h"

#include <string>
#include <vector>

class StringPool
{
public:
    StringPool();
    Str put(const char *s);
    Str put(const char *s, size_t n);
    Str put(const std::string& s);
    Str get(const char *s) const;
    Str get(const char *s, size_t n) const;
    Str get(const std::string& s) const;
    const std::string& lookup(size_t id) const;
    Str importFrom(const StringPool& other, size_t idInOther);

private:
    std::vector<std::string> _pool;
};
