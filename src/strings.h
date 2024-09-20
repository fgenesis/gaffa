#pragma once

#include "defs.h"
#include "dedupset.h"

#include <string>



class StringPool : private Dedup
{
public:
    StringPool(GC& gc);
    bool init() { return Dedup::init(); }
    void dealloc() { Dedup::dealloc(); }
    Str put(const char *s);
    Str put(const char *s, size_t n);
    Str put(const std::string& s);
    Str get(const char *s) const;
    Str get(const char *s, size_t n) const;
    Str get(const std::string& s) const;
    Strp lookup(size_t id) const;
    Str importFrom(const StringPool& other, size_t idInOther);

    FORCEINLINE void mark(sref ref) { Dedup::mark(ref); }

};

