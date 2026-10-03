#include <cstdio>
#include <cstdlib>

static long long work(long long n) {
    long long total = 0;
    long long i = 0;
    while (i < n) {
        long long j = 0;
        while (j < n) {
            total = (total + i * j) % 1000000007;
            j = j + 1;
        }
        i = i + 1;
    }
    return total;
}

int main(int argc, char **argv) {
    printf("%lld\n", work(atoll(argv[1])));
    return 0;
}
