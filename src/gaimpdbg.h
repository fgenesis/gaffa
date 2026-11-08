#pragma once

#include "gainternal.h"
#include <string>

class StringPool;
struct Runtime;
struct HLNode;

void hlirDebugDump(const Runtime& rt, const HLNode *root);

// Type name in script syntax, eg. "Const(Array(int))?"
std::string getTypeName(const Runtime& rt, Type t);
