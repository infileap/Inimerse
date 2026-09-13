import subprocess
import sys
import tempfile
from pathlib import Path

from eidos_desugar import translate


def main():
    if len(sys.argv) != 2:
        raise SystemExit('usage: eidos_runtime.test.py INIMERSE')
    source = '''\
eidos Counter {
  value = 0
  inc(n) { value += n; return value }
  get -> value
}
c = Counter(4)
say c["get"]()
say c["inc"](3)

eidos Base {
  value = 1
  inc(n) { return value + n }
  base_only -> value
}
eidos Child: Base {
  value = 4
  inc(n) { return super.inc(n) + 1 }
}
child = Child()
say child["inc"](2)
say child["base_only"]()
say Base()["inc"](2)

func default_arg(x) { return x ?? 9 }
say default_arg()
'''
    with tempfile.TemporaryDirectory(prefix='eidos-runtime-') as td:
        script = Path(td) / 'counter.im'
        script.write_text(translate(source), encoding='utf-8')
        result = subprocess.run(
            [sys.argv[1], str(script)],
            capture_output=True, text=True, check=False,
        )
        if result.returncode != 0:
            raise SystemExit(result.stderr or 'translated Eidos program failed')
        lines = [line.strip() for line in result.stdout.splitlines()
                 if line.strip() in {'3', '4', '7', '9'}]
        if lines[-6:] != ['4', '7', '7', '4', '3', '9']:
            raise SystemExit(f'unexpected Eidos output: {result.stdout}')
    print('eidos runtime: ok')


if __name__ == '__main__':
    main()
