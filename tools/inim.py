#!/usr/bin/env python3
"""Inim V0.4 package manager (offline-first minimal implementation)."""
import argparse, hashlib, json, os, re, zipfile, subprocess
from pathlib import Path

def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for b in iter(lambda: f.read(1024 * 1024), b''): h.update(b)
    return h.hexdigest()

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

def cmd_install(args):
    target = Path(args.target or '.')
    if not args.package:
        manifest = load_manifest(target); deps = manifest.get('dependencies', {})
        if not deps: print('nothing to install'); return
        for name, spec in deps.items():
            if not isinstance(spec, dict) or not spec.get('path'):
                raise SystemExit(f'inim: dependency {name} requires a local path in V0.4')
            dep = (target / spec['path']).resolve()
            if not dep.is_file(): raise SystemExit(f'inim: dependency package not found: {dep}')
            cmd_install(argparse.Namespace(package=str(dep), target=str(target)))
        return
    pkg = Path(args.package)
    if not pkg.is_file(): raise SystemExit(f'inim: package not found: {pkg}')
    with zipfile.ZipFile(pkg) as z:
        try: m = json.loads(z.read('manifest.json').decode('utf-8'))
        except Exception as e: raise SystemExit(f'inim: invalid package manifest: {e}')
        if not isinstance(m.get('name'), str) or not isinstance(m.get('version'), str): raise SystemExit('inim: manifest missing name/version')
        dest = target / '.inim-cache' / m['name'] / m['version']; dest.mkdir(parents=True, exist_ok=True)
        safe_extract(z, dest)
    lock = target / 'lock.json'; data = json.loads(lock.read_text(encoding='utf-8')) if lock.exists() else {'lock_version': 1, 'packages': {}}
    data.setdefault('lock_version', 1); data.setdefault('packages', {})[m['name']] = {'version': m['version'], 'sha256': sha256(pkg), 'path': str(dest)}
    lock.write_text(json.dumps(data, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    print(f"installed {m['name']}@{m['version']} -> {dest}")

def cmd_add(args):
    root = Path(args.path); manifest = load_manifest(root)
    dep_name = args.name
    if args.package:
        pkg = Path(args.package).resolve()
        if not pkg.is_file(): raise SystemExit(f'inim: package not found: {pkg}')
        with zipfile.ZipFile(pkg) as z: dep = json.loads(z.read('manifest.json').decode('utf-8'))
        dep_name = dep.get('name'); spec = {'version': dep.get('version', '0.0.0'), 'path': os.path.relpath(pkg, root)}
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
    data.setdefault('index_version', 1); data.setdefault('packages', {}).setdefault(m['name'], {})[m['version']] = {'file': pkg.name, 'sha256': sha256(pkg), 'engine': m.get('engine', '')}
    index.write_text(json.dumps(data, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    print(f"published {m['name']}@{m['version']} -> {pkg}")

def cmd_verify(args):
    root = Path(args.path).resolve(); index = root / 'index.json'
    if not index.is_file(): raise SystemExit(f'inim: missing index.json in {root}')
    data = json.loads(index.read_text(encoding='utf-8')); checked = 0
    for name, versions in data.get('packages', {}).items():
        for version, item in versions.items():
            pkg = root / item.get('file', '')
            if not pkg.is_file(): raise SystemExit(f'inim: missing package {name}@{version}: {pkg.name}')
            actual = sha256(pkg)
            if actual != item.get('sha256'): raise SystemExit(f'inim: hash mismatch {name}@{version}')
            checked += 1
    print(f'verified {checked} package(s)')

def ver_tuple(v):
    m = re.match(r'^(\d+)\.(\d+)\.(\d+)', v or '')
    return tuple(map(int, m.groups())) if m else None

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
    root = Path(args.path).resolve(); manifest = load_manifest(root); index_path = Path(args.registry).resolve() / 'index.json'
    if not index_path.is_file(): raise SystemExit(f'inim: missing registry index: {index_path}')
    index = json.loads(index_path.read_text(encoding='utf-8')); changed = 0
    for name, spec in manifest.get('dependencies', {}).items():
        if isinstance(spec, dict): spec = spec.get('version', '')
        versions = index.get('packages', {}).get(name, {}); choices = [v for v in versions if satisfies(v, spec)]
        if not choices: raise SystemExit(f'inim: no registry version satisfies {name}: {spec}')
        chosen = max(choices, key=ver_tuple); item = versions[chosen]; pkg = index_path.parent / item['file']
        cmd_install(argparse.Namespace(package=str(pkg), target=str(root))); changed += 1
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
    p = sp.add_parser('pack'); p.add_argument('path', nargs='?', default='.'); p.add_argument('-o', '--output'); p.set_defaults(fn=cmd_pack)
    p = sp.add_parser('install'); p.add_argument('package', nargs='?'); p.add_argument('-t', '--target', default='.'); p.set_defaults(fn=cmd_install)
    p = sp.add_parser('add'); p.add_argument('name', nargs='?'); p.add_argument('version', nargs='?'); p.add_argument('--package'); p.add_argument('-p', '--path', default='.'); p.set_defaults(fn=cmd_add)
    p = sp.add_parser('remove'); p.add_argument('name'); p.add_argument('-p', '--path', default='.'); p.set_defaults(fn=cmd_remove)
    p = sp.add_parser('run'); p.add_argument('args', nargs='*'); p.add_argument('-p', '--path', default='.'); p.add_argument('--engine'); p.set_defaults(fn=cmd_run)
    p = sp.add_parser('publish'); p.add_argument('-p', '--path', default='.'); p.add_argument('-o', '--output'); p.set_defaults(fn=cmd_publish)
    p = sp.add_parser('verify'); p.add_argument('path', nargs='?', default='.'); p.set_defaults(fn=cmd_verify)
    p = sp.add_parser('update'); p.add_argument('-p', '--path', default='.'); p.add_argument('-r', '--registry', default='dist'); p.set_defaults(fn=cmd_update)
    p = sp.add_parser('doctor'); p.add_argument('path', nargs='?', default='.'); p.set_defaults(fn=cmd_doctor)
    p = sp.add_parser('list'); p.add_argument('path', nargs='?', default='.'); p.set_defaults(fn=cmd_list)
    args = ap.parse_args(); args.fn(args)
if __name__ == '__main__': main()
