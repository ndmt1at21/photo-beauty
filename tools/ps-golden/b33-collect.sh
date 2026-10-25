#!/usr/bin/env bash
# B33: Photoshop's results brought back from the folder b33-stage.sh staged (README.md, section B33) into this repo:
# out/ of B4's blend.jsx into tests/golden/ps/, p1/out/<script>/ into tests/golden/ps/p1/<script>/, out/strokes/,
# out/select/, out/resample/ into tests/golden/ps/<script>/, out/psd/ into tests/golden/psd/, out/psd-big/ into
# samples/psd/ (not in git). Each set is copied as its index.json names it (every image, a filter's kept input, a PSD's
# files; index.json, photoshop.txt, presets.json, e33-warm.cube beside them) and checked against it: what is missing,
# PNGs at another depth than "bits", cases that failed, files the index does not name (left where they are). A script
# that stopped (index.json's "complete" false, or STOPPED in B4's photoshop.txt) is not copied. Commits nothing.
# Exits 0 when every set came whole.
# Usage: tools/ps-golden/b33-collect.sh [folder]   (default /mnt/c/Users/Teko/Downloads/ps-golden; C:\... works too)
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
src=${1:-/mnt/c/Users/Teko/Downloads/ps-golden}
case $src in [A-Za-z]:[\\/]*) src=$(wslpath -u "$src") ;; esac
[ -d "$src" ] || { echo "b33-collect: no folder $src (tools/ps-golden/b33-stage.sh makes it)" >&2; exit 2; }
samples=$repo/samples  # a worktree has the main checkout's linked, or none
[ -e "$samples" ] || samples=$(dirname "$(git -C "$repo" rev-parse --path-format=absolute --git-common-dir)")/samples
exec python3 - "$src" "$repo" "$samples" <<'EOF'
import json, os, shutil, subprocess, sys

src, repo, samples = sys.argv[1:4]
# B4's 11 modes (tools/ps-golden/blend.jsx), each at 100 and 50%.
B4 = ['normal', 'darken', 'multiply', 'colorBurn', 'lighten', 'screen', 'colorDodge', 'overlay', 'softLight',
      'linearLight', 'difference']
# Each set: its name, its folder in the staged one, where it goes, the files beside its index.json.
SETS = [('blend', 'p1/out/blend', 'tests/golden/ps/p1/blend', []),
        ('adjust', 'p1/out/adjust', 'tests/golden/ps/p1/adjust', ['presets.json']),
        ('gradient', 'p1/out/gradient', 'tests/golden/ps/p1/gradient', []),
        ('filters', 'p1/out/filters', 'tests/golden/ps/p1/filters', []),
        ('styles', 'p1/out/styles', 'tests/golden/ps/p1/styles', []),
        ('strokes', 'out/strokes', 'tests/golden/ps/strokes', []),
        ('select', 'out/select', 'tests/golden/ps/select', []),
        ('resample', 'out/resample', 'tests/golden/ps/resample', []),
        ('psd', 'out/psd', 'tests/golden/psd', ['e33-warm.cube']),
        ('psd-big', 'out/psd-big', os.path.join(samples, 'psd'), [])]
KEEP = {'README.md', 'cases.json', 'base.png', 'top.png'}  # the repo's own files where results go


def png_bits(path):
    with open(path, 'rb') as f:
        head = f.read(26)
    return head[24] if head[:8] == b'\x89PNG\r\n\x1a\n' and head[12:16] == b'IHDR' else None


def size(path):  # bytes of the regular files under path
    return sum(os.path.getsize(os.path.join(d, f)) for d, _, fs in os.walk(path) for f in fs)


def layers(ls):  # every layer of a case, groups' children too
    for l in ls:
        yield l
        yield from layers(l.get('children') or [])


def files_of(index, psd):  # the files an index names: {file: bits of its PNG, None when not checked}
    files = {}
    for e in index.get('images', []):
        if psd:
            for k in ('file', 'png', 'tif'):
                if e.get(k):
                    files[e[k]] = e.get('bits') if k == 'png' else None
            continue
        files[e['name'] + '.png'] = e.get('bits')
        for l in layers(e.get('layers') or []):
            for step in l.get('filters') or []:
                if step.get('keep'):
                    files[step['keep'] + '.png'] = None
    return files


def short(s, n=110):
    s = ' '.join(str(s).split())
    return s if len(s) <= n else s[:n - 3] + '...'


whole, versions, report = 0, {}, []


