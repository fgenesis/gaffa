#pragma once

// Convention: dst, src
// All IDs are local and stored in their respective tables
// Negative IDs refer to the import table
// Tables:
// import table
// constant table
// locals table
// upvalues table
// 
enum MLCmd
{
    ML_NOP,
    ML_UNOP,      // op, localid
    ML_BINOP,     // op, localid, localid
    ML_DECL_NS,   // namespace, name, localid
    ML_GETSYM,    // localid, symid
    ML_GETUPVAL,  // localid, upvalueid
    ML_SETLOCAL,  // localid, localid
    ML_SETUPVAL,  // upvalueid, localid
    ML_CLOSEUPVAL,// upvalueid
    ML_JUMP,      // reljump
    ML_JC,        // opid, localid, localid, reljump
    ML_ITER,      // TODO
    ML_ITERPACK,  // localid, numiters
    ML_FOR,       // numiters
    ML_FOREND,    // numiters
    ML_FNCALL,    // localid, localid(begin), n
    ML_MTHCALL,   // localid, name, localid(begin), n
    ML_FUNC,      // localid
    ML_NEW_ARRAY, // localid, typeid, size
    ML_NEW_TABLE, // localid, typeid, size
    ML_LOADK,     // localid, constantid
    ML_SETINDEXK, // localid, constantid, localid
    ML_GETINDEXK, // localid, localid, constantid
    ML_RETURN,    // localid(begin), n
    ML_YIELD,     // localid(begin), n
    ML_EMIT,      // localid(begin), n

    ML_END,
};

struct MLNode
{

};

