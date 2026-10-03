// Baseline for tools/bench/channels/arith.im -- same algorithm, static types.
fn sum(n: i64) -> i64 {
    let mut s: i64 = 0;
    let mut i: i64 = 0;
    while i < n {
        s = s + i;
        i = i + 1;
    }
    s
}

fn main() {
    let n: i64 = std::env::args().nth(1).unwrap().parse().unwrap();
    println!("{}", sum(n));
}
