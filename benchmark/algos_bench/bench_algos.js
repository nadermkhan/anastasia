function js_loop_100m() {
    let sum_val = 0;
    for (let i = 0; i < 100000000; ++i) {
        sum_val = (sum_val + i) ^ (i << 1);
    }
    return sum_val;
}

function js_fib(n) {
    if (n <= 1) return n;
    return js_fib(n - 1) + js_fib(n - 2);
}

function js_prime_sieve() {
    const N = 10000000;
    const is_prime = new Uint8Array(N + 1);
    is_prime.fill(1);
    is_prime[0] = is_prime[1] = 0;
    for (let p = 2; p * p <= N; ++p) {
        if (is_prime[p]) {
            for (let i = p * p; i <= N; i += p) {
                is_prime[i] = 0;
            }
        }
    }
    let count = 0;
    for (let i = 0; i <= N; ++i) {
        if (is_prime[i]) count++;
    }
    return count;
}

function js_quicksort_rec(arr, low, high) {
    if (low < high) {
        const pivot = arr[high];
        let i = low - 1;
        for (let j = low; j < high; ++j) {
            if (arr[j] < pivot) {
                i++;
                const t = arr[i]; arr[i] = arr[j]; arr[j] = t;
            }
        }
        const t = arr[i + 1]; arr[i + 1] = arr[high]; arr[high] = t;
        const pi = i + 1;
        js_quicksort_rec(arr, low, pi - 1);
        js_quicksort_rec(arr, pi + 1, high);
    }
}

function js_quicksort() {
    const N = 50000;
    const arr = new Int32Array(N);
    let seed = 12345;
    for (let i = 0; i < N; ++i) {
        seed = (seed * 1103515245 + 12345) & 0x7fffffff;
        arr[i] = seed;
    }
    js_quicksort_rec(arr, 0, N - 1);
    return arr[N - 1];
}

const mode = process.argv[2] || "js_loop";
if (mode === "js_loop") js_loop_100m();
else if (mode === "js_fib") js_fib(35);
else if (mode === "js_sieve") js_prime_sieve();
else if (mode === "js_sort") js_quicksort();
