#include "leetcode_suite.h"
#include "../src/sys/sys_raw.h"
#include "../src/frontend/arena_allocator.h"
#include "../src/frontend/ana_lexer.h"
#include "../src/frontend/ana_parser.h"
#include "../src/backend/vmem_provider.h"
#include "../src/backend/ana_lowerer.h"
#include "../src/optimizer/ana_ssa.h"

namespace ana {
namespace tests {

static void print_lc(const char* msg) {
    ana::sys::raw_write(1, msg, ana::sys::freestanding_strlen(msg));
}

static void print_int_lc(int64_t val) {
    char buf[32];
    int len = 0;
    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        print_lc(buf);
        return;
    }
    bool neg = false;
    if (val < 0) { neg = true; val = -val; }
    while (val > 0) {
        buf[len++] = '0' + (val % 10);
        val /= 10;
    }
    if (neg) buf[len++] = '-';
    for (int i = 0; i < len / 2; ++i) {
        char t = buf[i];
        buf[i] = buf[len - 1 - i];
        buf[len - 1 - i] = t;
    }
    buf[len] = '\0';
    print_lc(buf);
}

typedef int64_t (*LcFnP1)(int64_t);
typedef int64_t (*LcFnP2)(int64_t, int64_t);
typedef int64_t (*LcFnP3)(int64_t, int64_t, int64_t);
typedef bool (*LcTestRunner)();

static bool run_single_lc_test(const char* name, const char* code, LcTestRunner test_runner) {
    print_lc("  Running ");
    print_lc(name);
    print_lc("... ");

    frontend::ArenaAllocator arena;
    frontend::Parser parser(code, arena);
    frontend::Program* prog = parser.parse_program();

    if (!prog || !prog->functions) {
        print_lc("FAILED (AST Parsing)\n");
        return false;
    }

    // SSA Optimization pass
    optimizer::AnaSSAIR ssa;
    ssa.optimize_program(prog);

    // JIT Lowering
    backend::AnastasiaJitRuntime runtime;
    backend::AnaLowerer lowerer(runtime);
    void* exec_ptr = lowerer.compile_function(prog->functions, prog);

    if (!exec_ptr) {
        print_lc("FAILED (JIT Lowering)\n");
        return false;
    }

    bool pass = test_runner();
    if (pass) {
        print_lc("PASSED\n");
    } else {
        print_lc("FAILED (Assertion mismatch)\n");
    }
    return pass;
}

// -------------------------------------------------------------------
// LC 1: Two Sum
// Given array arr [p0] of size n [p1] and target [p2], returns i * 100 + j
// -------------------------------------------------------------------
static const char* code_lc1_two_sum =
    ".fn two_sum(p0: ptr, p1: i64, p2: i64) -> i64\n"
    "  .registers 6 local\n"
    "  move-const v0, 0\n" // i = 0
    "loop_i:\n"
    "  if-ge v0, p1, not_found\n"
    "  add-int/64 v1, v0, 1\n" // j = i + 1
    "loop_j:\n"
    "  if-ge v1, p1, next_i\n"
    "  shl-int/64 v2, v0, 3\n"
    "  add-int/64 v2, p0, v2\n"
    "  load-mem v2, [v2 + 0]\n" // v2 = arr[i]
    "  shl-int/64 v3, v1, 3\n"
    "  add-int/64 v3, p0, v3\n"
    "  load-mem v3, [v3 + 0]\n" // v3 = arr[j]
    "  add-int/64 v4, v2, v3\n"
    "  if-eq v4, p2, found\n"
    "  add-int/64 v1, v1, 1\n"
    "  goto loop_j\n"
    "next_i:\n"
    "  add-int/64 v0, v0, 1\n"
    "  goto loop_i\n"
    "found:\n"
    "  mul-int/64 v5, v0, 100\n"
    "  add-int/64 v5, v5, v1\n"
    "  return-val v5\n"
    "not_found:\n"
    "  move-const v5, -1\n"
    "  return-val v5\n"
    ".end_fn\n";

// -------------------------------------------------------------------
// LC 2: Reverse Integer
// Given integer p0, reverse digits: 12345 -> 54321
// -------------------------------------------------------------------
static const char* code_lc2_reverse_integer =
    ".fn reverse_integer(p0: i64) -> i64\n"
    "  .registers 4 local\n"
    "  move-const v0, 0\n" // rev = 0
    "  move v1, p0\n"       // x = p0
    "loop:\n"
    "  if-eq v1, 0, done\n"
    "  mul-int/64 v2, v1, 0\n" // temp for div/mod
    "  div-int/64 v2, v1, 10\n" // v2 = x / 10
    "  mul-int/64 v3, v2, 10\n"
    "  sub-int/64 v3, v1, v3\n" // v3 = x % 10
    "  mul-int/64 v0, v0, 10\n"
    "  add-int/64 v0, v0, v3\n" // rev = rev * 10 + digit
    "  move v1, v2\n"          // x = x / 10
    "  goto loop\n"
    "done:\n"
    "  return-val v0\n"
    ".end_fn\n";

// -------------------------------------------------------------------
// LC 3: Palindrome Number
// Returns 1 if p0 is palindrome, 0 otherwise
// -------------------------------------------------------------------
static const char* code_lc3_palindrome_number =
    ".fn palindrome_number(p0: i64) -> i64\n"
    "  .registers 4 local\n"
    "  if-lt p0, 0, is_false\n"
    "  move-const v0, 0\n" // rev = 0
    "  move v1, p0\n"       // x = p0
    "loop:\n"
    "  if-eq v1, 0, check\n"
    "  div-int/64 v2, v1, 10\n"
    "  mul-int/64 v3, v2, 10\n"
    "  sub-int/64 v3, v1, v3\n"
    "  mul-int/64 v0, v0, 10\n"
    "  add-int/64 v0, v0, v3\n"
    "  move v1, v2\n"
    "  goto loop\n"
    "check:\n"
    "  if-ne v0, p0, is_false\n"
    "  move-const v0, 1\n"
    "  return-val v0\n"
    "is_false:\n"
    "  move-const v0, 0\n"
    "  return-val v0\n"
    ".end_fn\n";

// -------------------------------------------------------------------
// LC 4: Roman to Integer
// String pointer p0 ("MCMXCIV" -> 1994)
// -------------------------------------------------------------------
static const char* code_lc4_roman_to_int =
    ".fn roman_to_int(p0: ptr) -> i64\n"
    "  .registers 6 local\n"
    "  move-const v0, 0\n" // sum = 0
    "  move-const v1, 0\n" // idx = 0
    "loop:\n"
    "  add-int/64 v2, p0, v1\n"
    "  load-mem v3, [v2 + 0]\n"
    "  and-int/64 v3, v3, 255\n"
    "  if-eq v3, 0, done\n"
    "  if-eq v3, 73, char_I\n"  // 'I'
    "  if-eq v3, 86, char_V\n"  // 'V'
    "  if-eq v3, 88, char_X\n"  // 'X'
    "  if-eq v3, 76, char_L\n"  // 'L'
    "  if-eq v3, 67, char_C\n"  // 'C'
    "  if-eq v3, 68, char_D\n"  // 'D'
    "  if-eq v3, 77, char_M\n"  // 'M'
    "  goto next_char\n"
    "char_I:\n"
    "  load-mem v4, [v2 + 1]\n"
    "  and-int/64 v4, v4, 255\n"
    "  if-eq v4, 86, I_sub_V\n"
    "  if-eq v4, 88, I_sub_X\n"
    "  add-int/64 v0, v0, 1\n"
    "  goto next_char\n"
    "I_sub_V:\n"
    "  add-int/64 v0, v0, 4\n"
    "  add-int/64 v1, v1, 1\n"
    "  goto next_char\n"
    "I_sub_X:\n"
    "  add-int/64 v0, v0, 9\n"
    "  add-int/64 v1, v1, 1\n"
    "  goto next_char\n"
    "char_V:\n"
    "  add-int/64 v0, v0, 5\n"
    "  goto next_char\n"
    "char_X:\n"
    "  load-mem v4, [v2 + 1]\n"
    "  and-int/64 v4, v4, 255\n"
    "  if-eq v4, 76, X_sub_L\n"
    "  if-eq v4, 67, X_sub_C\n"
    "  add-int/64 v0, v0, 10\n"
    "  goto next_char\n"
    "X_sub_L:\n"
    "  add-int/64 v0, v0, 40\n"
    "  add-int/64 v1, v1, 1\n"
    "  goto next_char\n"
    "X_sub_C:\n"
    "  add-int/64 v0, v0, 90\n"
    "  add-int/64 v1, v1, 1\n"
    "  goto next_char\n"
    "char_L:\n"
    "  add-int/64 v0, v0, 50\n"
    "  goto next_char\n"
    "char_C:\n"
    "  load-mem v4, [v2 + 1]\n"
    "  and-int/64 v4, v4, 255\n"
    "  if-eq v4, 68, C_sub_D\n"
    "  if-eq v4, 77, C_sub_M\n"
    "  add-int/64 v0, v0, 100\n"
    "  goto next_char\n"
    "C_sub_D:\n"
    "  add-int/64 v0, v0, 400\n"
    "  add-int/64 v1, v1, 1\n"
    "  goto next_char\n"
    "C_sub_M:\n"
    "  add-int/64 v0, v0, 900\n"
    "  add-int/64 v1, v1, 1\n"
    "  goto next_char\n"
    "char_D:\n"
    "  add-int/64 v0, v0, 500\n"
    "  goto next_char\n"
    "char_M:\n"
    "  add-int/64 v0, v0, 1000\n"
    "  goto next_char\n"
    "next_char:\n"
    "  add-int/64 v1, v1, 1\n"
    "  goto loop\n"
    "done:\n"
    "  return-val v0\n"
    ".end_fn\n";

// -------------------------------------------------------------------
// LC 5: Longest Common Prefix Length
// p0: str1, p1: str2
// -------------------------------------------------------------------
static const char* code_lc5_common_prefix_len =
    ".fn common_prefix_len(p0: ptr, p1: ptr) -> i64\n"
    "  .registers 5 local\n"
    "  move-const v0, 0\n" // idx = 0
    "loop:\n"
    "  add-int/64 v1, p0, v0\n"
    "  load-mem v2, [v1 + 0]\n"
    "  and-int/64 v2, v2, 255\n"
    "  add-int/64 v3, p1, v0\n"
    "  load-mem v4, [v3 + 0]\n"
    "  and-int/64 v4, v4, 255\n"
    "  if-eq v2, 0, done\n"
    "  if-eq v4, 0, done\n"
    "  if-ne v2, v4, done\n"
    "  add-int/64 v0, v0, 1\n"
    "  goto loop\n"
    "done:\n"
    "  return-val v0\n"
    ".end_fn\n";

static const char* code_lc6_valid_parentheses =
    ".fn valid_parentheses(p0: ptr, p1: ptr) -> i64\n"
    "  .registers 6 local\n"
    "  move-const v0, 0\n" // string idx
    "  move-const v1, 0\n" // stack size
    "loop:\n"
    "  add-int/64 v2, p0, v0\n"
    "  load-mem v3, [v2 + 0]\n"
    "  and-int/64 v3, v3, 255\n"
    "  if-eq v3, 0, done\n"
    "  if-eq v3, 40, push_char\n" // '('
    "  if-eq v3, 123, push_char\n" // '{'
    "  if-eq v3, 91, push_char\n" // '['
    "  ; Closing char handling\n"
    "  if-eq v1, 0, invalid\n"
    "  sub-int/64 v1, v1, 1\n"
    "  add-int/64 v4, p1, v1\n"
    "  load-mem v5, [v4 + 0]\n" // top char
    "  and-int/64 v5, v5, 255\n"
    "  if-eq v3, 41, check_paren\n" // ')'
    "  if-eq v3, 125, check_brace\n" // '}'
    "  if-eq v3, 93, check_bracket\n" // ']'
    "  goto invalid\n"
    "check_paren:\n"
    "  if-ne v5, 40, invalid\n"
    "  goto next_char\n"
    "check_brace:\n"
    "  if-ne v5, 123, invalid\n"
    "  goto next_char\n"
    "check_bracket:\n"
    "  if-ne v5, 91, invalid\n"
    "  goto next_char\n"
    "push_char:\n"
    "  add-int/64 v4, p1, v1\n"
    "  store-mem [v4 + 0], v3\n"
    "  add-int/64 v1, v1, 1\n"
    "  goto next_char\n"
    "next_char:\n"
    "  add-int/64 v0, v0, 1\n"
    "  goto loop\n"
    "done:\n"
    "  if-ne v1, 0, invalid\n"
    "  move-const v0, 1\n"
    "  return-val v0\n"
    "invalid:\n"
    "  move-const v0, 0\n"
    "  return-val v0\n"
    ".end_fn\n";

// -------------------------------------------------------------------
// LC 7: Merge Two Sorted Arrays
// p0: arr1, p1: n1, p2: arr2, p3: n2, p4: out_arr
// -------------------------------------------------------------------
static const char* code_lc7_merge_sorted =
    ".fn merge_sorted(p0: ptr, p1: i64, p2: ptr, p3: i64, p4: ptr) -> i64\n"
    "  .registers 7 local\n"
    "  move-const v0, 0\n" // i
    "  move-const v1, 0\n" // j
    "  move-const v2, 0\n" // k
    "loop:\n"
    "  if-ge v0, p1, append_rest_j\n"
    "  if-ge v1, p3, append_rest_i\n"
    "  shl-int/64 v3, v0, 3\n"
    "  add-int/64 v3, p0, v3\n"
    "  load-mem v3, [v3 + 0]\n" // v3 = arr1[i]
    "  shl-int/64 v4, v1, 3\n"
    "  add-int/64 v4, p2, v4\n"
    "  load-mem v4, [v4 + 0]\n" // v4 = arr2[j]
    "  if-ge v3, v4, take_j\n"
    "  ; take i\n"
    "  shl-int/64 v5, v2, 3\n"
    "  add-int/64 v5, p4, v5\n"
    "  store-mem v3, [v5 + 0]\n"
    "  add-int/64 v0, v0, 1\n"
    "  add-int/64 v2, v2, 1\n"
    "  goto loop\n"
    "take_j:\n"
    "  shl-int/64 v5, v2, 3\n"
    "  add-int/64 v5, p4, v5\n"
    "  store-mem v4, [v5 + 0]\n"
    "  add-int/64 v1, v1, 1\n"
    "  add-int/64 v2, v2, 1\n"
    "  goto loop\n"
    "append_rest_i:\n"
    "  if-ge v0, p1, done\n"
    "  shl-int/64 v3, v0, 3\n"
    "  add-int/64 v3, p0, v3\n"
    "  load-mem v3, [v3 + 0]\n"
    "  shl-int/64 v5, v2, 3\n"
    "  add-int/64 v5, p4, v5\n"
    "  store-mem v3, [v5 + 0]\n"
    "  add-int/64 v0, v0, 1\n"
    "  add-int/64 v2, v2, 1\n"
    "  goto append_rest_i\n"
    "append_rest_j:\n"
    "  if-ge v1, p3, done\n"
    "  shl-int/64 v4, v1, 3\n"
    "  add-int/64 v4, p2, v4\n"
    "  load-mem v4, [v4 + 0]\n"
    "  shl-int/64 v5, v2, 3\n"
    "  add-int/64 v5, p4, v5\n"
    "  store-mem v4, [v5 + 0]\n"
    "  add-int/64 v1, v1, 1\n"
    "  add-int/64 v2, v2, 1\n"
    "  goto append_rest_j\n"
    "done:\n"
    "  return-val v2\n"
    ".end_fn\n";

// -------------------------------------------------------------------
// LC 8: Remove Duplicates from Sorted Array
// p0: arr, p1: n. Returns new size
// -------------------------------------------------------------------
static const char* code_lc8_remove_duplicates =
    ".fn remove_duplicates(p0: ptr, p1: i64) -> i64\n"
    "  .registers 6 local\n"
    "  if-eq p1, 0, zero_size\n"
    "  move-const v0, 1\n" // write_idx = 1
    "  move-const v1, 1\n" // read_idx = 1
    "loop:\n"
    "  if-ge v1, p1, done\n"
    "  shl-int/64 v2, v1, 3\n"
    "  add-int/64 v2, p0, v2\n"
    "  load-mem v2, [v2 + 0]\n" // curr = arr[read_idx]
    "  sub-int/64 v3, v1, 1\n"
    "  shl-int/64 v3, v3, 3\n"
    "  add-int/64 v3, p0, v3\n"
    "  load-mem v3, [v3 + 0]\n" // prev = arr[read_idx - 1]
    "  if-eq v2, v3, next_iter\n"
    "  shl-int/64 v4, v0, 3\n"
    "  add-int/64 v4, p0, v4\n"
    "  store-mem v2, [v4 + 0]\n" // arr[write_idx] = curr
    "  add-int/64 v0, v0, 1\n"
    "next_iter:\n"
    "  add-int/64 v1, v1, 1\n"
    "  goto loop\n"
    "done:\n"
    "  return-val v0\n"
    "zero_size:\n"
    "  move-const v0, 0\n"
    "  return-val v0\n"
    ".end_fn\n";

// -------------------------------------------------------------------
// LC 9: Remove Element
// p0: arr, p1: n, p2: target_val. Returns new size
// -------------------------------------------------------------------
static const char* code_lc9_remove_element =
    ".fn remove_element(p0: ptr, p1: i64, p2: i64) -> i64\n"
    "  .registers 5 local\n"
    "  move-const v0, 0\n" // write_idx = 0
    "  move-const v1, 0\n" // read_idx = 0
    "loop:\n"
    "  if-ge v1, p1, done\n"
    "  shl-int/64 v2, v1, 3\n"
    "  add-int/64 v2, p0, v2\n"
    "  load-mem v3, [v2 + 0]\n"
    "  if-eq v3, p2, next_iter\n"
    "  shl-int/64 v4, v0, 3\n"
    "  add-int/64 v4, p0, v4\n"
    "  store-mem v3, [v4 + 0]\n"
    "  add-int/64 v0, v0, 1\n"
    "next_iter:\n"
    "  add-int/64 v1, v1, 1\n"
    "  goto loop\n"
    "done:\n"
    "  return-val v0\n"
    ".end_fn\n";

// -------------------------------------------------------------------
// LC 10: Search Insert Position (Binary Search)
// p0: arr, p1: n, p2: target
// -------------------------------------------------------------------
static const char* code_lc10_search_insert =
    ".fn search_insert(p0: ptr, p1: i64, p2: i64) -> i64\n"
    "  .registers 5 local\n"
    "  move-const v0, 0\n"       // low = 0
    "  sub-int/64 v1, p1, 1\n"   // high = n - 1
    "loop:\n"
    "  if-lt v1, v0, done\n"
    "  add-int/64 v2, v0, v1\n"
    "  shr-int/64 v2, v2, 1\n"   // mid = (low + high) / 2
    "  shl-int/64 v3, v2, 3\n"
    "  add-int/64 v3, p0, v3\n"
    "  load-mem v4, [v3 + 0]\n" // val = arr[mid]
    "  if-eq v4, p2, match\n"
    "  if-lt v4, p2, go_right\n"
    "  sub-int/64 v1, v2, 1\n"   // high = mid - 1
    "  goto loop\n"
    "go_right:\n"
    "  add-int/64 v0, v2, 1\n"   // low = mid + 1
    "  goto loop\n"
    "match:\n"
    "  return-val v2\n"
    "done:\n"
    "  return-val v0\n"
    ".end_fn\n";

// -------------------------------------------------------------------
// LC 11: Maximum Subarray (Kadane's Algorithm)
// p0: arr, p1: n. Returns max subarray sum
// -------------------------------------------------------------------
static const char* code_lc11_max_subarray =
    ".fn max_subarray(p0: ptr, p1: i64) -> i64\n"
    "  .registers 6 local\n"
    "  load-mem v0, [p0 + 0]\n" // max_so_far = arr[0]
    "  load-mem v1, [p0 + 0]\n" // curr_max = arr[0]
    "  move-const v2, 1\n"       // i = 1
    "loop:\n"
    "  if-ge v2, p1, done\n"
    "  shl-int/64 v3, v2, 3\n"
    "  add-int/64 v3, p0, v3\n"
    "  load-mem v4, [v3 + 0]\n" // x = arr[i]
    "  add-int/64 v5, v1, v4\n" // curr_max + x
    "  if-ge v5, v4, keep_sum\n"
    "  move v1, v4\n"           // curr_max = x
    "  goto check_max\n"
    "keep_sum:\n"
    "  move v1, v5\n"
    "check_max:\n"
    "  if-ge v0, v1, next_iter\n"
    "  move v0, v1\n"           // max_so_far = curr_max
    "next_iter:\n"
    "  add-int/64 v2, v2, 1\n"
    "  goto loop\n"
    "done:\n"
    "  return-val v0\n"
    ".end_fn\n";

// -------------------------------------------------------------------
// LC 12: Length of Last Word
// p0: string pointer
// -------------------------------------------------------------------
static const char* code_lc12_length_last_word =
    ".fn length_last_word(p0: ptr) -> i64\n"
    "  .registers 5 local\n"
    "  move-const v0, 0\n" // len = 0
    "loop_len:\n"
    "  add-int/64 v1, p0, v0\n"
    "  load-mem v2, [v1 + 0]\n"
    "  and-int/64 v2, v2, 255\n"
    "  if-eq v2, 0, find_last\n"
    "  add-int/64 v0, v0, 1\n"
    "  goto loop_len\n"
    "find_last:\n"
    "  sub-int/64 v1, v0, 1\n" // idx = len - 1
    "  move-const v3, 0\n"     // count = 0
    "skip_spaces:\n"
    "  if-lt v1, 0, done\n"
    "  add-int/64 v2, p0, v1\n"
    "  load-mem v4, [v2 + 0]\n"
    "  and-int/64 v4, v4, 255\n"
    "  if-ne v4, 32, count_chars\n" // ' ' space
    "  if-ne v3, 0, done\n"
    "  sub-int/64 v1, v1, 1\n"
    "  goto skip_spaces\n"
    "count_chars:\n"
    "  add-int/64 v3, v3, 1\n"
    "  sub-int/64 v1, v1, 1\n"
    "  goto skip_spaces\n"
    "done:\n"
    "  return-val v3\n"
    ".end_fn\n";

static const char* code_lc13_plus_one =
    ".fn plus_one(p0: ptr, p1: i64) -> i64\n"
    "  .registers 5 local\n"
    "  sub-int/64 v0, p1, 1\n" // i = n - 1
    "loop:\n"
    "  if-lt v0, 0, overflow\n"
    "  shl-int/64 v1, v0, 3\n"
    "  add-int/64 v1, p0, v1\n"
    "  load-mem v2, [v1 + 0]\n" // d = arr[i]
    "  add-int/64 v3, v2, 1\n"  // d + 1
    "  if-eq v3, 10, carry\n"
    "  store-mem [v1 + 0], v3\n"
    "  move-const v4, 0\n"
    "  return-val v4\n"
    "carry:\n"
    "  move-const v3, 0\n"
    "  store-mem [v1 + 0], v3\n"
    "  sub-int/64 v0, v0, 1\n"
    "  goto loop\n"
    "overflow:\n"
    "  move-const v4, 1\n"
    "  return-val v4\n"
    ".end_fn\n";

static const char* code_lc14_add_binary =
    ".fn add_binary(p0: i64, p1: i64) -> i64\n"
    "  .registers 1 local\n"
    "  add-int/64 v0, p0, p1\n"
    "  return-val v0\n"
    ".end_fn\n";

static const char* code_lc15_climb_stairs =
    ".fn climb_stairs(p0: i64) -> i64\n"
    "  .registers 5 local\n"
    "  if-ge p0, 3, compute\n"
    "  return-val p0\n"
    "compute:\n"
    "  move-const v0, 1\n" // a = 1
    "  move-const v1, 2\n" // b = 2
    "  move-const v2, 3\n" // i = 3
    "loop:\n"
    "  if-ge v2, p0, done_loop\n"
    "  add-int/64 v3, v0, v1\n" // c = a + b
    "  move v0, v1\n"
    "  move v1, v3\n"
    "  add-int/64 v2, v2, 1\n"
    "  goto loop\n"
    "done_loop:\n"
    "  add-int/64 v3, v0, v1\n"
    "  return-val v3\n"
    ".end_fn\n";

static const char* code_lc16_max_profit =
    ".fn max_profit(p0: ptr, p1: i64) -> i64\n"
    "  .registers 6 local\n"
    "  if-eq p1, 0, zero_profit\n"
    "  load-mem v0, [p0 + 0]\n" // min_price = prices[0]
    "  move-const v1, 0\n"       // max_profit = 0
    "  move-const v2, 1\n"       // i = 1
    "loop:\n"
    "  if-ge v2, p1, done\n"
    "  shl-int/64 v3, v2, 3\n"
    "  add-int/64 v3, p0, v3\n"
    "  load-mem v4, [v3 + 0]\n" // price = prices[i]
    "  if-ge v4, v0, check_profit\n"
    "  move v0, v4\n"           // min_price = price
    "  goto next_iter\n"
    "check_profit:\n"
    "  sub-int/64 v5, v4, v0\n" // profit = price - min_price
    "  if-ge v1, v5, next_iter\n"
    "  move v1, v5\n"
    "next_iter:\n"
    "  add-int/64 v2, v2, 1\n"
    "  goto loop\n"
    "done:\n"
    "  return-val v1\n"
    "zero_profit:\n"
    "  move-const v1, 0\n"
    "  return-val v1\n"
    ".end_fn\n";

static const char* code_lc17_single_number =
    ".fn single_number(p0: ptr, p1: i64) -> i64\n"
    "  .registers 4 local\n"
    "  move-const v0, 0\n" // res = 0
    "  move-const v1, 0\n" // i = 0
    "loop:\n"
    "  if-ge v1, p1, done\n"
    "  shl-int/64 v2, v1, 3\n"
    "  add-int/64 v2, p0, v2\n"
    "  load-mem v3, [v2 + 0]\n"
    "  xor-int/64 v0, v0, v3\n"
    "  add-int/64 v1, v1, 1\n"
    "  goto loop\n"
    "done:\n"
    "  return-val v0\n"
    ".end_fn\n";

static const char* code_lc18_majority_element =
    ".fn majority_element(p0: ptr, p1: i64) -> i64\n"
    "  .registers 5 local\n"
    "  move-const v0, 0\n" // candidate = 0
    "  move-const v1, 0\n" // count = 0
    "  move-const v2, 0\n" // i = 0
    "loop:\n"
    "  if-ge v2, p1, done\n"
    "  shl-int/64 v3, v2, 3\n"
    "  add-int/64 v3, p0, v3\n"
    "  load-mem v4, [v3 + 0]\n" // num = arr[i]
    "  if-ne v1, 0, check_match\n"
    "  move v0, v4\n"
    "  move-const v1, 1\n"
    "  goto next_iter\n"
    "check_match:\n"
    "  if-eq v4, v0, match\n"
    "  sub-int/64 v1, v1, 1\n"
    "  goto next_iter\n"
    "match:\n"
    "  add-int/64 v1, v1, 1\n"
    "next_iter:\n"
    "  add-int/64 v2, v2, 1\n"
    "  goto loop\n"
    "done:\n"
    "  return-val v0\n"
    ".end_fn\n";

static const char* code_lc19_excel_column =
    ".fn excel_column(p0: ptr) -> i64\n"
    "  .registers 5 local\n"
    "  move-const v0, 0\n" // result = 0
    "  move-const v1, 0\n" // idx = 0
    "loop:\n"
    "  add-int/64 v2, p0, v1\n"
    "  load-mem v3, [v2 + 0]\n"
    "  and-int/64 v3, v3, 255\n"
    "  if-eq v3, 0, done\n"
    "  sub-int/64 v4, v3, 64\n" // ch - 'A' + 1
    "  mul-int/64 v0, v0, 26\n"
    "  add-int/64 v0, v0, v4\n"
    "  add-int/64 v1, v1, 1\n"
    "  goto loop\n"
    "done:\n"
    "  return-val v0\n"
    ".end_fn\n";

static const char* code_lc20_happy_number =
    ".fn happy_number(p0: i64) -> i64\n"
    "  .registers 5 local\n"
    "  move v0, p0\n" // n
    "loop:\n"
    "  if-eq v0, 1, is_happy\n"
    "  if-eq v0, 4, not_happy\n" // 4 is known cycle start
    "  move-const v1, 0\n"       // sum = 0
    "sum_digits:\n"
    "  if-eq v0, 0, next_cycle\n"
    "  div-int/64 v2, v0, 10\n"
    "  mul-int/64 v3, v2, 10\n"
    "  sub-int/64 v3, v0, v3\n"  // d = n % 10
    "  mul-int/64 v4, v3, v3\n"  // d * d
    "  add-int/64 v1, v1, v4\n"
    "  move v0, v2\n"
    "  goto sum_digits\n"
    "next_cycle:\n"
    "  move v0, v1\n"
    "  goto loop\n"
    "is_happy:\n"
    "  move-const v0, 1\n"
    "  return-val v0\n"
    "not_happy:\n"
    "  move-const v0, 0\n"
    "  return-val v0\n"
    ".end_fn\n";

static const char* code_lc21_reverse_bits =
    ".fn reverse_bits(p0: i64) -> i64\n"
    "  .registers 5 local\n"
    "  move-const v0, 0\n" // rev = 0
    "  move-const v1, 0\n" // i = 0
    "loop:\n"
    "  if-ge v1, 32, done\n"
    "  shl-int/64 v0, v0, 1\n"
    "  ushr-int/64 v2, p0, v1\n"
    "  and-int/64 v3, v2, 1\n"
    "  or-int/64 v0, v0, v3\n"
    "  add-int/64 v1, v1, 1\n"
    "  goto loop\n"
    "done:\n"
    "  return-val v0\n"
    ".end_fn\n";

static const char* code_lc22_hamming_weight =
    ".fn hamming_weight(p0: i64) -> i64\n"
    "  .registers 1 local\n"
    "  popcount-int/64 v0, p0\n"
    "  return-val v0\n"
    ".end_fn\n";

static const char* code_lc23_power_of_two =
    ".fn power_of_two(p0: i64) -> i64\n"
    "  .registers 3 local\n"
    "  move-const v2, 0\n"
    "  if-ge v2, p0, is_false\n"
    "  sub-int/64 v0, p0, 1\n"
    "  and-int/64 v1, p0, v0\n"
    "  if-ne v1, 0, is_false\n"
    "  move-const v0, 1\n"
    "  return-val v0\n"
    "is_false:\n"
    "  move-const v0, 0\n"
    "  return-val v0\n"
    ".end_fn\n";

// -------------------------------------------------------------------
// LC 24: Move Zeroes
// p0: arr, p1: n
// -------------------------------------------------------------------
static const char* code_lc24_move_zeroes =
    ".fn move_zeroes(p0: ptr, p1: i64) -> i64\n"
    "  .registers 5 local\n"
    "  move-const v0, 0\n" // write_idx = 0
    "  move-const v1, 0\n" // read_idx = 0
    "loop:\n"
    "  if-ge v1, p1, fill_zeroes\n"
    "  shl-int/64 v2, v1, 3\n"
    "  add-int/64 v2, p0, v2\n"
    "  load-mem v3, [v2 + 0]\n"
    "  if-eq v3, 0, next_read\n"
    "  shl-int/64 v4, v0, 3\n"
    "  add-int/64 v4, p0, v4\n"
    "  store-mem v3, [v4 + 0]\n"
    "  add-int/64 v0, v0, 1\n"
    "next_read:\n"
    "  add-int/64 v1, v1, 1\n"
    "  goto loop\n"
    "fill_zeroes:\n"
    "  if-ge v0, p1, done\n"
    "  shl-int/64 v4, v0, 3\n"
    "  add-int/64 v4, p0, v4\n"
    "  move-const v3, 0\n"
    "  store-mem v3, [v4 + 0]\n"
    "  add-int/64 v0, v0, 1\n"
    "  goto fill_zeroes\n"
    "done:\n"
    "  return-val p1\n"
    ".end_fn\n";

// -------------------------------------------------------------------
// LC 25: Missing Number
// p0: arr, p1: n
// -------------------------------------------------------------------
static const char* code_lc25_missing_number =
    ".fn missing_number(p0: ptr, p1: i64) -> i64\n"
    "  .registers 5 local\n"
    "  add-int/64 v0, p1, 1\n"
    "  mul-int/64 v0, p1, v0\n"
    "  shr-int/64 v0, v0, 1\n" // expected_sum = n * (n + 1) / 2
    "  move-const v1, 0\n"     // actual_sum = 0
    "  move-const v2, 0\n"     // i = 0
    "loop:\n"
    "  if-ge v2, p1, done\n"
    "  shl-int/64 v3, v2, 3\n"
    "  add-int/64 v3, p0, v3\n"
    "  load-mem v4, [v3 + 0]\n"
    "  add-int/64 v1, v1, v4\n"
    "  add-int/64 v2, v2, 1\n"
    "  goto loop\n"
    "done:\n"
    "  sub-int/64 v0, v0, v1\n"
    "  return-val v0\n"
    ".end_fn\n";

// -------------------------------------------------------------------
// LC 26: Intersection of Two Arrays
// p0: arr1, p1: n1, p2: arr2, p3: n2. Returns count of matching elements
// -------------------------------------------------------------------
static const char* code_lc26_intersection_count =
    ".fn intersection_count(p0: ptr, p1: i64, p2: ptr, p3: i64) -> i64\n"
    "  .registers 7 local\n"
    "  move-const v0, 0\n" // count = 0
    "  move-const v1, 0\n" // i = 0
    "loop_i:\n"
    "  if-ge v1, p1, done\n"
    "  shl-int/64 v2, v1, 3\n"
    "  add-int/64 v2, p0, v2\n"
    "  load-mem v3, [v2 + 0]\n" // x = arr1[i]
    "  move-const v4, 0\n"     // j = 0
    "loop_j:\n"
    "  if-ge v4, p3, next_i\n"
    "  shl-int/64 v5, v4, 3\n"
    "  add-int/64 v5, p2, v5\n"
    "  load-mem v6, [v5 + 0]\n" // y = arr2[j]
    "  if-eq v3, v6, match\n"
    "  add-int/64 v4, v4, 1\n"
    "  goto loop_j\n"
    "match:\n"
    "  add-int/64 v0, v0, 1\n"
    "next_i:\n"
    "  add-int/64 v1, v1, 1\n"
    "  goto loop_i\n"
    "done:\n"
    "  return-val v0\n"
    ".end_fn\n";

// -------------------------------------------------------------------
// LC 27: First Unique Character in a String
// p0: str, p1: freq_table_buf (256 bytes)
// -------------------------------------------------------------------
static const char* code_lc27_first_uniq_char =
    ".fn first_uniq_char(p0: ptr, p1: ptr) -> i64\n"
    "  .registers 6 local\n"
    "  move-const v0, 0\n" // i = 0
    "zero_table:\n"
    "  if-ge v0, 256, build_freq\n"
    "  shl-int/64 v1, v0, 3\n"
    "  add-int/64 v1, p1, v1\n"
    "  move-const v2, 0\n"
    "  store-mem [v1 + 0], v2\n"
    "  add-int/64 v0, v0, 1\n"
    "  goto zero_table\n"
    "build_freq:\n"
    "  move-const v0, 0\n"
    "loop1:\n"
    "  add-int/64 v1, p0, v0\n"
    "  load-mem v2, [v1 + 0]\n"
    "  and-int/64 v2, v2, 255\n"
    "  if-eq v2, 0, find_uniq\n"
    "  shl-int/64 v3, v2, 3\n"
    "  add-int/64 v3, p1, v3\n"
    "  load-mem v4, [v3 + 0]\n"
    "  add-int/64 v4, v4, 1\n"
    "  store-mem [v3 + 0], v4\n"
    "  add-int/64 v0, v0, 1\n"
    "  goto loop1\n"
    "find_uniq:\n"
    "  move-const v0, 0\n"
    "loop2:\n"
    "  add-int/64 v1, p0, v0\n"
    "  load-mem v2, [v1 + 0]\n"
    "  and-int/64 v2, v2, 255\n"
    "  if-eq v2, 0, not_found\n"
    "  shl-int/64 v3, v2, 3\n"
    "  add-int/64 v3, p1, v3\n"
    "  load-mem v4, [v3 + 0]\n"
    "  if-eq v4, 1, found\n"
    "  add-int/64 v0, v0, 1\n"
    "  goto loop2\n"
    "found:\n"
    "  return-val v0\n"
    "not_found:\n"
    "  move-const v5, -1\n"
    "  return-val v5\n"
    ".end_fn\n";

static const char* code_lc28_fizz_buzz =
    ".fn fizz_buzz_count(p0: i64) -> i64\n"
    "  .registers 6 local\n"
    "  move-const v0, 0\n" // count = 0
    "  move-const v1, 1\n" // i = 1
    "loop:\n"
    "  if-ge v1, p0, check_last\n"
    "  goto check_i\n"
    "check_last:\n"
    "  if-ne v1, p0, done\n"
    "check_i:\n"
    "  div-int/64 v2, v1, 15\n"
    "  mul-int/64 v3, v2, 15\n"
    "  sub-int/64 v3, v1, v3\n"
    "  if-ne v3, 0, next_i\n"
    "  add-int/64 v0, v0, 1\n"
    "next_i:\n"
    "  add-int/64 v1, v1, 1\n"
    "  goto loop\n"
    "done:\n"
    "  return-val v0\n"
    ".end_fn\n";

static const char* code_lc29_power_of_three =
    ".fn power_of_three(p0: i64) -> i64\n"
    "  .registers 5 local\n"
    "  move-const v3, 0\n"
    "  if-ge v3, p0, is_false\n"
    "  move v0, p0\n"
    "loop:\n"
    "  if-eq v0, 1, is_true\n"
    "  div-int/64 v1, v0, 3\n"
    "  mul-int/64 v2, v1, 3\n"
    "  sub-int/64 v2, v0, v2\n" // rem = v0 % 3
    "  if-ne v2, 0, is_false\n"
    "  move v0, v1\n"
    "  goto loop\n"
    "is_true:\n"
    "  move-const v4, 1\n"
    "  return-val v4\n"
    "is_false:\n"
    "  move-const v4, 0\n"
    "  return-val v4\n"
    ".end_fn\n";

static const char* code_lc30_valid_anagram =
    ".fn valid_anagram(p0: ptr, p1: ptr, p2: ptr) -> i64\n"
    "  .registers 6 local\n"
    "  move-const v0, 0\n"
    "zero_tbl:\n"
    "  if-ge v0, 256, count_str1\n"
    "  shl-int/64 v1, v0, 3\n"
    "  add-int/64 v1, p2, v1\n"
    "  move-const v2, 0\n"
    "  store-mem [v1 + 0], v2\n"
    "  add-int/64 v0, v0, 1\n"
    "  goto zero_tbl\n"
    "count_str1:\n"
    "  move-const v0, 0\n"
    "loop1:\n"
    "  add-int/64 v1, p0, v0\n"
    "  load-mem v2, [v1 + 0]\n"
    "  and-int/64 v2, v2, 255\n"
    "  if-eq v2, 0, count_str2\n"
    "  shl-int/64 v3, v2, 3\n"
    "  add-int/64 v3, p2, v3\n"
    "  load-mem v4, [v3 + 0]\n"
    "  add-int/64 v4, v4, 1\n"
    "  store-mem [v3 + 0], v4\n"
    "  add-int/64 v0, v0, 1\n"
    "  goto loop1\n"
    "count_str2:\n"
    "  move-const v0, 0\n"
    "loop2:\n"
    "  add-int/64 v1, p1, v0\n"
    "  load-mem v2, [v1 + 0]\n"
    "  and-int/64 v2, v2, 255\n"
    "  if-eq v2, 0, verify_zeros\n"
    "  shl-int/64 v3, v2, 3\n"
    "  add-int/64 v3, p2, v3\n"
    "  load-mem v4, [v3 + 0]\n"
    "  sub-int/64 v4, v4, 1\n"
    "  store-mem [v3 + 0], v4\n"
    "  add-int/64 v0, v0, 1\n"
    "  goto loop2\n"
    "verify_zeros:\n"
    "  move-const v0, 0\n"
    "loop3:\n"
    "  if-ge v0, 256, is_valid\n"
    "  shl-int/64 v1, v0, 3\n"
    "  add-int/64 v1, p2, v1\n"
    "  load-mem v2, [v1 + 0]\n"
    "  if-ne v2, 0, invalid\n"
    "  add-int/64 v0, v0, 1\n"
    "  goto loop3\n"
    "is_valid:\n"
    "  move-const v5, 1\n"
    "  return-val v5\n"
    "invalid:\n"
    "  move-const v5, 0\n"
    "  return-val v5\n"
    ".end_fn\n";

// -------------------------------------------------------------------
// Master LeetCode Suite Execution
// -------------------------------------------------------------------
bool run_leetcode_tests() {
    print_lc("\n=======================================================\n");
    print_lc("    Anastasia Assembly LeetCode Test Suite (30 Problems)\n");
    print_lc("=======================================================\n");

    bool all_ok = true;

    // LC1
    all_ok &= run_single_lc_test("LC 1: Two Sum", code_lc1_two_sum, []() -> bool {
        int64_t arr[4] = { 2, 7, 11, 15 };
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_lc1_two_sum, arena);
        frontend::Program* prog = parser.parse_program();
        LcFnP3 fn = reinterpret_cast<LcFnP3>(lowerer.compile_function(prog->functions, prog));
        int64_t res = fn(reinterpret_cast<int64_t>(arr), 4, 9);
        return res == 1; // index 0 * 100 + index 1 = 1
    });

