#ifndef ANA_AST_H
#define ANA_AST_H

#include <stdint.h>
#include <stddef.h>

namespace ana {
namespace frontend {

enum class DataType {
    VOID,
    I32,
    I64,
    PTR,
    F32
};

enum class Opcode {
    ADD_I32,
    SUB_I32,
    MUL_I32,
    DIV_I32,
    NEG_I32,
    ADD_I64,
    SUB_I64,
    MUL_I64,
    DIV_I64,
    NEG_I64,
    MOVE,
    MOVE_CONST,
    SYS_CALL,
    LOAD_MEM,
    STORE_MEM,
    BIND_VTABLE,
    CALL_VIRT,
    CALL_VIRT_FAST,
    RETURN_VOID,
    RETURN_VAL,
    GOTO,
    IF_EQ,
    IF_NE,
    IF_LT,
    IF_GE,
    IF_Z,
    IF_NZ,
    AND_I32,
    AND_I64,
    OR_I32,
    OR_I64,
    XOR_I32,
    XOR_I64,
    SHL_I32,
    SHL_I64,
    SHR_I32,
    SHR_I64,
    USHR_I32,
    USHR_I64,
    BTS_I64,
    BTR_I64,
    POPCOUNT_I64,
    LZCNT_I64,
    ATOMIC_CAS_I64,
    ATOMIC_XCHG_I64,
    ATOMIC_ADD_I64,
    ATOMIC_AND_I64,
    ATOMIC_OR_I64,
    FENCE,
    NEW_INSTANCE,
    ADD_FLOAT_32,
    ADD_FLOAT_64,
    SUB_FLOAT_64,
    MUL_FLOAT_64,
    DIV_FLOAT_64,
    ADD_VECTOR_I32X4,
    SUB_VECTOR_I32X4,
    ADD_VECTOR_I32X8,
    ADD_VECTOR_I32X16,
    MUL_VECTOR_I32X8,
    LOAD_VECTOR_256,
    LOAD_VECTOR_512,
    SINK_MEM,
    CONST_STRING,
    STR_LEN,
    CALL_EXTERN,
    LOAD_FN_PTR
};

enum class RegisterType {
    NONE,
    PARAM, // p0..pN
    LOCAL  // v0..vN
};

struct Register {
    RegisterType type;
    uint32_t index;

    bool operator==(const Register& other) const {
        return type == other.type && index == other.index;
    }
};

enum class OperandKind {
    NONE,
    REGISTER,
    CONST_INT,
    MEM_OFFSET
};

struct MemOffset {
    Register base;
    int32_t offset;
};

struct Operand {
    OperandKind kind;
    union {
        Register reg;
        int64_t  const_val;
        MemOffset mem;
    };

    static Operand make_reg(RegisterType t, uint32_t idx) {
        Operand op;
        op.kind = OperandKind::REGISTER;
        op.reg.type = t;
        op.reg.index = idx;
        return op;
    }

    static Operand make_const(int64_t val) {
        Operand op;
        op.kind = OperandKind::CONST_INT;
        op.const_val = val;
        return op;
    }

    static Operand make_mem(RegisterType t, uint32_t idx, int32_t off) {
        Operand op;
        op.kind = OperandKind::MEM_OFFSET;
        op.mem.base.type = t;
        op.mem.base.index = idx;
        op.mem.offset = off;
        return op;
    }
};

struct Instruction {
    Opcode op;
    Operand dest;
    Operand src1;
    Operand src2;
    int32_t vtable_slot;
    const char* target_label;
    const char* string_val;
    size_t string_len;
    uint64_t string_hash;
    bool is_likely;
    Instruction* next;
};

struct BasicBlock {
    const char* label;
    Instruction* first_insn;
    Instruction* last_insn;
    BasicBlock* next;
};

struct ClassField {
    const char* name;
    DataType type;
    uint32_t offset;
    ClassField* next;
};

struct ClassDecl {
    const char* name;
    ClassField* fields;
    uint32_t size;
    void** vtable_array; // 64-byte aligned vtable array
    uint32_t vtable_size;
    ClassDecl* next;
};

struct Parameter {
    const char* name;
    DataType type;
    Register reg;
    Parameter* next;
};

struct Function {
    const char* name;
    DataType return_type;
    Parameter* params;
    uint32_t param_count;
    uint32_t local_count;
    BasicBlock* first_block;
    Function* next;
};

struct Program {
    ClassDecl* classes;
    Function* functions;
};

} // namespace frontend
} // namespace ana

#endif // ANA_AST_H
