// Adversarial regression harness for the Anastasia runtime layer.
//
// Every case here fails against the pre-fix sources. It is deliberately
// independent of tests/test_suite.cpp so that a bug in one cannot mask the
// other.

#include "sys/sys_raw.h"
#include "sys/cpu_features.h"
#include "sys/object_heap.h"
#include "backend/jit_string_pool.h"

using namespace ana;

static int g_fail = 0;

static size_t slen(const char* s) {
    size_t n = 0;
    while (s[n]) ++n;
    return n;
}

static void P(const char* s) { sys::raw_write(1, s, slen(s)); }

static void PI(int64_t v) {
    char b[32];
    int i = 0;
    bool neg = v < 0;
    uint64_t u = neg ? static_cast<uint64_t>(-v) : static_cast<uint64_t>(v);
    if (!u) b[i++] = '0';
    while (u) { b[i++] = static_cast<char>('0' + (u % 10)); u /= 10; }
    if (neg) b[i++] = '-';
    char o[32];
    int j = 0;
    while (i) o[j++] = b[--i];
    sys::raw_write(1, o, static_cast<size_t>(j));
}

static void CHECK(bool ok, const char* what) {
    if (ok) {
        P("    [ok]   ");
    } else {
        P("    [BUG]  ");
        ++g_fail;
    }
    P(what);
    P("\n");
}

// ---------------------------------------------------------------------------
// Object heap: growth must not relocate live objects.
// ---------------------------------------------------------------------------
static void t_heap_nonmoving() {
    sys::ObjectHeap heap;

    void* z = heap.allocate_object(128, reinterpret_cast<void*>(0xABCD), 7);
    bool zero_ok = (z != nullptr);
    if (z) {
        unsigned char* f = reinterpret_cast<unsigned char*>(z) + sizeof(sys::ObjectHeader);
        for (int i = 0; i < 128; ++i) {
            if (f[i] != 0) { zero_ok = false; break; }
        }
    }
    CHECK(zero_ok, "field area is zeroed on allocation");

    const int N = 400;
    static void* ptrs[400];
    bool alloc_ok = true;
    for (int i = 0; i < N; ++i) {
        void* o = heap.allocate_object(256, reinterpret_cast<void*>(0x1000 + static_cast<uintptr_t>(i)),
                                       static_cast<uint32_t>(i));
        ptrs[i] = o;
        if (!o) { alloc_ok = false; break; }
        unsigned char* f = reinterpret_cast<unsigned char*>(o) + sizeof(sys::ObjectHeader);
        for (int j = 0; j < 256; ++j) {
            f[j] = static_cast<unsigned char>((i * 7 + j) & 0xFF);
        }
    }
    CHECK(alloc_ok, "400 objects allocated without failure");
    CHECK(heap.chunk_count() > 1, "allocation actually spilled into additional chunks");

    bool intact = true;
    int bad = -1;
    for (int i = 0; i < N && intact; ++i) {
        sys::ObjectHeader* h = reinterpret_cast<sys::ObjectHeader*>(ptrs[i]);
        if (!h || h->class_id != static_cast<uint32_t>(i) ||
            h->vtable_ptr != reinterpret_cast<void*>(0x1000 + static_cast<uintptr_t>(i))) {
            intact = false;
            bad = i;
            break;
        }
        unsigned char* f = reinterpret_cast<unsigned char*>(ptrs[i]) + sizeof(sys::ObjectHeader);
        for (int j = 0; j < 256; ++j) {
            if (f[j] != static_cast<unsigned char>((i * 7 + j) & 0xFF)) {
                intact = false;
                bad = i;
                break;
            }
        }
    }
    if (!intact) { P("    first corrupted object index = "); PI(bad); P("\n"); }
    CHECK(intact, "every pointer issued before growth is still valid and unmodified");

    size_t before = heap.bytes_allocated();
    heap.reset();
    CHECK(before > 0 && heap.bytes_allocated() == 0, "reset() rewinds the bump cursor");
    CHECK(heap.chunk_count() == 1, "reset() releases overflow chunks but keeps the head");
    CHECK(heap.allocate_object(64, nullptr, 1) != nullptr, "heap is reusable after reset()");
}

