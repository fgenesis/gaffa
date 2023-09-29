#pragma once

#include <string>
#include <vector>

class StringPool
{
public:
    unsigned put(const char *s);
    unsigned put(const char *s, size_t n);
    unsigned put(const std::string& s);
    unsigned get(const char *s) const;
    unsigned get(const char *s, size_t n) const;
    unsigned get(const std::string& s) const;

private:
    std::vector<std::string> _pool;
};
