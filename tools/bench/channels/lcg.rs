fn run(n: i64) -> i64 {
    let mut x: i64 = 1;
    let mut i: i64 = 0;
    while i < n {
        x = (x * 31) % 1000003;
        i = i + 1;
    }
    x
}

fn main() {
    let n: i64 = std::env::args().nth(1).unwrap().parse().unwrap();
    println!("{}", run(n));
}
