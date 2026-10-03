// Baseline for tools/bench/channels/arith.im -- the same algorithm with a
// static type.  The point of the comparison is that .im values are dynamically
// typed and this one is not; that difference is the thing being measured, not
// an accident of the translation.
#include <cstdio>
#include <cstdlib>

static long long sum(long long n) {
    long long s = 0;
    long long i = 0;
    while (i < n) {
        s = s + i;
        i = i + 1;
    }
    return s;
}

int main(int argc, char **argv) {
    long long N = atoll(argv[1]);
    printf("%lld\n", sum(N));
    return 0;
}
