#!/usr/bin/env python3
"""Inim V0.4 package manager (offline-first minimal implementation)."""
import argparse, hashlib, json, zipfile
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
    pkg = Path(args.package); target = Path(args.target or '.')
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

def cmd_list(args):
    lock = Path(args.path) / 'lock.json'
    if not lock.exists(): return
    data = json.loads(lock.read_text(encoding='utf-8'))
    for n, p in sorted(data.get('packages', {}).items()): print(f"{n}@{p.get('version', '?')}  {p.get('sha256', '')}")

def main():
    ap = argparse.ArgumentParser(prog='inim'); sp = ap.add_subparsers(dest='cmd', required=True)
    p = sp.add_parser('init'); p.add_argument('path', nargs='?', default='.'); p.add_argument('--name'); p.add_argument('--force', action='store_true'); p.set_defaults(fn=cmd_init)
    p = sp.add_parser('pack'); p.add_argument('path', nargs='?', default='.'); p.add_argument('-o', '--output'); p.set_defaults(fn=cmd_pack)
    p = sp.add_parser('install'); p.add_argument('package'); p.add_argument('-t', '--target', default='.'); p.set_defaults(fn=cmd_install)
    p = sp.add_parser('list'); p.add_argument('path', nargs='?', default='.'); p.set_defaults(fn=cmd_list)
    args = ap.parse_args(); args.fn(args)
if __name__ == '__main__': main()
