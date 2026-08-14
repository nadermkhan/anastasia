import subprocess
import time

def run_bench(name, cmd, runs=5):
    print(f"[*] Benchmarking {name} (1,000,000 prints of 'Hello, World!')...")
    times = []
    for r in range(runs):
        start = time.perf_counter()
        res = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, shell=True)
        end = time.perf_counter()
        if res.returncode != 0:
            print(f"    [ERROR] {name} failed with exit code {res.returncode}")
            return None
        elapsed_ms = (end - start) * 1000.0
        times.append(elapsed_ms)
        print(f"    Run {r+1}: {elapsed_ms:.2f} ms")
    
    avg_ms = sum(times) / len(times)
    min_ms = min(times)
    max_ms = max(times)
    print(f"    ==> {name} Average: {avg_ms:.2f} ms (Min: {min_ms:.2f} ms, Max: {max_ms:.2f} ms)\n")
    return avg_ms

def main():
    print("=======================================================================")
    print("  Anastasia vs C vs Python vs Node.js: 1 Million 'Hello, World!' Benchmark")
    print("=======================================================================\n")

    results = {}
    results["Anastasia (64 KiB Stream Buffer)"] = run_bench("Anastasia (64 KiB Stream Buffer)", "./bench_1m_anastasia_buffered")
    results["Anastasia (Raw Unbuffered Syscalls)"] = run_bench("Anastasia (Raw Unbuffered Syscalls)", "./bench_1m_anastasia")
    results["C (gcc -O3 / buffered fwrite)"] = run_bench("C (gcc -O3 / buffered fwrite)", "./hello_c")
    results["Python 3 (v3.13.5)"] = run_bench("Python 3 (v3.13.5)", "python3 hello.py")
    results["Node.js (v20.19.2)"] = run_bench("Node.js (v20.19.2)", "node hello.js")

    print("=======================================================================")
    print("  FINAL HEAD-TO-HEAD SPEED BENCHMARK RESULTS (1,000,000 PRINTS)")
    print("=======================================================================")
    print(f"{'Language / Engine':<36} | {'Execution Time (ms)':<20} | {'Throughput (prints/sec)':<22} | {'Relative Speed':<15}")
    print("-" * 100)

    anastasia_ms = results["Anastasia (64 KiB Stream Buffer)"]
    for lang, ms in sorted(results.items(), key=lambda x: x[1]):
        tps = (1_000_000.0 / (ms / 1000.0))
        rel = f"{ms / anastasia_ms:.2f}x slower" if ms > anastasia_ms else f"{anastasia_ms / ms:.2f}x faster"
        print(f"{lang:<40} | {ms:<20.2f} | {tps:<22,.0f} | {rel:<15}")

if __name__ == "__main__":
    main()
