try {
  null.foo;
} catch (e) {
  console.log(e.name + " | " + e.message);
}
