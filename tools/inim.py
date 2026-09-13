#!/usr/bin/env python3
"""Inim V0.4 package manager (offline-first minimal implementation)."""
import argparse, base64, hashlib, json, os, re, shutil, tempfile, urllib.error, urllib.parse, urllib.request, zipfile, subprocess
from pathlib import Path

def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for b in iter(lambda: f.read(1024 * 1024), b''): h.update(b)
    return h.hexdigest()

def crypto_backend():
    try:
        from cryptography.hazmat.primitives import serialization
        from cryptography.hazmat.primitives.asymmetric import ed25519
        return serialization, ed25519
    except ImportError as exc:
        raise SystemExit(
            'inim: Ed25519 signing requires the Python cryptography package '
            '(pip install cryptography)'
        ) from exc

def signature_payload(name, version, digest):
    return f'inim-package-v1\n{name}\n{version}\n{digest}\n'.encode('utf-8')

def registry_payload(data):
    body = {
        'index_version': data.get('index_version', 1),
        'packages': data.get('packages', {}),
    }
    return (
        b'inim-registry-v1\n'
        + json.dumps(body, ensure_ascii=False, sort_keys=True, separators=(',', ':')).encode('utf-8')
        + b'\n'
    )

def load_private_key(path):
    serialization, ed25519 = crypto_backend()
    try:
        key = serialization.load_pem_private_key(
            Path(path).read_bytes(), password=None
        )
    except Exception as exc:
        raise SystemExit(f'inim: invalid Ed25519 private key: {exc}') from exc
    if not isinstance(key, ed25519.Ed25519PrivateKey):
        raise SystemExit('inim: signing key is not Ed25519')
    return key

def load_public_key_file(path):
    serialization, ed25519 = crypto_backend()
    try:
        key = serialization.load_pem_public_key(Path(path).read_bytes())
    except Exception as exc:
        raise SystemExit(f'inim: invalid Ed25519 public key: {exc}') from exc
    if not isinstance(key, ed25519.Ed25519PublicKey):
        raise SystemExit('inim: trusted key is not Ed25519')
    return key

def public_key_der(key):
    serialization, _ = crypto_backend()
    return key.public_bytes(
        serialization.Encoding.DER,
        serialization.PublicFormat.SubjectPublicKeyInfo,
    )

def load_public_key(value):
    serialization, ed25519 = crypto_backend()
    try:
        raw = base64.b64decode(value, validate=True)
        key = serialization.load_der_public_key(raw)
    except Exception as exc:
        raise SystemExit(f'inim: invalid Ed25519 public key: {exc}') from exc
    if not isinstance(key, ed25519.Ed25519PublicKey):
        raise SystemExit('inim: signature public key is not Ed25519')
    return key

def make_signature(name, version, digest, private_key):
    return make_detached_signature(signature_payload(name, version, digest), private_key)

def make_detached_signature(payload, private_key):
    serialization, _ = crypto_backend()
    public_key = private_key.public_key().public_bytes(
        serialization.Encoding.DER,
        serialization.PublicFormat.SubjectPublicKeyInfo,
    )
    signature = private_key.sign(payload)
    return {
        'algorithm': 'ed25519',
        'public_key': base64.b64encode(public_key).decode('ascii'),
        'signature': base64.b64encode(signature).decode('ascii'),
    }

def verify_signature(name, version, digest, item, trusted_key=None):
    verify_detached_signature(signature_payload(name, version, digest), item,
                              f'{name}@{version}', trusted_key)

def verify_detached_signature(payload, item, subject, trusted_key=None):
    if not isinstance(item, dict):
        raise SystemExit(f'inim: invalid signature for {subject}')
    if item.get('algorithm') != 'ed25519':
        raise SystemExit(f'inim: unsupported signature algorithm for {subject}')
    try:
        signature = base64.b64decode(item.get('signature', ''), validate=True)
    except Exception as exc:
        raise SystemExit(f'inim: invalid signature encoding for {subject}') from exc
    if len(signature) != 64:
        raise SystemExit(f'inim: invalid Ed25519 signature length for {subject}')
    key = load_public_key(item.get('public_key', ''))
    if trusted_key is not None and public_key_der(key) != public_key_der(trusted_key):
        raise SystemExit(f'inim: untrusted signing key for {subject}')
    try:
        key.verify(signature, payload)
    except Exception as exc:
        raise SystemExit(f'inim: signature verification failed for {subject}') from exc

