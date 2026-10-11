#!/usr/bin/env python3
"""Find calls through prototypes that differ from the function's definition.

    python tools/web_prototype_check.py [build/web/obj] [--llvm-dis PATH]

The game's files declare many functions themselves. Where such a prototype has
other parameter types than the definition, x86 does not care (every argument is
a stack word), but the web build links with link-time optimization, which
treats a call through a mismatched type as undefined and replaces it with a
trap (the game stops with "RuntimeError: unreachable"). This reads the build's
bitcode objects and lists every declaration whose type is not the definition's.
"""
import os
import re
import subprocess
import sys

ATTRIBUTE = re.compile(
    r"\b(noundef|zeroext|signext|nonnull|nocapture|readonly|readnone|writeonly|noalias|returned|nofree|inreg|"
    r"captures\([^)]*\)|range\([^)]*\)|align \d+|dereferenceable(_or_null)?\(\d+\)|sret\([^)]*\)|byval\([^)]*\)|"
    r"initializes\(\([^)]*\)\)|memory\([^)]*\)|nofpclass\([^)]*\))")
LINE = re.compile(r"^(define|declare)\s+(.*?)@\"?([\w.$?@]+)\"?\((.*)\)[^()]*$")
LINKAGE = re.compile(r"\b(hidden|internal|dso_local|local_unnamed_addr|unnamed_addr|weak|linkonce_odr|external|"
                     r"private|protected|default|fastcc|ccc|coldcc)\b")


def split(parameters):
    out, depth, start = [], 0, 0
    for i, c in enumerate(parameters):
        depth += c in "([{<"
        depth -= c in ")]}>"
        if c == "," and depth == 0:
            out.append(parameters[start:i])
            start = i + 1
    out.append(parameters[start:])
    return [p for p in (x.strip() for x in out) if p]


def clean(text):
    text = re.sub(r"initializes\((?:[^()]|\([^()]*\))*\)", "", text)    # (a parameter attribute with nested lists)
    text = ATTRIBUTE.sub("", text.replace("extern_weak", ""))
    text = re.sub(r"%[\w.]+$", "", text.strip())          # a definition names its parameters
    return " ".join(text.split())


def signature(result, parameters):
    return clean(LINKAGE.sub("", result)), tuple(clean(p) for p in split(parameters))


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    root = args[0] if args else "build/web/obj"
    dis = sys.argv[sys.argv.index("--llvm-dis") + 1] if "--llvm-dis" in sys.argv else "llvm-dis"
    defined, declared = {}, {}
    for folder, _, files in os.walk(root):
        for name in files:
            if not name.endswith(".o"):
                continue
            path = os.path.join(folder, name)
            text = subprocess.run([dis, path, "-o", "-"], capture_output=True, text=True, errors="replace").stdout
            for line in text.splitlines():
                if not line.startswith(("define ", "declare ")):
                    continue
                if line.startswith("define "):
                    line = line.rsplit("{", 1)[0]
                m = LINE.match(line.strip())
                if not m:
                    continue
                kind, result, function, parameters = m.groups()
                if kind == "define" and "internal" in result:
                    continue                                   # a file's own static function
                sig = signature(result, parameters)
                (defined if kind == "define" else declared.setdefault(function, {}))[
                    function if kind == "define" else os.path.relpath(path, root)] = sig
    bad = 0
    for function, users in sorted(declared.items()):
        if function not in defined:
            continue
        want = defined[function]
        for user, got in sorted(users.items()):
            if got != want:
                bad += 1
                print(f"{function}: {user.replace(os.sep, '/')} declares {got[0]} ({', '.join(got[1])})")
                print(f"{' ' * len(function)}  defined as {want[0]} ({', '.join(want[1])})")
    print(f"{bad} mismatched prototypes; {len(defined)} definitions, {len(declared)} declared functions")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
