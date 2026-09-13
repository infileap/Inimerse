import functools, http.server, json, os, subprocess, sys, tempfile, threading, zipfile
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
        key = Path(td) / 'registry-key.pem'
        public = Path(td) / 'registry-key.pub.pem'
        run('keygen', '-o', str(key), '--public-output', str(public))
        signed_dist = Path(td) / 'signed-dist'
        run('publish', '-p', str(dep_root), '-o', str(signed_dist), '--signing-key', str(key))
        run('publish', '-p', str(root), '-o', str(signed_dist), '--signing-key', str(key))
        signed_index = json.loads((signed_dist / 'index.json').read_text(encoding='utf-8'))
        signed_item = signed_index['packages']['demo/app']['0.1.0']
        assert signed_item['signature']['algorithm'] == 'ed25519'
        assert signed_index['signature']['algorithm'] == 'ed25519'
        run('verify', str(signed_dist), '--require-signature', '--trusted-key', str(public))
        run('update', '-p', str(root), '-r', str(signed_dist),
            '--require-signature', '--trusted-key', str(public))
        handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=str(signed_dist))
        server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            run('update', '-p', str(root), '-r',
                f'http://127.0.0.1:{server.server_port}',
                '--require-signature', '--trusted-key', str(public))
            remote_index = json.loads((signed_dist / 'index.json').read_text(encoding='utf-8'))
            remote_index['packages']['other/lib']['0.1.0']['file'] = '../outside.inim'
            remote_index.pop('signature', None)
            (signed_dist / 'index.json').write_text(json.dumps(remote_index), encoding='utf-8')
            escaped = subprocess.run(
                CLI + ['update', '-p', str(root), '-r',
                       f'http://127.0.0.1:{server.server_port}'],
                capture_output=True, text=True
            )
            assert escaped.returncode != 0
            assert 'package URL escapes registry' in escaped.stderr
            remote_index['packages']['other/lib']['0.1.0']['file'] = '%2e%2e/outside.inim'
            (signed_dist / 'index.json').write_text(json.dumps(remote_index), encoding='utf-8')
            encoded_escape = subprocess.run(
                CLI + ['update', '-p', str(root), '-r',
                       f'http://127.0.0.1:{server.server_port}'],
                capture_output=True, text=True
            )
            assert encoded_escape.returncode != 0
            assert 'package URL escapes registry' in encoded_escape.stderr
        finally:
            (signed_dist / 'index.json').write_text(json.dumps(signed_index), encoding='utf-8')
            server.shutdown()
            server.server_close()
            thread.join(timeout=5)
        # The remote archive is content-addressed in the project cache, so
        # dependency installation remains reproducible after the registry is
        # unavailable.
        run('install', '-t', str(root), '--offline')
        cached_archives = list((root / '.inim-cache' / 'archives').glob('*.inim'))
        assert cached_archives
        other_key = Path(td) / 'other-key.pem'
        other_public = Path(td) / 'other-key.pub.pem'
        run('keygen', '-o', str(other_key), '--public-output', str(other_public))
        assert subprocess.run(
            CLI + ['verify', str(signed_dist), '--require-signature', '--trusted-key', str(other_public)],
            capture_output=True, text=True
        ).returncode != 0
        signed_item['signature']['signature'] = 'A' * 88
        (signed_dist / 'index.json').write_text(json.dumps(signed_index), encoding='utf-8')
        assert subprocess.run(CLI + ['verify', str(signed_dist)]).returncode != 0
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
