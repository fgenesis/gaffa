#pragma once

class SymTable;
struct GC;
class StringPool;
class TypeRegistry;

// What registering operators needs. Operators go into the namespace of their first parameter's type.
struct OpReg
{
    SymTable& syms;
    GC& gc;
    StringPool& sp;
    TypeRegistry& tr;
};

void reg_uint_ops(OpReg& r);
void reg_sint_ops(OpReg& r);
void reg_float_ops(OpReg& r);
void reg_bool_ops(OpReg& r);
void reg_string_ops(OpReg& r);
