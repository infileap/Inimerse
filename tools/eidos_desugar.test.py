from eidos_desugar import translate

def main():
    src = 'eidos Player { name = "eidos ed" }\n// eidos\ned = 1\n'
    out = translate(src)
    if not (out.startswith('func Player(')): raise AssertionError
    if not ('__eidos_obj["name"]' in out): raise AssertionError
    if not ('"eidos ed"' in out): raise AssertionError
    if not ('// eidos' in out): raise AssertionError
    if not ('record = 1' in out): raise AssertionError
    untouched = translate('msg = "eidos Player { not an object }"\n# eidos Fake { nope }\n')
    if not ('msg = "eidos Player { not an object }"' in untouched): raise AssertionError
    if not ('# eidos Fake { nope }' in untouched): raise AssertionError
    composed = translate('h = f >> g\n# f >> g\nmsg = "f >> g"\n')
    if not ('h = (x -> g(f(x)))' in composed): raise AssertionError
    if not ('# f >> g' in composed and '"f >> g"' in composed): raise AssertionError
    object_src = translate('''
eidos Counter {
  value = 0
  inc(n) { value += n; return value }
  get -> value
}
c = Counter(4)
say c["get"]()
say c["inc"](3)
''')
    if not ('func Counter__inc(__eidos_obj, n)' in object_src): raise AssertionError
    if not ('c = Counter(4)' in object_src): raise AssertionError
    if not ('/*' not in object_src): raise AssertionError
    commented = translate('''
eidos Probe {
  value = 2
  inspect(a, b) {
    #[ value should stay text ]#
    // value should stay text
    return value + a + b
  }
}
p = Probe()
say p["inspect"](3, 4)
''')
    if not ('#[ value should stay text ]#' in commented): raise AssertionError
    if not ('// value should stay text' in commented): raise AssertionError
    if not ('return __eidos_obj["value"] + a + b' in commented): raise AssertionError
    inherited = translate('''
eidos Base {
  value = 1
  inc(n) { return value + n }
}
eidos Child: Base {
  value = 4
  inc(n) { return super.inc(n) + 1 }
}
c = Child()
say c["inc"](2)
''')
    if not ('func Base__inc(__eidos_obj, n)' in inherited): raise AssertionError
    if not ('func Child__inc(__eidos_obj, n)' in inherited): raise AssertionError
    if not ('Base__inc(__eidos_obj, n)' in inherited): raise AssertionError
    if not ('Child( )' not in inherited): raise AssertionError

    try:
        translate('eidos Child: Parent + Flying { value = 1 }\n')
    except ValueError as exc:
        if not ('mixins' in str(exc)): raise AssertionError
    else:
        raise AssertionError('Eidos mixins must be rejected by the v0.4 subset')
    print('eidos desugar: ok')

if __name__ == '__main__': main()