def bring(name, folder, dest, files, side, cases, failed, prune, what='cases'):
    """Copies files and side (what of them is there) from folder into dest; prints what is off. True when whole."""
    missing = [f for f in list(files) + side if not os.path.isfile(os.path.join(folder, f))]
    depth = [f for f, b in files.items() if b and f not in missing and png_bits(os.path.join(folder, f)) != b]
    os.makedirs(dest, exist_ok=True)
    for f in list(files) + side:
        if f not in missing:
            shutil.copyfile(os.path.join(folder, f), os.path.join(dest, f))
    unnamed = sorted(f for f in os.listdir(folder) if os.path.isfile(os.path.join(folder, f)) and f not in files and
                     f not in side)
    stale = sorted(f for f in os.listdir(dest) if os.path.isfile(os.path.join(dest, f)) and f not in files and
                   f not in side and f not in KEEP) if prune else []
    done = cases - len(failed)
    shown = os.path.relpath(dest, repo) if dest.startswith(repo + os.sep) else dest
    print('%-9s %d of %d %s, %d failed, %d files missing: %s' % (name, done, cases, what, len(failed), len(missing),
                                                                  shown))
    for e in failed[:5]:
        print('          failed %s: %s' % (e.get('name'), short(e.get('error'))))
    if len(failed) > 5:
        print('          failed: %d more (photoshop.txt)' % (len(failed) - 5))
    if missing:
        print('          missing: %s' % short(', '.join(missing), 300))
    if depth:
        print('          not at the bits index.json says: %s' % short(', '.join(depth), 300))
    if unnamed:
        print('          in %s/ but not in index.json, not copied: %s' % (os.path.relpath(folder, src),
                                                                         short(', '.join(unnamed), 200)))
    if stale:
        print('          in the repo from before, not of this run (remove by hand if so): %s' %
              short(', '.join(stale), 200))
    if failed or missing or depth:
        report.append(os.path.join(folder, 'photoshop.txt'))
    return not (failed or missing or depth)


# B4: out/ itself, no index.json.
folder, log = os.path.join(src, 'out'), os.path.join(src, 'out', 'photoshop.txt')
print('from %s:' % src)
if not os.path.isfile(log):
    print('%-9s not run: no out/photoshop.txt' % 'B4')
elif 'STOPPED:' in open(log, encoding='utf-8', errors='replace').read():
    print('%-9s stopped (STOPPED in out/photoshop.txt): not copied' % 'B4')
    report.append(log)
else:
    files = {'%s-%d.png' % (m, o): 16 for m in B4 for o in (100, 50)}
    files['dabs.png'] = 16
    whole += bring('B4', folder, os.path.join(repo, 'tests/golden/ps'), files, ['photoshop.txt'], len(files), [], False,
                   'images')

for name, rel, dest, side in SETS:
    folder = os.path.join(src, rel)
    dest = os.path.join(repo, dest)
    path = os.path.join(folder, 'index.json')
    if not os.path.isfile(path):
        print('%-9s not run: no %s' % (name, os.path.relpath(path, src)))
        continue
    try:
        with open(path, encoding='utf-8') as f:
            index = json.load(f)
    except ValueError as e:
        print('%-9s index.json is not JSON (%s): not copied' % (name, e))
        report.append(path)
        continue
    versions.setdefault(str(index.get('photoshop')), []).append(name)
    failed = index.get('failed') or []
    if not index.get('complete', True):
        print('%-9s stopped: %s; not copied' % (name, short(index.get('error'))))
        report.append(os.path.join(folder, 'photoshop.txt'))
        continue
    files = files_of(index, name.startswith('psd'))
    whole += bring(name, folder, dest, files, ['index.json', 'photoshop.txt'] + side,
                   len(index.get('images', [])) + len(failed), failed, not name.startswith('psd-big'))
    inputs = index.get('inputs')
    if inputs and not os.path.isdir(os.path.join(dest, inputs)):
        print('          its inputs ("inputs": %s) are not there from %s' % (inputs, os.path.relpath(dest, repo)))

mb = [size(os.path.join(repo, 'tests/golden', p)) / 1048576
      for p in ('ps/p1', 'ps/strokes', 'ps/select', 'ps/resample', 'psd')]
print('sizes: tests/golden/ps/p1 %.1f MB (30 at most), strokes %.1f, select %.1f, resample %.1f MB (3 each), '
      'tests/golden/psd %.1f MB' % tuple(mb))
if versions:
    print('Photoshop: ' + '; '.join('%s (%s)' % (v, ', '.join(n)) for v, n in versions.items()))
try:
    changed = subprocess.run(['git', '-C', repo, 'status', '--porcelain', '--untracked-files=all', '--',
                              'tests/golden'], capture_output=True, text=True, check=True).stdout.splitlines()
    print('git: %d files new or changed under tests/golden, not committed' % len(changed))
except (OSError, subprocess.CalledProcessError):
    pass
total = 1 + len(SETS)
print('%d of %d sets whole.' % (whole, total))
if report:
    print('For the lead (README.md, B33):\n  ' + '\n  '.join(report))
sys.exit(0 if whole == total else 1)
EOF
