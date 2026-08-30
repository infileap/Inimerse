from eidos_desugar import translate

def main():
    src = 'eidos Player { name = "eidos ed" }\n// eidos\ned = 1\n'
    out = translate(src)
    assert out.startswith('record Player')
    assert '"eidos ed"' in out
    assert '// eidos' in out
    assert 'record = 1' in out
    composed = translate('h = f >> g\n# f >> g\nmsg = "f >> g"\n')
    assert 'h = (x -> g(f(x)))' in composed
    assert '# f >> g' in composed and '"f >> g"' in composed
    print('eidos desugar: ok')

if __name__ == '__main__': main()
