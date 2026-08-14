#include <stdio.h>
#include <unistd.h>

int main() {
    const char msg[] = "Hello, World!\n";
    for (int i = 0; i < 1000000; ++i) {
        fwrite(msg, 1, 14, stdout);
    }
    return 0;
}
