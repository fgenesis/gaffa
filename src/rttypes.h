#pragma once

#include "symtable.h"
#include "strings.h"
#include "typing.h"


void rtinit(SymTable& syms, GC& gc, StringPool& sp, TypeRegistry& tr);
