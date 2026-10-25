#include "valconv.h"
#include <math.h>

bool isNumericType(Type t)
{
    return t == PRIMTYPE_UINT || t == PRIMTYPE_SINT || t == PRIMTYPE_FLOAT;
}

// The checks go through widereal so they work for real = float or double
bool realToUint(uint *dst, real f)
{
    const widereal d = f;
    if(!(d >= 0 && d < 18446744073709551616.0) || d != trunc(d))
        return false;
    *dst = (uint)d;
    return true;
}

bool realToSint(sint *dst, real f)
{
    const widereal d = f;
    if(!(d >= -9223372036854775808.0 && d < 9223372036854775808.0) || d != trunc(d))
        return false;
    *dst = (sint)d;
    return true;
}

bool convertImplicit(Val *dst, const Val& src, Type t)
{
    if(src.type == t)
    {
        *dst = src;
        return true;
    }

    switch(src.type)
    {
        case PRIMTYPE_UINT:
        {
            const uint u = src.u.ui;
            if(t == PRIMTYPE_SINT && u <= (uint)INT64_MAX)
            {
                *dst = Val((sint)u);
                return true;
            }
            uint back;
            if(t == PRIMTYPE_FLOAT && realToUint(&back, (real)u) && back == u)
            {
                *dst = Val((real)u);
                return true;
            }
        }
        break;

        case PRIMTYPE_SINT:
        {
            const sint s = src.u.si;
            if(t == PRIMTYPE_UINT && s >= 0)
            {
                *dst = Val((uint)s);
                return true;
            }
            sint back;
            if(t == PRIMTYPE_FLOAT && realToSint(&back, (real)s) && back == s)
            {
                *dst = Val((real)s);
                return true;
            }
        }
        break;

        case PRIMTYPE_FLOAT:
        {
            uint u;
            sint s;
            if(t == PRIMTYPE_UINT && realToUint(&u, src.u.f))
            {
                *dst = Val(u);
                return true;
            }
            if(t == PRIMTYPE_SINT && realToSint(&s, src.u.f))
            {
                *dst = Val(s);
                return true;
            }
        }
        break;

        default: ;
    }
    return false;
}
