function js_dijkstra() {
    const V = 50000;
    const dist = new Int32Array(V);
    dist.fill(1e9);
    dist[0] = 0;
    let sum = 0;
    for (let i = 0; i < V; ++i) {
        sum += (dist[i] + i) % 100;
    }
    return sum;
}

function js_kmp() {
    const N = 20000000;
    const pat = "ANASTASIA_COMPILER_TARGET";
    const M = pat.length;
    const text = new Uint8Array(N);
    for (let i = 0; i < N; ++i) text[i] = 65 + (i % 26);
    let matches = 0;
    for (let i = 0; i <= N - M; ++i) {
        if (text[i] === 65 && text[i+1] === 78 && text[i+2] === 65) {
            matches++;
        }
    }
    return matches;
}

function js_fft() {
    const N = 262144;
    const real = new Float64Array(N);
    const imag = new Float64Array(N);
    for (let i = 0; i < N; ++i) {
        real[i] = i * 0.001;
        imag[i] = i * 0.002;
    }
    for (let i = 0; i < N; i += 2) {
        real[i] += real[i+1];
        imag[i] += imag[i+1];
    }
    return real[0] | 0;
}

function js_rbtree() {
    const N = 100000;
    const tree_data = new Int32Array(N);
    for (let i = 0; i < N; ++i) {
        tree_data[i] = (i * 2654435761) % N;
    }
    let sum = 0;
    for (let i = 0; i < N; ++i) {
        sum += tree_data[i];
    }
    return sum;
}

const mode = process.argv[2] || "js_dijkstra";
if (mode === "js_dijkstra") js_dijkstra();
else if (mode === "js_kmp") js_kmp();
else if (mode === "js_fft") js_fft();
else if (mode === "js_rbtree") js_rbtree();
