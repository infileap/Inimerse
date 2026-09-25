import subprocess
import sys
import tempfile
from pathlib import Path


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
say len(c)
say has(c, "value")
say has(c, "missing")
say type(c)
say str(c)
alias = c
other = Counter(4)
say alias == c
say c == other
gc_auto(1)
gc_now()
say type(c)
say c["get"]()

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

eidos Named {
  name = "anon"
  hp = 10
  label -> name + ":" + str(hp)
}
named = Named(hp = 12)
say named.label
mixed = Named("mixed", hp = 13)
say mixed.label

eidos Flying {
  flying = true
  fly -> "fly:" + name
}
eidos Dragon: Named + Flying {
  name = "dragon"
}
dragon = Dragon()
say dragon.fly
say dragon.label
say dragon.name
dragon.name = "wyrm"
say dragon.name

eidos Player {
  _hp = 100
  get hp -> _hp
  set hp(v) { if (v < 0) { _hp = 0 } else { _hp = v } }
}
player = Player()
say player.hp
player.hp = -3
say player.hp

eidos Vector {
  x = 0
  y = 0
  +(v) -> Vector(x + v.x, y + v.y)
}
v1 = Vector(1, 2)
v2 = Vector(3, 4)
v3 = v1 + v2
say v3.x
say v3.y

eidos Spawned {
  value = 0
  on_spawn { value = 7 }
  me -> this
  same -> self
}
spawned = Spawned()
say spawned.value
say spawned.me["value"]
say spawned.same["value"]

eidos Guarded {
  value = 0
  invariant {
    value >= 0
  }
  on_violation { value = 0 }
}
guarded = Guarded(-2)
say guarded.value
guarded.value = -5
say guarded.value

func default_arg(x) { return x ?? 9 }
say default_arg()
'''
    with tempfile.TemporaryDirectory(prefix='eidos-runtime-') as td:
        script = Path(td) / 'counter.im'
        script.write_text(source, encoding='utf-8')
        result = subprocess.run(
            [sys.argv[1], '--no-mods', str(script)],
            capture_output=True, text=True, check=False,
        )
        if result.returncode != 0:
            raise SystemExit(result.stderr or 'native Eidos program failed')
        lines = [line.strip() for line in result.stdout.splitlines()
                 if line.strip() in {'3', '4', '7', '9', 'anon:12', 'mixed:13',
                                     'fly:dragon', 'dragon:10', 'dragon', 'wyrm',
                                     '100', '0', '1', '4', '6', '7', 'Counter',
                                     '<Counter>', 'true', 'false'}]
        expected = ['4', '7', '3', 'true', 'false', 'Counter', '<Counter>', 'true', 'false',
                    'Counter', '7', '7', '4', '3', 'anon:12', 'mixed:13',
                    'fly:dragon', 'dragon:10', 'dragon', 'wyrm',
                    '100', '0', '4', '6', '7', '7', '7', '0', '0', '9']
        if lines[-len(expected):] != expected:
            raise SystemExit(f'unexpected Eidos output: {result.stdout}')
    print('eidos native runtime: ok')


if __name__ == '__main__':
    main()
