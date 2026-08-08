// A/B probe: uses ONLY APIs that exist in both the pre-fix and post-fix
// sources, so the identical file can be compiled against either tree.
// Each case is selected by argv[1] so that a crash in one cannot hide another.

#include "sys/sys_raw.h"
#include "sys/object_heap.h"

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

static int g_once_runs = 0;
static void once_body() { ++g_once_runs; }

static void c_once() {
    pthread_once_t oc = PTHREAD_ONCE_INIT;
    g_once_runs = 0;
    pthread_once(&oc, once_body);
    pthread_once(&oc, once_body);
    pthread_once(&oc, once_body);
    P("    init_routine invocations = ");
    PI(g_once_runs);
    P("\n");
    CHECK(g_once_runs == 1, "pthread_once runs the initialiser exactly once");
}

static void c_mutex() {
    pthread_mutex_t m;
    pthread_mutex_init(&m, nullptr);
    pthread_mutex_lock(&m);
    int held = *reinterpret_cast<volatile int*>(&m);
    P("    mutex word while held = ");
    PI(held);
    P("\n");
    CHECK(held != 0, "mutex actually records that it is held");
    pthread_mutex_unlock(&m);
}

static void c_io() {
    const char* path = "/tmp/anastasia_baseline_probe.tmp";
    const char* payload = "anastasia-io-probe";
    const size_t plen = 18;

    int fd = open(path, 01102, 0644);
    P("    open() -> fd ");
    PI(fd);
    P("\n");
    CHECK(fd >= 0, "open() returns a real descriptor");
    if (fd < 0) return;

    write(fd, payload, plen);
    close(fd);

    int fd2 = open(path, 0, 0);
    if (fd2 < 0) {
        CHECK(false, "reopen for reading");
        return;
    }
    char buf[32];
    for (int i = 0; i < 32; ++i) buf[i] = 0;
    int64_t r = read(fd2, buf, 32);
    P("    read() -> ");
    PI(r);
    P(" bytes\n");
    CHECK(r == static_cast<int64_t>(plen), "read() returns the real byte count");
    CHECK(sys::freestanding_memcmp(buf, payload, plen) == 0, "round-tripped bytes match");
    close(fd2);
}

static void c_overflow() {
    sys::ObjectHeap heap;
    P("    requesting instance_size = 4294967280\n");
    void* o = heap.allocate_object(0xFFFFFFF0u, nullptr, 1);
    CHECK(o == nullptr, "an instance_size near UINT32_MAX is rejected");
}

struct Case {
    const char* name;
    void (*fn)();
};

static const Case kCases[] = {
    { "once", c_once },
    { "mutex", c_mutex },
    { "io", c_io },
    { "overflow", c_overflow },
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
    const size_t ncase = sizeof(kCases) / sizeof(kCases[0]);
    for (size_t i = 0; i < ncase; ++i) {
        if (argc >= 2 && !name_eq(argv[1], kCases[i].name)) continue;
        P("=== ");
        P(kCases[i].name);
        P(" ===\n");
        kCases[i].fn();
    }
    P("PROBE_FAILED_CHECKS=");
    PI(g_fail);
    P("\n");
    return g_fail == 0 ? 0 : 1;
}
