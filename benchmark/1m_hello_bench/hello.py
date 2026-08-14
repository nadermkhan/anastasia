import sys

def main():
    msg = "Hello, World!\n"
    write = sys.stdout.write
    for _ in range(1_000_000):
        write(msg)

if __name__ == "__main__":
    main()
