fn run(n: i64) -> i64 {
    let mut s: i64 = 0;
    let mut x: i64 = 1;
    let mut i: i64 = 1;
    while i <= n {
        x = (x * 31) % 1000003;
        if x % 2 == 0 { s = s + i; } else { s = s - 1; }
        i = i + 1;
    }
    s
}

fn main() {
    let n: i64 = std::env::args().nth(1).unwrap().parse().unwrap();
    println!("{}", run(n));
}
