fn main() {
    let r: Result<i32, String> = Err("division_by_zero".into());
    println!("{:?}", r);
}
