#pragma once

class MLIR;
struct Runtime;

enum MLDumpFlags
{
    MLDUMP_DEFAULT = 0,
    MLDUMP_TYPES   = 0x01, // expression and var types; only valid after folding
    MLDUMP_TABLES  = 0x02, // vars, externals and functions after the tree
};

// Print the MLIR node tree, one node per line, children indented
void mlirDebugDump(const MLIR& ml, const Runtime& rt, unsigned flags);
