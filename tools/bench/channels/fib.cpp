#include <cstdio>
#include <cstdlib>

static long long fib(long long n) {
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
}

int main(int argc, char **argv) {
    long long N = atoll(argv[1]);
    printf("%lld\n", fib(N));
    return 0;
}
