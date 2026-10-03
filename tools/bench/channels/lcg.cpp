// Multiplier 31 and modulus 1000003 keep every product inside int32, so the
// interpreter's modulo path (src/vm/vm.c:3815) computes the same answer as
// the AOT, C++ and Rust channels. A larger multiplier would overflow int32 in
// the interpreter and the correctness gate would refuse to report a time.
#include <cstdio>
#include <cstdlib>

static long long run(long long n) {
    long long x = 1;
    long long i = 0;
    while (i < n) {
        x = (x * 31) % 1000003;
        i = i + 1;
    }
    return x;
}

int main(int argc, char **argv) {
    printf("%lld\n", run(atoll(argv[1])));
    return 0;
}