// ---------------------------------------------------------------------------
// Object heap: the header+size total used to be computed in uint32_t.
// ---------------------------------------------------------------------------
static void t_heap_overflow() {
    sys::ObjectHeap heap;
    CHECK(heap.allocate_object(0xFFFFFFF0u, nullptr, 1) == nullptr,
          "instance_size near UINT32_MAX is rejected instead of wrapping to a tiny block");
    CHECK(heap.allocate_object(0x7FFFFFFFu, nullptr, 2) == nullptr,
          "a 2 GiB instance is rejected");
    CHECK(heap.allocate_object(4096, nullptr, 3) != nullptr,
          "a legitimately large object still allocates");
}

// ---------------------------------------------------------------------------
// JIT string pool: interned addresses are baked into generated code, so they
// must never move.
// ---------------------------------------------------------------------------
static size_t make_lit(char* buf, int i) {
    const char* pre = "jit_interned_literal_number_padded_";
    size_t n = 0;
    while (pre[n]) { buf[n] = pre[n]; ++n; }
    int d = 10000;
    while (d >= 1) {
        buf[n++] = static_cast<char>('0' + ((i / d) % 10));
        d /= 10;
    }
    buf[n] = '\0';
    return n;
}

static uint64_t h64(const char* s, size_t n) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; ++i) {
        h ^= static_cast<unsigned char>(s[i]);
        h *= 1099511628211ULL;
    }
    return h;
}

static void t_strpool_nonmoving() {
    backend::JitStringPool pool;
    const int N = 3000;
    static const char* got[3000];
    char tmp[64];

    bool ok = true;
    for (int i = 0; i < N; ++i) {
        size_t n = make_lit(tmp, i);
        got[i] = pool.get_or_intern(tmp, n, h64(tmp, n));
        if (!got[i] || got[i][0] == '\0') { ok = false; break; }
    }
    CHECK(ok, "3000 literals interned (the entry table grew past the old fixed 256 cap)");
    CHECK(pool.count() == static_cast<uint32_t>(N), "all 3000 are recorded as distinct entries");
    CHECK(pool.chunk_count() > 1, "interning actually spilled into additional chunks");

    bool intact = true;
    int bad = -1;
    for (int i = 0; i < N; ++i) {
        size_t n = make_lit(tmp, i);
        if (sys::freestanding_memcmp(got[i], tmp, n) != 0 || got[i][n] != '\0') {
            intact = false;
            bad = i;
            break;
        }
    }
    if (!intact) { P("    first corrupted literal index = "); PI(bad); P("\n"); }
    CHECK(intact, "every interned pointer still reads back its original bytes after growth");

    size_t n0 = make_lit(tmp, 0);
    CHECK(pool.get_or_intern(tmp, n0, h64(tmp, n0)) == got[0],
          "re-interning returns the identical address (dedup survives growth)");
}

// ---------------------------------------------------------------------------
// pthread shims used to be unconditional no-ops.
// ---------------------------------------------------------------------------
static int g_once_runs = 0;
static void once_body() { ++g_once_runs; }

static void t_pthread_once_mutex() {
    pthread_once_t oc = PTHREAD_ONCE_INIT;
    g_once_runs = 0;
    pthread_once(&oc, once_body);
    pthread_once(&oc, once_body);
    pthread_once(&oc, once_body);
    CHECK(g_once_runs == 1, "pthread_once ran the initialiser exactly once");

    pthread_mutex_t m;
    CHECK(pthread_mutex_init(&m, nullptr) == 0, "mutex init succeeds");
    CHECK(pthread_mutex_lock(&m) == 0, "mutex lock succeeds");
    CHECK(*reinterpret_cast<volatile int*>(&m) != 0,
          "mutex word is non-zero while held (locking was previously a no-op)");
    CHECK(pthread_mutex_unlock(&m) == 0, "mutex unlock succeeds");
    CHECK(*reinterpret_cast<volatile int*>(&m) == 0, "mutex word returns to free after unlock");
    CHECK(pthread_mutex_lock(&m) == 0, "mutex can be re-acquired");
    pthread_mutex_unlock(&m);
    pthread_mutex_destroy(&m);
}

