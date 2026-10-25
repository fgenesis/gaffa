#pragma once

#include "defs.h"

bool isNumericType(Type t); // uint, int, float

// float -> integer without data loss: finite, no fraction, in range
bool realToUint(uint *dst, real f);
bool realToSint(sint *dst, real f);

// Implicit conversion of src to type t, only if no data is lost and the result fits t:
// int <-> uint unless the sign flips, int/uint -> float if exactly representable,
// float -> int/uint without a fractional part. Strings and everything else don't convert.
// dst is only written on success.
bool convertImplicit(Val *dst, const Val& src, Type t);
