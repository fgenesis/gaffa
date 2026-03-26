#pragma once

#ifdef __GNUC__
#include <features.h>
#undef __USE_MISC
#endif

#ifdef __USE_MISC
#error This will probably not build. Some old, non-standard POSIX shit that gcc likes to include for some reason.
#endif

#ifdef _MSC_VER
#pragma warning(disable: 26812) // unscoped enum (we're C++03-compatible on purpose!)
#endif
