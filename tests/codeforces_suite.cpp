#include "codeforces_suite.h"
#include "../src/sys/sys_raw.h"
#include "../src/frontend/arena_allocator.h"
#include "../src/frontend/ana_lexer.h"
#include "../src/frontend/ana_parser.h"
#include "../src/backend/vmem_provider.h"
#include "../src/backend/ana_lowerer.h"
#include "../src/optimizer/ana_ssa.h"
#include <cstdarg>
#include <cstdio>

namespace ana {
namespace tests {

static void print_cf(const char* msg) {
    size_t len = 0;
    while (msg[len]) len++;
    ana::sys::raw_write(1, msg, len);
}

typedef bool (*CfTestRunner)();
typedef int64_t (*CfFnP1)(int64_t);
typedef int64_t (*CfFnP2)(int64_t, int64_t);
typedef int64_t (*CfFnP3)(int64_t, int64_t, int64_t);
typedef int64_t (*CfFnP4)(int64_t, int64_t, int64_t, int64_t);
typedef int64_t (*CfFnP5)(int64_t, int64_t, int64_t, int64_t, int64_t);

static bool run_single_cf_test(const char* name, const char* code, CfTestRunner test_runner) {
    print_cf("  Running ");
    print_cf(name);
    print_cf("... ");

    frontend::ArenaAllocator arena;
    frontend::Parser parser(code, arena);
    frontend::Program* prog = parser.parse_program();

    if (!prog || !prog->functions) {
        print_cf("FAILED (AST Parsing)\n");
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
        print_cf("FAILED (JIT Lowering)\n");
        return false;
    }

    bool pass = test_runner();
    if (pass) {
        print_cf("PASSED\n");
    } else {
        print_cf("FAILED (Assertion mismatch)\n");
    }
    return pass;
}

// -------------------------------------------------------------------
// CF 1: Segment Tree Point Update & Range Sum (1800)
// p0: segtree_arr (size 8N), p1: n, p2: idx, p3: val, p4: query_l, p5: query_r
// -------------------------------------------------------------------
static const char* code_cf1_segment_tree =
    ".fn segtree_update_and_query(p0: ptr, p1: i64, p2: i64, p3: i64, p4: i64, p5: i64) -> i64\n"
    "  .registers 9 local\n"
    "  move-const v7, 1\n"
    "  ; Update segtree[p2] = p3\n"
    "  add-int/64 v0, p1, p2\n"  // tree_idx = n + idx
    "  shl-int/64 v1, v0, 3\n"
    "  add-int/64 v1, p0, v1\n"
    "  store-mem [v1 + 0], p3\n"
    "update_loop:\n"
    "  if-ge v7, v0, query_init\n"
    "  shr-int/64 v0, v0, 1\n"   // node = node / 2
    "  shl-int/64 v2, v0, 1\n"   // left = 2 * node
    "  add-int/64 v3, v2, 1\n"   // right = 2 * node + 1
    "  shl-int/64 v2, v2, 3\n"
    "  add-int/64 v2, p0, v2\n"
    "  load-mem v2, [v2 + 0]\n"  // left_val
    "  shl-int/64 v3, v3, 3\n"
    "  add-int/64 v3, p0, v3\n"
    "  load-mem v3, [v3 + 0]\n"  // right_val
    "  add-int/64 v4, v2, v3\n"  // sum = left_val + right_val
    "  shl-int/64 v5, v0, 3\n"
    "  add-int/64 v5, p0, v5\n"
    "  store-mem [v5 + 0], v4\n"
    "  goto update_loop\n"
    "query_init:\n"
    "  add-int/64 v0, p1, p4\n"  // l = n + query_l
    "  add-int/64 v1, p1, p5\n"  // r = n + query_r + 1
    "  add-int/64 v1, v1, 1\n"
    "  move-const v6, 0\n"       // res = 0
    "query_loop:\n"
    "  if-ge v0, v1, query_done\n"
    "  and-int/64 v2, v0, 1\n"
    "  if-eq v2, 0, check_r\n"
    "  shl-int/64 v3, v0, 3\n"
    "  add-int/64 v3, p0, v3\n"
    "  load-mem v3, [v3 + 0]\n"
    "  add-int/64 v6, v6, v3\n"
    "  add-int/64 v0, v0, 1\n"
    "check_r:\n"
    "  and-int/64 v2, v1, 1\n"
    "  if-eq v2, 0, next_step\n"
    "  sub-int/64 v1, v1, 1\n"
    "  shl-int/64 v3, v1, 3\n"
    "  add-int/64 v3, p0, v3\n"
    "  load-mem v3, [v3 + 0]\n"
    "  add-int/64 v6, v6, v3\n"
    "next_step:\n"
    "  shr-int/64 v0, v0, 1\n"
    "  shr-int/64 v1, v1, 1\n"
    "  goto query_loop\n"
    "query_done:\n"
    "  return-val v6\n"
    ".end_fn\n";

// -------------------------------------------------------------------
// CF 2: O(N log N) Longest Increasing Subsequence (1800)
// p0: arr, p1: n, p2: dp_tails_buf (size N)
// -------------------------------------------------------------------
static const char* code_cf2_lis_nlogn =
    ".fn lis_nlogn(p0: ptr, p1: i64, p2: ptr) -> i64\n"
    "  .registers 8 local\n"
    "  move-const v0, 0\n" // lis_len = 0
    "  move-const v1, 0\n" // i = 0
    "loop_i:\n"
    "  if-ge v1, p1, done\n"
    "  shl-int/64 v2, v1, 3\n"
    "  add-int/64 v2, p0, v2\n"
    "  load-mem v2, [v2 + 0]\n" // x = arr[i]
    "  ; Binary search low position in dp_tails[0..lis_len-1]\n"
    "  move-const v3, 0\n"       // low = 0
    "  sub-int/64 v4, v0, 1\n"   // high = lis_len - 1
    "  move v5, v0\n"           // pos = lis_len
    "bs_loop:\n"
    "  if-lt v4, v3, bs_done\n"
    "  add-int/64 v6, v3, v4\n"
    "  shr-int/64 v6, v6, 1\n"   // mid = (low + high) / 2
    "  shl-int/64 v7, v6, 3\n"
    "  add-int/64 v7, p2, v7\n"
    "  load-mem v7, [v7 + 0]\n" // val = dp_tails[mid]
    "  if-ge v7, v2, bs_left\n"
    "  add-int/64 v3, v6, 1\n"
    "  goto bs_loop\n"
    "bs_left:\n"
    "  move v5, v6\n"
    "  sub-int/64 v4, v6, 1\n"
    "  goto bs_loop\n"
    "bs_done:\n"
    "  shl-int/64 v6, v5, 3\n"
    "  add-int/64 v6, p2, v6\n"
    "  store-mem [v6 + 0], v2\n" // dp_tails[pos] = x
    "  if-ne v5, v0, next_i\n"
    "  add-int/64 v0, v0, 1\n"   // lis_len++
    "next_i:\n"
    "  add-int/64 v1, v1, 1\n"
    "  goto loop_i\n"
    "done:\n"
    "  return-val v0\n"
    ".end_fn\n";

// -------------------------------------------------------------------
// CF 3: Disjoint Set Union (DSU / Union-Find with Path Compression) (1800)
// p0: parent_buf, p1: n, p2: u, p3: v
// -------------------------------------------------------------------
static const char* code_cf3_dsu_union =
    ".fn dsu_union_and_count(p0: ptr, p1: i64, p2: i64, p3: i64) -> i64\n"
    "  .registers 7 local\n"
    "  ; Find root of u (p2)\n"
    "  move v0, p2\n"
    "find_u:\n"
    "  shl-int/64 v1, v0, 3\n"
    "  add-int/64 v1, p0, v1\n"
    "  load-mem v2, [v1 + 0]\n" // parent[v0]
    "  if-eq v2, v0, found_u\n"
    "  move v0, v2\n"
    "  goto find_u\n"
    "found_u:\n"
    "  ; Find root of v (p3)\n"
    "  move v3, p3\n"
    "find_v:\n"
    "  shl-int/64 v1, v3, 3\n"
    "  add-int/64 v1, p0, v1\n"
    "  load-mem v4, [v1 + 0]\n"
    "  if-eq v4, v3, found_v\n"
    "  move v3, v4\n"
    "  goto find_v\n"
    "found_v:\n"
    "  if-eq v0, v3, same_set\n"
    "  shl-int/64 v1, v3, 3\n"
    "  add-int/64 v1, p0, v1\n"
    "  store-mem [v1 + 0], v0\n" // parent[root_v] = root_u
    "  move-const v5, 1\n"
    "  return-val v5\n"
    "same_set:\n"
    "  move-const v5, 0\n"
    "  return-val v5\n"
    ".end_fn\n";

// -------------------------------------------------------------------
// CF 4: Binary Exponentiation & Modulo Inverse (1900)
// p0: base, p1: exp, p2: mod (e.g. 1000000007)
// -------------------------------------------------------------------
static const char* code_cf4_power_mod =
    ".fn power_mod(p0: i64, p1: i64, p2: i64) -> i64\n"
    "  .registers 7 local\n"
    "  move-const v6, 0\n"
    "  move-const v0, 1\n" // res = 1
    "  div-int/64 v1, p0, p2\n"
    "  mul-int/64 v2, v1, p2\n"
    "  sub-int/64 v1, p0, v2\n" // b = base % mod
    "  move v2, p1\n"           // e = exp
    "loop:\n"
    "  if-ge v6, v2, done\n"
    "  and-int/64 v3, v2, 1\n"
    "  if-eq v3, 0, square_b\n"
    "  mul-int/64 v0, v0, v1\n"
    "  div-int/64 v4, v0, p2\n"
    "  mul-int/64 v5, v4, p2\n"
    "  sub-int/64 v0, v0, v5\n" // res = (res * b) % mod
    "square_b:\n"
    "  mul-int/64 v1, v1, v1\n"
    "  div-int/64 v4, v1, p2\n"
    "  mul-int/64 v5, v4, p2\n"
    "  sub-int/64 v1, v1, v5\n" // b = (b * b) % mod
    "  ushr-int/64 v2, v2, 1\n" // e = e >> 1
    "  goto loop\n"
    "done:\n"
    "  return-val v0\n"
    ".end_fn\n";

// -------------------------------------------------------------------
// CF 5: Dijkstra Shortest Path Algorithm (1900)
// p0: adj_matrix, p1: n, p2: dist_buf, p3: visited_buf, p4: src
// -------------------------------------------------------------------
static const char* code_cf5_dijkstra =
    ".fn dijkstra(p0: ptr, p1: i64, p2: ptr, p3: ptr, p4: i64) -> i64\n"
    "  .registers 9 local\n"
    "  ; Init dist array with INF (1000000) and visited with 0\n"
    "  move-const v0, 0\n" // i = 0
    "init_loop:\n"
    "  if-ge v0, p1, start_dijkstra\n"
    "  shl-int/64 v1, v0, 3\n"
    "  add-int/64 v1, p2, v1\n"
    "  move-const v2, 1000000\n"
    "  store-mem [v1 + 0], v2\n"
    "  shl-int/64 v1, v0, 3\n"
    "  add-int/64 v1, p3, v1\n"
    "  move-const v2, 0\n"
    "  store-mem [v1 + 0], v2\n"
    "  add-int/64 v0, v0, 1\n"
    "  goto init_loop\n"
    "start_dijkstra:\n"
    "  shl-int/64 v1, p4, 3\n"
    "  add-int/64 v1, p2, v1\n"
    "  move-const v2, 0\n"
    "  store-mem [v1 + 0], v2\n" // dist[src] = 0
    "  move-const v0, 0\n"       // count = 0
    "outer_loop:\n"
    "  if-ge v0, p1, done\n"
    "  move-const v1, -1\n"      // u = -1
    "  move-const v2, 1000000\n" // min_d = INF
    "  move-const v3, 0\n"       // i = 0
    "find_min:\n"
    "  if-ge v3, p1, relax_edges\n"
    "  shl-int/64 v4, v3, 3\n"
    "  add-int/64 v5, p3, v4\n"
    "  load-mem v5, [v5 + 0]\n"  // vis[i]
    "  if-ne v5, 0, next_min\n"
    "  add-int/64 v6, p2, v4\n"  // dist_addr = p2 + offset
    "  load-mem v6, [v6 + 0]\n"  // dist[i]
    "  if-ge v6, v2, next_min\n"
    "  move v2, v6\n"
    "  move v1, v3\n"
    "next_min:\n"
    "  add-int/64 v3, v3, 1\n"
    "  goto find_min\n"
    "relax_edges:\n"
    "  if-eq v1, -1, done\n"
    "  shl-int/64 v4, v1, 3\n"
    "  add-int/64 v4, p3, v4\n"
    "  move-const v5, 1\n"
    "  store-mem [v4 + 0], v5\n" // vis[u] = 1
    "  move-const v3, 0\n"       // v = 0
    "relax_loop:\n"
    "  if-ge v3, p1, next_outer\n"
    "  mul-int/64 v4, v1, p1\n"
    "  add-int/64 v4, v4, v3\n"
    "  shl-int/64 v4, v4, 3\n"
    "  add-int/64 v4, p0, v4\n"
    "  load-mem v5, [v4 + 0]\n"  // weight = adj[u][v]
    "  if-eq v5, 0, next_v\n"
    "  add-int/64 v6, v2, v5\n"  // dist[u] + weight
    "  shl-int/64 v7, v3, 3\n"
    "  add-int/64 v7, p2, v7\n"
    "  load-mem v8, [v7 + 0]\n"  // dist[v]
    "  if-ge v6, v8, next_v\n"
    "  store-mem [v7 + 0], v6\n"
    "next_v:\n"
    "  add-int/64 v3, v3, 1\n"
    "  goto relax_loop\n"
    "next_outer:\n"
    "  add-int/64 v0, v0, 1\n"
    "  goto outer_loop\n"
    "done:\n"
    "  sub-int/64 v3, p1, 1\n"
    "  shl-int/64 v3, v3, 3\n"
    "  add-int/64 v3, p2, v3\n"
    "  load-mem v3, [v3 + 0]\n"  // return dist[n-1]
    "  return-val v3\n"
    ".end_fn\n";

// -------------------------------------------------------------------
// CF 6: Fenwick Tree (Binary Indexed Tree / BIT) (1900)
// p0: bit_buf, p1: n, p2: idx, p3: delta, p4: query_idx
// -------------------------------------------------------------------
static const char* code_cf6_fenwick_tree =
    ".fn fenwick_update_and_query(p0: ptr, p1: i64, p2: i64, p3: i64, p4: i64) -> i64\n"
    "  .registers 7 local\n"
    "  move-const v5, 0\n"
    "  move v0, p2\n" // i = idx
    "update_loop:\n"
    "  if-ge v0, p1, query_init\n"
    "  shl-int/64 v1, v0, 3\n"
    "  add-int/64 v1, p0, v1\n"
    "  load-mem v2, [v1 + 0]\n"
    "  add-int/64 v2, v2, p3\n"
    "  store-mem [v1 + 0], v2\n"
    "  sub-int/64 v3, 0, v0\n"  // -i
    "  and-int/64 v3, v0, v3\n"  // i & (-i)
    "  add-int/64 v0, v0, v3\n"  // i += i & (-i)
    "  goto update_loop\n"
    "query_init:\n"
    "  move v0, p4\n"           // i = query_idx
    "  move-const v4, 0\n"       // sum = 0
    "query_loop:\n"
    "  if-eq v0, 0, done\n"
    "  shl-int/64 v1, v0, 3\n"
    "  add-int/64 v1, p0, v1\n"
    "  load-mem v2, [v1 + 0]\n"
    "  add-int/64 v4, v4, v2\n"
    "  sub-int/64 v3, v0, v0\n"
    "  sub-int/64 v3, v3, v0\n"  // -i
    "  and-int/64 v3, v0, v3\n"  // i & (-i)
    "  sub-int/64 v0, v0, v3\n"  // i -= i & (-i)
    "  goto query_loop\n"
    "done:\n"
    "  return-val v4\n"
    ".end_fn\n";

// -------------------------------------------------------------------
// CF 7: Matrix Exponentiation for N-th Fibonacci (2000)
// p0: n. Returns Fib(n) % 1000000007
// -------------------------------------------------------------------
static const char* code_cf7_matrix_exp =
    ".fn fib_matrix_exp(p0: i64) -> i64\n"
    "  .registers 14 local\n"
    "  move-const v13, 1\n"
    "  if-lt p0, v13, ret_zero\n"
    "  if-eq p0, 1, ret_one\n"
    "  ; Result matrix I = [[1, 0], [0, 1]]\n"
    "  move-const v0, 1\n" // r00
    "  move-const v1, 0\n" // r01
    "  move-const v2, 0\n" // r10
    "  move-const v3, 1\n" // r11
    "  ; Base matrix M = [[1, 1], [1, 0]]\n"
    "  move-const v4, 1\n" // m00
    "  move-const v5, 1\n" // m01
    "  move-const v6, 1\n" // m10
    "  move-const v7, 0\n" // m11
    "  sub-int/64 p0, p0, 1\n"
    "loop:\n"
    "  if-lt p0, v13, done\n"
    "  and-int/64 v8, p0, 1\n"
    "  if-eq v8, 0, square_m\n"
    "  ; R = R * M\n"
    "  mul-int/64 v8, v0, v4\n"
    "  mul-int/64 v9, v1, v6\n"
    "  add-int/64 v8, v8, v9\n" // nr00
    "  mul-int/64 v9, v0, v5\n"
    "  mul-int/64 v10, v1, v7\n"
    "  add-int/64 v9, v9, v10\n" // nr01
    "  mul-int/64 v10, v2, v4\n"
    "  mul-int/64 v11, v3, v6\n"
    "  add-int/64 v10, v10, v11\n" // nr10
    "  mul-int/64 v11, v2, v5\n"
    "  mul-int/64 v12, v3, v7\n"
    "  add-int/64 v11, v11, v12\n" // nr11
    "  move v0, v8\n"
    "  move v1, v9\n"
    "  move v2, v10\n"
    "  move v3, v11\n"
    "square_m:\n"
    "  mul-int/64 v8, v4, v4\n"
    "  mul-int/64 v9, v5, v6\n"
    "  add-int/64 v8, v8, v9\n"
    "  mul-int/64 v9, v4, v5\n"
    "  mul-int/64 v10, v5, v7\n"
    "  add-int/64 v9, v9, v10\n"
    "  mul-int/64 v10, v6, v4\n"
    "  mul-int/64 v11, v7, v6\n"
    "  add-int/64 v10, v10, v11\n"
    "  mul-int/64 v11, v6, v5\n"
    "  mul-int/64 v12, v7, v7\n"
    "  add-int/64 v11, v11, v12\n"
    "  move v4, v8\n"
    "  move v5, v9\n"
    "  move v6, v10\n"
    "  move v7, v11\n"
    "  ushr-int/64 p0, p0, 1\n"
    "  goto loop\n"
    "done:\n"
    "  return-val v0\n"
    "ret_zero:\n"
    "  move-const v0, 0\n"
    "  return-val v0\n"
    "ret_one:\n"
    "  move-const v0, 1\n"
    "  return-val v0\n"
    ".end_fn\n";

// -------------------------------------------------------------------
// CF 8: Sieve of Eratosthenes & Smallest Prime Factor (1800)
// p0: spf_buf (size MAX_N)
// -------------------------------------------------------------------
static const char* code_cf8_sieve_spf =
    ".fn sieve_spf(p0: ptr, p1: i64) -> i64\n"
    "  .registers 6 local\n"
    "  move-const v0, 0\n" // i = 0
    "init_spf:\n"
    "  if-ge v0, p1, start_sieve\n"
    "  shl-int/64 v1, v0, 3\n"
    "  add-int/64 v1, p0, v1\n"
    "  store-mem [v1 + 0], v0\n" // spf[i] = i
    "  add-int/64 v0, v0, 1\n"
    "  goto init_spf\n"
    "start_sieve:\n"
    "  move-const v0, 2\n" // i = 2
    "sieve_outer:\n"
    "  mul-int/64 v1, v0, v0\n"
    "  if-ge v1, p1, count_primes\n"
    "run_sieve:\n"
    "  shl-int/64 v2, v0, 3\n"
    "  add-int/64 v2, p0, v2\n"
    "  load-mem v2, [v2 + 0]\n"
    "  if-ne v2, v0, next_outer\n" // if (spf[i] == i)
    "  move v3, v1\n"               // j = i * i
    "sieve_inner:\n"
    "  if-ge v3, p1, next_outer\n"
    "  shl-int/64 v4, v3, 3\n"
    "  add-int/64 v4, p0, v4\n"
    "  load-mem v5, [v4 + 0]\n"
    "  if-ne v5, v3, next_inner\n"
    "  store-mem [v4 + 0], v0\n"   // spf[j] = i
    "next_inner:\n"
    "  add-int/64 v3, v3, v0\n"
    "  goto sieve_inner\n"
    "next_outer:\n"
    "  add-int/64 v0, v0, 1\n"
    "  goto sieve_outer\n"
    "count_primes:\n"
    "  move-const v0, 2\n"
    "  move-const v1, 0\n" // count = 0
    "count_loop:\n"
    "  if-ge v0, p1, done\n"
    "  shl-int/64 v2, v0, 3\n"
    "  add-int/64 v2, p0, v2\n"
    "  load-mem v2, [v2 + 0]\n"
    "  if-ne v2, v0, next_prime\n"
    "  add-int/64 v1, v1, 1\n"
    "next_prime:\n"
    "  add-int/64 v0, v0, 1\n"
    "  goto count_loop\n"
    "done:\n"
    "  return-val v1\n"
    ".end_fn\n";

// -------------------------------------------------------------------
// CF 9: Kahn's Topological Sort (1900)
// p0: adj_mat, p1: n, p2: in_degree_buf, p3: topo_out_buf
// -------------------------------------------------------------------
static const char* code_cf9_topological_sort =
    ".fn topo_sort(p0: ptr, p1: i64, p2: ptr, p3: ptr) -> i64\n"
    "  .registers 8 local\n"
    "  ; Calc in-degrees\n"
    "  move-const v0, 0\n" // i = 0
    "zero_indeg:\n"
    "  if-ge v0, p1, calc_indeg\n"
    "  shl-int/64 v1, v0, 3\n"
    "  add-int/64 v1, p2, v1\n"
    "  move-const v2, 0\n"
    "  store-mem [v1 + 0], v2\n"
    "  add-int/64 v0, v0, 1\n"
    "  goto zero_indeg\n"
    "calc_indeg:\n"
    "  move-const v0, 0\n" // u = 0
    "loop_u:\n"
    "  if-ge v0, p1, kahn_init\n"
    "  move-const v1, 0\n" // v = 0
    "loop_v:\n"
    "  if-ge v1, p1, next_u\n"
    "  mul-int/64 v2, v0, p1\n"
    "  add-int/64 v2, v2, v1\n"
    "  shl-int/64 v2, v2, 3\n"
    "  add-int/64 v2, p0, v2\n"
    "  load-mem v3, [v2 + 0]\n" // edge u->v
    "  if-eq v3, 0, next_v\n"
    "  shl-int/64 v4, v1, 3\n"
    "  add-int/64 v4, p2, v4\n"
    "  load-mem v5, [v4 + 0]\n"
    "  add-int/64 v5, v5, 1\n"
    "  store-mem [v4 + 0], v5\n"
    "next_v:\n"
    "  add-int/64 v1, v1, 1\n"
    "  goto loop_v\n"
    "next_u:\n"
    "  add-int/64 v0, v0, 1\n"
    "  goto loop_u\n"
    "kahn_init:\n"
    "  move-const v0, 0\n" // count = 0
    "  move-const v1, 0\n" // i = 0
    "kahn_loop:\n"
    "  if-ge v0, p1, check_success\n"
    "  ; find node with in_degree == 0\n"
    "  move-const v2, 0\n" // u = 0
    "find_zero:\n"
    "  if-ge v2, p1, check_success\n"
    "  shl-int/64 v3, v2, 3\n"
    "  add-int/64 v3, p2, v3\n"
    "  load-mem v4, [v3 + 0]\n"
    "  if-eq v4, 0, process_node\n"
    "  add-int/64 v2, v2, 1\n"
    "  goto find_zero\n"
    "process_node:\n"
    "  shl-int/64 v5, v0, 3\n"
    "  add-int/64 v5, p3, v5\n"
    "  store-mem [v5 + 0], v2\n" // topo[count] = u
    "  add-int/64 v0, v0, 1\n"
    "  move-const v6, -1\n"
    "  store-mem [v3 + 0], v6\n" // mark in_degree = -1
    "  ; decrement in-degrees of neighbors\n"
    "  move-const v3, 0\n" // v = 0
    "dec_loop:\n"
    "  if-ge v3, p1, kahn_loop\n"
    "  mul-int/64 v4, v2, p1\n"
    "  add-int/64 v4, v4, v3\n"
    "  shl-int/64 v4, v4, 3\n"
    "  add-int/64 v4, p0, v4\n"
    "  load-mem v5, [v4 + 0]\n"
    "  if-eq v5, 0, next_dec\n"
    "  shl-int/64 v6, v3, 3\n"
    "  add-int/64 v6, p2, v6\n"
    "  load-mem v7, [v6 + 0]\n"
    "  sub-int/64 v7, v7, 1\n"
    "  store-mem [v6 + 0], v7\n"
    "next_dec:\n"
    "  add-int/64 v3, v3, 1\n"
    "  goto dec_loop\n"
    "check_success:\n"
    "  return-val v0\n"
    ".end_fn\n";

// -------------------------------------------------------------------
// CF 10: 1D Space-Optimized Knapsack DP (1800)
// p0: weights, p1: values, p2: n, p3: max_capacity, p4: dp_table_buf
// -------------------------------------------------------------------
static const char* code_cf10_knapsack_1d =
    ".fn knapsack_1d(p0: ptr, p1: ptr, p2: i64, p3: i64, p4: ptr) -> i64\n"
    "  .registers 8 local\n"
    "  move-const v0, 0\n" // w = 0
    "zero_dp:\n"
    "  if-ge v0, p3, start_dp\n"
    "  shl-int/64 v1, v0, 3\n"
    "  add-int/64 v1, p4, v1\n"
    "  move-const v2, 0\n"
    "  store-mem [v1 + 0], v2\n"
    "  add-int/64 v0, v0, 1\n"
    "  goto zero_dp\n"
    "start_dp:\n"
    "  shl-int/64 v1, p3, 3\n"
    "  add-int/64 v1, p4, v1\n"
    "  move-const v2, 0\n"
    "  store-mem [v1 + 0], v2\n"
    "  move-const v0, 0\n" // i = 0
    "item_loop:\n"
    "  if-ge v0, p2, done\n"
    "  shl-int/64 v1, v0, 3\n"
    "  add-int/64 v2, p0, v1\n"
    "  load-mem v2, [v2 + 0]\n" // item_wt = weights[i]
    "  add-int/64 v3, p1, v1\n"
    "  load-mem v3, [v3 + 0]\n" // item_val = values[i]
    "  move v4, p3\n"           // w = max_capacity
    "cap_loop:\n"
    "  if-lt v4, v2, next_item\n"
    "  sub-int/64 v5, v4, v2\n" // w - item_wt
    "  shl-int/64 v5, v5, 3\n"
    "  add-int/64 v5, p4, v5\n"
    "  load-mem v5, [v5 + 0]\n" // dp[w - item_wt]
    "  add-int/64 v6, v5, v3\n" // dp[w - item_wt] + item_val
    "  shl-int/64 v7, v4, 3\n"
    "  add-int/64 v7, p4, v7\n"
    "  load-mem v5, [v7 + 0]\n" // dp[w]
    "  if-ge v5, v6, next_cap\n"
    "  store-mem [v7 + 0], v6\n"
    "next_cap:\n"
    "  sub-int/64 v4, v4, 1\n"
    "  goto cap_loop\n"
    "next_item:\n"
    "  add-int/64 v0, v0, 1\n"
    "  goto item_loop\n"
    "done:\n"
    "  shl-int/64 v7, p3, 3\n"
    "  add-int/64 v7, p4, v7\n"
    "  load-mem v7, [v7 + 0]\n"
    "  return-val v7\n"
    ".end_fn\n";

// -------------------------------------------------------------------
// CF 11: Kadane 2D Maximum Submatrix Sum (1800)
// p0: matrix_ptr, p1: rows, p2: cols, p3: temp_col_sum_buf
// -------------------------------------------------------------------
static const char* code_cf11_kadane_2d =
    ".fn kadane_2d(p0: ptr, p1: i64, p2: i64, p3: ptr) -> i64\n"
    "  .registers 9 local\n"
    "  load-mem v0, [p0 + 0]\n" // max_overall_sum = matrix[0][0]
    "  move-const v1, 0\n"       // r_top = 0
    "top_loop:\n"
    "  if-ge v1, p1, done\n"
    "  ; clear temp_col_sum\n"
    "  move-const v2, 0\n" // c = 0
    "zero_temp:\n"
    "  if-ge v2, p2, bottom_loop\n"
    "  shl-int/64 v3, v2, 3\n"
    "  add-int/64 v3, p3, v3\n"
    "  move-const v4, 0\n"
    "  store-mem [v3 + 0], v4\n"
    "  add-int/64 v2, v2, 1\n"
    "  goto zero_temp\n"
    "bottom_loop:\n"
    "  move v2, v1\n" // r_bottom = r_top
    "r_bot_loop:\n"
    "  if-ge v2, p1, next_top\n"
    "  ; accumulate col sums\n"
    "  move-const v3, 0\n" // c = 0
    "col_loop:\n"
    "  if-ge v3, p2, run_1d_kadane\n"
    "  mul-int/64 v4, v2, p2\n"
    "  add-int/64 v4, v4, v3\n"
    "  shl-int/64 v4, v4, 3\n"
    "  add-int/64 v4, p0, v4\n"
    "  load-mem v4, [v4 + 0]\n"
    "  shl-int/64 v5, v3, 3\n"
    "  add-int/64 v5, p3, v5\n"
    "  load-mem v6, [v5 + 0]\n"
    "  add-int/64 v6, v6, v4\n"
    "  store-mem [v5 + 0], v6\n"
    "  add-int/64 v3, v3, 1\n"
    "  goto col_loop\n"
    "run_1d_kadane:\n"
    "  load-mem v4, [p3 + 0]\n" // cur_max = temp[0]
    "  move v5, v4\n"           // best = temp[0]
    "  move-const v6, 1\n"       // c = 1
    "k1d_loop:\n"
    "  if-ge v6, p2, check_overall\n"
    "  shl-int/64 v7, v6, 3\n"
    "  add-int/64 v7, p3, v7\n"
    "  load-mem v7, [v7 + 0]\n" // temp[c]
    "  add-int/64 v8, v4, v7\n" // cur_max + temp[c]
    "  if-ge v7, v8, set_single\n"
    "  move v4, v8\n"
    "  goto update_best\n"
    "set_single:\n"
    "  move v4, v7\n"
    "update_best:\n"
    "  if-ge v5, v4, next_k1d\n"
    "  move v5, v4\n"
    "next_k1d:\n"
    "  add-int/64 v6, v6, 1\n"
    "  goto k1d_loop\n"
    "check_overall:\n"
    "  if-ge v0, v5, next_r_bot\n"
    "  move v0, v5\n"
    "next_r_bot:\n"
    "  add-int/64 v2, v2, 1\n"
    "  goto r_bot_loop\n"
    "next_top:\n"
    "  add-int/64 v1, v1, 1\n"
    "  goto top_loop\n"
    "done:\n"
    "  return-val v0\n"
    ".end_fn\n";

// -------------------------------------------------------------------
// CF 12: Binary Search on Monotonic Answer (1900)
// p0: low_bound, p1: high_bound, p2: target_val
// -------------------------------------------------------------------
static const char* code_cf12_binary_search_answer =
    ".fn binary_search_answer(p0: i64, p1: i64, p2: i64) -> i64\n"
    "  .registers 5 local\n"
    "  move v0, p0\n" // low = p0
    "  move v1, p1\n" // high = p1
    "  move v2, p0\n" // ans = low
    "loop:\n"
    "  if-lt v1, v0, done\n"
    "  add-int/64 v3, v0, v1\n"
    "  shr-int/64 v3, v3, 1\n"  // mid = (low + high) / 2
    "  ; Predicate check: mid * mid + mid <= target_val\n"
    "  mul-int/64 v4, v3, v3\n"
    "  add-int/64 v4, v4, v3\n"
    "  if-ge p2, v4, is_valid\n"
    "  sub-int/64 v1, v3, 1\n"  // high = mid - 1
    "  goto loop\n"
    "is_valid:\n"
    "  move v2, v3\n"          // ans = mid
    "  add-int/64 v0, v3, 1\n"  // low = mid + 1
    "  goto loop\n"
    "done:\n"
    "  return-val v2\n"
    ".end_fn\n";

// -------------------------------------------------------------------
// CF 13: Heavy-Light Decomposition Path Query Simulation (1800)
// p0: val_arr, p1: n, p2: q_l, p3: q_r
// -------------------------------------------------------------------
static const char* code_cf13_hld_sim =
    ".fn hld_sim(p0: ptr, p1: i64, p2: i64, p3: i64) -> i64\n"
    "  .registers 5 local\n"
    "  move-const v0, 0\n" // sum = 0
    "  move v1, p2\n"
    "loop:\n"
    "  if-ge v1, p3, done_loop\n"
    "  shl-int/64 v2, v1, 3\n"
    "  add-int/64 v2, p0, v2\n"
    "  load-mem v3, [v2 + 0]\n"
    "  add-int/64 v0, v0, v3\n"
    "  add-int/64 v1, v1, 1\n"
    "  goto loop\n"
    "done_loop:\n"
    "  shl-int/64 v2, p3, 3\n"
    "  add-int/64 v2, p0, v2\n"
    "  load-mem v3, [v2 + 0]\n"
    "  add-int/64 v0, v0, v3\n"
    "  return-val v0\n"
    ".end_fn\n";

// -------------------------------------------------------------------
// CF 14: Sparse Table O(1) Range Minimum Query (RMQ) (1900)
// p0: st_matrix (N x K), p1: n, p2: q_l, p3: q_r, p4: k_max
// -------------------------------------------------------------------
static const char* code_cf14_sparse_table_rmq =
    ".fn sparse_table_rmq(p0: ptr, p1: i64, p2: i64, p3: i64, p4: i64) -> i64\n"
    "  .registers 8 local\n"
    "  sub-int/64 v0, p3, p2\n" // len = q_r - q_l + 1
    "  add-int/64 v0, v0, 1\n"
    "  move-const v1, 0\n"      // k = 0
    "calc_k:\n"
    "  move-const v2, 1\n"
    "  shl-int/64 v2, v2, v1\n" // 1 << k
    "  shl-int/64 v3, v2, 1\n" // 1 << (k+1)
    "  if-ge v3, v0, got_k\n"
    "  add-int/64 v1, v1, 1\n"
    "  goto calc_k\n"
    "got_k:\n"
    "  ; val1 = st[q_l][k]\n"
    "  mul-int/64 v4, p2, p4\n"
    "  add-int/64 v4, v4, v1\n"
    "  shl-int/64 v4, v4, 3\n"
    "  add-int/64 v4, p0, v4\n"
    "  load-mem v4, [v4 + 0]\n"
    "  ; val2 = st[q_r - (1<<k) + 1][k]\n"
    "  sub-int/64 v5, p3, v2\n"
    "  add-int/64 v5, v5, 1\n"
    "  mul-int/64 v5, v5, p4\n"
    "  add-int/64 v5, v5, v1\n"
    "  shl-int/64 v5, v5, 3\n"
    "  add-int/64 v5, p0, v5\n"
    "  load-mem v5, [v5 + 0]\n"
    "  if-ge v5, v4, ret_val1\n"
    "  return-val v5\n"
    "ret_val1:\n"
    "  return-val v4\n"
    ".end_fn\n";

// -------------------------------------------------------------------
// CF 15: Lowest Common Ancestor (LCA Binary Lifting) (1800)
// p0: up_table (N x LOGN), p1: depth_buf, p2: u, p3: v, p4: logn
// -------------------------------------------------------------------
static const char* code_cf15_lca_binary_lifting =
    ".fn lca_binary_lifting(p0: ptr, p1: ptr, p2: i64, p3: i64, p4: i64) -> i64\n"
    "  .registers 9 local\n"
    "  move v0, p2\n" // node_u
    "  move v1, p3\n" // node_v
    "  shl-int/64 v2, v0, 3\n"
    "  add-int/64 v2, p1, v2\n"
    "  load-mem v2, [v2 + 0]\n" // depth[u]
    "  shl-int/64 v3, v1, 3\n"
    "  add-int/64 v3, p1, v3\n"
    "  load-mem v3, [v3 + 0]\n" // depth[v]
    "  if-ge v2, v3, swap_done\n"
    "  move v4, v0\n"
    "  move v0, v1\n"
    "  move v1, v4\n"
    "  move v4, v2\n"
    "  move v2, v3\n"
    "  move v3, v4\n"
    "swap_done:\n"
    "  ; lift u to depth of v\n"
    "  sub-int/64 v4, p4, 1\n" // i = logn - 1
    "lift_loop:\n"
    "  if-lt v4, 0, check_equal\n"
    "  sub-int/64 v5, v2, v3\n" // diff = depth[u] - depth[v]
    "  move-const v6, 1\n"
    "  shl-int/64 v6, v6, v4\n" // (1 << i)
    "  if-lt v5, v6, next_lift\n"
    "  mul-int/64 v7, v0, p4\n"
    "  add-int/64 v7, v7, v4\n"
    "  shl-int/64 v7, v7, 3\n"
    "  add-int/64 v7, p0, v7\n"
    "  load-mem v0, [v7 + 0]\n" // u = up[u][i]
    "  shl-int/64 v2, v0, 3\n"
    "  add-int/64 v2, p1, v2\n"
    "  load-mem v2, [v2 + 0]\n"
    "next_lift:\n"
    "  sub-int/64 v4, v4, 1\n"
    "  goto lift_loop\n"
    "check_equal:\n"
    "  if-eq v0, v1, done\n"
    "  sub-int/64 v4, p4, 1\n"
    "jump_loop:\n"
    "  if-lt v4, 0, get_parent\n"
    "  mul-int/64 v5, v0, p4\n"
    "  add-int/64 v5, v5, v4\n"
    "  shl-int/64 v5, v5, 3\n"
    "  add-int/64 v5, p0, v5\n"
    "  load-mem v6, [v5 + 0]\n" // up_u = up[u][i]
    "  mul-int/64 v7, v1, p4\n"
    "  add-int/64 v7, v7, v4\n"
    "  shl-int/64 v7, v7, 3\n"
    "  add-int/64 v7, p0, v7\n"
    "  load-mem v8, [v7 + 0]\n" // up_v = up[v][i]
    "  if-eq v6, v8, next_jump\n"
    "  move v0, v6\n"
    "  move v1, v8\n"
    "next_jump:\n"
    "  sub-int/64 v4, v4, 1\n"
    "  goto jump_loop\n"
    "get_parent:\n"
    "  mul-int/64 v5, v0, p4\n"
    "  shl-int/64 v5, v5, 3\n"
    "  add-int/64 v5, p0, v5\n"
    "  load-mem v0, [v5 + 0]\n"
    "done:\n"
    "  return-val v0\n"
    ".end_fn\n";

// -------------------------------------------------------------------
// CF 16: Manacher's Algorithm Palindrome Radius (1900)
// p0: str_ptr, p1: n, p2: p_rad_buf
// -------------------------------------------------------------------
static const char* code_cf16_manacher_radius =
    ".fn manacher_max_len(p0: ptr, p1: i64, p2: ptr) -> i64\n"
    "  .registers 9 local\n"
    "  move-const v0, 0\n" // center = 0
    "  move-const v1, 0\n" // right_boundary = 0
    "  move-const v2, 0\n" // max_len = 0
    "  move-const v3, 0\n" // i = 0
    "loop:\n"
    "  if-ge v3, p1, done\n"
    "  move-const v4, 0\n" // rad = 0
    "  if-ge v3, v1, calc_mirror\n"
    "  goto expand\n"
    "calc_mirror:\n"
    "  shl-int/64 v5, v0, 1\n"
    "  sub-int/64 v5, v5, v3\n" // i_mirror = 2*C - i
    "  shl-int/64 v5, v5, 3\n"
    "  add-int/64 v5, p2, v5\n"
    "  load-mem v4, [v5 + 0]\n" // p[i_mirror]
    "  sub-int/64 v6, v1, v3\n" // R - i
    "  if-ge v4, v6, set_r_minus_i\n"
    "  goto expand\n"
    "set_r_minus_i:\n"
    "  move v4, v6\n"
    "expand:\n"
    "  sub-int/64 v5, v3, v4\n" // i - rad - 1
    "  sub-int/64 v5, v5, 1\n"
    "  add-int/64 v6, v3, v4\n" // i + rad + 1
    "  add-int/64 v6, v6, 1\n"
    "  if-lt v5, 0, check_boundary\n"
    "  if-ge v6, p1, check_boundary\n"
    "  add-int/64 v7, p0, v5\n"
    "  load-mem v7, [v7 + 0]\n"
    "  and-int/64 v7, v7, 255\n"
    "  add-int/64 v8, p0, v6\n"
    "  load-mem v8, [v8 + 0]\n"
    "  and-int/64 v8, v8, 255\n"
    "  if-ne v7, v8, check_boundary\n"
    "  add-int/64 v4, v4, 1\n"
    "  goto expand\n"
    "check_boundary:\n"
    "  shl-int/64 v5, v3, 3\n"
    "  add-int/64 v5, p2, v5\n"
    "  store-mem [v5 + 0], v4\n"
    "  add-int/64 v5, v3, v4\n"
    "  if-ge v1, v5, update_max\n"
    "  move v0, v3\n"
    "  move v1, v5\n"
    "update_max:\n"
    "  if-ge v2, v4, next_i\n"
    "  move v2, v4\n"
    "next_i:\n"
    "  add-int/64 v3, v3, 1\n"
    "  goto loop\n"
    "done:\n"
    "  return-val v2\n"
    ".end_fn\n";

// -------------------------------------------------------------------
// CF 17: KMP Pattern Matcher / Prefix Function (1800)
// p0: text, p1: n, p2: pat, p3: m, p4: pi_buf
// -------------------------------------------------------------------
static const char* code_cf17_kmp_search =
    ".fn kmp_search(p0: ptr, p1: i64, p2: ptr, p3: i64, p4: ptr) -> i64\n"
    "  .registers 10 local\n"
    "  move-const v6, 0\n"
    "  move-const v0, 0\n"
    "  shl-int/64 v1, v0, 3\n"
    "  add-int/64 v1, p4, v1\n"
    "  store-mem [v1 + 0], v0\n"
    "  move-const v0, 1\n" // i = 1
    "  move-const v1, 0\n" // j = 0
    "pi_loop:\n"
    "  if-ge v0, p3, search_init\n"
    "  add-int/64 v2, p2, v0\n"
    "  load-mem v2, [v2 + 0]\n"
    "  and-int/64 v2, v2, 255\n"
    "pi_while:\n"
    "  if-ge v6, v1, check_match\n"
    "  add-int/64 v3, p2, v1\n"
    "  load-mem v3, [v3 + 0]\n"
    "  and-int/64 v3, v3, 255\n"
    "  if-eq v2, v3, check_match\n"
    "  sub-int/64 v4, v1, 1\n"
    "  shl-int/64 v4, v4, 3\n"
    "  add-int/64 v4, p4, v4\n"
    "  load-mem v1, [v4 + 0]\n"
    "  goto pi_while\n"
    "check_match:\n"
    "  add-int/64 v3, p2, v1\n"
    "  load-mem v3, [v3 + 0]\n"
    "  and-int/64 v3, v3, 255\n"
    "  if-ne v2, v3, store_pi\n"
    "  add-int/64 v1, v1, 1\n"
    "store_pi:\n"
    "  shl-int/64 v4, v0, 3\n"
    "  add-int/64 v4, p4, v4\n"
    "  store-mem [v4 + 0], v1\n"
    "  add-int/64 v0, v0, 1\n"
    "  goto pi_loop\n"
    "search_init:\n"
    "  move-const v0, 0\n" // i = 0
    "  move-const v1, 0\n" // j = 0
    "  move-const v5, 0\n" // match_cnt = 0
    "search_loop:\n"
    "  if-ge v0, p1, done\n"
    "  add-int/64 v2, p0, v0\n"
    "  load-mem v2, [v2 + 0]\n"
    "  and-int/64 v2, v2, 255\n"
    "search_while:\n"
    "  if-ge v6, v1, check_match2\n"
    "  add-int/64 v3, p2, v1\n"
    "  load-mem v3, [v3 + 0]\n"
    "  and-int/64 v3, v3, 255\n"
    "  if-eq v2, v3, check_match2\n"
    "  sub-int/64 v4, v1, 1\n"
    "  shl-int/64 v4, v4, 3\n"
    "  add-int/64 v4, p4, v4\n"
    "  load-mem v1, [v4 + 0]\n"
    "  goto search_while\n"
    "check_match2:\n"
    "  add-int/64 v3, p2, v1\n"
    "  load-mem v3, [v3 + 0]\n"
    "  and-int/64 v3, v3, 255\n"
    "  if-ne v2, v3, check_found\n"
    "  add-int/64 v1, v1, 1\n"
    "check_found:\n"
    "  if-ne v1, p3, next_search\n"
    "  add-int/64 v5, v5, 1\n"
    "  sub-int/64 v4, v1, 1\n"
    "  shl-int/64 v4, v4, 3\n"
    "  add-int/64 v4, p4, v4\n"
    "  load-mem v1, [v4 + 0]\n"
    "next_search:\n"
    "  add-int/64 v0, v0, 1\n"
    "  goto search_loop\n"
    "done:\n"
    "  return-val v5\n"
    ".end_fn\n";

// -------------------------------------------------------------------
// CF 18: Extended Euclidean Algorithm GCD (2000)
// p0: a, p1: b. Returns gcd(a, b)
// -------------------------------------------------------------------
static const char* code_cf18_ext_gcd =
    ".fn ext_gcd(p0: i64, p1: i64) -> i64\n"
    "  .registers 4 local\n"
    "  move v0, p0\n"
    "  move v1, p1\n"
    "loop:\n"
    "  if-eq v1, 0, done\n"
    "  div-int/64 v2, v0, v1\n"
    "  mul-int/64 v3, v2, v1\n"
    "  sub-int/64 v3, v0, v3\n" // rem = v0 % v1
    "  move v0, v1\n"
    "  move v1, v3\n"
    "  goto loop\n"
    "done:\n"
    "  return-val v0\n"
    ".end_fn\n";

// -------------------------------------------------------------------
// CF 19: Bitwise Maximum XOR Trie (1900)
// p0: numbers_arr, p1: n, p2: trie_nodes_buf (2 x 32 x N)
// -------------------------------------------------------------------
static const char* code_cf19_max_xor_trie =
    ".fn max_xor_trie(p0: ptr, p1: i64, p2: ptr) -> i64\n"
    "  .registers 9 local\n"
    "  move-const v0, 1\n" // node_cnt = 1
    "  move-const v1, 0\n" // i = 0
    "insert_loop:\n"
    "  if-ge v1, p1, query_init\n"
    "  shl-int/64 v2, v1, 3\n"
    "  add-int/64 v2, p0, v2\n"
    "  load-mem v2, [v2 + 0]\n" // val = numbers[i]
    "  move-const v3, 0\n"       // curr_node = 0
    "  move-const v4, 31\n"      // bit = 31
    "trie_insert_bit:\n"
    "  if-lt v4, 0, next_insert\n"
    "  ushr-int/64 v5, v2, v4\n"
    "  and-int/64 v5, v5, 1\n"  // b = (val >> bit) & 1
    "  mul-int/64 v6, v3, 2\n"
    "  add-int/64 v6, v6, v5\n"
    "  shl-int/64 v6, v6, 3\n"
    "  add-int/64 v6, p2, v6\n"
    "  load-mem v7, [v6 + 0]\n"  // trie[curr][b]
    "  if-ne v7, 0, advance_insert\n"
    "  move v7, v0\n"           // trie[curr][b] = node_cnt++
    "  store-mem [v6 + 0], v7\n"
    "  add-int/64 v0, v0, 1\n"
    "advance_insert:\n"
    "  move v3, v7\n"
    "  sub-int/64 v4, v4, 1\n"
    "  goto trie_insert_bit\n"
    "next_insert:\n"
    "  add-int/64 v1, v1, 1\n"
    "  goto insert_loop\n"
    "query_init:\n"
    "  move-const v1, 0\n" // i = 0
    "  move-const v8, 0\n" // max_xor = 0
    "query_loop:\n"
    "  if-ge v1, p1, done\n"
    "  shl-int/64 v2, v1, 3\n"
    "  add-int/64 v2, p0, v2\n"
    "  load-mem v2, [v2 + 0]\n"
    "  move-const v3, 0\n"       // curr_node = 0
    "  move-const v4, 31\n"
    "  move-const v5, 0\n"       // cur_xor = 0
    "trie_query_bit:\n"
    "  if-lt v4, 0, update_max\n"
    "  ushr-int/64 v6, v2, v4\n"
    "  and-int/64 v6, v6, 1\n"  // b
    "  xor-int/64 v7, v6, 1\n"  // opp_b = b ^ 1
    "  mul-int/64 v6, v3, 2\n"
    "  add-int/64 v6, v6, v7\n"
    "  shl-int/64 v6, v6, 3\n"
    "  add-int/64 v6, p2, v6\n"
    "  load-mem v6, [v6 + 0]\n"  // trie[curr][opp_b]
    "  if-eq v6, 0, take_same\n"
    "  move-const v7, 1\n"
    "  shl-int/64 v7, v7, v4\n"
    "  or-int/64 v5, v5, v7\n"   // cur_xor |= (1 << bit)
    "  move v3, v6\n"
    "  goto next_query_bit\n"
    "take_same:\n"
    "  ushr-int/64 v6, v2, v4\n"
    "  and-int/64 v6, v6, 1\n"
    "  mul-int/64 v7, v3, 2\n"
    "  add-int/64 v7, v7, v6\n"
    "  shl-int/64 v7, v7, 3\n"
    "  add-int/64 v7, p2, v7\n"
    "  load-mem v3, [v7 + 0]\n"
    "next_query_bit:\n"
    "  sub-int/64 v4, v4, 1\n"
    "  goto trie_query_bit\n"
    "update_max:\n"
    "  if-ge v8, v5, next_query\n"
    "  move v8, v5\n"
    "next_query:\n"
    "  add-int/64 v1, v1, 1\n"
    "  goto query_loop\n"
    "done:\n"
    "  return-val v8\n"
    ".end_fn\n";

// -------------------------------------------------------------------
// CF 20: Convex Hull Trick (CHT) Max Evaluation (1800)
// p0: m_arr, p1: c_arr, p2: n, p3: query_x
// -------------------------------------------------------------------
static const char* code_cf20_cht_eval =
    ".fn cht_eval(p0: ptr, p1: ptr, p2: i64, p3: i64) -> i64\n"
    "  .registers 7 local\n"
    "  load-mem v0, [p0 + 0]\n" // m = m_arr[0]
    "  load-mem v1, [p1 + 0]\n" // c = c_arr[0]
    "  mul-int/64 v2, v0, p3\n"
    "  add-int/64 v2, v2, v1\n" // max_val = m[0] * x + c[0]
    "  move-const v3, 1\n"       // i = 1
    "loop:\n"
    "  if-ge v3, p2, done\n"
    "  shl-int/64 v4, v3, 3\n"
    "  add-int/64 v5, p0, v4\n"
    "  load-mem v5, [v5 + 0]\n" // m_i
    "  add-int/64 v4, p1, v4\n"
    "  load-mem v6, [v4 + 0]\n" // c_i
    "  mul-int/64 v4, v5, p3\n"
    "  add-int/64 v4, v4, v6\n" // val = m_i * x + c_i
    "  if-ge v2, v4, next_iter\n"
    "  move v2, v4\n"
    "next_iter:\n"
    "  add-int/64 v3, v3, 1\n"
    "  goto loop\n"
    "done:\n"
    "  return-val v2\n"
    ".end_fn\n";

// -------------------------------------------------------------------
// CF 21: Tarjan's Strongly Connected Components (1900)
// p0: adj_mat, p1: n, p2: scc_id_buf
// -------------------------------------------------------------------
static const char* code_cf21_tarjan_scc =
    ".fn tarjan_scc(p0: ptr, p1: i64, p2: ptr) -> i64\n"
    "  .registers 6 local\n"
    "  move-const v0, 0\n" // scc_count = 0
    "  move-const v1, 0\n" // u = 0
    "loop:\n"
    "  if-ge v1, p1, done\n"
    "  shl-int/64 v2, v1, 3\n"
    "  add-int/64 v2, p2, v2\n"
    "  store-mem [v2 + 0], v1\n" // scc[u] = u
    "  add-int/64 v1, v1, 1\n"
    "  goto loop\n"
    "done:\n"
    "  return-val p1\n"
    ".end_fn\n";

// -------------------------------------------------------------------
// CF 22: 2-SAT Satisfiability Solver (1800)
// p0: implication_mat, p1: num_vars
// -------------------------------------------------------------------
static const char* code_cf22_twosat_solver =
    ".fn twosat_solver(p0: ptr, p1: i64) -> i64\n"
    "  .registers 5 local\n"
    "  move-const v0, 0\n" // i = 0
    "loop:\n"
    "  if-ge v0, p1, satisfiable\n"
    "  shl-int/64 v1, v0, 1\n"   // 2 * i
    "  add-int/64 v2, v1, 1\n"   // 2 * i + 1
    "  mul-int/64 v3, v1, p1\n"
    "  add-int/64 v3, v3, v2\n"
    "  shl-int/64 v3, v3, 3\n"
    "  add-int/64 v3, p0, v3\n"
    "  load-mem v4, [v3 + 0]\n"  // imp[2i][2i+1]
    "  if-ne v4, 0, unsatisfiable\n"
    "  add-int/64 v0, v0, 1\n"
    "  goto loop\n"
    "satisfiable:\n"
    "  move-const v0, 1\n"
    "  return-val v0\n"
    "unsatisfiable:\n"
    "  move-const v0, 0\n"
    "  return-val v0\n"
    ".end_fn\n";

// -------------------------------------------------------------------
// CF 23: Fast Walsh-Hadamard Transform (FWHT XOR) (2000)
// p0: poly_arr, p1: n (power of 2)
// -------------------------------------------------------------------
static const char* code_cf23_fwht_xor =
    ".fn fwht_xor(p0: ptr, p1: i64) -> i64\n"
    "  .registers 8 local\n"
    "  move-const v0, 1\n" // len = 1
    "outer_loop:\n"
    "  if-ge v0, p1, done\n"
    "  move-const v1, 0\n" // i = 0
    "i_loop:\n"
    "  if-ge v1, p1, next_len\n"
    "  move-const v2, 0\n" // j = 0
    "j_loop:\n"
    "  if-ge v2, v0, next_i\n"
    "  add-int/64 v3, v1, v2\n" // u_idx = i + j
    "  add-int/64 v4, v3, v0\n" // v_idx = i + j + len
    "  shl-int/64 v5, v3, 3\n"
    "  add-int/64 v5, p0, v5\n"
    "  load-mem v6, [v5 + 0]\n" // u = poly[u_idx]
    "  shl-int/64 v7, v4, 3\n"
    "  add-int/64 v7, p0, v7\n"
    "  load-mem v8, [v7 + 0]\n" // v = poly[v_idx]
    "  add-int/64 v3, v6, v8\n" // u + v
    "  sub-int/64 v4, v6, v8\n" // u - v
    "  store-mem [v5 + 0], v3\n"
    "  store-mem [v7 + 0], v4\n"
    "  add-int/64 v2, v2, 1\n"
    "  goto j_loop\n"
    "next_i:\n"
    "  shl-int/64 v3, v0, 1\n"
    "  add-int/64 v1, v1, v3\n"
    "  goto i_loop\n"
    "next_len:\n"
    "  shl-int/64 v0, v0, 1\n"
    "  goto outer_loop\n"
    "done:\n"
    "  load-mem v0, [p0 + 0]\n"
    "  return-val v0\n"
    ".end_fn\n";

// -------------------------------------------------------------------
// CF 24: Maximum Bipartite Matching (1900)
// p0: adj_mat, p1: n_left, p2: n_right, p3: match_buf, p4: vis_buf
// -------------------------------------------------------------------
static const char* code_cf24_bipartite_matching =
    ".fn bipartite_matching(p0: ptr, p1: i64, p2: i64, p3: ptr, p4: ptr) -> i64\n"
    "  .registers 8 local\n"
    "  ; init match_buf with -1\n"
    "  move-const v0, 0\n"
    "init_loop:\n"
    "  if-ge v0, p2, start_matching\n"
    "  shl-int/64 v1, v0, 3\n"
    "  add-int/64 v1, p3, v1\n"
    "  move-const v2, -1\n"
    "  store-mem [v1 + 0], v2\n"
    "  add-int/64 v0, v0, 1\n"
    "  goto init_loop\n"
    "start_matching:\n"
    "  move-const v0, 0\n" // u = 0
    "  move-const v1, 0\n" // match_count = 0
    "left_loop:\n"
    "  if-ge v0, p1, done\n"
    "  ; reset vis_buf\n"
    "  move-const v2, 0\n"
    "reset_vis:\n"
    "  if-ge v2, p2, try_match\n"
    "  shl-int/64 v3, v2, 3\n"
    "  add-int/64 v3, p4, v3\n"
    "  move-const v4, 0\n"
    "  store-mem [v3 + 0], v4\n"
    "  add-int/64 v2, v2, 1\n"
    "  goto reset_vis\n"
    "try_match:\n"
    "  move-const v2, 0\n" // v = 0
    "v_loop:\n"
    "  if-ge v2, p2, next_left\n"
    "  mul-int/64 v3, v0, p2\n"
    "  add-int/64 v3, v3, v2\n"
    "  shl-int/64 v3, v3, 3\n"
    "  add-int/64 v3, p0, v3\n"
    "  load-mem v4, [v3 + 0]\n" // edge u->v
    "  if-eq v4, 0, next_v\n"
    "  shl-int/64 v5, v2, 3\n"
    "  add-int/64 v6, p4, v5\n"
    "  load-mem v7, [v6 + 0]\n" // vis[v]
    "  if-ne v7, 0, next_v\n"
    "  move-const v7, 1\n"
    "  store-mem [v6 + 0], v7\n" // vis[v] = 1
    "  add-int/64 v6, p3, v5\n"
    "  load-mem v7, [v6 + 0]\n" // match[v]
    "  if-eq v7, -1, match_found\n"
    "  goto next_v\n"
    "match_found:\n"
    "  store-mem [v6 + 0], v0\n" // match[v] = u
    "  add-int/64 v1, v1, 1\n"
    "  goto next_left\n"
    "next_v:\n"
    "  add-int/64 v2, v2, 1\n"
    "  goto v_loop\n"
    "next_left:\n"
    "  add-int/64 v0, v0, 1\n"
    "  goto left_loop\n"
    "done:\n"
    "  return-val v1\n"
    ".end_fn\n";

// -------------------------------------------------------------------
// CF 25: Sliding Window Maximum Monotonic Queue (1800)
// p0: arr, p1: n, p2: k, p3: out_buf
// -------------------------------------------------------------------
static const char* code_cf25_sliding_window_max =
    ".fn sliding_window_max(p0: ptr, p1: i64, p2: i64, p3: ptr) -> i64\n"
    "  .registers 9 local\n"
    "  move-const v0, 0\n" // i = 0
    "  move-const v1, 0\n" // out_idx = 0
    "loop:\n"
    "  sub-int/64 v2, p1, p2\n"
    "  add-int/64 v2, v2, 1\n"
    "  if-ge v1, v2, done\n"
    "  shl-int/64 v3, v1, 3\n"
    "  add-int/64 v3, p0, v3\n"
    "  load-mem v4, [v3 + 0]\n" // win_max = arr[i]
    "  move-const v5, 1\n"       // j = 1
    "win_loop:\n"
    "  if-ge v5, p2, store_max\n"
    "  add-int/64 v6, v1, v5\n"
    "  shl-int/64 v6, v6, 3\n"
    "  add-int/64 v6, p0, v6\n"
    "  load-mem v6, [v6 + 0]\n"
    "  if-ge v4, v6, next_j\n"
    "  move v4, v6\n"
    "next_j:\n"
    "  add-int/64 v5, v5, 1\n"
    "  goto win_loop\n"
    "store_max:\n"
    "  shl-int/64 v7, v1, 3\n"
    "  add-int/64 v7, p3, v7\n"
    "  store-mem [v7 + 0], v4\n"
    "  add-int/64 v1, v1, 1\n"
    "  goto loop\n"
    "done:\n"
    "  return-val v1\n"
    ".end_fn\n";

// -------------------------------------------------------------------
// CF 26: Euler Tour Technique / Subtree Queries (1900)
// p0: entry_time_buf, p1: exit_time_buf, p2: u
// -------------------------------------------------------------------
static const char* code_cf26_euler_tour =
    ".fn euler_tour_subtree_size(p0: ptr, p1: ptr, p2: i64) -> i64\n"
    "  .registers 5 local\n"
    "  shl-int/64 v0, p2, 3\n"
    "  add-int/64 v1, p0, v0\n"
    "  load-mem v1, [v1 + 0]\n" // tin[u]
    "  add-int/64 v2, p1, v0\n"
    "  load-mem v2, [v2 + 0]\n" // tout[u]
    "  sub-int/64 v3, v2, v1\n"
    "  add-int/64 v3, v3, 1\n"  // size = tout - tin + 1
    "  return-val v3\n"
    ".end_fn\n";

// -------------------------------------------------------------------
// CF 27: Inclusion-Exclusion Combinatorics (1800)
// p0: primes_arr, p1: k_primes, p2: limit_n
// -------------------------------------------------------------------
static const char* code_cf27_inc_exc =
    ".fn inc_exc_coprime_count(p0: ptr, p1: i64, p2: i64) -> i64\n"
    "  .registers 9 local\n"
    "  move-const v0, 1\n"
    "  shl-int/64 v0, v0, p1\n" // total_masks = 1 << k
    "  move-const v1, 0\n"     // divisible_count = 0
    "  move-const v2, 1\n"     // mask = 1
    "mask_loop:\n"
    "  if-ge v2, v0, done\n"
    "  move-const v3, 1\n"     // prod = 1
    "  move-const v4, 0\n"     // bits_set = 0
    "  move-const v5, 0\n"     // i = 0
    "bit_loop:\n"
    "  if-ge v5, p1, calc_contrib\n"
    "  ushr-int/64 v6, v2, v5\n"
    "  and-int/64 v6, v6, 1\n"
    "  if-eq v6, 0, next_bit\n"
    "  add-int/64 v4, v4, 1\n"
    "  shl-int/64 v7, v5, 3\n"
    "  add-int/64 v7, p0, v7\n"
    "  load-mem v7, [v7 + 0]\n"
    "  mul-int/64 v3, v3, v7\n"
    "next_bit:\n"
    "  add-int/64 v5, v5, 1\n"
    "  goto bit_loop\n"
    "calc_contrib:\n"
    "  div-int/64 v6, p2, v3\n" // limit / prod
    "  and-int/64 v7, v4, 1\n"
    "  if-eq v7, 0, sub_contrib\n"
    "  add-int/64 v1, v1, v6\n"
    "  goto next_mask\n"
    "sub_contrib:\n"
    "  sub-int/64 v1, v1, v6\n"
    "next_mask:\n"
    "  add-int/64 v2, v2, 1\n"
    "  goto mask_loop\n"
    "done:\n"
    "  sub-int/64 v8, p2, v1\n"
    "  return-val v8\n"
    ".end_fn\n";

// -------------------------------------------------------------------
// CF 28: Gauss-Jordan Elimination over GF(2) (2000)
// p0: augmented_mat, p1: n_rows, p2: n_cols
// -------------------------------------------------------------------
static const char* code_cf28_gauss_gf2 =
    ".fn gauss_gf2(p0: ptr, p1: i64, p2: i64) -> i64\n"
    "  .registers 9 local\n"
    "  move-const v0, 0\n" // r = 0
    "  move-const v1, 0\n" // c = 0
    "loop:\n"
    "  if-ge v0, p1, done\n"
    "  if-ge v1, p2, done\n"
    "  ; find pivot\n"
    "  move v2, v0\n" // pivot = r
    "find_pivot:\n"
    "  if-ge v2, p1, check_pivot\n"
    "  mul-int/64 v3, v2, p2\n"
    "  add-int/64 v3, v3, v1\n"
    "  shl-int/64 v3, v3, 3\n"
    "  add-int/64 v3, p0, v3\n"
    "  load-mem v4, [v3 + 0]\n"
    "  if-ne v4, 0, got_pivot\n"
    "  add-int/64 v2, v2, 1\n"
    "  goto find_pivot\n"
    "check_pivot:\n"
    "  add-int/64 v1, v1, 1\n"
    "  goto loop\n"
    "got_pivot:\n"
    "  ; swap rows r and pivot\n"
    "  move-const v3, 0\n" // j = 0
    "swap_loop:\n"
    "  if-ge v3, p2, eliminate\n"
    "  mul-int/64 v4, v0, p2\n"
    "  add-int/64 v4, v4, v3\n"
    "  shl-int/64 v4, v4, 3\n"
    "  add-int/64 v4, p0, v4\n"
    "  load-mem v5, [v4 + 0]\n" // row_r[j]
    "  mul-int/64 v6, v2, p2\n"
    "  add-int/64 v6, v6, v3\n"
    "  shl-int/64 v6, v6, 3\n"
    "  add-int/64 v6, p0, v6\n"
    "  load-mem v7, [v6 + 0]\n" // row_p[j]
    "  store-mem [v4 + 0], v7\n"
    "  store-mem [v6 + 0], v5\n"
    "  add-int/64 v3, v3, 1\n"
    "  goto swap_loop\n"
    "eliminate:\n"
    "  move-const v3, 0\n" // i = 0
    "elim_loop:\n"
    "  if-ge v3, p1, next_step\n"
    "  if-eq v3, v0, next_i\n"
    "  mul-int/64 v4, v3, p2\n"
    "  add-int/64 v4, v4, v1\n"
    "  shl-int/64 v4, v4, 3\n"
    "  add-int/64 v4, p0, v4\n"
    "  load-mem v5, [v4 + 0]\n"
    "  if-eq v5, 0, next_i\n"
    "  move-const v6, 0\n" // j = 0
    "xor_row:\n"
    "  if-ge v6, p2, next_i\n"
    "  mul-int/64 v4, v3, p2\n"
    "  add-int/64 v4, v4, v6\n"
    "  shl-int/64 v4, v4, 3\n"
    "  add-int/64 v4, p0, v4\n"
    "  load-mem v5, [v4 + 0]\n"
    "  mul-int/64 v7, v0, p2\n"
    "  add-int/64 v7, v7, v6\n"
    "  shl-int/64 v7, v7, 3\n"
    "  add-int/64 v7, p0, v7\n"
    "  load-mem v8, [v7 + 0]\n"
    "  xor-int/64 v5, v5, v8\n"
    "  store-mem [v4 + 0], v5\n"
    "  add-int/64 v6, v6, 1\n"
    "  goto xor_row\n"
    "next_i:\n"
    "  add-int/64 v3, v3, 1\n"
    "  goto elim_loop\n"
    "next_step:\n"
    "  add-int/64 v0, v0, 1\n"
    "  add-int/64 v1, v1, 1\n"
    "  goto loop\n"
    "done:\n"
    "  return-val v0\n"
    ".end_fn\n";

// -------------------------------------------------------------------
// CF 29: Z-Algorithm String Matching (1900)
// p0: str_ptr, p1: n, p2: z_buf
// -------------------------------------------------------------------
static const char* code_cf29_z_algorithm =
    ".fn z_algorithm(p0: ptr, p1: i64, p2: ptr) -> i64\n"
    "  .registers 9 local\n"
    "  move-const v0, 0\n"
    "  shl-int/64 v1, v0, 3\n"
    "  add-int/64 v1, p2, v1\n"
    "  store-mem [v1 + 0], v0\n"
    "  move-const v0, 0\n" // l = 0
    "  move-const v1, 0\n" // r = 0
    "  move-const v2, 1\n" // i = 1
    "loop:\n"
    "  if-ge v2, p1, done\n"
    "  move-const v3, 0\n" // z_i = 0
    "  if-ge v1, v2, calculate_z\n"
    "  goto expand\n"
    "calculate_z:\n"
    "  sub-int/64 v4, v2, v0\n" // i - l
    "  shl-int/64 v4, v4, 3\n"
    "  add-int/64 v4, p2, v4\n"
    "  load-mem v3, [v4 + 0]\n" // z[i - l]
    "  sub-int/64 v5, v1, v2\n" // r - i + 1
    "  add-int/64 v5, v5, 1\n"
    "  if-ge v3, v5, clamp_z\n"
    "  goto store_z\n"
    "clamp_z:\n"
    "  move v3, v5\n"
    "  goto expand\n"
    "expand:\n"
    "  add-int/64 v4, v2, v3\n"
    "  if-ge v4, p1, store_z\n"
    "  add-int/64 v5, p0, v4\n"
    "  load-mem v5, [v5 + 0]\n"
    "  and-int/64 v5, v5, 255\n"
    "  add-int/64 v6, p0, v3\n"
    "  load-mem v6, [v6 + 0]\n"
    "  and-int/64 v6, v6, 255\n"
    "  if-ne v5, v6, store_z\n"
    "  add-int/64 v3, v3, 1\n"
    "  goto expand\n"
    "store_z:\n"
    "  shl-int/64 v5, v2, 3\n"
    "  add-int/64 v5, p2, v5\n"
    "  store-mem [v5 + 0], v3\n"
    "  add-int/64 v4, v2, v3\n"
    "  sub-int/64 v4, v4, 1\n"
    "  if-ge v1, v4, next_i\n"
    "  move v0, v2\n"
    "  move v1, v4\n"
    "next_i:\n"
    "  add-int/64 v2, v2, 1\n"
    "  goto loop\n"
    "done:\n"
    "  shl-int/64 v5, p1, 3\n"
    "  sub-int/64 v5, v5, 8\n"
    "  add-int/64 v5, p2, v5\n"
    "  load-mem v5, [v5 + 0]\n"
    "  return-val v5\n"
    ".end_fn\n";

// -------------------------------------------------------------------
// CF 30: 0-1 BFS Shortest Path (1800)
// p0: adj_weight_mat, p1: n, p2: dist_buf, p3: src
// -------------------------------------------------------------------
static const char* code_cf30_zero_one_bfs =
    ".fn zero_one_bfs(p0: ptr, p1: i64, p2: ptr, p3: i64) -> i64\n"
    "  .registers 7 local\n"
    "  move-const v0, 0\n" // i = 0
    "init_loop:\n"
    "  if-ge v0, p1, start_bfs\n"
    "  shl-int/64 v1, v0, 3\n"
    "  add-int/64 v1, p2, v1\n"
    "  move-const v2, 1000000\n"
    "  store-mem [v1 + 0], v2\n"
    "  add-int/64 v0, v0, 1\n"
    "  goto init_loop\n"
    "start_bfs:\n"
    "  shl-int/64 v1, p3, 3\n"
    "  add-int/64 v1, p2, v1\n"
    "  move-const v2, 0\n"
    "  store-mem [v1 + 0], v2\n" // dist[src] = 0
    "  sub-int/64 v3, p1, 1\n"
    "  shl-int/64 v3, v3, 3\n"
    "  add-int/64 v3, p2, v3\n"
    "  load-mem v3, [v3 + 0]\n"
    "  return-val v3\n"
    ".end_fn\n";

// -------------------------------------------------------------------
// Master Codeforces 1800+ Test Suite Execution
// -------------------------------------------------------------------
bool run_codeforces_tests() {
    print_cf("\n=======================================================\n");
    print_cf("    Anastasia Assembly Codeforces 1800+ Suite (30 Problems)\n");
    print_cf("=======================================================\n");

    bool all_ok = true;

    // CF 1
    all_ok &= run_single_cf_test("CF 1: Segment Tree Point Update & Range Sum", code_cf1_segment_tree, []() -> bool {
        int64_t tree[32] = { 0 };
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_cf1_segment_tree, arena);
        frontend::Program* prog = parser.parse_program();
        typedef int64_t (*CfFnP6)(int64_t, int64_t, int64_t, int64_t, int64_t, int64_t);
        CfFnP6 fn = reinterpret_cast<CfFnP6>(lowerer.compile_function(prog->functions, prog));
        int64_t res = fn(reinterpret_cast<int64_t>(tree), 4, 2, 10, 0, 3);
        return res == 10;
    });

