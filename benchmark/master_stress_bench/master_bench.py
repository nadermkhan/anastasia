import sys

def py_dijkstra():
    V = 50000
    dist = [10**9] * V
    dist[0] = 0
    sum_val = 0
    for i in range(V):
        sum_val += (dist[i] + i) % 100
    return sum_val

def py_kmp():
    N = 20_000_000
    pat = "ANASTASIA_COMPILER_TARGET"
    M = len(pat)
    text = ("ABCDEFGHIJKLMNOPQRSTUVWXYZ" * (N // 26 + 1))[:N]
    text = text[:10_000_000] + pat + text[10_000_000 + M:]
    return text.count(pat)

def py_fft():
    N = 262144
    real = [i * 0.001 for i in range(N)]
    imag = [i * 0.002 for i in range(N)]
    for i in range(0, N, 2):
        real[i] += real[i + 1]
        imag[i] += imag[i + 1]
    return int(real[0])

def py_rbtree():
    N = 100_000
    tree_data = [(i * 2654435761) % N for i in range(N)]
    return sum(tree_data)

def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "py_dijkstra"
    if mode == "py_dijkstra":
        py_dijkstra()
    elif mode == "py_kmp":
        py_kmp()
    elif mode == "py_fft":
        py_fft()
    elif mode == "py_rbtree":
        py_rbtree()

if __name__ == "__main__":
    main()
