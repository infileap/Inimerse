import json, subprocess, sys, tempfile, zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
CLI = [sys.executable, str(HERE / 'inim.py')]

def run(*args):
    return subprocess.run(CLI + list(args), check=True, capture_output=True, text=True)

def main():
    with tempfile.TemporaryDirectory(prefix='inim-test-') as td:
        root = Path(td) / 'app'; run('init', str(root), '--name', 'demo/app')
        (root / 'src' / 'main.im').write_text('say("ok")\n', encoding='utf-8')
        pkg = Path(td) / 'demo.inim'; run('pack', str(root), '-o', str(pkg))
        dep_root = Path(td) / 'dep'; run('init', str(dep_root), '--name', 'other/lib')
        dep_pkg = Path(td) / 'other.inim'; run('pack', str(dep_root), '-o', str(dep_pkg))
        run('add', '--package', str(dep_pkg), '-p', str(root))
        target = Path(td) / 'target'; target.mkdir(); run('install', str(pkg), '-t', str(target))
        lock = json.loads((target / 'lock.json').read_text(encoding='utf-8'))
        assert lock['packages']['demo/app']['version'] == '0.1.0'
        assert (target / '.inim-cache' / 'demo' / 'app' / '0.1.0' / 'src' / 'main.im').is_file()
        run('install', '-t', str(root))
        assert (root / '.inim-cache' / 'other' / 'lib' / '0.1.0').is_dir()
        run('add', 'other/lib', '>=1.0.0 <2.0.0', '-p', str(root))
        manifest = json.loads((root / 'manifest.json').read_text(encoding='utf-8'))
        assert manifest['dependencies']['other/lib'] == '>=1.0.0 <2.0.0'
        run('remove', 'demo/app', '-p', str(target))
        assert not (target / '.inim-cache' / 'demo' / 'app' / '0.1.0').exists()
        bad = Path(td) / 'bad.inim'
        with zipfile.ZipFile(bad, 'w') as z:
            z.writestr('manifest.json', '{"name":"bad","version":"1.0.0"}')
            z.writestr('../escape.txt', 'no')
        assert subprocess.run(CLI + ['install', str(bad), '-t', str(target)]).returncode != 0
    print('inim tests: ok')

if __name__ == '__main__': main()
