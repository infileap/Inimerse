import subprocess
import sys
import tempfile
from pathlib import Path


def main():
    if len(sys.argv) != 2:
        raise SystemExit('usage: eidos_native_desugar.test.py INIMERSE')
    source = r'''\
eidos Player { name = "eidos ed" }
// eidos
ed = 1

ed Alias {
  value = 3
}

eidos Probe {
  value = 2
  quoted = "text with escaped quote: \"probe.value + probe.value\""
  inspect(a, b) {
    #[ value should stay text ]#
    // value should stay text
    return value + a + b
  }
}
probe = Probe()
say "probe.value + probe.value"
say "escaped quote: \"probe.value + probe.value\""
// probe.value + probe.value
#[ probe.value + probe.value should stay text ]#
'''
    with tempfile.TemporaryDirectory(prefix='eidos-native-desugar-') as td:
        src = Path(td) / 'source.im'
        out = Path(td) / 'out.im'
        src.write_text(source, encoding='utf-8')
        result = subprocess.run(
            [sys.argv[1], '--desugar', str(src), str(out)],
            capture_output=True, text=True, check=False,
        )
        if result.returncode != 0:
            raise SystemExit(result.stderr or 'native Eidos desugar failed')
        text = out.read_text(encoding='utf-8')
        assert text.startswith('func Player(')
        assert 'func Alias(' in text
        assert '__eidos_obj = __eidos_new("Player")' in text
        assert '__eidos_obj["name"]' in text
        assert '"eidos ed"' in text
        assert '# eidos' in text
        assert 'record = 1' in text
        assert '#[ value should stay text ]#' in text
        assert '# value should stay text' in text
        assert 'return __eidos_obj["value"] + a + b' in text
        assert r'"text with escaped quote: \"probe.value + probe.value\""' in text
        assert r'say "probe.value + probe.value"' in text
        assert r'say "escaped quote: \"probe.value + probe.value\""' in text
        assert '# probe.value + probe.value' in text
        assert '#[ probe.value + probe.value should stay text ]#' in text
        assert '/*' not in text
        bad = Path(td) / 'bad.im'
        bad_out = Path(td) / 'bad-out.im'
        bad.write_text('eidos Child: Missing { value = 1 }\n', encoding='utf-8')
        rejected = subprocess.run(
            [sys.argv[1], '--desugar', str(bad), str(bad_out)],
            capture_output=True, text=True, check=False,
        )
        if rejected.returncode == 0:
            raise SystemExit('native Eidos desugar should reject undeclared parents')
        if 'declared before' not in rejected.stderr:
            raise SystemExit(f'unexpected native Eidos diagnostic: {rejected.stderr}')
    print('eidos native desugar: ok')


if __name__ == '__main__':
    main()
