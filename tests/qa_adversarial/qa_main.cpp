// Adversarial QA harness for Anastasia (v2).
#include "frontend/ana_parser.h"
#include "frontend/ana_lexer.h"
#include "frontend/arena_allocator.h"
#include "backend/ana_lowerer.h"
#include "backend/ana_encoder.h"
#include "backend/vmem_provider.h"
#include "sys/sys_raw.h"

using namespace ana;

extern "C" long strtol(const char* nptr, char** endptr, int base);
extern "C" int snprintf(char* str, size_t size, const char* format, ...);

static int g_fail = 0;
static size_t slen(const char* s) { size_t n = 0; while (s && s[n]) ++n; return n; }
static void P(const char* s) { ana::sys::raw_write(1, s, slen(s)); }

static void PI(int64_t v) {
    char buf[24]; int i = 0;
    bool neg = v < 0;
    uint64_t m = neg ? (0ULL - (uint64_t)v) : (uint64_t)v;
    do { buf[i++] = (char)('0' + (m % 10)); m /= 10; } while (m && i < 24);
    if (neg && i < 24) buf[i++] = '-';
    char out[24]; int k = 0;
    while (i > 0) out[k++] = buf[--i];
    ana::sys::raw_write(1, out, (size_t)k);
}

static void PX(uint64_t v) {
    const char* d = "0123456789abcdef";
    char buf[19]; buf[0] = '0'; buf[1] = 'x';
    int k = 2; bool started = false;
    for (int sh = 60; sh >= 0; sh -= 4) {
        uint8_t nib = (uint8_t)((v >> sh) & 0xF);
        if (nib || started || sh == 0) { buf[k++] = d[nib]; started = true; }
    }
    ana::sys::raw_write(1, buf, (size_t)k);
}

static void CHECK(bool cond, const char* what) {
    if (cond) P("    [ok]   "); else { P("    [BUG]  "); g_fail++; }
    P(what); P("\n");
}

static void* jit(const char* src, frontend::ArenaAllocator& arena,
                 backend::AnastasiaJitRuntime& rt, frontend::Program** out_prog) {
    frontend::Parser p(src, arena);
    frontend::Program* prog = p.parse_program();
    if (!prog || !prog->functions) return nullptr;
    if (out_prog) *out_prog = prog;
    backend::AnaLowerer low(rt);
    return low.compile_function(prog->functions, prog);
}

static bool bytes_are(const uint8_t* got, size_t n, const uint8_t* want, size_t wn) {
    if (n != wn) return false;
    for (size_t i = 0; i < n; ++i) if (got[i] != want[i]) return false;
    return true;
}
static void dump(const uint8_t* b, size_t n) { for (size_t i = 0; i < n; ++i) { PX(b[i]); P(" "); } }

static int t_encoder_golden() {
    { backend::AnaEncoder e; e.vmovdqu_ymm_mem(0, backend::X86Reg::RDI, 0);
      static const uint8_t w[] = { 0xC4, 0xE1, 0x7E, 0x6F, 0x07 };
      P("    vmovdqu ymm0,[rdi]: "); dump(e.code_bytes(), e.code_size()); P("\n");
      CHECK(bytes_are(e.code_bytes(), e.code_size(), w, sizeof(w)), "VMOVDQU load uses F3 (not 66 = VMOVDQA)"); }
    { backend::AnaEncoder e; e.vmovdqu_mem_ymm(backend::X86Reg::RDI, 0, 0);
      static const uint8_t w[] = { 0xC4, 0xE1, 0x7E, 0x7F, 0x07 };
      P("    vmovdqu [rdi],ymm0: "); dump(e.code_bytes(), e.code_size()); P("\n");
      CHECK(bytes_are(e.code_bytes(), e.code_size(), w, sizeof(w)), "VMOVDQU store uses F3"); }
    { backend::AnaEncoder e; e.vpaddd_ymm_ymm(0, 0, 1);
      static const uint8_t w[] = { 0xC4, 0xE1, 0x7D, 0xFE, 0xC1 };
      P("    vpaddd ymm0,ymm0,ymm1: "); dump(e.code_bytes(), e.code_size()); P("\n");
      CHECK(bytes_are(e.code_bytes(), e.code_size(), w, sizeof(w)), "VPADDD ymm uses the 0F map (0F38 decoded as (bad))"); }
    { backend::AnaEncoder e; e.vpaddd_zmm_zmm(0, 0, 1);
      static const uint8_t w[] = { 0x62, 0xF1, 0x7D, 0x48, 0xFE, 0xC1 };
      P("    vpaddd zmm0,zmm0,zmm1: "); dump(e.code_bytes(), e.code_size()); P("\n");
      CHECK(bytes_are(e.code_bytes(), e.code_size(), w, sizeof(w)), "EVEX VPADDD matches objdump exactly"); }
    { backend::AnaEncoder e; e.vmovdqu_zmm_mem(0, backend::X86Reg::RDI, 0);
      static const uint8_t w[] = { 0x62, 0xF1, 0x7E, 0x48, 0x6F, 0x07 };
      P("    vmovdqu32 zmm0,[rdi]: "); dump(e.code_bytes(), e.code_size()); P("\n");
      CHECK(bytes_are(e.code_bytes(), e.code_size(), w, sizeof(w)), "EVEX VMOVDQU32 matches objdump exactly"); }
    return 0;
}

