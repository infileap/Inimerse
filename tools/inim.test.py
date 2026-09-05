import json, os, subprocess, sys, tempfile, zipfile
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
        dep_spec = json.loads((root / 'manifest.json').read_text(encoding='utf-8'))['dependencies']['other/lib']
        assert dep_spec['sha256']
        run('add', 'other/lib', '>=0.1.0 <1.0.0', '-p', str(root))
        run('doctor', str(root))
        manifest = json.loads((root / 'manifest.json').read_text(encoding='utf-8'))
        assert manifest['dependencies']['other/lib'] == '>=0.1.0 <1.0.0'
        (root / 'manifest.json').write_text(json.dumps({**manifest, 'version': 'bad'}, indent=2), encoding='utf-8')
        assert subprocess.run(CLI + ['pack', str(root), '-o', str(Path(td) / 'bad.inim')]).returncode != 0
        (root / 'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
        run('publish', '-p', str(dep_root), '-o', str(Path(td) / 'dist'))
        run('publish', '-p', str(root), '-o', str(Path(td) / 'dist'))
        index = json.loads((Path(td) / 'dist' / 'index.json').read_text(encoding='utf-8'))
        assert index['packages']['demo/app']['0.1.0']['file'].endswith('.inim')
        run('verify', str(Path(td) / 'dist'))
        run('update', '-p', str(root), '-r', str(Path(td) / 'dist'))
        prev_engine = os.environ.get('INIMERSE_ENGINE_VERSION'); os.environ['INIMERSE_ENGINE_VERSION'] = '0.3.0'
        assert subprocess.run(CLI + ['install', str(dep_pkg), '-t', str(target)]).returncode != 0
        if prev_engine is None: os.environ.pop('INIMERSE_ENGINE_VERSION', None)
        else: os.environ['INIMERSE_ENGINE_VERSION'] = prev_engine
        (Path(td) / 'dist' / index['packages']['demo/app']['0.1.0']['file']).write_bytes(b'tampered')
        assert subprocess.run(CLI + ['verify', str(Path(td) / 'dist')]).returncode != 0
        malicious_index = {'index_version': 1, 'packages': {'evil': {'1.0.0': {'file': '../outside.inim', 'sha256': '0' * 64}}}}
        (Path(td) / 'dist' / 'index.json').write_text(json.dumps(malicious_index), encoding='utf-8')
        assert subprocess.run(CLI + ['verify', str(Path(td) / 'dist')]).returncode != 0
        run('remove', 'demo/app', '-p', str(target))
        assert not (target / '.inim-cache' / 'demo' / 'app' / '0.1.0').exists()
        bad = Path(td) / 'bad.inim'
        with zipfile.ZipFile(bad, 'w') as z:
            z.writestr('manifest.json', '{"name":"bad","version":"1.0.0"}')
            z.writestr('../escape.txt', 'no')
        assert subprocess.run(CLI + ['install', str(bad), '-t', str(target)]).returncode != 0
        bad_name = Path(td) / 'bad-name.inim'
        with zipfile.ZipFile(bad_name, 'w') as z:
            z.writestr('manifest.json', '{"name":"../escape","version":"1.0.0"}')
        assert subprocess.run(CLI + ['install', str(bad_name), '-t', str(target)]).returncode != 0
    print('inim tests: ok')

if __name__ == '__main__': main()
