from eidos_desugar import translate

def main():
    src = 'eidos Player { name = "eidos ed" }\n// eidos\ned = 1\n'
    out = translate(src)
    assert out.startswith('record Player')
    assert '"eidos ed"' in out
    assert '// eidos' in out
    assert 'record = 1' in out
    print('eidos desugar: ok')

if __name__ == '__main__': main()