static int t_encoder_growth() {
    backend::AnaEncoder e;
    for (int i = 0; i < 2000; ++i) e.add_reg_imm32(backend::X86Reg::R8, 1);
    size_t sz = e.code_size();
    P("    2000 adds -> "); PI((int64_t)sz); P(" bytes\n");
    CHECK(sz == 8000, "every byte survives growth past 4096");
    CHECK(!e.failed(), "encoder reports no failure after growing");
    const uint8_t* c = e.code_bytes();
    bool intact = (c != nullptr);
    for (size_t i = 0; intact && i + 3 < sz; i += 4)
        if (c[i] != 0x49 || c[i+1] != 0x83 || c[i+2] != 0xC0 || c[i+3] != 0x01) intact = false;
    CHECK(intact, "grown buffer preserves previously emitted bytes");
    return 0;
}

static int t_encoder_limits() {
    backend::AnaEncoder e;
    uint32_t first = e.new_label(), last = first;
    for (int i = 1; i < 600; ++i) last = e.new_label();
    P("    first = "); PI((int64_t)first); P(", 600th = "); PI((int64_t)last); P("\n");
    CHECK(first == 0, "first label id is 0");
    CHECK(last == backend::AnaEncoder::kInvalidLabel, "exhaustion returns kInvalidLabel, not 0");
    CHECK(e.failed(), "label exhaustion latches the failure flag");
    return 0;
}

static int t_jit_overflow() {
    static char src[80000];
    size_t o = 0;
    const char* head = ".fn big(p0: i64) -> i64\n    .registers 2 local\n    move-const v0, 0\n";
    for (const char* s = head; *s; ++s) src[o++] = *s;
    for (int i = 0; i < 1200; ++i) { const char* ln = "    add-int/64 v0, v0, 1\n"; for (const char* s = ln; *s; ++s) src[o++] = *s; }
    const char* tail = "    return-val v0\n.end_fn\n";
    for (const char* s = tail; *s; ++s) src[o++] = *s;
    src[o] = 0;
    P("    source bytes: "); PI((int64_t)o); P("\n");
    frontend::ArenaAllocator arena; backend::AnastasiaJitRuntime rt;
    typedef int64_t (*Fn)(int64_t);
    Fn f = (Fn)jit(src, arena, rt, nullptr);
    if (!f) { CHECK(false, "large function compiles"); return 1; }
    int64_t r = f(0);
    P("    result = "); PI(r); P(" (expected 1200)\n");
    CHECK(r == 1200, "function spanning multiple pages executes correctly");
    return 0;
}

static int t_div_guard() {
    frontend::ArenaAllocator arena; backend::AnastasiaJitRuntime rt;
    typedef int64_t (*Fn)(int64_t, int64_t);
    Fn d = (Fn)jit(".fn d(p0: i64, p1: i64) -> i64\n    .registers 1 local\n"
                   "    div-int/64 v0, p0, p1\n    return-val v0\n.end_fn\n", arena, rt, nullptr);
    if (!d) { CHECK(false, "division compiles"); return 1; }
    int64_t ok = d(100, 7); P("    100 / 7 = "); PI(ok); P("\n");
    CHECK(ok == 14, "normal division is still correct");
    int64_t z = d(42, 0); P("    42 / 0 = "); PI(z); P(" (survived)\n");
    CHECK(z == 0, "divide by zero returns 0 instead of SIGFPE");
    int64_t mn = d((int64_t)0x8000000000000000ULL, -1);
    P("    INT64_MIN / -1 = "); PI(mn); P(" (survived)\n");
    CHECK(mn == (int64_t)0x8000000000000000ULL, "INT64_MIN / -1 does not raise SIGFPE");
    return 0;
}