def verify_registry_signature(data, trusted_key=None):
    signature = data.get('signature') if isinstance(data, dict) else None
    if signature:
        verify_detached_signature(registry_payload(data), signature, 'registry index', trusted_key)
    return bool(signature)

def valid_package_name(name):
    if not isinstance(name, str) or not name or name.startswith(('/', '\\')):
        return False
    parts = name.replace('\\', '/').split('/')
    return all(p not in ('', '.', '..') for p in parts) and all(re.fullmatch(r'[A-Za-z0-9._-]+', p) for p in parts)

def package_path(root, relative):
    base = Path(root).resolve(); candidate = (base / str(relative)).resolve()
    if candidate != base and base not in candidate.parents:
        raise SystemExit(f'inim: unsafe package index path: {relative}')
    return candidate

def is_registry_url(value):
    return urllib.parse.urlparse(str(value)).scheme in ('http', 'https')

def fetch_url(url, limit):
    try:
        with urllib.request.urlopen(url, timeout=15) as response:
            length = response.headers.get('Content-Length')
            if length and int(length) > limit:
                raise SystemExit(f'inim: registry response exceeds {limit} bytes')
            data = response.read(limit + 1)
    except (urllib.error.URLError, OSError, ValueError) as exc:
        raise SystemExit(f'inim: cannot fetch registry resource {url}: {exc}') from exc
    if len(data) > limit:
        raise SystemExit(f'inim: registry response exceeds {limit} bytes')
    return data

def registry_package_url(registry_url, relative):
    if not isinstance(relative, str) or not relative:
        raise SystemExit('inim: invalid remote registry package path')
    parsed = urllib.parse.urlparse(relative)
    if parsed.scheme or parsed.netloc:
        raise SystemExit(f'inim: absolute package URL is not allowed: {relative}')
    # Reject traversal before URL normalization turns "../x" into a
    # different absolute path, which would otherwise hide the real error.
    decoded_path = urllib.parse.unquote(parsed.path)
    path_parts = decoded_path.replace('\\', '/').split('/')
    if '..' in path_parts:
        raise SystemExit(f'inim: package URL escapes registry: {relative}')
    base = urllib.parse.urlparse(registry_url)
    joined = urllib.parse.urljoin(registry_url.rstrip('/') + '/', relative)
    target = urllib.parse.urlparse(joined)
    if target.scheme != base.scheme or target.netloc != base.netloc:
        raise SystemExit(f'inim: package URL escapes registry: {relative}')
    base_path = base.path.rstrip('/') + '/'
    if not target.path.startswith(base_path):
        raise SystemExit(f'inim: package path escapes registry: {relative}')
    return joined

def load_manifest(root):
    p = Path(root) / 'manifest.json'
    if not p.is_file(): raise SystemExit(f'inim: missing {p}')
    try: m = json.loads(p.read_text(encoding='utf-8'))
    except Exception as e: raise SystemExit(f'inim: invalid manifest: {e}')
    for k in ('name', 'version', 'entry'):
        if not isinstance(m.get(k), str) or not m[k]: raise SystemExit(f'inim: manifest requires {k}')
    if not re.fullmatch(r'(?:0|[1-9]\d*)\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?', m['version']):
        raise SystemExit(f"inim: invalid semver version: {m['version']}")
    return m

