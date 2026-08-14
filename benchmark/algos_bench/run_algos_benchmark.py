import time
import subprocess
import sys

def measure(cmd_args, runs=3):
    durations = []
    for r in range(runs):
        start = time.perf_counter()
        res = subprocess.run(cmd_args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        elapsed = (time.perf_counter() - start) * 1000.0
        if res.returncode != 0:
            print(f"Error running {cmd_args}: {res.stderr.decode('utf-8')}")
            return float('inf')
        durations.append(elapsed)
    return sum(durations) / len(durations)

def main():
    print("=======================================================================================")
    print("  Anastasia JIT Engine vs C (gcc -O3) vs Python 3 vs Node.js: Algorithm Benchmark Suite")
    print("=======================================================================================\n")

    workloads = [
        ("100M Iteration Math Loop", "./bench_algos ana_loop", "./bench_algos c_loop", "python3 bench_algos.py py_loop", "node bench_algos.js js_loop"),
        ("Recursive Fibonacci N=40", "./bench_algos ana_fib", "./bench_algos c_fib", "python3 bench_algos.py py_fib", "node bench_algos.js js_fib"),
        ("Prime Sieve 10M", "./bench_algos ana_sieve", "./bench_algos c_sieve", "python3 bench_algos.py py_sieve", "node bench_algos.js js_sieve"),
        ("QuickSort 500K Integers", "./bench_algos ana_sort", "./bench_algos c_sort", "python3 bench_algos.py py_sort", "node bench_algos.js js_sort"),
    ]

    print(f"{'Workload Name':<28} | {'Anastasia JIT':<15} | {'C (gcc -O3)':<15} | {'Python 3':<15} | {'Node.js':<15}")
    print("-" * 98)

    for name, ana_cmd, c_cmd, py_cmd, js_cmd in workloads:
        ana_ms = measure(ana_cmd.split())
        c_ms = measure(c_cmd.split())
        py_ms = measure(py_cmd.split())
        js_ms = measure(js_cmd.split())

        print(f"{name:<28} | {ana_ms:<15.2f} | {c_ms:<15.2f} | {py_ms:<15.2f} | {js_ms:<15.2f}")

    print("\n=======================================================================================")

if __name__ == "__main__":
    main()
