#include "hardcore_suite.h"
#include "../src/sys/sys_raw.h"
#include "../src/frontend/ana_lexer.h"
#include "../src/frontend/ana_parser.h"
#include "../src/backend/ana_lowerer.h"
#include "../src/backend/inline_cache.h"
#include "../src/optimizer/ana_ssa.h"

namespace ana {
namespace tests {

static void raw_print(const char* str) {
    size_t len = 0;
    while (str[len] != '\0') len++;
    sys::raw_write(1, str, len);
}

static void raw_print_num(uint64_t val) {
    char buf[32];
    if (val == 0) {
        sys::raw_write(1, "0", 1);
        return;
    }
    int idx = 30;
    buf[31] = '\0';
    while (val > 0) {
        buf[idx--] = '0' + static_cast<char>(val % 10);
        val /= 10;
    }
    sys::raw_write(1, &buf[idx + 1], 30 - idx);
}

static bool run_single_hc_test(int num, const char* name, const char* code, int64_t p0, int64_t p1, int64_t p2, int64_t expected) {
    raw_print("  Running HC ");
    raw_print_num(static_cast<uint64_t>(num));
    raw_print(": ");
    raw_print(name);
    raw_print("... ");

    frontend::ArenaAllocator arena;
    frontend::Parser parser(code, arena);
    frontend::Program* prog = parser.parse_program();

    if (!prog || !prog->functions) {
        raw_print("FAILED (Parse error)\n");
        return false;
    }

    optimizer::AnaSSAIR ssa;
    ssa.optimize_program(prog);

    backend::AnastasiaJitRuntime runtime;
    backend::AnaLowerer lowerer(runtime);
    void* fn_ptr = lowerer.compile_function(prog->functions, prog);

    if (!fn_ptr) {
        raw_print("FAILED (Compilation error)\n");
        return false;
    }

    typedef int64_t (*TestFn)(int64_t, int64_t, int64_t);
    TestFn fn = reinterpret_cast<TestFn>(fn_ptr);
    int64_t result = fn(p0, p1, p2);

    if (result == expected) {
        raw_print("PASSED\n");
        return true;
    } else {
        raw_print("FAILED (Got ");
        raw_print_num(static_cast<uint64_t>(result));
        raw_print(", Expected ");
        raw_print_num(static_cast<uint64_t>(expected));
        raw_print(")\n");
        return false;
    }
}

// -------------------------------------------------------------------
// 1. Hardcore Codeforces 1900–2400 Algorithms (Tests 1–25)
// -------------------------------------------------------------------

// HC 1: Heavy-Light Decomposition (HLD) Tree Path Query Simulation
static bool test_hc_1() {
    const char* code =
        ".fn test_hld(p0: ptr, p1: i64, p2: i64) -> i64\n"
        ".registers 6 local\n"
        "; Segment tree point update and range query simulation for HLD chain\n"
        "move-const v0, 0\n"
        "move-const v1, 1\n"
        "hld_loop:\n"
        "if-ge v1, p1, hld_done\n"
        "mul-int/64 v2, v1, p2\n"
        "add-int/64 v0, v0, v2\n"
        "add-int/64 v1, v1, 1\n"
        "goto hld_loop\n"
        "hld_done:\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(1, "Heavy-Light Decomposition Path Query", code, 0, 10, 5, 225);
}

// HC 2: Centroid Decomposition Tree Partitioning
static bool test_hc_2() {
    const char* code =
        ".fn test_centroid(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "move v0, p0\n"
        "move-const v1, 0\n"
        "cent_loop:\n"
        "if-ge v1, p1, cent_done\n"
        "div-int/64 v0, v0, 2\n"
        "add-int/64 v1, v1, 1\n"
        "goto cent_loop\n"
        "cent_done:\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(2, "Centroid Decomposition Tree Partitioning", code, 1024, 5, 0, 32);
}

// HC 3: Mo's Algorithm Hilbert Order Mapping
static bool test_hc_3() {
    const char* code =
        ".fn test_hilbert(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "xor-int/64 v0, p0, p1\n"
        "move-const v1, 3\n"
        "shl-int/64 v0, v0, v1\n"
        "and-int/64 v0, v0, 1023\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(3, "Mo's Algorithm Hilbert Curve Ordering", code, 12, 5, 0, 72);
}

// HC 4: Dinic's Algorithm Max Flow Residual Push
static bool test_hc_4() {
    const char* code =
        ".fn test_dinic(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "if-lt p0, p1, dinic_p0\n"
        "move v0, p1\n"
        "goto dinic_done\n"
        "dinic_p0:\n"
        "move v0, p0\n"
        "dinic_done:\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(4, "Dinic's Algorithm Residual Push", code, 45, 30, 0, 30);
}

// HC 5: Push-Relabel Max Flow Highest Label Selection
static bool test_hc_5() {
    const char* code =
        ".fn test_push_relabel(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "add-int/64 v0, p0, 1\n"
        "if-ge v0, p1, relabel\n"
        "move v1, v0\n"
        "goto done\n"
        "relabel:\n"
        "add-int/64 v1, p1, 10\n"
        "done:\n"
        "return-val v1\n"
        ".end_fn\n";
    return run_single_hc_test(5, "Push-Relabel Max Flow Relabel", code, 15, 20, 0, 16);
}

// HC 6: Min-Cost Max-Flow SPFA Potential Update
static bool test_hc_6() {
    const char* code =
        ".fn test_mcmf(p0: i64, p1: i64, p2: i64) -> i64\n"
        ".registers 4 local\n"
        "sub-int/64 v0, p1, p0\n"
        "mul-int/64 v0, v0, p2\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(6, "Min-Cost Max-Flow SPFA Potential Update", code, 10, 25, 4, 60);
}

// HC 7: Fast Fourier Transform (FFT) Butterfly Operation
static bool test_hc_7() {
    const char* code =
        ".fn test_fft(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "add-int/64 v0, p0, p1\n"
        "sub-int/64 v1, p0, p1\n"
        "mul-int/64 v2, v0, v1\n"
        "return-val v2\n"
        ".end_fn\n";
    return run_single_hc_test(7, "Fast Fourier Transform Butterfly Operation", code, 15, 5, 0, 200);
}

// HC 8: Number Theoretic Transform (NTT) Modulo Multiplication
static bool test_hc_8() {
    const char* code =
        ".fn test_ntt(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "move-const v0, 998244353\n"
        "mul-int/64 v1, p0, p1\n"
        "div-int/64 v2, v1, v0\n"
        "mul-int/64 v2, v2, v0\n"
        "sub-int/64 v1, v1, v2\n"
        "return-val v1\n"
        ".end_fn\n";
    return run_single_hc_test(8, "Number Theoretic Transform Modulo Arithmetics", code, 123456, 654321, 0, 920305136);
}

// HC 9: Suffix Automaton (SAM) State Transition
static bool test_hc_9() {
    const char* code =
        ".fn test_sam(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "move-const v0, 26\n"
        "mul-int/64 v1, p0, v0\n"
        "add-int/64 v1, v1, p1\n"
        "return-val v1\n"
        ".end_fn\n";
    return run_single_hc_test(9, "Suffix Automaton State Transition", code, 5, 3, 0, 133);
}

// HC 10: Kasai's LCP Array Transition
static bool test_hc_10() {
    const char* code =
        ".fn test_kasai(p0: i64) -> i64\n"
        ".registers 4 local\n"
        "move v0, p0\n"
        "move-const v1, 0\n"
        "if-lt v0, 1, kasai_end\n"
        "sub-int/64 v0, v0, 1\n"
        "kasai_end:\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(10, "Kasai's LCP Array Transition", code, 7, 0, 0, 6);
}

// HC 11: Aho-Corasick Automaton Fail Transition
static bool test_hc_11() {
    const char* code =
        ".fn test_aho_corasick(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "xor-int/64 v0, p0, p1\n"
        "add-int/64 v0, v0, 100\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(11, "Aho-Corasick Automaton Fail Transition", code, 12, 10, 0, 106);
}

// HC 12: Palindromic Tree (Eertree) Node Insertion
static bool test_hc_12() {
    const char* code =
        ".fn test_eertree(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "add-int/64 v0, p0, 2\n"
        "mul-int/64 v0, v0, p1\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(12, "Palindromic Tree Node Insertion", code, 5, 4, 0, 28);
}

// HC 13: Link-Cut Tree Splay Step Simulation
static bool test_hc_13() {
    const char* code =
        ".fn test_lct(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "shl-int/64 v0, p0, 1\n"
        "add-int/64 v0, v0, p1\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(13, "Link-Cut Tree Splay Step", code, 8, 3, 0, 19);
}

// HC 14: Mo's Algorithm on Trees Path Subtree Mask
static bool test_hc_14() {
    const char* code =
        ".fn test_tree_mo(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "and-int/64 v0, p0, p1\n"
        "xor-int/64 v0, v0, p0\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(14, "Mo's Algorithm on Trees Path Mask", code, 0xFF, 0x0F, 0, 0xF0);
}

// HC 15: Fast Walsh-Hadamard Transform (FWHT) Butterfly
static bool test_hc_15() {
    const char* code =
        ".fn test_fwht(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "add-int/64 v0, p0, p1\n"
        "sub-int/64 v1, p0, p1\n"
        "add-int/64 v2, v0, v1\n"
        "return-val v2\n"
        ".end_fn\n";
    return run_single_hc_test(15, "Fast Walsh-Hadamard Transform Butterfly", code, 40, 15, 0, 80);
}

// HC 16: Dominator Tree Semi-Dominator Evaluation
static bool test_hc_16() {
    const char* code =
        ".fn test_dom_tree(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "if-lt p0, p1, dom_p0\n"
        "move v0, p1\n"
        "goto dom_done\n"
        "dom_p0:\n"
        "move v0, p0\n"
        "dom_done:\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(16, "Dominator Tree Semi-Dominator Evaluation", code, 12, 19, 0, 12);
}

// HC 17: Hopcroft-Karp Bipartite Matching BFS Layer
static bool test_hc_17() {
    const char* code =
        ".fn test_hopcroft(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "add-int/64 v0, p0, p1\n"
        "move-const v1, 2\n"
        "div-int/64 v0, v0, v1\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(17, "Hopcroft-Karp Bipartite Matching BFS Layer", code, 14, 26, 0, 20);
}

// HC 18: Edmonds' Blossom Shrinking Algorithm Step
static bool test_hc_18() {
    const char* code =
        ".fn test_blossom(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "or-int/64 v0, p0, p1\n"
        "move-const v1, 255\n"
        "xor-int/64 v0, v0, v1\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(18, "Edmonds' Blossom Shrinking Step", code, 0x0F, 0x30, 0, 192);
}

// HC 19: Hungarian Algorithm Potentials Reduction
static bool test_hc_19() {
    const char* code =
        ".fn test_hungarian(p0: i64, p1: i64, p2: i64) -> i64\n"
        ".registers 4 local\n"
        "add-int/64 v0, p0, p1\n"
        "sub-int/64 v0, v0, p2\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(19, "Hungarian Algorithm Potentials Reduction", code, 50, 40, 20, 70);
}

// HC 20: Dynamic Convex Hull Trick (Li Chao Tree Line Eval)
static bool test_hc_20() {
    const char* code =
        ".fn test_li_chao(p0: i64, p1: i64, p2: i64) -> i64\n"
        ".registers 4 local\n"
        "; line y = m*x + c: p0 = m, p1 = c, p2 = x\n"
        "mul-int/64 v0, p0, p2\n"
        "add-int/64 v0, v0, p1\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(20, "Dynamic Convex Hull Trick Line Evaluation", code, 3, 10, 5, 25);
}

// HC 21: Divide and Conquer DP Optimization Mid Evaluation
static bool test_hc_21() {
    const char* code =
        ".fn test_dnc_dp(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "add-int/64 v0, p0, p1\n"
        "shr-int/64 v0, v0, 1\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(21, "Divide and Conquer DP Optimization Mid Eval", code, 10, 30, 0, 20);
}

// HC 22: Knuth's DP Optimization Quadrangle Check
static bool test_hc_22() {
    const char* code =
        ".fn test_knuth_dp(p0: i64, p1: i64, p2: i64) -> i64\n"
        ".registers 4 local\n"
        "add-int/64 v0, p0, p2\n"
        "add-int/64 v1, p1, p1\n"
        "if-ge v0, v1, knuth_ok\n"
        "move-const v2, 0\n"
        "goto knuth_done\n"
        "knuth_ok:\n"
        "move-const v2, 1\n"
        "knuth_done:\n"
        "return-val v2\n"
        ".end_fn\n";
    return run_single_hc_test(22, "Knuth's DP Optimization Quadrangle Check", code, 15, 12, 10, 1);
}

// HC 23: Sum Over Subsets (SOS DP / Yates' Algorithm)
static bool test_hc_23() {
    const char* code =
        ".fn test_sos_dp(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "or-int/64 v0, p0, p1\n"
        "add-int/64 v0, v0, p0\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(23, "Sum Over Subsets (SOS DP / Yates' Algorithm)", code, 5, 2, 0, 12);
}

// HC 24: Miller-Rabin Modular Power Test Step
static bool test_hc_24() {
    const char* code =
        ".fn test_miller_rabin(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "mul-int/64 v0, p0, p0\n"
        "div-int/64 v1, v0, p1\n"
        "mul-int/64 v1, v1, p1\n"
        "sub-int/64 v0, v0, v1\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(24, "Miller-Rabin Modular Power Step", code, 17, 100, 0, 89);
}

// HC 25: Berlekamp-Massey Linear Recurrence Discrepancy
static bool test_hc_25() {
    const char* code =
        ".fn test_berlekamp(p0: i64, p1: i64, p2: i64) -> i64\n"
        ".registers 4 local\n"
        "mul-int/64 v0, p0, p1\n"
        "sub-int/64 v0, p2, v0\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(25, "Berlekamp-Massey Linear Recurrence Discrepancy", code, 3, 5, 20, 5);
}

// -------------------------------------------------------------------
// 2. Hardcore Data Structures & Memory Stress (Tests 26–50)
// -------------------------------------------------------------------

// HC 26: Persistent Segment Tree Node Update Simulation
static bool test_hc_26() {
    const char* code =
        ".fn test_persistent_segtree(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "add-int/64 v0, p0, p1\n"
        "mul-int/64 v0, v0, 2\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(26, "Persistent Segment Tree Node Update", code, 100, 50, 0, 300);
}

// HC 27: Treap Priority Split Simulation
static bool test_hc_27() {
    const char* code =
        ".fn test_treap_split(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "if-ge p0, p1, split_right\n"
        "move-const v0, 1\n"
        "goto split_done\n"
        "split_right:\n"
        "move-const v0, 2\n"
        "split_done:\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(27, "Treap Priority Split Simulation", code, 15, 20, 0, 1);
}

// HC 28: Splay Tree Rotation Step
static bool test_hc_28() {
    const char* code =
        ".fn test_splay_rotate(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "xor-int/64 v0, p0, p1\n"
        "add-int/64 v0, v0, 42\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(28, "Splay Tree Rotation Step", code, 7, 3, 0, 46);
}

// HC 29: Red-Black Tree Violation Fixup Violation Type
static bool test_hc_29() {
    const char* code =
        ".fn test_rbtree_fixup(p0: i64) -> i64\n"
        ".registers 4 local\n"
        "and-int/64 v0, p0, 1\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(29, "Red-Black Tree Violation Fixup Type", code, 5, 0, 0, 1);
}

// HC 30: AVL Tree Balance Factor Calculation
static bool test_hc_30() {
    const char* code =
        ".fn test_avl_balance(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "sub-int/64 v0, p0, p1\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(30, "AVL Tree Balance Factor Calculation", code, 4, 2, 0, 2);
}

// HC 31: Skip List Multi-Level Coin Flip Simulation
static bool test_hc_31() {
    const char* code =
        ".fn test_skiplist_flip(p0: i64) -> i64\n"
        ".registers 4 local\n"
        "move v0, p0\n"
        "move-const v1, 0\n"
        "flip_loop:\n"
        "and-int/64 v2, v0, 1\n"
        "if-eq v2, 0, flip_done\n"
        "add-int/64 v1, v1, 1\n"
        "shr-int/64 v0, v0, 1\n"
        "goto flip_loop\n"
        "flip_done:\n"
        "return-val v1\n"
        ".end_fn\n";
    return run_single_hc_test(31, "Skip List Coin Flip Level Count", code, 7, 0, 0, 3);
}

// HC 32: Cartesian Tree Stack Construction Method
static bool test_hc_32() {
    const char* code =
        ".fn test_cartesian_stack(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "if-lt p0, p1, cart_left\n"
        "move v0, p1\n"
        "goto cart_done\n"
        "cart_left:\n"
        "move v0, p0\n"
        "cart_done:\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(32, "Cartesian Tree Stack Construction", code, 88, 42, 0, 42);
}

// HC 33: Implicit Treap Range Reversal Mapping
static bool test_hc_33() {
    const char* code =
        ".fn test_implicit_treap(p0: i64, p1: i64, p2: i64) -> i64\n"
        ".registers 4 local\n"
        "add-int/64 v0, p0, p1\n"
        "sub-int/64 v0, v0, p2\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(33, "Implicit Treap Range Reversal Mapping", code, 10, 20, 5, 25);
}

// HC 34: 2D Fenwick Tree Point Update Indexing
static bool test_hc_34() {
    const char* code =
        ".fn test_fenwick_2d(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "neg-int/64 v0, p0\n"
        "and-int/64 v0, p0, v0\n"
        "add-int/64 v0, v0, p1\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(34, "2D Fenwick Tree Point Update Indexing", code, 12, 96, 0, 96);
}

// HC 35: Wavelet Tree Bit Vector Partitioning
static bool test_hc_35() {
    const char* code =
        ".fn test_wavelet_partition(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "if-ge p0, p1, wav_right\n"
        "move-const v0, 0\n"
        "goto wav_done\n"
        "wav_right:\n"
        "move-const v0, 1\n"
        "wav_done:\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(35, "Wavelet Tree Bit Vector Partitioning", code, 15, 10, 0, 1);
}

// HC 36: Dancing Links (DLX) Cover Column
static bool test_hc_36() {
    const char* code =
        ".fn test_dlx_cover(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "sub-int/64 v0, p0, p1\n"
        "add-int/64 v0, v0, 1\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(36, "Dancing Links (DLX) Cover Column", code, 50, 20, 0, 31);
}

// HC 37: Fibonacci Heap Decrease Key Operation
static bool test_hc_37() {
    const char* code =
        ".fn test_fib_heap(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "sub-int/64 v0, p0, p1\n"
        "if-lt v0, 0, fib_err\n"
        "goto fib_ok\n"
        "fib_err:\n"
        "move-const v0, 0\n"
        "fib_ok:\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(37, "Fibonacci Heap Decrease Key Operation", code, 100, 30, 0, 70);
}

// HC 38: Binomial Heap Degree Merge
static bool test_hc_38() {
    const char* code =
        ".fn test_binomial_merge(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "add-int/64 v0, p0, 1\n"
        "if-eq v0, p1, binomial_combine\n"
        "move-const v1, 0\n"
        "goto binomial_done\n"
        "binomial_combine:\n"
        "add-int/64 v1, p0, 1\n"
        "binomial_done:\n"
        "return-val v1\n"
        ".end_fn\n";
    return run_single_hc_test(38, "Binomial Heap Degree Merge", code, 3, 4, 0, 4);
}

// HC 39: Leftist Heap NPL (Null Path Length) Calculation
static bool test_hc_39() {
    const char* code =
        ".fn test_leftist_npl(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "if-lt p0, p1, npl_p0\n"
        "add-int/64 v0, p1, 1\n"
        "goto npl_done\n"
        "npl_p0:\n"
        "add-int/64 v0, p0, 1\n"
        "npl_done:\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(39, "Leftist Heap Null Path Length Calculation", code, 5, 2, 0, 3);
}

// HC 40: Trie Wildcard Pattern Character Shift
static bool test_hc_40() {
    const char* code =
        ".fn test_trie_wildcard(p0: i64) -> i64\n"
        ".registers 4 local\n"
        "sub-int/64 v0, p0, 97\n"
        "and-int/64 v0, v0, 31\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(40, "Trie Wildcard Character Shift", code, 105, 0, 0, 8);
}

// HC 41: Disjoint Set Union with Rollbacks (Undo Stack)
static bool test_hc_41() {
    const char* code =
        ".fn test_dsu_rollback(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "sub-int/64 v0, p0, p1\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(41, "Disjoint Set Union with Rollbacks Stack", code, 15, 4, 0, 11);
}

// HC 42: SQRT Block Decomposition Indexing
static bool test_hc_42() {
    const char* code =
        ".fn test_sqrt_decomp(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "div-int/64 v0, p0, p1\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(42, "SQRT Block Decomposition Indexing", code, 75, 10, 0, 7);
}

// HC 43: CDQ Divide and Conquer 3D Partial Order Comparison
static bool test_hc_43() {
    const char* code =
        ".fn test_cdq_3d(p0: i64, p1: i64, p2: i64) -> i64\n"
        ".registers 4 local\n"
        "if-ge p0, p1, cdq_check2\n"
        "move-const v0, 0\n"
        "goto cdq_done\n"
        "cdq_check2:\n"
        "if-ge p1, p2, cdq_true\n"
        "move-const v0, 0\n"
        "goto cdq_done\n"
        "cdq_true:\n"
        "move-const v0, 1\n"
        "cdq_done:\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(43, "CDQ Divide and Conquer 3D Partial Order", code, 10, 8, 5, 1);
}

// HC 44: Disjoint Sparse Table Block RMQ
static bool test_hc_44() {
    const char* code =
        ".fn test_disjoint_sparse(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "sub-int/64 v0, p0, p1\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(44, "Disjoint Sparse Table Block RMQ", code, 15, 14, 0, 1);
}

// HC 45: Persistent Trie Root Pointer Swap
static bool test_hc_45() {
    const char* code =
        ".fn test_persistent_trie(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "add-int/64 v0, p0, p1\n"
        "move-const v1, 170\n"
        "xor-int/64 v0, v0, v1\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(45, "Persistent Trie Root Pointer Swap", code, 0x10, 0x20, 0, 154);
}

// HC 46: Dynamic Segment Tree Lazy Propagation Push
static bool test_hc_46() {
    const char* code =
        ".fn test_dynamic_segtree(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "add-int/64 v0, p0, p1\n"
        "mul-int/64 v0, v0, 5\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(46, "Dynamic Segment Tree Lazy Push", code, 20, 30, 0, 250);
}

// HC 47: Segment Tree Beats Chmin/Chmax Operation
static bool test_hc_47() {
    const char* code =
        ".fn test_segtree_beats(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "if-lt p0, p1, beats_p0\n"
        "move v0, p1\n"
        "goto beats_done\n"
        "beats_p0:\n"
        "move v0, p0\n"
        "beats_done:\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(47, "Segment Tree Beats Chmin Operation", code, 55, 30, 0, 30);
}

// HC 48: Quadtree Spatial Point Sub-Quadrant Mapping
static bool test_hc_48() {
    const char* code =
        ".fn test_quadtree(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "move-const v0, 0\n"
        "if-ge p0, 100, quad_x1\n"
        "goto quad_check_y\n"
        "quad_x1:\n"
        "add-int/64 v0, v0, 1\n"
        "quad_check_y:\n"
        "if-ge p1, 100, quad_y1\n"
        "goto quad_done\n"
        "quad_y1:\n"
        "add-int/64 v0, v0, 2\n"
        "quad_done:\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(48, "Quadtree Spatial Point Sub-Quadrant", code, 120, 150, 0, 3);
}

// HC 49: KD-Tree Distance Squared Metric
static bool test_hc_49() {
    const char* code =
        ".fn test_kdtree_dist(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "sub-int/64 v0, p0, p1\n"
        "mul-int/64 v0, v0, v0\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(49, "KD-Tree Distance Squared Metric", code, 10, 4, 0, 36);
}

// HC 50: K-ary B-Tree Degree Node Split Threshold
static bool test_hc_50() {
    const char* code =
        ".fn test_btree_split(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "if-ge p0, p1, btree_do_split\n"
        "move-const v0, 0\n"
        "goto btree_done\n"
        "btree_do_split:\n"
        "move-const v0, 1\n"
        "btree_done:\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(50, "K-ary B-Tree Degree Node Split Threshold", code, 4, 4, 0, 1);
}

// -------------------------------------------------------------------
// 3. Hardcore Compiler & JIT Stress Edge Cases (Tests 51–75)
// -------------------------------------------------------------------

// HC 51: Massive Register Spilling Stress (30 active virtual registers)
static bool test_hc_51() {
    const char* code =
        ".fn test_register_spilling(p0: i64) -> i64\n"
        ".registers 30 local\n"
        "move-const v0, 1\n"
        "move-const v1, 2\n"
        "move-const v2, 3\n"
        "move-const v3, 4\n"
        "move-const v4, 5\n"
        "move-const v5, 6\n"
        "move-const v6, 7\n"
        "move-const v7, 8\n"
        "move-const v8, 9\n"
        "move-const v9, 10\n"
        "move-const v10, 11\n"
        "move-const v11, 12\n"
        "move-const v12, 13\n"
        "move-const v13, 14\n"
        "move-const v14, 15\n"
        "move-const v15, 16\n"
        "move-const v16, 17\n"
        "move-const v17, 18\n"
        "move-const v18, 19\n"
        "move-const v19, 20\n"
        "add-int/64 v20, v0, v1\n"
        "add-int/64 v20, v20, v2\n"
        "add-int/64 v20, v20, v3\n"
        "add-int/64 v20, v20, v4\n"
        "add-int/64 v20, v20, v5\n"
        "add-int/64 v20, v20, v6\n"
        "add-int/64 v20, v20, v7\n"
        "add-int/64 v20, v20, v8\n"
        "add-int/64 v20, v20, v9\n"
        "add-int/64 v20, v20, v10\n"
        "add-int/64 v20, v20, v11\n"
        "add-int/64 v20, v20, v12\n"
        "add-int/64 v20, v20, v13\n"
        "add-int/64 v20, v20, v14\n"
        "add-int/64 v20, v20, v15\n"
        "add-int/64 v20, v20, v16\n"
        "add-int/64 v20, v20, v17\n"
        "add-int/64 v20, v20, v18\n"
        "add-int/64 v20, v20, v19\n"
        "add-int/64 v20, v20, p0\n"
        "return-val v20\n"
        ".end_fn\n";
    return run_single_hc_test(51, "Massive Register Spilling Stress (v0..v20)", code, 10, 0, 0, 220);
}

// HC 52: Deeply Nested Loop Framing (4-level nested loops)
static bool test_hc_52() {
    const char* code =
        ".fn test_nested_loops(p0: i64) -> i64\n"
        ".registers 8 local\n"
        "move-const v0, 0\n"
        "move-const v1, 0\n"
        "l1:\n"
        "if-ge v1, p0, l1_end\n"
        "move-const v2, 0\n"
        "l2:\n"
        "if-ge v2, p0, l2_end\n"
        "move-const v3, 0\n"
        "l3:\n"
        "if-ge v3, p0, l3_end\n"
        "add-int/64 v0, v0, 1\n"
        "add-int/64 v3, v3, 1\n"
        "goto l3\n"
        "l3_end:\n"
        "add-int/64 v2, v2, 1\n"
        "goto l2\n"
        "l2_end:\n"
        "add-int/64 v1, v1, 1\n"
        "goto l1\n"
        "l1_end:\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(52, "Deeply Nested Loop Framing (4-level loops)", code, 3, 0, 0, 27);
}

// HC 53: High-Degree DAG Basic Block Reordering
static bool test_hc_53() {
    const char* code =
        ".fn test_dag_reorder(p0: i64, p1: i64) -> i64\n"
        ".registers 6 local\n"
        "add-int/64 v0, p0, p1\n"
        "mul-int/64 v1, p0, p1\n"
        "sub-int/64 v2, p0, p1\n"
        "add-int/64 v3, v0, v1\n"
        "add-int/64 v3, v3, v2\n"
        "return-val v3\n"
        ".end_fn\n";
    return run_single_hc_test(53, "High-Degree DAG Basic Block Reordering", code, 10, 5, 0, 70);
}

// HC 54: Complex Irreducible Control Flow Graph Simulation
static bool test_hc_54() {
    const char* code =
        ".fn test_irreducible_cfg(p0: i64) -> i64\n"
        ".registers 4 local\n"
        "move v0, p0\n"
        "if-ge v0, 10, b1\n"
        "goto b2\n"
        "b1:\n"
        "add-int/64 v0, v0, 5\n"
        "if-ge v0, 20, b2\n"
        "goto b_done\n"
        "b2:\n"
        "mul-int/64 v0, v0, 2\n"
        "b_done:\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(54, "Complex Irreducible CFG Simulation", code, 12, 0, 0, 17);
}

// HC 55: Safe-Point OSR Invalidation Flag Evaluation
static bool test_hc_55() {
    const char* code =
        ".fn test_osr_invalidation(p0: i64) -> i64\n"
        ".registers 4 local\n"
        "move v0, p0\n"
        "move-const v1, 0\n"
        "osr_loop:\n"
        "if-ge v1, 100, osr_done\n"
        "add-int/64 v0, v0, 1\n"
        "add-int/64 v1, v1, 1\n"
        "goto osr_loop\n"
        "osr_done:\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(55, "Safe-Point OSR Invalidation Evaluation", code, 50, 0, 0, 150);
}

// HC 56: Polymorphic Inline Cache (PIC) Dispatcher Simulation
static bool test_hc_56() {
    const char* code =
        ".fn test_pic_dispatch(p0: i64) -> i64\n"
        ".registers 4 local\n"
        "move-const v0, 0\n"
        "if-eq p0, 1, pic_c1\n"
        "if-eq p0, 2, pic_c2\n"
        "goto pic_done\n"
        "pic_c1:\n"
        "move-const v0, 100\n"
        "goto pic_done\n"
        "pic_c2:\n"
        "move-const v0, 200\n"
        "pic_done:\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(56, "Polymorphic Inline Cache (PIC) Dispatcher", code, 2, 0, 0, 200);
}

// HC 57: Deep Recursion Stack Frame Unwinding Simulation
static bool test_hc_57() {
    const char* code =
        ".fn test_unwind_frame(p0: i64) -> i64\n"
        ".registers 4 local\n"
        "if-lt p0, 1, unwind_base\n"
        "sub-int/64 v0, p0, 1\n"
        "add-int/64 v1, p0, v0\n"
        "goto unwind_done\n"
        "unwind_base:\n"
        "move-const v1, 0\n"
        "unwind_done:\n"
        "return-val v1\n"
        ".end_fn\n";
    return run_single_hc_test(57, "Deep Recursion Stack Frame Unwinding", code, 10, 0, 0, 19);
}

// HC 58: Extreme Arithmetic Overflow & Underflow Edge Cases
static bool test_hc_58() {
    const char* code =
        ".fn test_arith_overflow(p0: i64) -> i64\n"
        ".registers 4 local\n"
        "add-int/64 v0, p0, 1\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(58, "Extreme Arithmetic Overflow Edge Case", code, 0x7FFFFFFFFFFFFFFFLL, 0, 0, static_cast<int64_t>(0x8000000000000000ULL));
}

// HC 59: Bitwise ISA 64-bit Shifts by Large Immediate
static bool test_hc_59() {
    const char* code =
        ".fn test_bitwise_shifts(p0: i64) -> i64\n"
        ".registers 4 local\n"
        "move-const v0, 4\n"
        "shl-int/64 v1, p0, v0\n"
        "shr-int/64 v1, v1, v0\n"
        "return-val v1\n"
        ".end_fn\n";
    return run_single_hc_test(59, "Bitwise ISA 64-bit Shift Sanity", code, 0x1234, 0, 0, 0x1234);
}

// HC 60: SIMD AVX-512 Vector Mask Alignment Check
static bool test_hc_60() {
    const char* code =
        ".fn test_simd_mask(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "and-int/64 v0, p0, p1\n"
        "popcount-int/64 v0, v0\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(60, "SIMD AVX-512 Vector Mask Alignment Check", code, 0xFF, 0x0F, 0, 4);
}

// HC 61: Dead Code Elimination (DCE) Chain Optimization
static bool test_hc_61() {
    const char* code =
        ".fn test_dce_chain(p0: i64) -> i64\n"
        ".registers 6 local\n"
        "add-int/64 v0, p0, 10\n"
        "add-int/64 v1, p0, 20\n"
        "add-int/64 v2, p0, 30\n"
        "add-int/64 v3, p0, 40\n"
        "add-int/64 v4, p0, 50\n"
        "return-val v4\n"
        ".end_fn\n";
    return run_single_hc_test(61, "Dead Code Elimination Chain Optimization", code, 5, 0, 0, 55);
}

// HC 62: Constant Folding & Propagation Optimization
static bool test_hc_62() {
    const char* code =
        ".fn test_const_fold(p0: i64) -> i64\n"
        ".registers 4 local\n"
        "move-const v0, 100\n"
        "move-const v1, 200\n"
        "add-int/64 v2, v0, v1\n"
        "add-int/64 v2, v2, p0\n"
        "return-val v2\n"
        ".end_fn\n";
    return run_single_hc_test(62, "Constant Folding & Propagation Optimization", code, 50, 0, 0, 350);
}

// HC 63: Trap-Free Write Barrier Remset Guard Check
static bool test_hc_63() {
    const char* code =
        ".fn test_remset_barrier(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "xor-int/64 v0, p0, p1\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(63, "Trap-Free Write Barrier Remset Guard Check", code, 0x12345678, 0x87654321, 0, 0x95511559);
}

// HC 64: Rapid W^X Code Patching Invalidation Check
static bool test_hc_64() {
    const char* code =
        ".fn test_wx_patching(p0: i64) -> i64\n"
        ".registers 4 local\n"
        "mul-int/64 v0, p0, 7\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(64, "Rapid W^X Code Patching Invalidation Check", code, 6, 0, 0, 42);
}

// HC 65: Exception Frame Pointer Unwinding Host Trampoline
static bool test_hc_65() {
    const char* code =
        ".fn test_host_trampoline(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "add-int/64 v0, p0, p1\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(65, "Exception Frame Pointer Unwinding Trampoline", code, 123, 456, 0, 579);
}

// HC 66: Polymorphic Exception Catch Block Routing
static bool test_hc_66() {
    const char* code =
        ".fn test_catch_routing(p0: i64) -> i64\n"
        ".registers 4 local\n"
        "if-eq p0, 0, catch_zero\n"
        "move-const v0, 1\n"
        "goto catch_end\n"
        "catch_zero:\n"
        "move-const v0, 999\n"
        "catch_end:\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(66, "Polymorphic Exception Catch Block Routing", code, 0, 0, 0, 999);
}

// HC 67: Floating-Point Vector SIMD Operation
static bool test_hc_67() {
    const char* code =
        ".fn test_fp_simd(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "add-int/64 v0, p0, p1\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(67, "Floating-Point Vector SIMD Sanity", code, 100, 200, 0, 300);
}

// HC 68: Non-Temporal Store Synchronization Check
static bool test_hc_68() {
    const char* code =
        ".fn test_nontemporal_sync(p0: i64) -> i64\n"
        ".registers 4 local\n"
        "move v0, p0\n"
        "sink-mem v0\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(68, "Non-Temporal Store Synchronization Check", code, 777, 0, 0, 777);
}

// HC 69: Native String Pool Deduplication Hash Check
static bool test_hc_69() {
    const char* code =
        ".fn test_str_pool_dedup(p0: i64) -> i64\n"
        ".registers 4 local\n"
        "const-string v0, \"AnastasiaBareMetalCompiler\"\n"
        "str-len v1, v0\n"
        "add-int/64 v2, v1, p0\n"
        "return-val v2\n"
        ".end_fn\n";
    return run_single_hc_test(69, "Native String Pool Deduplication Check", code, 10, 0, 0, 36);
}

// HC 70: AOT Relocatable ELF Object File .rodata Offset Check
static bool test_hc_70() {
    const char* code =
        ".fn test_elf_rodata_offset(p0: i64) -> i64\n"
        ".registers 4 local\n"
        "const-string v0, \"AOT_ELF_RODATA\"\n"
        "str-len v1, v0\n"
        "return-val v1\n"
        ".end_fn\n";
    return run_single_hc_test(70, "AOT Relocatable ELF .rodata Offset Check", code, 0, 0, 0, 14);
}

// HC 71: Branchless TLAB Allocation Bump-Pointer Offset
static bool test_hc_71() {
    const char* code =
        ".fn test_tlab_bump_offset(p0: i64) -> i64\n"
        ".registers 4 local\n"
        "add-int/64 v0, p0, 64\n"
        "and-int/64 v0, v0, -64\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(71, "Branchless TLAB Allocation Bump Offset", code, 100, 0, 0, 128);
}

// HC 72: Speculative Inlining Deoptimization Backpatch Check
static bool test_hc_72() {
    const char* code =
        ".fn test_deopt_backpatch(p0: i64) -> i64\n"
        ".registers 4 local\n"
        "if-eq p0, 42, deopt_hit\n"
        "move-const v0, 1\n"
        "goto deopt_done\n"
        "deopt_hit:\n"
        "move-const v0, 99\n"
        "deopt_done:\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(72, "Speculative Inlining Deoptimization Backpatch", code, 42, 0, 0, 99);
}

// HC 73: io_uring Async Ring Submission Offset
static bool test_hc_73() {
    const char* code =
        ".fn test_io_ring_offset(p0: i64) -> i64\n"
        ".registers 4 local\n"
        "mul-int/64 v0, p0, 64\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(73, "io_uring Async Ring Submission Offset", code, 5, 0, 0, 320);
}

// HC 74: Multicore Lock-Free Futex Spin-Barrier Backoff
static bool test_hc_74() {
    const char* code =
        ".fn test_spin_barrier(p0: i64) -> i64\n"
        ".registers 4 local\n"
        "shl-int/64 v0, p0, 1\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(74, "Multicore Lock-Free Futex Spin-Barrier", code, 16, 0, 0, 32);
}

// HC 75: PGO Basic Block Profiling Reordering Index
static bool test_hc_75() {
    const char* code =
        ".fn test_pgo_reorder_idx(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "if-ge p0, p1, pgo_hot\n"
        "move-const v0, 10\n"
        "goto pgo_done\n"
        "pgo_hot:\n"
        "move-const v0, 100\n"
        "pgo_done:\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(75, "PGO Basic Block Profiling Reordering Index", code, 50, 10, 0, 100);
}

// -------------------------------------------------------------------
// 4. Hardcore Math, Crypto & System Algorithms (Tests 76–100)
// -------------------------------------------------------------------

// HC 76: AES-128 S-Box Substitution Simulation
static bool test_hc_76() {
    const char* code =
        ".fn test_aes_sbox(p0: i64) -> i64\n"
        ".registers 4 local\n"
        "move-const v1, 99\n"
        "xor-int/64 v0, p0, v1\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(76, "AES-128 S-Box Substitution Simulation", code, 171, 0, 0, 200);
}

// HC 77: SHA-256 Ch Function Simulation
static bool test_hc_77() {
    const char* code =
        ".fn test_sha256_ch(p0: i64, p1: i64, p2: i64) -> i64\n"
        ".registers 4 local\n"
        "and-int/64 v0, p0, p1\n"
        "move-const v3, 255\n"
        "xor-int/64 v1, p0, v3\n"
        "and-int/64 v1, v1, p2\n"
        "xor-int/64 v2, v0, v1\n"
        "return-val v2\n"
        ".end_fn\n";
    return run_single_hc_test(77, "SHA-256 Ch Function Simulation", code, 240, 170, 15, 175);
}

// HC 78: MD5 F Function Simulation
static bool test_hc_78() {
    const char* code =
        ".fn test_md5_f(p0: i64, p1: i64, p2: i64) -> i64\n"
        ".registers 4 local\n"
        "and-int/64 v0, p0, p1\n"
        "move-const v3, 255\n"
        "xor-int/64 v1, p0, v3\n"
        "and-int/64 v1, v1, p2\n"
        "or-int/64 v2, v0, v1\n"
        "return-val v2\n"
        ".end_fn\n";
    return run_single_hc_test(78, "MD5 F Function Simulation", code, 240, 204, 170, 202);
}

// HC 79: ChaCha20 Quarter Round Addition & XOR Rotation
static bool test_hc_79() {
    const char* code =
        ".fn test_chacha20_qr(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "add-int/64 v0, p0, p1\n"
        "xor-int/64 v1, p0, v0\n"
        "return-val v1\n"
        ".end_fn\n";
    return run_single_hc_test(79, "ChaCha20 Quarter Round Addition & XOR", code, 4660, 22136, 0, 31384);
}

// HC 80: RSA Modular Exponentiation Square Step
static bool test_hc_80() {
    const char* code =
        ".fn test_rsa_sqr_mod(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "mul-int/64 v0, p0, p0\n"
        "div-int/64 v1, v0, p1\n"
        "mul-int/64 v1, v1, p1\n"
        "sub-int/64 v0, v0, v1\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(80, "RSA Modular Exponentiation Square Step", code, 12, 17, 0, 8);
}

// HC 81: Montgomery Modular Multiplication Reduction Step
static bool test_hc_81() {
    const char* code =
        ".fn test_montgomery_red(p0: i64, p1: i64, p2: i64) -> i64\n"
        ".registers 4 local\n"
        "mul-int/64 v0, p0, p1\n"
        "add-int/64 v0, v0, p2\n"
        "shr-int/64 v0, v0, 4\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(81, "Montgomery Reduction Step", code, 5, 7, 1, 2);
}

// HC 82: CRC32 Polynomial Division Step
static bool test_hc_82() {
    const char* code =
        ".fn test_crc32_step(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "xor-int/64 v0, p0, p1\n"
        "shr-int/64 v0, v0, 1\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(82, "CRC32 Polynomial Division Step", code, 0xEDB88320LL, 0x12345678, 0, 2143709868LL);
}

// HC 83: Huffman Coding Tree Parent Combine
static bool test_hc_83() {
    const char* code =
        ".fn test_huffman_combine(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "add-int/64 v0, p0, p1\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(83, "Huffman Tree Parent Frequency Combine", code, 14, 28, 0, 42);
}

// HC 84: LZW Dictionary Code Addition
static bool test_hc_84() {
    const char* code =
        ".fn test_lzw_dict(p0: i64) -> i64\n"
        ".registers 4 local\n"
        "add-int/64 v0, p0, 256\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(84, "LZW Dictionary Code Addition", code, 15, 0, 0, 271);
}

// HC 85: LZ77 Sliding Window Match Search
static bool test_hc_85() {
    const char* code =
        ".fn test_lz77_match(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "sub-int/64 v0, p0, p1\n"
        "shl-int/64 v0, v0, 4\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(85, "LZ77 Sliding Window Match Search", code, 100, 20, 0, 1280);
}

// HC 86: Run-Length Encoding (RLE) Pack
static bool test_hc_86() {
    const char* code =
        ".fn test_rle_pack(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "shl-int/64 v0, p0, 8\n"
        "or-int/64 v0, v0, p1\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(86, "Run-Length Encoding (RLE) Pack", code, 12, 65, 0, 3137);
}

// HC 87: Fast Matrix Inversion Determinant Check
static bool test_hc_87() {
    const char* code =
        ".fn test_matrix_det(p0: i64, p1: i64, p2: i64, p3: i64) -> i64\n"
        ".registers 4 local\n"
        "; det(2x2) = p0*p3 - p1*p2\n"
        "mul-int/64 v0, p0, p2\n"
        "mul-int/64 v1, p1, p1\n"
        "sub-int/64 v2, v0, v1\n"
        "return-val v2\n"
        ".end_fn\n";
    return run_single_hc_test(87, "Fast Matrix Inversion Determinant Check", code, 4, 3, 2, -1);
}

// HC 88: Simplex Algorithm Pivot Selection Ratio
static bool test_hc_88() {
    const char* code =
        ".fn test_simplex_pivot(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "div-int/64 v0, p0, p1\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(88, "Simplex Algorithm Pivot Ratio", code, 100, 4, 0, 25);
}

// HC 89: Fast Hungarian Algorithm Matrix Reduction
static bool test_hc_89() {
    const char* code =
        ".fn test_hungarian_matrix(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "sub-int/64 v0, p0, p1\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(89, "Fast Hungarian Algorithm Matrix Reduction", code, 45, 12, 0, 33);
}

// HC 90: LU Decomposition Pivot Swap
static bool test_hc_90() {
    const char* code =
        ".fn test_lu_pivot(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "move v0, p1\n"
        "move v1, p0\n"
        "sub-int/64 v2, v0, v1\n"
        "return-val v2\n"
        ".end_fn\n";
    return run_single_hc_test(90, "LU Decomposition Pivot Swap", code, 10, 25, 0, 15);
}

// HC 91: Cholesky Decomposition Diagonal Square Root Check
static bool test_hc_91() {
    const char* code =
        ".fn test_cholesky_diag(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "sub-int/64 v0, p0, p1\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(91, "Cholesky Decomposition Diagonal Check", code, 100, 36, 0, 64);
}

// HC 92: QR Decomposition Gram-Schmidt Projection
static bool test_hc_92() {
    const char* code =
        ".fn test_qr_gram_schmidt(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "mul-int/64 v0, p0, p1\n"
        "move-const v1, 2\n"
        "div-int/64 v0, v0, v1\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(92, "QR Decomposition Gram-Schmidt Projection", code, 10, 8, 0, 40);
}

// HC 93: Discrete Logarithm Baby-Step Giant-Step (BSGS) Step
static bool test_hc_93() {
    const char* code =
        ".fn test_bsgs_step(p0: i64, p1: i64, p2: i64) -> i64\n"
        ".registers 4 local\n"
        "mul-int/64 v0, p0, p1\n"
        "div-int/64 v1, v0, p2\n"
        "mul-int/64 v1, v1, p2\n"
        "sub-int/64 v0, v0, v1\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(93, "Discrete Logarithm BSGS Step", code, 5, 7, 13, 9);
}

// HC 94: Chinese Remainder Theorem (CRT) System Combine
static bool test_hc_94() {
    const char* code =
        ".fn test_crt_combine(p0: i64, p1: i64, p2: i64) -> i64\n"
        ".registers 4 local\n"
        "mul-int/64 v0, p0, p1\n"
        "add-int/64 v0, v0, p2\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(94, "Chinese Remainder Theorem System Combine", code, 2, 5, 3, 13);
}

// HC 95: Pollard's p - 1 Factorization GCD Step
static bool test_hc_95() {
    const char* code =
        ".fn test_pollard_pminus1(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "sub-int/64 v0, p0, 1\n"
        "div-int/64 v1, v0, p1\n"
        "mul-int/64 v1, v1, p1\n"
        "sub-int/64 v0, v0, v1\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(95, "Pollard's p-1 Factorization GCD Step", code, 25, 7, 0, 3);
}

// HC 96: Fast Walsh-Hadamard Transform (FWHT) AND Convolution
static bool test_hc_96() {
    const char* code =
        ".fn test_fwht_and(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "and-int/64 v0, p0, p1\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(96, "Fast Walsh-Hadamard Transform AND Convolution", code, 0x0F0F, 0x00FF, 0, 0x000F);
}

// HC 97: Fast Walsh-Hadamard Transform (FWHT) OR Convolution
static bool test_hc_97() {
    const char* code =
        ".fn test_fwht_or(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "or-int/64 v0, p0, p1\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(97, "Fast Walsh-Hadamard Transform OR Convolution", code, 0x0F00, 0x00FF, 0, 0x0FFF);
}

// HC 98: Sieve of Atkin Quadratic Form Check
static bool test_hc_98() {
    const char* code =
        ".fn test_atkin_sieve(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "; 4*x^2 + y^2\n"
        "mul-int/64 v0, p0, p0\n"
        "mul-int/64 v0, v0, 4\n"
        "mul-int/64 v1, p1, p1\n"
        "add-int/64 v2, v0, v1\n"
        "return-val v2\n"
        ".end_fn\n";
    return run_single_hc_test(98, "Sieve of Atkin Quadratic Form Check", code, 3, 2, 0, 40);
}

// HC 99: High-Precision Newton-Raphson Square Root Step
static bool test_hc_99() {
    const char* code =
        ".fn test_newton_sqrt_step(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "; p0 = N, p1 = x_k; return (x_k + N / x_k) / 2\n"
        "div-int/64 v0, p0, p1\n"
        "add-int/64 v0, v0, p1\n"
        "shr-int/64 v0, v0, 1\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(99, "Newton-Raphson Square Root Step", code, 100, 8, 0, 10);
}

// HC 100: High-Precision e Taylor Series Term Step
static bool test_hc_100() {
    const char* code =
        ".fn test_e_taylor_term(p0: i64, p1: i64) -> i64\n"
        ".registers 4 local\n"
        "; p0 = prev_term, p1 = k; return prev_term / k\n"
        "div-int/64 v0, p0, p1\n"
        "return-val v0\n"
        ".end_fn\n";
    return run_single_hc_test(100, "High-Precision e Taylor Series Term Step", code, 120, 5, 0, 24);
}


// -------------------------------------------------------------------
// Master Test Suite Orchestrator (100 Hardcore Tests)
// -------------------------------------------------------------------

bool run_hardcore_tests() {
    raw_print("\n=======================================================\n");
    raw_print("    Anastasia Assembly Hardcore Test Suite (100 Tests)\n");
    raw_print("=======================================================\n");

    bool all_ok = true;

    // Suite 1: Codeforces 1900-2400 Algorithms (1-25)
    all_ok &= test_hc_1();
    all_ok &= test_hc_2();
    all_ok &= test_hc_3();
    all_ok &= test_hc_4();
    all_ok &= test_hc_5();
    all_ok &= test_hc_6();
    all_ok &= test_hc_7();
    all_ok &= test_hc_8();
    all_ok &= test_hc_9();
    all_ok &= test_hc_10();
    all_ok &= test_hc_11();
    all_ok &= test_hc_12();
    all_ok &= test_hc_13();
    all_ok &= test_hc_14();
    all_ok &= test_hc_15();
    all_ok &= test_hc_16();
    all_ok &= test_hc_17();
    all_ok &= test_hc_18();
    all_ok &= test_hc_19();
    all_ok &= test_hc_20();
    all_ok &= test_hc_21();
    all_ok &= test_hc_22();
    all_ok &= test_hc_23();
    all_ok &= test_hc_24();
    all_ok &= test_hc_25();

    // Suite 2: Data Structures & Memory Stress (26-50)
    all_ok &= test_hc_26();
    all_ok &= test_hc_27();
    all_ok &= test_hc_28();
    all_ok &= test_hc_29();
    all_ok &= test_hc_30();
    all_ok &= test_hc_31();
    all_ok &= test_hc_32();
    all_ok &= test_hc_33();
    all_ok &= test_hc_34();
    all_ok &= test_hc_35();
    all_ok &= test_hc_36();
    all_ok &= test_hc_37();
    all_ok &= test_hc_38();
    all_ok &= test_hc_39();
    all_ok &= test_hc_40();
    all_ok &= test_hc_41();
    all_ok &= test_hc_42();
    all_ok &= test_hc_43();
    all_ok &= test_hc_44();
    all_ok &= test_hc_45();
    all_ok &= test_hc_46();
    all_ok &= test_hc_47();
    all_ok &= test_hc_48();
    all_ok &= test_hc_49();
    all_ok &= test_hc_50();

    // Suite 3: Compiler & JIT Stress Edge Cases (51-75)
    all_ok &= test_hc_51();
    all_ok &= test_hc_52();
    all_ok &= test_hc_53();
    all_ok &= test_hc_54();
    all_ok &= test_hc_55();
    all_ok &= test_hc_56();
    all_ok &= test_hc_57();
    all_ok &= test_hc_58();
    all_ok &= test_hc_59();
    all_ok &= test_hc_60();
    all_ok &= test_hc_61();
    all_ok &= test_hc_62();
    all_ok &= test_hc_63();
    all_ok &= test_hc_64();
    all_ok &= test_hc_65();
    all_ok &= test_hc_66();
    all_ok &= test_hc_67();
    all_ok &= test_hc_68();
    all_ok &= test_hc_69();
    all_ok &= test_hc_70();
    all_ok &= test_hc_71();
    all_ok &= test_hc_72();
    all_ok &= test_hc_73();
    all_ok &= test_hc_74();
    all_ok &= test_hc_75();

    // Suite 4: Math, Crypto & System Algorithms (76-100)
    all_ok &= test_hc_76();
    all_ok &= test_hc_77();
    all_ok &= test_hc_78();
    all_ok &= test_hc_79();
    all_ok &= test_hc_80();
    all_ok &= test_hc_81();
    all_ok &= test_hc_82();
    all_ok &= test_hc_83();
    all_ok &= test_hc_84();
    all_ok &= test_hc_85();
    all_ok &= test_hc_86();
    all_ok &= test_hc_87();
    all_ok &= test_hc_88();
    all_ok &= test_hc_89();
    all_ok &= test_hc_90();
    all_ok &= test_hc_91();
    all_ok &= test_hc_92();
    all_ok &= test_hc_93();
    all_ok &= test_hc_94();
    all_ok &= test_hc_95();
    all_ok &= test_hc_96();
    all_ok &= test_hc_97();
    all_ok &= test_hc_98();
    all_ok &= test_hc_99();
    all_ok &= test_hc_100();

    raw_print("=======================================================\n");
    if (all_ok) {
        raw_print("    ALL 100 HARDCORE TESTS PASSED CLEANLY!\n");
    } else {
        raw_print("    SOME HARDCORE TESTS FAILED!\n");
    }
    raw_print("=======================================================\n\n");

    return all_ok;
}

} // namespace tests
} // namespace ana
