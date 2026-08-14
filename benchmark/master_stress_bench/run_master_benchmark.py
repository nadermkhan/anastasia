import time
import subprocess

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
    print("=================================================================================================")
    print("  Anastasia JIT Engine vs C (gcc -O3) vs Python 3 vs Node.js: Master-Level Benchmark Suite")
    print("=================================================================================================\n")

    workloads = [
        ("Dijkstra Graph Shortest Path", "./master_bench ana_dijkstra", "./master_bench c_dijkstra", "python3 master_bench.py py_dijkstra", "node master_bench.js js_dijkstra"),
        ("Red-Black Tree 100K Ops", "./master_bench ana_rbtree", "./master_bench c_rbtree", "python3 master_bench.py py_rbtree", "node master_bench.js js_rbtree"),
        ("KMP String Search (20M Chars)", "./master_bench ana_kmp", "./master_bench c_kmp", "python3 master_bench.py py_kmp", "node master_bench.js js_kmp"),
        ("Fast Fourier Transform (FFT 256K)", "./master_bench ana_fft", "./master_bench c_fft", "python3 master_bench.py py_fft", "node master_bench.js js_fft"),
    ]

    print(f"{'Master Workload Name':<32} | {'Anastasia JIT':<15} | {'C (gcc -O3)':<15} | {'Python 3':<15} | {'Node.js':<15}")
    print("-" * 100)

    for name, ana_cmd, c_cmd, py_cmd, js_cmd in workloads:
        ana_ms = measure(ana_cmd.split())
        c_ms = measure(c_cmd.split())
        py_ms = measure(py_cmd.split())
        js_ms = measure(js_cmd.split())

        print(f"{name:<32} | {ana_ms:<15.2f} | {c_ms:<15.2f} | {py_ms:<15.2f} | {js_ms:<15.2f}")

    print("\n=================================================================================================")

if __name__ == "__main__":
    main()
