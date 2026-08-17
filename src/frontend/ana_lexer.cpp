#include "ana_lexer.h"
#include "../sys/sys_raw.h"

namespace ana {
namespace frontend {

// Bump pool for string literals. The previous version mapped a whole page per
// literal (never released) and used the mmap result without checking it, so an
// allocation failure wrote to address -1. Blocks here are never relocated, so
// pointers handed out earlier stay valid for the life of the process.
static uint8_t* g_str_pool = nullptr;
static size_t g_str_pool_cap = 0;
static size_t g_str_pool_used = 0;

static char* lexer_alloc_string(const char* bytes, size_t len) {
    size_t need = (len + 8) & ~static_cast<size_t>(7);
    if (!g_str_pool || g_str_pool_used + need > g_str_pool_cap) {
        size_t cap = need > 65536 ? ((need + 4095) & ~static_cast<size_t>(4095)) : 65536;
        void* mem = sys::raw_mmap(nullptr, cap, ANA_PROT_READ | ANA_PROT_WRITE,
                                  ANA_MAP_PRIVATE | ANA_MAP_ANONYMOUS, -1, 0);
        if (!mem || mem == reinterpret_cast<void*>(-1)) return nullptr;
        g_str_pool = static_cast<uint8_t*>(mem);
        g_str_pool_cap = cap;
        g_str_pool_used = 0;
    }
    char* buf = reinterpret_cast<char*>(g_str_pool + g_str_pool_used);
    g_str_pool_used += need;
    sys::freestanding_memcpy(buf, bytes, len);
    buf[len] = '\0';
    return buf;
}

Lexer::Lexer(const char* source) : source_(source), cursor_(0), line_(1) {}

char Lexer::peek() const {
    return source_[cursor_];
}

char Lexer::advance() {
    char c = source_[cursor_];
    if (c != '\0') {
        cursor_++;
        if (c == '\n') line_++;
    }
    return c;
}

void Lexer::skip_whitespace_and_comments() {
    while (true) {
        char c = peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance();
        } else if (c == '#' || c == ';') {
            while (peek() != '\n' && peek() != '\0') {
                advance();
            }
        } else {
            break;
        }
    }
}

Token Lexer::next_token() {
    skip_whitespace_and_comments();

    Token tok;
    tok.line = line_;
    tok.view.str = &source_[cursor_];
    tok.view.length = 0;
    tok.opcode = Opcode::ADD_I32;
    tok.data_type = DataType::VOID;
    tok.int_val = 0;

    char c = peek();
    if (c == '\0') {
        tok.type = TokenType::TOKEN_EOF;
        return tok;
    }

    if (c == '"') {
        advance();
        char temp_buf[2048];
        size_t len = 0;
        while (peek() != '"' && peek() != '\0' && len < sizeof(temp_buf) - 1) {
            char ch = advance();
            if (ch == '\\') {
                char esc = advance();
                if (esc == 'n') temp_buf[len++] = '\n';
                else if (esc == 't') temp_buf[len++] = '\t';
                else if (esc == 'r') temp_buf[len++] = '\r';
                else if (esc == '0') temp_buf[len++] = '\0';
                else if (esc == '\\') temp_buf[len++] = '\\';
                else if (esc == '"') temp_buf[len++] = '"';
                else temp_buf[len++] = esc;
            } else {
                temp_buf[len++] = ch;
            }
        }
        if (peek() == '"') advance();

        char* str_copy = lexer_alloc_string(temp_buf, len);
        if (!str_copy) { tok.type = TokenType::TOKEN_ERROR; return tok; }
        tok.type = TokenType::TOKEN_STRING_LITERAL;
        tok.string_val = str_copy;
        tok.string_len = len;
        tok.string_hash = fnv1a_hash(str_copy, len);
        tok.view.length = &source_[cursor_] - tok.view.str;
        return tok;
    }

    if (c == '[') { advance(); tok.type = TokenType::TOKEN_LBRACKET; tok.view.length = 1; return tok; }
    if (c == ']') { advance(); tok.type = TokenType::TOKEN_RBRACKET; tok.view.length = 1; return tok; }
    if (c == '(') { advance(); tok.type = TokenType::TOKEN_LPAREN; tok.view.length = 1; return tok; }
    if (c == ')') { advance(); tok.type = TokenType::TOKEN_RPAREN; tok.view.length = 1; return tok; }
    if (c == ':') { advance(); tok.type = TokenType::TOKEN_COLON; tok.view.length = 1; return tok; }
    if (c == ',') { advance(); tok.type = TokenType::TOKEN_COMMA; tok.view.length = 1; return tok; }
    if (c == '+') { advance(); tok.type = TokenType::TOKEN_PLUS; tok.view.length = 1; return tok; }

    if (c == '-') {
        advance();
        if (peek() == '>') {
            advance();
            tok.type = TokenType::TOKEN_ARROW;
            tok.view.length = 2;
            return tok;
        }
        if (peek() >= '0' && peek() <= '9') {
            // Negative integer
            int64_t val = 0;
            while (peek() >= '0' && peek() <= '9') {
                val = val * 10 + (advance() - '0');
            }
            tok.type = TokenType::TOKEN_INT_LITERAL;
            tok.int_val = -val;
            tok.view.length = &source_[cursor_] - tok.view.str;
            return tok;
        }
        tok.type = TokenType::TOKEN_MINUS;
        tok.view.length = 1;
        return tok;
    }

    if (c >= '0' && c <= '9') {
        int64_t val = 0;
        while (peek() >= '0' && peek() <= '9') {
            val = val * 10 + (advance() - '0');
        }
        tok.type = TokenType::TOKEN_INT_LITERAL;
        tok.int_val = val;
        tok.view.length = &source_[cursor_] - tok.view.str;
        return tok;
    }

    // Directives starting with '.'
    if (c == '.') {
        advance();
        size_t start = cursor_;
        while ((peek() >= 'a' && peek() <= 'z') || (peek() >= 'A' && peek() <= 'Z') || peek() == '_' || peek() == '-') {
            advance();
        }
        StringView sub_view = { &source_[start], cursor_ - start };
        uint64_t hash = fnv1a_hash(sub_view.str, sub_view.length);
        tok.view.str = &source_[start - 1];
        tok.view.length = cursor_ - (start - 1);

        switch (hash) {
            case fnv1a_hash("fn", 2):           tok.type = TokenType::TOKEN_FN; return tok;
            case fnv1a_hash("end_fn", 6):       tok.type = TokenType::TOKEN_END_FN; return tok;
            case fnv1a_hash("class", 5):        tok.type = TokenType::TOKEN_CLASS; return tok;
            case fnv1a_hash("end_class", 9):    tok.type = TokenType::TOKEN_END_CLASS; return tok;
            case fnv1a_hash("field", 5):        tok.type = TokenType::TOKEN_FIELD; return tok;
            case fnv1a_hash("registers", 9):    tok.type = TokenType::TOKEN_REGISTERS; return tok;
            case fnv1a_hash("import-sys", 10):  tok.type = TokenType::TOKEN_IMPORT_SYS; return tok;
            default: break;
        }
        tok.type = TokenType::TOKEN_IDENTIFIER;
        return tok;
    }

    // Identifiers, registers (v0..vN, p0..pN), opcodes, and labels
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
        size_t start = cursor_;
        while ((peek() >= 'a' && peek() <= 'z') || (peek() >= 'A' && peek() <= 'Z') ||
               (peek() >= '0' && peek() <= '9') || peek() == '_' || peek() == '-' || peek() == '.' || peek() == '/') {
            advance();
        }
        tok.view.str = &source_[start];
        tok.view.length = cursor_ - start;

        if (peek() == ':') {
            advance(); // consume ':'
            tok.type = TokenType::TOKEN_LABEL;
            return tok;
        }

        // Register check: p[0-9]+ or v[0-9]+
        if ((tok.view.str[0] == 'p' || tok.view.str[0] == 'v') && tok.view.length > 1) {
            bool is_reg = true;
            uint32_t idx = 0;
            for (size_t i = 1; i < tok.view.length; ++i) {
                if (tok.view.str[i] >= '0' && tok.view.str[i] <= '9') {
                    idx = idx * 10 + (tok.view.str[i] - '0');
                } else {
                    is_reg = false;
                    break;
                }
            }
            if (is_reg) {
                tok.type = TokenType::TOKEN_REGISTER;
                tok.reg.type = (tok.view.str[0] == 'p') ? RegisterType::PARAM : RegisterType::LOCAL;
                tok.reg.index = idx;
                return tok;
            }
        }

        uint64_t hash = fnv1a_hash(tok.view.str, tok.view.length);

        // Native Types & Hints
        switch (hash) {
            case fnv1a_hash("i32", 3):      tok.type = TokenType::TOKEN_TYPE; tok.data_type = DataType::I32; return tok;
            case fnv1a_hash("i64", 3):      tok.type = TokenType::TOKEN_TYPE; tok.data_type = DataType::I64; return tok;
            case fnv1a_hash("ptr", 3):      tok.type = TokenType::TOKEN_TYPE; tok.data_type = DataType::PTR; return tok;
            case fnv1a_hash("f32", 3):      tok.type = TokenType::TOKEN_TYPE; tok.data_type = DataType::F32; return tok;
            case fnv1a_hash("void", 4):     tok.type = TokenType::TOKEN_TYPE; tok.data_type = DataType::VOID; return tok;
            case fnv1a_hash("likely", 6):   tok.type = TokenType::TOKEN_LIKELY; return tok;
            case fnv1a_hash("unlikely", 8): tok.type = TokenType::TOKEN_UNLIKELY; return tok;
            default: break;
        }

        // Anastasia Assembly Opcodes
        switch (hash) {
            case fnv1a_hash("add-int/32", 10):    tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::ADD_I32; return tok;
            case fnv1a_hash("sub-int/32", 10):    tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::SUB_I32; return tok;
            case fnv1a_hash("neg-int/32", 10):    tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::NEG_I32; return tok;
            case fnv1a_hash("mul-int/32", 10):    tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::MUL_I32; return tok;
            case fnv1a_hash("div-int/32", 10):    tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::DIV_I32; return tok;
            case fnv1a_hash("add-int/64", 10):    tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::ADD_I64; return tok;
            case fnv1a_hash("sub-int/64", 10):    tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::SUB_I64; return tok;
            case fnv1a_hash("neg-int/64", 10):    tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::NEG_I64; return tok;
            case fnv1a_hash("mul-int/64", 10):    tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::MUL_I64; return tok;
            case fnv1a_hash("div-int/64", 10):    tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::DIV_I64; return tok;
            case fnv1a_hash("and-int/32", 10):    tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::AND_I32; return tok;
            case fnv1a_hash("and-int/64", 10):    tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::AND_I64; return tok;
            case fnv1a_hash("or-int/32", 9):      tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::OR_I32; return tok;
            case fnv1a_hash("or-int/64", 9):      tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::OR_I64; return tok;
            case fnv1a_hash("xor-int/32", 10):    tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::XOR_I32; return tok;
            case fnv1a_hash("xor-int/64", 10):    tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::XOR_I64; return tok;
            case fnv1a_hash("shl-int/32", 10):    tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::SHL_I32; return tok;
            case fnv1a_hash("shl-int/64", 10):    tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::SHL_I64; return tok;
            case fnv1a_hash("shr-int/32", 10):    tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::SHR_I32; return tok;
            case fnv1a_hash("shr-int/64", 10):    tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::SHR_I64; return tok;
            case fnv1a_hash("ushr-int/32", 11):   tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::USHR_I32; return tok;
            case fnv1a_hash("ushr-int/64", 11):   tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::USHR_I64; return tok;
            case fnv1a_hash("bts-int/64", 10):    tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::BTS_I64; return tok;
            case fnv1a_hash("btr-int/64", 10):    tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::BTR_I64; return tok;
            case fnv1a_hash("popcount-int/64", 15): tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::POPCOUNT_I64; return tok;
            case fnv1a_hash("lzcnt-int/64", 12):  tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::LZCNT_I64; return tok;
            case fnv1a_hash("move", 4):           tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::MOVE; return tok;
            case fnv1a_hash("move-const", 10):
            case fnv1a_hash("move_const", 10):    tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::MOVE_CONST; return tok;
            case fnv1a_hash("sys-call", 8):
            case fnv1a_hash("sys_call", 8):       tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::SYS_CALL; return tok;
            case fnv1a_hash("load-mem", 8):
            case fnv1a_hash("load_mem", 8):       tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::LOAD_MEM; return tok;
            case fnv1a_hash("store-mem", 9):
            case fnv1a_hash("store_mem", 9):      tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::STORE_MEM; return tok;
            case fnv1a_hash("bind-vtable", 11):
            case fnv1a_hash("bind_vtable", 11):   tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::BIND_VTABLE; return tok;
            case fnv1a_hash("call-virt", 9):
            case fnv1a_hash("call_virt", 9):      tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::CALL_VIRT; return tok;
            case fnv1a_hash("call-virt-fast", 14):
            case fnv1a_hash("call_virt_fast", 14):tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::CALL_VIRT_FAST; return tok;
            case fnv1a_hash("return-void", 11):
            case fnv1a_hash("return_void", 11):   tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::RETURN_VOID; return tok;
            case fnv1a_hash("return-val", 10):
            case fnv1a_hash("return_val", 10):    tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::RETURN_VAL; return tok;
            case fnv1a_hash("goto", 4):           tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::GOTO; return tok;
            case fnv1a_hash("if-eq", 5):          tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::IF_EQ; return tok;
            case fnv1a_hash("if-ne", 5):          tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::IF_NE; return tok;
            case fnv1a_hash("if-lt", 5):          tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::IF_LT; return tok;
            case fnv1a_hash("if-ge", 5):          tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::IF_GE; return tok;
            case fnv1a_hash("if-z", 4):           tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::IF_Z; return tok;
            case fnv1a_hash("if-nz", 5):          tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::IF_NZ; return tok;
            case fnv1a_hash("atomic-cas/64", 13):tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::ATOMIC_CAS_I64; return tok;
            case fnv1a_hash("atomic-xchg/64", 14):tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::ATOMIC_XCHG_I64; return tok;
            case fnv1a_hash("atomic-add/64", 13): tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::ATOMIC_ADD_I64; return tok;
            case fnv1a_hash("atomic-and/64", 13): tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::ATOMIC_AND_I64; return tok;
            case fnv1a_hash("atomic-or/64", 12):  tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::ATOMIC_OR_I64; return tok;
            case fnv1a_hash("fence", 5):          tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::FENCE; return tok;
            case fnv1a_hash("new-instance", 12):  tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::NEW_INSTANCE; return tok;
            case fnv1a_hash("add-float/32", 12):  tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::ADD_FLOAT_32; return tok;
            case fnv1a_hash("add-float/64", 12):  tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::ADD_FLOAT_64; return tok;
            case fnv1a_hash("sub-float/64", 12):  tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::SUB_FLOAT_64; return tok;
            case fnv1a_hash("mul-float/64", 12):  tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::MUL_FLOAT_64; return tok;
            case fnv1a_hash("div-float/64", 12):  tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::DIV_FLOAT_64; return tok;
            case fnv1a_hash("add-vector/i32x4", 16): tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::ADD_VECTOR_I32X4; return tok;
            case fnv1a_hash("sub-vector/i32x4", 16): tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::SUB_VECTOR_I32X4; return tok;
            case fnv1a_hash("add-vector/i32x8", 16): tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::ADD_VECTOR_I32X8; return tok;
            case fnv1a_hash("add-vector/i32x16", 17): tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::ADD_VECTOR_I32X16; return tok;
            case fnv1a_hash("mul-vector/i32x8", 16): tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::MUL_VECTOR_I32X8; return tok;
            case fnv1a_hash("load-vector/256", 15): tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::LOAD_VECTOR_256; return tok;
            case fnv1a_hash("load-vector/512", 15): tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::LOAD_VECTOR_512; return tok;
            case fnv1a_hash("sink-mem", 8):         tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::SINK_MEM; return tok;
            case fnv1a_hash("const-string", 12):    tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::CONST_STRING; return tok;
            case fnv1a_hash("str-len", 7):          tok.type = TokenType::TOKEN_OPCODE; tok.opcode = Opcode::STR_LEN; return tok;
            default: break;
        }

        tok.type = TokenType::TOKEN_IDENTIFIER;
        return tok;
    }

    // An unrecognised character used to produce TOKEN_EOF, which silently
    // discarded the rest of the program. Report it instead.
    advance();
    tok.type = TokenType::TOKEN_ERROR;
    tok.view.length = 1;
    return tok;
}

} // namespace frontend
} // namespace ana