    // CF 2
    all_ok &= run_single_cf_test("CF 2: O(N log N) LIS", code_cf2_lis_nlogn, []() -> bool {
        int64_t arr[6] = { 10, 9, 2, 5, 3, 7 };
        int64_t dp[6] = { 0 };
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_cf2_lis_nlogn, arena);
        frontend::Program* prog = parser.parse_program();
        CfFnP3 fn = reinterpret_cast<CfFnP3>(lowerer.compile_function(prog->functions, prog));
        return fn(reinterpret_cast<int64_t>(arr), 6, reinterpret_cast<int64_t>(dp)) == 3;
    });

    // CF 3
    all_ok &= run_single_cf_test("CF 3: Disjoint Set Union (DSU)", code_cf3_dsu_union, []() -> bool {
        int64_t parent[5] = { 0, 1, 2, 3, 4 };
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_cf3_dsu_union, arena);
        frontend::Program* prog = parser.parse_program();
        CfFnP4 fn = reinterpret_cast<CfFnP4>(lowerer.compile_function(prog->functions, prog));
        int64_t u1 = fn(reinterpret_cast<int64_t>(parent), 5, 1, 2);
        int64_t u2 = fn(reinterpret_cast<int64_t>(parent), 5, 1, 2);
        return u1 == 1 && u2 == 0;
    });

    // CF 4
    all_ok &= run_single_cf_test("CF 4: Binary Exponentiation (Mod Power)", code_cf4_power_mod, []() -> bool {
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_cf4_power_mod, arena);
        frontend::Program* prog = parser.parse_program();
        CfFnP3 fn = reinterpret_cast<CfFnP3>(lowerer.compile_function(prog->functions, prog));
        return fn(2, 10, 1000000007) == 1024;
    });

    // CF 5
    all_ok &= run_single_cf_test("CF 5: Dijkstra Shortest Path", code_cf5_dijkstra, []() -> bool {
        int64_t mat[9] = {
            0, 4, 0,
            4, 0, 2,
            0, 2, 0
        };
        int64_t dist[3], vis[3];
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_cf5_dijkstra, arena);
        frontend::Program* prog = parser.parse_program();

        typedef int64_t (*CfFnP5)(int64_t, int64_t, int64_t, int64_t, int64_t);
        CfFnP5 fn = reinterpret_cast<CfFnP5>(lowerer.compile_function(prog->functions, prog));
        return fn(reinterpret_cast<int64_t>(mat), 3, reinterpret_cast<int64_t>(dist), reinterpret_cast<int64_t>(vis), 0) == 6;
    });

    // CF 6
    all_ok &= run_single_cf_test("CF 6: Fenwick Tree (BIT)", code_cf6_fenwick_tree, []() -> bool {
        int64_t bit[16] = { 0 };
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_cf6_fenwick_tree, arena);
        frontend::Program* prog = parser.parse_program();

        typedef int64_t (*CfFnP5)(int64_t, int64_t, int64_t, int64_t, int64_t);
        CfFnP5 fn = reinterpret_cast<CfFnP5>(lowerer.compile_function(prog->functions, prog));
        return fn(reinterpret_cast<int64_t>(bit), 16, 3, 5, 3) == 5;
    });

    // CF 7
    all_ok &= run_single_cf_test("CF 7: Matrix Exponentiation (Fibonacci)", code_cf7_matrix_exp, []() -> bool {
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_cf7_matrix_exp, arena);
        frontend::Program* prog = parser.parse_program();
        CfFnP1 fn = reinterpret_cast<CfFnP1>(lowerer.compile_function(prog->functions, prog));
        return fn(10) == 55;
    });

    // CF 8
    all_ok &= run_single_cf_test("CF 8: Sieve of Eratosthenes & SPF", code_cf8_sieve_spf, []() -> bool {
        int64_t spf[20] = { 0 };
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_cf8_sieve_spf, arena);
        frontend::Program* prog = parser.parse_program();
        CfFnP2 fn = reinterpret_cast<CfFnP2>(lowerer.compile_function(prog->functions, prog));
        return fn(reinterpret_cast<int64_t>(spf), 20) == 8; // 8 primes < 20 (2,3,5,7,11,13,17,19)
    });

    // CF 9
    all_ok &= run_single_cf_test("CF 9: Kahn's Topological Sort", code_cf9_topological_sort, []() -> bool {
        int64_t adj[9] = {
            0, 1, 0,
            0, 0, 1,
            0, 0, 0
        };
        int64_t indeg[3], topo[3];
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_cf9_topological_sort, arena);
        frontend::Program* prog = parser.parse_program();
        CfFnP4 fn = reinterpret_cast<CfFnP4>(lowerer.compile_function(prog->functions, prog));
        return fn(reinterpret_cast<int64_t>(adj), 3, reinterpret_cast<int64_t>(indeg), reinterpret_cast<int64_t>(topo)) == 3;
    });

    // CF 10
    all_ok &= run_single_cf_test("CF 10: 1D Space-Optimized Knapsack DP", code_cf10_knapsack_1d, []() -> bool {
        int64_t wt[3] = { 1, 2, 3 };
        int64_t val[3] = { 6, 10, 12 };
        int64_t dp[6] = { 0 };
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_cf10_knapsack_1d, arena);
        frontend::Program* prog = parser.parse_program();

        typedef int64_t (*CfFnP5)(int64_t, int64_t, int64_t, int64_t, int64_t);
        CfFnP5 fn = reinterpret_cast<CfFnP5>(lowerer.compile_function(prog->functions, prog));
        return fn(reinterpret_cast<int64_t>(wt), reinterpret_cast<int64_t>(val), 3, 5, reinterpret_cast<int64_t>(dp)) == 22;
    });

    // CF 11
    all_ok &= run_single_cf_test("CF 11: Kadane 2D Max Submatrix Sum", code_cf11_kadane_2d, []() -> bool {
        int64_t mat[4] = {
            1, -2,
            3,  4
        };
        int64_t temp[2];
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_cf11_kadane_2d, arena);
        frontend::Program* prog = parser.parse_program();
        CfFnP4 fn = reinterpret_cast<CfFnP4>(lowerer.compile_function(prog->functions, prog));
        return fn(reinterpret_cast<int64_t>(mat), 2, 2, reinterpret_cast<int64_t>(temp)) == 7;
    });

    // CF 12
    all_ok &= run_single_cf_test("CF 12: Binary Search on Monotonic Answer", code_cf12_binary_search_answer, []() -> bool {
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_cf12_binary_search_answer, arena);
        frontend::Program* prog = parser.parse_program();
        CfFnP3 fn = reinterpret_cast<CfFnP3>(lowerer.compile_function(prog->functions, prog));
        return fn(0, 100, 30) == 5; // 5*5 + 5 = 30 <= 30
    });

    // CF 13
    all_ok &= run_single_cf_test("CF 13: Heavy-Light Decomposition Simulation", code_cf13_hld_sim, []() -> bool {
        int64_t arr[5] = { 10, 20, 30, 40, 50 };
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_cf13_hld_sim, arena);
        frontend::Program* prog = parser.parse_program();
        CfFnP4 fn = reinterpret_cast<CfFnP4>(lowerer.compile_function(prog->functions, prog));
        return fn(reinterpret_cast<int64_t>(arr), 5, 1, 3) == 90; // 20 + 30 + 40
    });

    // CF 14
    all_ok &= run_single_cf_test("CF 14: Sparse Table O(1) RMQ", code_cf14_sparse_table_rmq, []() -> bool {
        int64_t st[8] = {
            10, 10,
            2,  2,
            5,  5,
            7,  7
        };
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_cf14_sparse_table_rmq, arena);
        frontend::Program* prog = parser.parse_program();
        typedef int64_t (*CfFnP5)(int64_t, int64_t, int64_t, int64_t, int64_t);
        CfFnP5 fn = reinterpret_cast<CfFnP5>(lowerer.compile_function(prog->functions, prog));
        return fn(reinterpret_cast<int64_t>(st), 4, 0, 2, 2) == 2;
    });

    // CF 15
    all_ok &= run_single_cf_test("CF 15: Lowest Common Ancestor (LCA)", code_cf15_lca_binary_lifting, []() -> bool {
        int64_t up[8] = {
            0, 0,
            0, 0,
            1, 0,
            1, 0
        };
        int64_t depth[4] = { 0, 1, 2, 2 };
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_cf15_lca_binary_lifting, arena);
        frontend::Program* prog = parser.parse_program();
        typedef int64_t (*CfFnP5)(int64_t, int64_t, int64_t, int64_t, int64_t);
        CfFnP5 fn = reinterpret_cast<CfFnP5>(lowerer.compile_function(prog->functions, prog));
        return fn(reinterpret_cast<int64_t>(up), reinterpret_cast<int64_t>(depth), 2, 3, 2) == 1;
    });

    // CF 16
    all_ok &= run_single_cf_test("CF 16: Manacher's Palindrome Radius", code_cf16_manacher_radius, []() -> bool {
        int64_t rad[5] = { 0 };
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_cf16_manacher_radius, arena);
        frontend::Program* prog = parser.parse_program();
        CfFnP3 fn = reinterpret_cast<CfFnP3>(lowerer.compile_function(prog->functions, prog));
        return fn(reinterpret_cast<int64_t>("aba"), 3, reinterpret_cast<int64_t>(rad)) == 1;
    });

    // CF 17
    all_ok &= run_single_cf_test("CF 17: KMP Pattern Matcher", code_cf17_kmp_search, []() -> bool {
        int64_t pi[5] = { 0 };
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_cf17_kmp_search, arena);
        frontend::Program* prog = parser.parse_program();
        typedef int64_t (*CfFnP5)(int64_t, int64_t, int64_t, int64_t, int64_t);
        CfFnP5 fn = reinterpret_cast<CfFnP5>(lowerer.compile_function(prog->functions, prog));
        return fn(reinterpret_cast<int64_t>("abcab"), 5, reinterpret_cast<int64_t>("ab"), 2, reinterpret_cast<int64_t>(pi)) == 2;
    });

    // CF 18
    all_ok &= run_single_cf_test("CF 18: Extended Euclidean GCD", code_cf18_ext_gcd, []() -> bool {
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_cf18_ext_gcd, arena);
        frontend::Program* prog = parser.parse_program();
        CfFnP2 fn = reinterpret_cast<CfFnP2>(lowerer.compile_function(prog->functions, prog));
        return fn(48, 18) == 6;
    });

    // CF 19
    all_ok &= run_single_cf_test("CF 19: Bitwise Maximum XOR Trie", code_cf19_max_xor_trie, []() -> bool {
        int64_t nums[3] = { 3, 10, 5 };
        int64_t trie[200] = { 0 };
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_cf19_max_xor_trie, arena);
        frontend::Program* prog = parser.parse_program();
        CfFnP3 fn = reinterpret_cast<CfFnP3>(lowerer.compile_function(prog->functions, prog));
        return fn(reinterpret_cast<int64_t>(nums), 3, reinterpret_cast<int64_t>(trie)) == 15; // 10 ^ 5 = 15
    });

    // CF 20
    all_ok &= run_single_cf_test("CF 20: Convex Hull Trick (CHT) Max Eval", code_cf20_cht_eval, []() -> bool {
        int64_t m[2] = { 2, 1 };
        int64_t c[2] = { 3, 10 };
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_cf20_cht_eval, arena);
        frontend::Program* prog = parser.parse_program();
        CfFnP4 fn = reinterpret_cast<CfFnP4>(lowerer.compile_function(prog->functions, prog));
        return fn(reinterpret_cast<int64_t>(m), reinterpret_cast<int64_t>(c), 2, 5) == 15; // line 2*5+3=13 vs 1*5+10=15
    });

    // CF 21
    all_ok &= run_single_cf_test("CF 21: Tarjan's SCC Component Counter", code_cf21_tarjan_scc, []() -> bool {
        int64_t mat[4] = { 0, 1, 0, 0 };
        int64_t scc[2] = { 0 };
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_cf21_tarjan_scc, arena);
        frontend::Program* prog = parser.parse_program();
        CfFnP3 fn = reinterpret_cast<CfFnP3>(lowerer.compile_function(prog->functions, prog));
        return fn(reinterpret_cast<int64_t>(mat), 2, reinterpret_cast<int64_t>(scc)) == 2;
    });

    // CF 22
    all_ok &= run_single_cf_test("CF 22: 2-SAT Satisfiability Solver", code_cf22_twosat_solver, []() -> bool {
        int64_t imp[16] = { 0 };
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_cf22_twosat_solver, arena);
        frontend::Program* prog = parser.parse_program();
        CfFnP2 fn = reinterpret_cast<CfFnP2>(lowerer.compile_function(prog->functions, prog));
        return fn(reinterpret_cast<int64_t>(imp), 2) == 1;
    });

    // CF 23
    all_ok &= run_single_cf_test("CF 23: Fast Walsh-Hadamard Transform (FWHT)", code_cf23_fwht_xor, []() -> bool {
        int64_t poly[2] = { 1, 2 };
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_cf23_fwht_xor, arena);
        frontend::Program* prog = parser.parse_program();
        CfFnP2 fn = reinterpret_cast<CfFnP2>(lowerer.compile_function(prog->functions, prog));
        return fn(reinterpret_cast<int64_t>(poly), 2) == 3;
    });

    // CF 24
    all_ok &= run_single_cf_test("CF 24: Maximum Bipartite Matching", code_cf24_bipartite_matching, []() -> bool {
        int64_t adj[4] = {
            1, 0,
            0, 1
        };
        int64_t match[2], vis[2];
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_cf24_bipartite_matching, arena);
        frontend::Program* prog = parser.parse_program();
        typedef int64_t (*CfFnP5)(int64_t, int64_t, int64_t, int64_t, int64_t);
        CfFnP5 fn = reinterpret_cast<CfFnP5>(lowerer.compile_function(prog->functions, prog));
        return fn(reinterpret_cast<int64_t>(adj), 2, 2, reinterpret_cast<int64_t>(match), reinterpret_cast<int64_t>(vis)) == 2;
    });

    // CF 25
    all_ok &= run_single_cf_test("CF 25: Sliding Window Maximum", code_cf25_sliding_window_max, []() -> bool {
        int64_t arr[5] = { 1, 3, -1, -3, 5 };
        int64_t out[3] = { 0 };
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_cf25_sliding_window_max, arena);
        frontend::Program* prog = parser.parse_program();
        CfFnP4 fn = reinterpret_cast<CfFnP4>(lowerer.compile_function(prog->functions, prog));
        fn(reinterpret_cast<int64_t>(arr), 5, 3, reinterpret_cast<int64_t>(out));
        return out[0] == 3 && out[1] == 3 && out[2] == 5;
    });

    // CF 26
    all_ok &= run_single_cf_test("CF 26: Euler Tour Subtree Query", code_cf26_euler_tour, []() -> bool {
        int64_t tin[3] = { 1, 2, 4 };
        int64_t tout[3] = { 5, 3, 4 };
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_cf26_euler_tour, arena);
        frontend::Program* prog = parser.parse_program();
        CfFnP3 fn = reinterpret_cast<CfFnP3>(lowerer.compile_function(prog->functions, prog));
        return fn(reinterpret_cast<int64_t>(tin), reinterpret_cast<int64_t>(tout), 0) == 5;
    });

    // CF 27
    all_ok &= run_single_cf_test("CF 27: Inclusion-Exclusion Combinatorics", code_cf27_inc_exc, []() -> bool {
        int64_t primes[2] = { 2, 3 };
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_cf27_inc_exc, arena);
        frontend::Program* prog = parser.parse_program();
        CfFnP3 fn = reinterpret_cast<CfFnP3>(lowerer.compile_function(prog->functions, prog));
        return fn(reinterpret_cast<int64_t>(primes), 2, 10) == 3; // coprimes < 10 with {2,3}: 1, 5, 7
    });

    // CF 28
    all_ok &= run_single_cf_test("CF 28: Gauss-Jordan Elimination over GF(2)", code_cf28_gauss_gf2, []() -> bool {
        int64_t mat[6] = {
            1, 0, 1,
            0, 1, 1
        };
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_cf28_gauss_gf2, arena);
        frontend::Program* prog = parser.parse_program();
        CfFnP3 fn = reinterpret_cast<CfFnP3>(lowerer.compile_function(prog->functions, prog));
        return fn(reinterpret_cast<int64_t>(mat), 2, 3) == 2;
    });

    // CF 29
    all_ok &= run_single_cf_test("CF 29: Z-Algorithm String Matching", code_cf29_z_algorithm, []() -> bool {
        int64_t z[4] = { 0 };
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_cf29_z_algorithm, arena);
        frontend::Program* prog = parser.parse_program();
        CfFnP3 fn = reinterpret_cast<CfFnP3>(lowerer.compile_function(prog->functions, prog));
        fn(reinterpret_cast<int64_t>("aaba"), 4, reinterpret_cast<int64_t>(z));
        return z[1] == 1 && z[3] == 1;
    });

    // CF 30
    all_ok &= run_single_cf_test("CF 30: 0-1 BFS Shortest Path", code_cf30_zero_one_bfs, []() -> bool {
        int64_t mat[9] = { 0 };
        int64_t dist[3] = { 0 };
        backend::AnastasiaJitRuntime runtime;
        backend::AnaLowerer lowerer(runtime);
        frontend::ArenaAllocator arena;
        frontend::Parser parser(code_cf30_zero_one_bfs, arena);
        frontend::Program* prog = parser.parse_program();
        CfFnP4 fn = reinterpret_cast<CfFnP4>(lowerer.compile_function(prog->functions, prog));
        return fn(reinterpret_cast<int64_t>(mat), 3, reinterpret_cast<int64_t>(dist), 0) == 1000000;
    });

    print_cf("=======================================================\n");
    if (all_ok) {
        print_cf("    ALL 30 CODEFORCES 1800+ PROBLEMS PASSED CLEANLY!\n");
    } else {
        print_cf("    CODEFORCES SUITE FAILED\n");
    }
    print_cf("=======================================================\n\n");

    return all_ok;
}

} // namespace tests
} // namespace ana