static void t_tls_keys() {
    int created = 0;
    pthread_key_t k0 = 0;
    bool have_k0 = false;
    for (int i = 0; i < 200; ++i) {
        pthread_key_t k;
        if (pthread_key_create(&k, nullptr) == 0) {
            if (!have_k0) { k0 = k; have_k0 = true; }
            ++created;
        }
    }
    CHECK(created > 0, "at least one TLS key was created");
    CHECK(created <= 64, "key creation is bounded by the table size (the counter ran past the end)");
    CHECK(have_k0 && pthread_setspecific(k0, reinterpret_cast<const void*>(0x1234)) == 0,
          "setspecific on a valid key");
    CHECK(pthread_getspecific(k0) == reinterpret_cast<void*>(0x1234), "getspecific round-trips");
}

// ---------------------------------------------------------------------------
// open/read/close were stubs that reported success without doing anything.
// ---------------------------------------------------------------------------
static void t_libc_io() {
    const char* path = "/tmp/anastasia_io_probe.tmp";
    const char* payload = "anastasia-io-probe";
    const size_t plen = 18;

    int fd = open(path, 01102 /* O_RDWR|O_CREAT|O_TRUNC */, 0644);
    CHECK(fd >= 0, "open() returns a real descriptor (it used to always return -1)");
    if (fd < 0) return;

    CHECK(write(fd, payload, plen) == static_cast<int64_t>(plen), "write() transfers the whole payload");
    CHECK(close(fd) == 0, "close() succeeds");

    int fd2 = open(path, 0 /* O_RDONLY */, 0);
    CHECK(fd2 >= 0, "reopen for reading");
    if (fd2 < 0) return;

    char buf[32];
    for (int i = 0; i < 32; ++i) buf[i] = 0;
    int64_t r = read(fd2, buf, 32);
    CHECK(r == static_cast<int64_t>(plen),
          "read() returns real data (it used to hard-code 0, a fake clean EOF)");
    CHECK(sys::freestanding_memcmp(buf, payload, plen) == 0, "round-tripped bytes match");
    close(fd2);
}

// ---------------------------------------------------------------------------
struct Case {
    const char* name;
    void (*fn)();
};

static const Case kCases[] = {
    { "heap_nonmoving", t_heap_nonmoving },
    { "heap_overflow", t_heap_overflow },
    { "strpool_nonmoving", t_strpool_nonmoving },
    { "pthread_once_mutex", t_pthread_once_mutex },
    { "tls_keys", t_tls_keys },
    { "libc_io", t_libc_io },
};

static bool name_eq(const char* a, const char* b) {
    while (*a && *b) {
        if (*a != *b) return false;
        ++a;
        ++b;
    }
    return *a == *b;
}

int ana_main(int argc, char** argv) {
    sys::detect_cpu_features();
    const size_t ncase = sizeof(kCases) / sizeof(kCases[0]);
    int ran = 0;
    for (size_t i = 0; i < ncase; ++i) {
        if (argc >= 2 && !name_eq(argv[1], kCases[i].name)) continue;
        P("=== ");
        P(kCases[i].name);
        P(" ===\n");
        kCases[i].fn();
        ran++;
    }
    P("\nSUMMARY: cases=");
    PI(ran);
    P(" failed_checks=");
    PI(g_fail);
    P("\n");
    return g_fail == 0 ? 0 : 1;
}
