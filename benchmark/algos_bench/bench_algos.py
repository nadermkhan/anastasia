import sys
sys.setrecursionlimit(2_000_000)

def py_loop_100m():
    sum_val = 0
    for i in range(100_000_000):
        sum_val = (sum_val + i) ^ (i << 1)
    return sum_val

def py_fib(n):
    if n <= 1:
        return n
    return py_fib(n - 1) + py_fib(n - 2)

def py_prime_sieve():
    N = 10_000_000
    is_prime = [True] * (N + 1)
    is_prime[0] = is_prime[1] = False
    p = 2
    while p * p <= N:
        if is_prime[p]:
            for i in range(p * p, N + 1, p):
                is_prime[i] = False
        p += 1
    return sum(is_prime)

def py_quicksort_rec(arr, low, high):
    if low < high:
        pivot = arr[high]
        i = low - 1
        for j in range(low, high):
            if arr[j] < pivot:
                i += 1
                arr[i], arr[j] = arr[j], arr[i]
        arr[i + 1], arr[high] = arr[high], arr[i + 1]
        pi = i + 1
        py_quicksort_rec(arr, low, pi - 1)
        py_quicksort_rec(arr, pi + 1, high)

def py_quicksort():
    N = 50_000
    seed = 12345
    arr = []
    for _ in range(N):
        seed = (seed * 1103515245 + 12345) & 0x7fffffff;
        arr.append(seed)
    py_quicksort_rec(arr, 0, N - 1)
    return arr[-1]

def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "loop"
    if mode == "py_loop":
        py_loop_100m()
    elif mode == "py_fib":
        py_fib(35)
    elif mode == "py_sieve":
        py_prime_sieve()
    elif mode == "py_sort":
        py_quicksort()

if __name__ == "__main__":
    main()
