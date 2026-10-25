#!/usr/bin/env python3
# copied from snapone 3891d67 (ui/i18n/check.py); the start's language (--app) is photo-beauty's.
# ctest "i18n": the .ts files cover every string of the sources (lupdate, run on copies) and each
# translation keeps the placeholders and the edge spaces of its source (they are joined to others).
# With --app <pb-ui>: pb-ui starts in the language it should (--lang, Thiết lập's, the system's).
# Usage: check.py <lupdate> <ui dir> <ts file>... [--app <pb-ui>]; on failure, what is wrong is listed.
import glob, json, os, re, shutil, subprocess, sys, tempfile
import xml.etree.ElementTree as ET

argv = sys.argv[1:]
app = None
if "--app" in argv:
    app = argv[argv.index("--app") + 1]
    del argv[argv.index("--app"):argv.index("--app") + 2]
lupdate, ui, files = argv[0], argv[1], argv[2:]
sources = sorted(glob.glob(os.path.join(ui, "qml", "*.qml")) + glob.glob(os.path.join(ui, "qml", "*.js"))
                 + glob.glob(os.path.join(ui, "*.cpp")) + glob.glob(os.path.join(ui, "*.h"))
                 + glob.glob(os.path.join(ui, "brand", "*.h")))
marks = lambda s: sorted(re.findall(r"%L?(?:\d+|n)", s))
edges = lambda s: (s[:1].isspace(), s[-1:].isspace())
bad = []
with tempfile.TemporaryDirectory() as tmp:
    copies = [shutil.copy(os.path.join(ui, f), tmp) for f in files]
    subprocess.run([lupdate, "-silent", "-no-obsolete", "-locations", "none", *sources, "-ts", *copies], check=True)
    for ts in copies:
        for ctx in ET.parse(ts).getroot().iter("context"):
            name = ctx.findtext("name")
            for m in ctx.iter("message"):
                src, tr = m.findtext("source"), m.find("translation")
                forms = [f.text or "" for f in tr.iter("numerusform")] if m.get("numerus") == "yes" else [tr.text or ""]
                why = ("not translated" if tr.get("type") == "unfinished" or not all(forms)
                       else "placeholders differ" if any(marks(f) != marks(src) for f in forms)
                       else "spaces at the ends differ" if any(edges(f) != edges(src) for f in forms) else "")
                if why: bad.append(f"{os.path.basename(ts)}: {name}: {why}: {src!r}")

# The language pb-ui takes (main.cpp, --which-lang): --lang, else Thiết lập's (Prefs' language: 0 Tiếng Việt,
# 1 English) kept by the last start, else the system's first of vi and en, else English; screenshots start in
# Vietnamese, as from the defaults, unless --lang.
def started(lang_env, prefs=None, *flags):
    with tempfile.TemporaryDirectory() as home:
        if prefs is not None:  # QSettings' ini, as Prefs keeps it: [prefs] json=…
            os.makedirs(os.path.join(home, "photo-beauty"))
            with open(os.path.join(home, "photo-beauty", "photo-beauty.conf"), "w") as f:
                f.write('[prefs]\njson="%s"\n' % json.dumps(prefs).replace("\\", "\\\\").replace('"', '\\"'))
        env = {k: v for k, v in os.environ.items() if k not in ("LANGUAGE", "LC_ALL", "LC_MESSAGES", "LANG")}
        env.update(lang_env, XDG_CONFIG_HOME=home, QT_QPA_PLATFORM="offscreen")
        r = subprocess.run([app, "--which-lang", *flags], env=env, capture_output=True, text=True)
        return r.stdout.strip() if r.returncode == 0 else "exit %d" % r.returncode

if app:
    vi, en, fr, c = {"LANG": "vi_VN.UTF-8"}, {"LANG": "en_US.UTF-8"}, {"LANG": "fr_FR.UTF-8"}, {"LANG": "C.UTF-8"}
    cases = [("vi", vi), ("en", en), ("en", fr), ("en", c), ("vi", {"LANGUAGE": "fr:vi", "LANG": "fr_FR.UTF-8"}),
             ("en", vi, {"language": 1}), ("vi", en, {"language": 0}), ("vi", en, {"language": 0, "theme": 1}),
             ("vi", vi, {"theme": 1}), ("en", en, {"theme": 1}), ("vi", vi, {"language": 7}), ("en", en, {"language": "0"}),
             ("vi", en, {"language": 1}, "--lang", "vi"), ("en", vi, None, "--lang", "en"),
             ("vi", en, {"language": 1}, "--screenshot", "x.png"), ("en", vi, None, "--screenshot", "x.png", "--lang", "en"),
             ("exit 2", vi, None, "--lang", "fr")]
    for want, env, *rest in cases:
        got = started(env, *rest)
        if got != want:
            bad.append(f"pb-ui with {env} {rest}: {got!r}, not {want!r}")
print("\n".join(bad) or f"{len(files)} translations complete" + (", the start's language as it should" if app else ""))
sys.exit(1 if bad else 0)