def cmd_init(args):
    root = Path(args.path); root.mkdir(parents=True, exist_ok=True)
    (root / 'src').mkdir(exist_ok=True)
    m = {'name': args.name or root.name, 'version': '0.1.0', 'engine': '>=0.4.0', 'entry': 'src/main.im', 'dependencies': {}}
    p = root / 'manifest.json'
    if p.exists() and not args.force: raise SystemExit(f'inim: {p} exists (use --force)')
    p.write_text(json.dumps(m, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    (root / 'lock.json').write_text(json.dumps({'lock_version': 1, 'packages': {}}, indent=2) + '\n', encoding='utf-8')
    print(f'initialized {root}')

def iter_files(root):
    for p in sorted(Path(root).rglob('*')):
        if p.is_file() and '.inim-cache' not in p.parts and p.name not in ('lock.json', 'manifest.json'): yield p

def cmd_pack(args):
    root = Path(args.path).resolve(); m = load_manifest(root); out = Path(args.output or f"{m['name'].replace('/', '-')}-{m['version']}.inim")
    out.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(out, 'w', compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr('manifest.json', json.dumps(m, indent=2, ensure_ascii=False) + '\n')
        for p in iter_files(root): z.write(p, p.relative_to(root).as_posix())
    print(f'{out} {sha256(out)}')

def safe_extract(z, target):
    base = Path(target).resolve()
    for n in z.namelist():
        q = (base / n).resolve()
        if q != base and base not in q.parents: raise SystemExit(f'inim: unsafe package path: {n}')
    z.extractall(base)

def archive_cache_path(target, digest):
    if not isinstance(digest, str) or not re.fullmatch(r'[0-9a-fA-F]{64}', digest):
        return None
    return Path(target) / '.inim-cache' / 'archives' / (digest.lower() + '.inim')

def cache_archive(target, package, digest=None):
    digest = digest or sha256(package)
    cached = archive_cache_path(target, digest)
    if cached is None:
        return None
    cached.parent.mkdir(parents=True, exist_ok=True)
    if not cached.exists():
        shutil.copyfile(package, cached)
    return cached

def cmd_install(args):
    target = Path(args.target or '.')
    if not args.package:
        manifest = load_manifest(target); deps = manifest.get('dependencies', {})
        if not deps: print('nothing to install'); return
        lock_path = target / 'lock.json'
        try:
            lock = json.loads(lock_path.read_text(encoding='utf-8')) if lock_path.exists() else {}
        except (OSError, json.JSONDecodeError) as exc:
            raise SystemExit(f'inim: invalid lock.json: {exc}') from exc
        for name, spec in deps.items():
            dep = None
            expected_hash = None
            if isinstance(spec, dict) and spec.get('path'):
                dep = (target / spec['path']).resolve()
                expected_hash = spec.get('sha256')
                if not dep.is_file() and expected_hash:
                    dep = archive_cache_path(target, expected_hash)
            elif isinstance(spec, str):
                locked = lock.get('packages', {}).get(name, {})
                expected_hash = locked.get('sha256') if isinstance(locked, dict) else None
                dep = archive_cache_path(target, expected_hash)
                if dep is None or not dep.is_file():
                    locked_path = locked.get('path') if isinstance(locked, dict) else None
                    dep = Path(locked_path) if locked_path else None
            if dep is None or not dep.exists():
                if getattr(args, 'offline', False):
                    raise SystemExit(f'inim: offline package not found in cache: {name}')
                raise SystemExit(f'inim: dependency {name} requires a cached package; run update first')
            if dep.is_file() and expected_hash and sha256(dep) != expected_hash:
                raise SystemExit(f'inim: dependency hash mismatch: {name}')
            if dep.is_dir():
                cached_manifest = dep / 'manifest.json'
                if not cached_manifest.is_file():
                    raise SystemExit(f'inim: invalid cached package: {dep}')
                print(f'using cached {name} -> {dep}')
            else:
                cmd_install(argparse.Namespace(package=str(dep), target=str(target)))
        return
    pkg = Path(args.package)
    if not pkg.is_file(): raise SystemExit(f'inim: package not found: {pkg}')
    package_digest = sha256(pkg)
    cache_archive(target, pkg, package_digest)
    with zipfile.ZipFile(pkg) as z:
        try: m = json.loads(z.read('manifest.json').decode('utf-8'))
        except Exception as e: raise SystemExit(f'inim: invalid package manifest: {e}')
        if not valid_package_name(m.get('name')) or not isinstance(m.get('version'), str): raise SystemExit('inim: invalid package name/version')
        if not re.fullmatch(r'(?:0|[1-9]\d*)\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?', m['version']): raise SystemExit('inim: invalid package version')
        if not engine_ok(m.get('engine')): raise SystemExit(f"inim: package requires engine {m.get('engine')}")
        dest = target / '.inim-cache' / m['name'] / m['version']; dest.mkdir(parents=True, exist_ok=True)
        safe_extract(z, dest)
    lock = target / 'lock.json'; data = json.loads(lock.read_text(encoding='utf-8')) if lock.exists() else {'lock_version': 1, 'packages': {}}
    data.setdefault('lock_version', 1); data.setdefault('packages', {})[m['name']] = {'version': m['version'], 'sha256': package_digest, 'path': str(dest)}
    lock.write_text(json.dumps(data, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    print(f"installed {m['name']}@{m['version']} -> {dest}")

def cmd_add(args):
    root = Path(args.path); manifest = load_manifest(root)
    dep_name = args.name
    if args.package:
        pkg = Path(args.package).resolve()
        if not pkg.is_file(): raise SystemExit(f'inim: package not found: {pkg}')
        with zipfile.ZipFile(pkg) as z: dep = json.loads(z.read('manifest.json').decode('utf-8'))
        if not valid_package_name(dep.get('name')): raise SystemExit('inim: invalid dependency package name')
        dep_name = dep.get('name'); spec = {'version': dep.get('version', '0.0.0'), 'path': os.path.relpath(pkg, root), 'sha256': sha256(pkg)}
    else:
        if not dep_name or not args.version: raise SystemExit('inim: add requires NAME VERSION or a .inim package')
        spec = args.version
    manifest.setdefault('dependencies', {})[dep_name] = spec
    (root / 'manifest.json').write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    print(f'added {dep_name}: {spec}')

def cmd_list(args):
    lock = Path(args.path) / 'lock.json'
    if not lock.exists(): return
    data = json.loads(lock.read_text(encoding='utf-8'))
    for n, p in sorted(data.get('packages', {}).items()): print(f"{n}@{p.get('version', '?')}  {p.get('sha256', '')}")

def cmd_remove(args):
    root = Path(args.path); lock = root / 'lock.json'
    if not lock.exists(): return
    data = json.loads(lock.read_text(encoding='utf-8')); item = data.get('packages', {}).pop(args.name, None)
    if item and item.get('path'):
        cache = Path(item['path'])
        if cache.exists() and (root / '.inim-cache') in cache.parents: import shutil; shutil.rmtree(cache)
    lock.write_text(json.dumps(data, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    print(f'removed {args.name}')

def cmd_run(args):
    root = Path(args.path).resolve(); m = load_manifest(root); entry = (root / m['entry']).resolve()
    if root not in entry.parents and entry != root: raise SystemExit('inim: entry escapes project root')
    if not entry.is_file(): raise SystemExit(f'inim: entry not found: {entry}')
    engine = args.engine or os.environ.get('INIMERSE_EXE', 'inimerse')
    raise SystemExit(subprocess.call([engine, str(entry), *args.args]))

def cmd_publish(args):
    root = Path(args.path).resolve(); m = load_manifest(root)
    outdir = Path(args.output or (root / 'dist')); outdir.mkdir(parents=True, exist_ok=True)
    pkg = outdir / f"{m['name'].replace('/', '-')}-{m['version']}.inim"
    cmd_pack(argparse.Namespace(path=str(root), output=str(pkg)))
    index = outdir / 'index.json'; data = json.loads(index.read_text(encoding='utf-8')) if index.exists() else {'index_version': 1, 'packages': {}}
    digest = sha256(pkg)
    item = {'file': pkg.name, 'sha256': digest, 'engine': m.get('engine', '')}
    if args.signing_key:
        item['signature'] = make_signature(
            m['name'], m['version'], digest, load_private_key(args.signing_key)
        )
    data.setdefault('index_version', 1)
    data.setdefault('packages', {}).setdefault(m['name'], {})[m['version']] = item
    if args.signing_key:
        data['signature'] = make_detached_signature(
            registry_payload(data), load_private_key(args.signing_key)
        )
    else:
        data.pop('signature', None)
    index.write_text(json.dumps(data, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    print(f"published {m['name']}@{m['version']} -> {pkg}")

def cmd_keygen(args):
    serialization, ed25519 = crypto_backend()
    private_path = Path(args.output)
    public_path = Path(args.public_output or (str(private_path) + '.pub.pem'))
    if (private_path.exists() or public_path.exists()) and not args.force:
        raise SystemExit('inim: key file exists (use --force)')
    private_path.parent.mkdir(parents=True, exist_ok=True)
    private_key = ed25519.Ed25519PrivateKey.generate()
    private_path.write_bytes(private_key.private_bytes(
        serialization.Encoding.PEM,
        serialization.PrivateFormat.PKCS8,
        serialization.NoEncryption(),
    ))
    public_path.write_bytes(private_key.public_key().public_bytes(
        serialization.Encoding.PEM,
        serialization.PublicFormat.SubjectPublicKeyInfo,
    ))
    try:
        os.chmod(private_path, 0o600)
    except OSError:
        pass
    print(f'generated Ed25519 keypair: {private_path} / {public_path}')

def cmd_verify(args):
    root = Path(args.path).resolve(); index = root / 'index.json'
    if not index.is_file(): raise SystemExit(f'inim: missing index.json in {root}')
    data = json.loads(index.read_text(encoding='utf-8')); checked = 0
    trusted_key = load_public_key_file(args.trusted_key) if args.trusted_key else None
    signed_index = verify_registry_signature(data, trusted_key)
    if args.require_signature and not signed_index:
        raise SystemExit('inim: registry index is not signed')
    for name, versions in data.get('packages', {}).items():
        for version, item in versions.items():
            if not isinstance(item, dict) or item.get('file', '') == '':
                raise SystemExit(f'inim: invalid index entry {name}@{version}')
            pkg = package_path(root, item.get('file', ''))
            if not pkg.is_file(): raise SystemExit(f'inim: missing package {name}@{version}: {pkg.name}')
            actual = sha256(pkg)
            if actual != item.get('sha256'): raise SystemExit(f'inim: hash mismatch {name}@{version}')
            signature = item.get('signature')
            if signature:
                verify_signature(name, version, actual, signature, trusted_key)
            elif args.require_signature:
                raise SystemExit(f'inim: missing signature for {name}@{version}')
            checked += 1
    mode = ' with Ed25519 signatures' if args.require_signature else ''
    print(f'verified {checked} package(s){mode}')

def ver_tuple(v):
    m = re.match(r'^(\d+)\.(\d+)\.(\d+)', v or '')
    return tuple(map(int, m.groups())) if m else None

def engine_ok(spec):
    current = ver_tuple(os.environ.get('INIMERSE_ENGINE_VERSION', '0.4.0'))
    for token in str(spec or '').split():
        if token.startswith('>='):
            required = ver_tuple(token[2:])
            if required and (not current or current < required): return False
    return True

def satisfies(version, spec):
    v = ver_tuple(version)
    if not v: return False
    for token in str(spec or '').split():
        op = '>=' if token.startswith('>=') else '<=' if token.startswith('<=') else '>' if token.startswith('>') else '<' if token.startswith('<') else '='
        rhs = token[len(op):] if op != '=' else token
        r = ver_tuple(rhs)
        if not r: continue
        if op == '>=' and not v >= r or op == '<=' and not v <= r or op == '>' and not v > r or op == '<' and not v < r or op == '=' and not v == r: return False
    return True

def cmd_update(args):
    root = Path(args.path).resolve(); manifest = load_manifest(root); changed = 0
    trusted_key = load_public_key_file(args.trusted_key) if args.trusted_key else None
    registry = str(args.registry)
    remote = is_registry_url(registry)
    temp_context = tempfile.TemporaryDirectory(prefix='inim-registry-') if remote else None
    try:
        if remote:
            index_url = registry.rstrip('/') + '/index.json'
            try:
                index = json.loads(fetch_url(index_url, 2 * 1024 * 1024).decode('utf-8'))
            except (UnicodeDecodeError, json.JSONDecodeError) as exc:
                raise SystemExit(f'inim: invalid remote registry index: {exc}') from exc
            index_root = None
        else:
            index_path = Path(registry).resolve() / 'index.json'
            if not index_path.is_file(): raise SystemExit(f'inim: missing registry index: {index_path}')
            try:
                index = json.loads(index_path.read_text(encoding='utf-8'))
            except (OSError, json.JSONDecodeError) as exc:
                raise SystemExit(f'inim: invalid registry index: {exc}') from exc
            index_root = index_path.parent
        signed_index = verify_registry_signature(index, trusted_key)
        if args.require_signature and not signed_index:
            raise SystemExit('inim: registry index is not signed')
        for name, spec in manifest.get('dependencies', {}).items():
            if isinstance(spec, dict): spec = spec.get('version', '')
            versions = index.get('packages', {}).get(name, {}) if isinstance(index, dict) else {}
            choices = [v for v in versions if satisfies(v, spec)]
            if not choices: raise SystemExit(f'inim: no registry version satisfies {name}: {spec}')
            chosen = max(choices, key=ver_tuple); item = versions[chosen]
            if not isinstance(item, dict):
                raise SystemExit(f'inim: invalid registry entry {name}@{chosen}')
            if remote:
                package_url = registry_package_url(registry, item.get('file', ''))
                package_data = fetch_url(package_url, 128 * 1024 * 1024)
                pkg = Path(temp_context.name) / Path(urllib.parse.urlparse(package_url).path).name
                pkg.write_bytes(package_data)
            else:
                pkg = index_root / item.get('file', '')
            digest = sha256(pkg)
            if digest != item.get('sha256'):
                raise SystemExit(f'inim: hash mismatch {name}@{chosen}')
            if item.get('signature'):
                verify_signature(name, chosen, digest, item['signature'], trusted_key)
            elif args.require_signature:
                raise SystemExit(f'inim: missing signature for {name}@{chosen}')
            cmd_install(argparse.Namespace(package=str(pkg), target=str(root))); changed += 1
    finally:
        if temp_context is not None:
            temp_context.cleanup()
    print(f'updated {changed} package(s)')

def cmd_doctor(args):
    root = Path(args.path).resolve(); m = load_manifest(root); entry = (root / m['entry']).resolve()
    if root not in entry.parents and entry != root: raise SystemExit('inim: manifest entry escapes project root')
    if not entry.is_file(): raise SystemExit(f'inim: entry not found: {m["entry"]}')
    lock = root / 'lock.json'
    if lock.exists():
        try: data = json.loads(lock.read_text(encoding='utf-8'))
        except Exception as e: raise SystemExit(f'inim: invalid lock.json: {e}')
        if data.get('lock_version') != 1 or not isinstance(data.get('packages', {}), dict): raise SystemExit('inim: unsupported lock.json format')
    for name, spec in m.get('dependencies', {}).items():
        if not isinstance(name, str) or not name or not isinstance(spec, (str, dict)): raise SystemExit(f'inim: invalid dependency declaration: {name}')
    print(f"doctor: ok ({m['name']}@{m['version']})")

def main():
    ap = argparse.ArgumentParser(prog='inim'); sp = ap.add_subparsers(dest='cmd', required=True)
    p = sp.add_parser('init'); p.add_argument('path', nargs='?', default='.'); p.add_argument('--name'); p.add_argument('--force', action='store_true'); p.set_defaults(fn=cmd_init)
    p = sp.add_parser('keygen'); p.add_argument('-o', '--output', required=True); p.add_argument('--public-output'); p.add_argument('--force', action='store_true'); p.set_defaults(fn=cmd_keygen)
    p = sp.add_parser('pack'); p.add_argument('path', nargs='?', default='.'); p.add_argument('-o', '--output'); p.set_defaults(fn=cmd_pack)
    p = sp.add_parser('install'); p.add_argument('package', nargs='?'); p.add_argument('-t', '--target', default='.'); p.add_argument('--offline', action='store_true'); p.set_defaults(fn=cmd_install)
    p = sp.add_parser('add'); p.add_argument('name', nargs='?'); p.add_argument('version', nargs='?'); p.add_argument('--package'); p.add_argument('-p', '--path', default='.'); p.set_defaults(fn=cmd_add)
    p = sp.add_parser('remove'); p.add_argument('name'); p.add_argument('-p', '--path', default='.'); p.set_defaults(fn=cmd_remove)
    p = sp.add_parser('run'); p.add_argument('args', nargs='*'); p.add_argument('-p', '--path', default='.'); p.add_argument('--engine'); p.set_defaults(fn=cmd_run)
    p = sp.add_parser('publish'); p.add_argument('-p', '--path', default='.'); p.add_argument('-o', '--output'); p.add_argument('--signing-key'); p.set_defaults(fn=cmd_publish)
    p = sp.add_parser('verify'); p.add_argument('path', nargs='?', default='.'); p.add_argument('--require-signature', action='store_true'); p.add_argument('--trusted-key'); p.set_defaults(fn=cmd_verify)
    p = sp.add_parser('update'); p.add_argument('-p', '--path', default='.'); p.add_argument('-r', '--registry', default='dist'); p.add_argument('--require-signature', action='store_true'); p.add_argument('--trusted-key'); p.set_defaults(fn=cmd_update)
    p = sp.add_parser('doctor'); p.add_argument('path', nargs='?', default='.'); p.set_defaults(fn=cmd_doctor)
    p = sp.add_parser('list'); p.add_argument('path', nargs='?', default='.'); p.set_defaults(fn=cmd_list)
    args = ap.parse_args(); args.fn(args)
if __name__ == '__main__': main()