static int t_constfold() {
    frontend::ArenaAllocator a3;
    frontend::Parser p3(".fn h() -> i64\n    .registers 1 local\n    shl-int/64 v0, 1, 200\n    return-val v0\n.end_fn\n", a3);
    frontend::Program* pr3 = p3.parse_program();
    if (!pr3) { CHECK(false, "shift program parses"); return 1; }
    int64_t folded = pr3->functions->first_block->first_insn->src1.const_val;
    frontend::ArenaAllocator a4; backend::AnastasiaJitRuntime rt4;
    typedef int64_t (*Fn2)(int64_t, int64_t);
    Fn2 sh = (Fn2)jit(".fn s(p0: i64, p1: i64) -> i64\n    .registers 1 local\n"
                      "    shl-int/64 v0, p0, p1\n    return-val v0\n.end_fn\n", a4, rt4, nullptr);
    if (!sh) { CHECK(false, "shift compiles"); return 1; }
    int64_t runtime = sh(1, 200);
    P("    1 << 200: folded="); PI(folded); P(" runtime="); PI(runtime); P("\n");
    CHECK(folded == runtime, "out-of-range shift folds exactly as the CPU computes it");
    frontend::ArenaAllocator a5;
    frontend::Parser p5(".fn u() -> i64\n    .registers 1 local\n    ushr-int/32 v0, -1, 1\n    return-val v0\n.end_fn\n", a5);
    frontend::Program* pr5 = p5.parse_program();
    if (!pr5) { CHECK(false, "ushr program parses"); return 1; }
    int64_t uf = pr5->functions->first_block->first_insn->src1.const_val;
    P("    ushr-int/32 -1 >>> 1 = "); PI(uf); P(" (expected 2147483647)\n");
    CHECK(uf == 2147483647LL, "ushr-int/32 folds as a 32-bit unsigned shift");
    return 0;
}

static int t_class_layout() {
    const char* src = ".class Pt\n.field a: i64\n.field b: i32\n.field c: ptr\n.end_class\n"
                      ".fn f() -> void\n    .registers 1 local\n    return-void\n.end_fn\n";
    { frontend::Lexer lx(src);
      P("    token types:");
      for (int i = 0; i < 14; ++i) {
          frontend::Token t = lx.next_token();
          P(" "); PI((int64_t)t.type);
          if (t.type == frontend::TokenType::TOKEN_EOF) break;
      }
      P("  (FIELD="); PI((int64_t)frontend::TokenType::TOKEN_FIELD);
      P(" TYPE="); PI((int64_t)frontend::TokenType::TOKEN_TYPE);
      P(" COLON="); PI((int64_t)frontend::TokenType::TOKEN_COLON); P(")\n"); }
    frontend::ArenaAllocator arena;
    frontend::Parser p(src, arena);
    frontend::Program* prog = p.parse_program();
    if (!prog || !prog->classes) { P("    parse failed: "); P(p.error_message()); P("\n"); CHECK(false, "class parses"); return 1; }
    frontend::ClassDecl* c = prog->classes;
    int n = 0; for (frontend::ClassField* f = c->fields; f; f = f->next) n++;
    P("    class size = "); PI((int64_t)c->size); P(", fields = "); PI(n); P("\n");
    CHECK(n == 3, "all three fields are parsed");
    CHECK(c->size >= 24, "class size accounts for every field");
    int idx = 0; uint32_t last = 0; bool inc = true, novt = true;
    for (frontend::ClassField* f = c->fields; f; f = f->next) {
        P("    field "); PI(idx); P(" offset = "); PI((int64_t)f->offset); P("\n");
        if (f->offset < 8) novt = false;
        if (idx > 0 && f->offset <= last) inc = false;
        last = f->offset; idx++;
    }
    CHECK(inc && idx > 0, "fields get distinct increasing offsets");
    CHECK(novt && idx > 0, "no field overlaps the vtable pointer at offset 0");
    return 0;
}

