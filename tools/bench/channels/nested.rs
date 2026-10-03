fn work(n: i64) -> i64 {
    let mut total: i64 = 0;
    let mut i: i64 = 0;
    while i < n {
        let mut j: i64 = 0;
        while j < n {
            total = (total + i * j) % 1000000007;
            j = j + 1;
        }
        i = i + 1;
    }
    total
}

fn main() {
    let n: i64 = std::env::args().nth(1).unwrap().parse().unwrap();
    println!("{}", work(n));
}
