from eidos_desugar import translate

def main():
    src = 'eidos Player { name = "eidos ed" }\n// eidos\ned = 1\n'
    out = translate(src)
    assert out.startswith('func Player(')
    assert '__eidos_obj["name"]' in out
    assert '"eidos ed"' in out
    assert '// eidos' in out
    assert 'record = 1' in out
    untouched = translate('msg = "eidos Player { not an object }"\n# eidos Fake { nope }\n')
    assert 'msg = "eidos Player { not an object }"' in untouched
    assert '# eidos Fake { nope }' in untouched
    composed = translate('h = f >> g\n# f >> g\nmsg = "f >> g"\n')
    assert 'h = (x -> g(f(x)))' in composed
    assert '# f >> g' in composed and '"f >> g"' in composed
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
    assert 'func Counter__inc(__eidos_obj, n)' in object_src
    assert 'c = Counter(4)' in object_src
    assert '/*' not in object_src
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
    assert '#[ value should stay text ]#' in commented
    assert '// value should stay text' in commented
    assert 'return __eidos_obj["value"] + a + b' in commented
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
    assert 'func Base__inc(__eidos_obj, n)' in inherited
    assert 'func Child__inc(__eidos_obj, n)' in inherited
    assert 'Base__inc(__eidos_obj, n)' in inherited
    assert 'Child( )' not in inherited

    try:
        translate('eidos Child: Parent + Flying { value = 1 }\n')
    except ValueError as exc:
        assert 'mixins' in str(exc)
    else:
        raise AssertionError('Eidos mixins must be rejected by the v0.4 subset')
    print('eidos desugar: ok')

if __name__ == '__main__': main()
