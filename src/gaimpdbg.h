#pragma once

#include "gainternal.h"

class StringPool;
struct Runtime;
struct HLNode;

void hlirDebugDump(const Runtime& rt, const HLNode *root);
