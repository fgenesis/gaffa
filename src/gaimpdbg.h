#pragma once

#include "gainternal.h"

class StringPool;
struct HLNode;
struct MLNodeBase;

void hlirDebugDump(const StringPool& p, const HLNode *root);

void mlirDebugDump(const MLNodeBase *root);