    // LC2
    all_ok &= run_single_lc_test("LC 2: Reverse Integer", code_lc2_reverse_integer, []() -> bool {
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_lc2_reverse_integer, arena);
        frontend::Program* prog = parser.parse_program();
        LcFnP1 fn = reinterpret_cast<LcFnP1>(lowerer.compile_function(prog->functions, prog));
        return fn(12345) == 54321;
    });

    // LC3
    all_ok &= run_single_lc_test("LC 3: Palindrome Number", code_lc3_palindrome_number, []() -> bool {
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_lc3_palindrome_number, arena);
        frontend::Program* prog = parser.parse_program();
        LcFnP1 fn = reinterpret_cast<LcFnP1>(lowerer.compile_function(prog->functions, prog));
        return fn(12321) == 1 && fn(12345) == 0;
    });

    // LC4
    all_ok &= run_single_lc_test("LC 4: Roman to Integer", code_lc4_roman_to_int, []() -> bool {
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_lc4_roman_to_int, arena);
        frontend::Program* prog = parser.parse_program();
        LcFnP1 fn = reinterpret_cast<LcFnP1>(lowerer.compile_function(prog->functions, prog));
        int64_t r1 = fn(reinterpret_cast<int64_t>("MCMXCIV"));
        int64_t r2 = fn(reinterpret_cast<int64_t>("LVIII"));
        if (r1 != 1994 || r2 != 58) {
            print_lc(" [r1="); print_int_lc(r1); print_lc(", r2="); print_int_lc(r2); print_lc("] ");
            return false;
        }
        return true;
    });

    // LC5
    all_ok &= run_single_lc_test("LC 5: Longest Common Prefix Length", code_lc5_common_prefix_len, []() -> bool {
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_lc5_common_prefix_len, arena);
        frontend::Program* prog = parser.parse_program();
        LcFnP2 fn = reinterpret_cast<LcFnP2>(lowerer.compile_function(prog->functions, prog));
        int64_t r1 = fn(reinterpret_cast<int64_t>("flower"), reinterpret_cast<int64_t>("flow"));
        int64_t r2 = fn(reinterpret_cast<int64_t>("flower"), reinterpret_cast<int64_t>("flight"));
        if (r1 != 4 || r2 != 2) {
            print_lc(" [r1="); print_int_lc(r1); print_lc(", r2="); print_int_lc(r2); print_lc("] ");
            return false;
        }
        return true;
    });

    // LC6
    all_ok &= run_single_lc_test("LC 6: Valid Parentheses", code_lc6_valid_parentheses, []() -> bool {
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_lc6_valid_parentheses, arena);
        frontend::Program* prog = parser.parse_program();
        if (!prog || !prog->functions) return false;
        LcFnP2 fn = reinterpret_cast<LcFnP2>(lowerer.compile_function(prog->functions, prog));
        if (!fn) return false;
        char stackbuf[64];
        int64_t r1 = fn(reinterpret_cast<int64_t>("{[()]}"), reinterpret_cast<int64_t>(stackbuf));
        int64_t r2 = fn(reinterpret_cast<int64_t>("{[(])}"), reinterpret_cast<int64_t>(stackbuf));
        if (r1 != 1 || r2 != 0) {
            print_lc(" [r1="); print_int_lc(r1); print_lc(", r2="); print_int_lc(r2); print_lc("] ");
            return false;
        }
        return true;
    });

    // LC7
    all_ok &= run_single_lc_test("LC 7: Merge Two Sorted Arrays", code_lc7_merge_sorted, []() -> bool {
        int64_t arr1[3] = { 1, 3, 5 };
        int64_t arr2[3] = { 2, 4, 6 };
        int64_t out[6] = { 0 };
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_lc7_merge_sorted, arena);
        frontend::Program* prog = parser.parse_program();
        if (!prog || !prog->functions) return false;
        typedef int64_t (*LcFnP5)(int64_t, int64_t, int64_t, int64_t, int64_t);
        LcFnP5 fn = reinterpret_cast<LcFnP5>(lowerer.compile_function(prog->functions, prog));
        if (!fn) return false;
        int64_t cnt = fn(reinterpret_cast<int64_t>(arr1), 3, reinterpret_cast<int64_t>(arr2), 3, reinterpret_cast<int64_t>(out));
        if (cnt != 6 || out[0] != 1 || out[1] != 2 || out[2] != 3 || out[3] != 4 || out[4] != 5 || out[5] != 6) {
            print_lc(" [cnt="); print_int_lc(cnt); print_lc("] ");
            return false;
        }
        return true;
    });

    // LC8
    all_ok &= run_single_lc_test("LC 8: Remove Duplicates from Sorted Array", code_lc8_remove_duplicates, []() -> bool {
        int64_t arr[7] = { 1, 1, 2, 2, 3, 4, 4 };
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_lc8_remove_duplicates, arena);
        frontend::Program* prog = parser.parse_program();
        LcFnP2 fn = reinterpret_cast<LcFnP2>(lowerer.compile_function(prog->functions, prog));
        int64_t new_len = fn(reinterpret_cast<int64_t>(arr), 7);
        if (new_len != 4) {
            print_lc(" [len="); print_int_lc(new_len); print_lc("] ");
            return false;
        }
        return true;
    });

    // LC9
    all_ok &= run_single_lc_test("LC 9: Remove Element", code_lc9_remove_element, []() -> bool {
        int64_t arr[4] = { 3, 2, 2, 3 };
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_lc9_remove_element, arena);
        frontend::Program* prog = parser.parse_program();
        LcFnP3 fn = reinterpret_cast<LcFnP3>(lowerer.compile_function(prog->functions, prog));
        int64_t new_len = fn(reinterpret_cast<int64_t>(arr), 4, 3);
        return new_len == 2 && arr[0] == 2 && arr[1] == 2;
    });

    // LC10
    all_ok &= run_single_lc_test("LC 10: Search Insert Position", code_lc10_search_insert, []() -> bool {
        int64_t arr[4] = { 1, 3, 5, 6 };
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_lc10_search_insert, arena);
        frontend::Program* prog = parser.parse_program();
        LcFnP3 fn = reinterpret_cast<LcFnP3>(lowerer.compile_function(prog->functions, prog));
        return fn(reinterpret_cast<int64_t>(arr), 4, 5) == 2 &&
               fn(reinterpret_cast<int64_t>(arr), 4, 2) == 1;
    });

    // LC11
    all_ok &= run_single_lc_test("LC 11: Maximum Subarray (Kadane)", code_lc11_max_subarray, []() -> bool {
        int64_t arr[9] = { -2, 1, -3, 4, -1, 2, 1, -5, 4 };
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_lc11_max_subarray, arena);
        frontend::Program* prog = parser.parse_program();
        LcFnP2 fn = reinterpret_cast<LcFnP2>(lowerer.compile_function(prog->functions, prog));
        return fn(reinterpret_cast<int64_t>(arr), 9) == 6;
    });

    // LC12
    all_ok &= run_single_lc_test("LC 12: Length of Last Word", code_lc12_length_last_word, []() -> bool {
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_lc12_length_last_word, arena);
        frontend::Program* prog = parser.parse_program();
        LcFnP1 fn = reinterpret_cast<LcFnP1>(lowerer.compile_function(prog->functions, prog));
        return fn(reinterpret_cast<int64_t>("Hello World Anastasia")) == 9;
    });

    // LC13
    all_ok &= run_single_lc_test("LC 13: Plus One", code_lc13_plus_one, []() -> bool {
        int64_t arr[3] = { 1, 2, 9 };
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_lc13_plus_one, arena);
        frontend::Program* prog = parser.parse_program();
        LcFnP2 fn = reinterpret_cast<LcFnP2>(lowerer.compile_function(prog->functions, prog));
        int64_t overflow = fn(reinterpret_cast<int64_t>(arr), 3);
        return overflow == 0 && arr[0] == 1 && arr[1] == 3 && arr[2] == 0;
    });

    // LC14
    all_ok &= run_single_lc_test("LC 14: Add Binary", code_lc14_add_binary, []() -> bool {
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_lc14_add_binary, arena);
        frontend::Program* prog = parser.parse_program();
        LcFnP2 fn = reinterpret_cast<LcFnP2>(lowerer.compile_function(prog->functions, prog));
        return fn(10, 11) == 21;
    });

    // LC15
    all_ok &= run_single_lc_test("LC 15: Climbing Stairs", code_lc15_climb_stairs, []() -> bool {
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_lc15_climb_stairs, arena);
        frontend::Program* prog = parser.parse_program();
        LcFnP1 fn = reinterpret_cast<LcFnP1>(lowerer.compile_function(prog->functions, prog));
        return fn(5) == 8;
    });

    // LC16
    all_ok &= run_single_lc_test("LC 16: Best Time to Buy and Sell Stock", code_lc16_max_profit, []() -> bool {
        int64_t arr[6] = { 7, 1, 5, 3, 6, 4 };
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_lc16_max_profit, arena);
        frontend::Program* prog = parser.parse_program();
        LcFnP2 fn = reinterpret_cast<LcFnP2>(lowerer.compile_function(prog->functions, prog));
        return fn(reinterpret_cast<int64_t>(arr), 6) == 5;
    });

    // LC17
    all_ok &= run_single_lc_test("LC 17: Single Number (XOR)", code_lc17_single_number, []() -> bool {
        int64_t arr[5] = { 4, 1, 2, 1, 2 };
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_lc17_single_number, arena);
        frontend::Program* prog = parser.parse_program();
        LcFnP2 fn = reinterpret_cast<LcFnP2>(lowerer.compile_function(prog->functions, prog));
        return fn(reinterpret_cast<int64_t>(arr), 5) == 4;
    });

    // LC18
    all_ok &= run_single_lc_test("LC 18: Majority Element (Boyer-Moore)", code_lc18_majority_element, []() -> bool {
        int64_t arr[7] = { 2, 2, 1, 1, 1, 2, 2 };
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_lc18_majority_element, arena);
        frontend::Program* prog = parser.parse_program();
        LcFnP2 fn = reinterpret_cast<LcFnP2>(lowerer.compile_function(prog->functions, prog));
        return fn(reinterpret_cast<int64_t>(arr), 7) == 2;
    });

    // LC19
    all_ok &= run_single_lc_test("LC 19: Excel Sheet Column Number", code_lc19_excel_column, []() -> bool {
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_lc19_excel_column, arena);
        frontend::Program* prog = parser.parse_program();
        LcFnP1 fn = reinterpret_cast<LcFnP1>(lowerer.compile_function(prog->functions, prog));
        return fn(reinterpret_cast<int64_t>("ZY")) == 701 && fn(reinterpret_cast<int64_t>("A")) == 1;
    });

    // LC20
    all_ok &= run_single_lc_test("LC 20: Happy Number", code_lc20_happy_number, []() -> bool {
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_lc20_happy_number, arena);
        frontend::Program* prog = parser.parse_program();
        LcFnP1 fn = reinterpret_cast<LcFnP1>(lowerer.compile_function(prog->functions, prog));
        return fn(19) == 1 && fn(2) == 0;
    });

    // LC21
    all_ok &= run_single_lc_test("LC 21: Reverse Bits", code_lc21_reverse_bits, []() -> bool {
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_lc21_reverse_bits, arena);
        frontend::Program* prog = parser.parse_program();
        LcFnP1 fn = reinterpret_cast<LcFnP1>(lowerer.compile_function(prog->functions, prog));
        return fn(0x12345678) == 0x1E6A2C48;
    });

    // LC22
    all_ok &= run_single_lc_test("LC 22: Number of 1 Bits (Hamming Weight)", code_lc22_hamming_weight, []() -> bool {
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_lc22_hamming_weight, arena);
        frontend::Program* prog = parser.parse_program();
        LcFnP1 fn = reinterpret_cast<LcFnP1>(lowerer.compile_function(prog->functions, prog));
        return fn(45) == 4; // 0b101101 -> 4 ones
    });

    // LC23
    all_ok &= run_single_lc_test("LC 23: Power of Two", code_lc23_power_of_two, []() -> bool {
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_lc23_power_of_two, arena);
        frontend::Program* prog = parser.parse_program();
        LcFnP1 fn = reinterpret_cast<LcFnP1>(lowerer.compile_function(prog->functions, prog));
        return fn(16) == 1 && fn(18) == 0;
    });

    // LC24
    all_ok &= run_single_lc_test("LC 24: Move Zeroes", code_lc24_move_zeroes, []() -> bool {
        int64_t arr[5] = { 0, 1, 0, 3, 12 };
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_lc24_move_zeroes, arena);
        frontend::Program* prog = parser.parse_program();
        LcFnP2 fn = reinterpret_cast<LcFnP2>(lowerer.compile_function(prog->functions, prog));
        fn(reinterpret_cast<int64_t>(arr), 5);
        return arr[0] == 1 && arr[1] == 3 && arr[2] == 12 && arr[3] == 0 && arr[4] == 0;
    });

    // LC25
    all_ok &= run_single_lc_test("LC 25: Missing Number", code_lc25_missing_number, []() -> bool {
        int64_t arr[3] = { 3, 0, 1 };
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_lc25_missing_number, arena);
        frontend::Program* prog = parser.parse_program();
        LcFnP2 fn = reinterpret_cast<LcFnP2>(lowerer.compile_function(prog->functions, prog));
        return fn(reinterpret_cast<int64_t>(arr), 3) == 2;
    });

    // LC26
    all_ok &= run_single_lc_test("LC 26: Intersection of Two Arrays", code_lc26_intersection_count, []() -> bool {
        int64_t arr1[4] = { 1, 2, 2, 1 };
        int64_t arr2[2] = { 2, 2 };
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_lc26_intersection_count, arena);
        frontend::Program* prog = parser.parse_program();
        typedef int64_t (*LcFnP4)(int64_t, int64_t, int64_t, int64_t);
        LcFnP4 fn = reinterpret_cast<LcFnP4>(lowerer.compile_function(prog->functions, prog));
        return fn(reinterpret_cast<int64_t>(arr1), 4, reinterpret_cast<int64_t>(arr2), 2) == 2;
    });

    // LC27
    all_ok &= run_single_lc_test("LC 27: First Unique Character in String", code_lc27_first_uniq_char, []() -> bool {
        // 256 eight-byte counters (the program indexes with shl-int/64 by 3),
        // not 256 bytes. This overflowed its frame by 1792 bytes.
        int64_t freqbuf[256];
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_lc27_first_uniq_char, arena);
        frontend::Program* prog = parser.parse_program();
        LcFnP2 fn = reinterpret_cast<LcFnP2>(lowerer.compile_function(prog->functions, prog));
        return fn(reinterpret_cast<int64_t>("leetcode"), reinterpret_cast<int64_t>(freqbuf)) == 0 &&
               fn(reinterpret_cast<int64_t>("loveleetcode"), reinterpret_cast<int64_t>(freqbuf)) == 2;
    });

    // LC28
    all_ok &= run_single_lc_test("LC 28: Fizz Buzz (Count Divisible by 15)", code_lc28_fizz_buzz, []() -> bool {
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_lc28_fizz_buzz, arena);
        frontend::Program* prog = parser.parse_program();
        LcFnP1 fn = reinterpret_cast<LcFnP1>(lowerer.compile_function(prog->functions, prog));
        return fn(15) == 1 && fn(30) == 2;
    });

    // LC29
    all_ok &= run_single_lc_test("LC 29: Power of Three", code_lc29_power_of_three, []() -> bool {
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_lc29_power_of_three, arena);
        frontend::Program* prog = parser.parse_program();
        LcFnP1 fn = reinterpret_cast<LcFnP1>(lowerer.compile_function(prog->functions, prog));
        return fn(27) == 1 && fn(45) == 0;
    });

    // LC30
    all_ok &= run_single_lc_test("LC 30: Valid Anagram", code_lc30_valid_anagram, []() -> bool {
        // 256 eight-byte counters (the program indexes with shl-int/64 by 3),
        // not 256 bytes. This overflowed its frame by 1792 bytes.
        int64_t freqbuf[256];
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_lc30_valid_anagram, arena);
        frontend::Program* prog = parser.parse_program();
        LcFnP3 fn = reinterpret_cast<LcFnP3>(lowerer.compile_function(prog->functions, prog));
        return fn(reinterpret_cast<int64_t>("anagram"), reinterpret_cast<int64_t>("nagaram"), reinterpret_cast<int64_t>(freqbuf)) == 1 &&
               fn(reinterpret_cast<int64_t>("rat"), reinterpret_cast<int64_t>("car"), reinterpret_cast<int64_t>(freqbuf)) == 0;
    });

    print_lc("=======================================================\n");
    if (all_ok) {
        print_lc("    ALL 30 LEETCODE PROBLEMS EXECUTED SUCCESSFULLY!\n");
    } else {
        print_lc("    LEETCODE SUITE FAILED\n");
    }
    print_lc("=======================================================\n\n");

    return all_ok;
}

} // namespace tests
} // namespace ana