static int t_parse_errors() {
    static const char* bad[] = {
        ".fn broken(\n",
        ".class\n",
        ".fn x() -> i64\n    .registers 1 local\n    add-int/64 v0,\n.end_fn\n",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        frontend::ArenaAllocator arena;
        frontend::Parser p(bad[i], arena);
        frontend::Program* prog = p.parse_program();
        P("    case "); PI((int64_t)i); P(": "); P(prog == nullptr ? "rejected" : "ACCEPTED");
        if (prog == nullptr) { P(" ("); P(p.error_message()); P(")"); }
        P("\n");
        CHECK(prog == nullptr, "malformed program is rejected");
    }
    return 0;
}

static int t_arena_page_edge() {
    static const size_t sizes[] = { 65528, 65520, 65504, 61440, 131064 };
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        frontend::ArenaAllocator arena;
        size_t want = sizes[i];
        unsigned char* p = (unsigned char*)arena.alloc(want, 8);
        P("    alloc("); PI((int64_t)want); P(") -> ");
        if (!p) { P("nullptr\n"); CHECK(false, "page-edge allocation succeeds"); continue; }
        P("writing every byte\n");
        for (size_t k = 0; k < want; ++k) p[k] = (unsigned char)(k & 0xFF);
        bool ok = true;
        for (size_t k = 0; k < want; k += 997) if (p[k] != (unsigned char)(k & 0xFF)) ok = false;
        CHECK(ok, "page-edge allocation is fully writable");
    }
    return 0;
}

static int t_libc() {
    long a = strtol("-42", nullptr, 10);
    P("    strtol(-42) = "); PI(a); P("\n");
    CHECK(a == -42, "strtol handles a negative sign");
    long b = strtol("ff", nullptr, 16);
    P("    strtol(ff,16) = "); PI(b); P("\n");
    CHECK(b == 255, "strtol honours the base argument");
    long c = strtol("0x1A", nullptr, 0);
    P("    strtol(0x1A,0) = "); PI(c); P("\n");
    CHECK(c == 26, "strtol auto-detects the 0x prefix");
    char* end = nullptr;
    long d = strtol("  123abc", &end, 10);
    P("    strtol(  123abc) = "); PI(d); P(", endptr='"); if (end) ana::sys::raw_write(1, end, slen(end)); P("'\n");
    CHECK(d == 123 && end && end[0] == 'a', "strtol sets endptr past the digits");
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "%s=%d/%x", "v", -7, 255);
    P("    snprintf -> '"); P(buf); P("' n="); PI(n); P("\n");
    CHECK(n == 7, "snprintf returns the C99 length");
    bool eq = buf[0]=='v' && buf[1]=='=' && buf[2]=='-' && buf[3]=='7' && buf[4]=='/' && buf[5]=='f' && buf[6]=='f' && buf[7]==0;
    CHECK(eq, "snprintf formats %s, %d and %x correctly");
    char small[4];
    int n2 = snprintf(small, sizeof(small), "abcdefgh");
    P("    truncating snprintf -> '"); P(small); P("' n="); PI(n2); P("\n");
    CHECK(n2 == 8, "snprintf reports the would-be length when truncating");
    CHECK(small[3] == 0, "snprintf always NUL-terminates");
    return 0;
}

struct Case { const char* name; int (*fn)(); };
static const Case kCases[] = {
    { "encoder_golden", t_encoder_golden }, { "encoder_growth", t_encoder_growth },
    { "encoder_limits", t_encoder_limits }, { "jit_overflow", t_jit_overflow },
    { "div_guard", t_div_guard }, { "constfold", t_constfold },
    { "class_layout", t_class_layout }, { "parse_errors", t_parse_errors },
    { "arena_page_edge", t_arena_page_edge }, { "libc", t_libc },
};

static bool name_eq(const char* a, const char* b) {
    while (*a && *b) { if (*a != *b) return false; ++a; ++b; }
    return *a == *b;
}

int ana_main(int argc, char** argv) {
    const size_t ncase = sizeof(kCases) / sizeof(kCases[0]);
    int ran = 0;
    for (size_t i = 0; i < ncase; ++i) {
        if (argc >= 2 && !name_eq(argv[1], kCases[i].name)) continue;
        P("=== "); P(kCases[i].name); P(" ===\n");
        kCases[i].fn();
        ran++;
    }
    P("\nSUMMARY: cases="); PI(ran); P(" failed_checks="); PI(g_fail); P("\n");
    return g_fail == 0 ? 0 : 1;
}
