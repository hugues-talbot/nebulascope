#!/usr/bin/env python3
"""Translation catalogs are complete and keep the placeholders of the source.

Usage: check_translations.py <src/i18n directory>

For every nebulascope_<lang>.ts: each message has a non-empty, finished
translation; a plural message has two forms; the %1, %2 ... placeholders are
the same set as in the English source (a lost one silently drops a value, an
invented one prints "%3"). All catalogs must also hold the same messages, so a
string added to one language cannot be forgotten in another.
"""
import glob, os, re, sys
import xml.etree.ElementTree as ET


def placeholders(s):
    return sorted(set(re.findall(r"%\d+", s)))


def load(path):
    errors, keys = [], set()
    for ctx in ET.parse(path).getroot().findall("context"):
        name = ctx.find("name").text
        for m in ctx.findall("message"):
            src = m.find("source").text or ""
            tr = m.find("translation")
            keys.add((name, src))
            where = "%s: [%s] %r" % (os.path.basename(path), name, src[:60])
            if tr is None or tr.get("type") in ("unfinished", "vanished", "obsolete"):
                errors.append(where + ": not translated")
                continue
            if m.get("numerus") == "yes":
                forms = [f.text or "" for f in tr.findall("numerusform")]
                if len(forms) != 2:
                    errors.append(where + ": expected 2 plural forms, found %d" % len(forms))
            else:
                forms = [tr.text or ""]
            for f in forms:
                if not f.strip() and src.strip():
                    errors.append(where + ": empty translation")
                elif placeholders(f) != placeholders(src):
                    errors.append(where + ": placeholders %s, source has %s"
                                  % (placeholders(f), placeholders(src)))
                elif m.get("numerus") == "yes" and "%n" in src and "%n" not in "".join(forms):
                    errors.append(where + ": %n missing from every plural form")
    return keys, errors


def main():
    files = sorted(glob.glob(os.path.join(sys.argv[1], "nebulascope_*.ts")))
    if len(files) < 2:
        print("expected at least two catalogs in", sys.argv[1])
        return 1
    errors, sets = [], {}
    for f in files:
        sets[f], e = load(f)
        errors += e
    ref = files[0]
    for f in files[1:]:
        for ctx, src in sorted(sets[ref] - sets[f]):
            errors.append("%s lacks [%s] %r" % (os.path.basename(f), ctx, src[:60]))
        for ctx, src in sorted(sets[f] - sets[ref]):
            errors.append("%s lacks [%s] %r" % (os.path.basename(ref), ctx, src[:60]))
    for e in errors:
        print("FAIL", e)
    print("%d catalog(s), %d message(s) each, %d problem(s)"
          % (len(files), len(sets[ref]), len(errors)))
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
