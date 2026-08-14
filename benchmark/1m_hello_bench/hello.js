const msg = "Hello, World!\n";
for (let i = 0; i < 1000000; i++) {
    process.stdout.write(msg);
}
