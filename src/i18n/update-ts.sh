#!/bin/sh
# Refresh the translation catalogs after adding/changing tr() strings:
#   ./src/i18n/update-ts.sh          (from the repo root)
#
# lupdate's heuristic C++ parser loses the `namespace astro { ... }` scope in
# our sources and emits UNQUALIFIED contexts ("MainWindow"), while Qt resolves
# tr() at runtime against the metaobject's QUALIFIED class name
# ("astro::MainWindow") — unqualified contexts silently translate nothing
# (verified empirically). So: de-qualify before the merge (or lupdate marks
# every existing entry obsolete and drops the translations), let lupdate
# merge against its own naming, then re-qualify. Explicit
# QCoreApplication::translate() calls in the sources already use the
# qualified form, so both renames are uniform.
set -e
cd "$(dirname "$0")/../.."

TS=src/i18n/nebulascope_fr.ts

python3 - "$TS" strip <<'EOF'
import re, sys
p = sys.argv[1]
s = open(p).read()
# astro::MainWindowHelpers is a LITERAL context in the sources
# (QCoreApplication::translate for non-QObject undo commands and helpers):
# lupdate emits it verbatim, so it must not be de-qualified.
s = re.sub(r"<name>astro::(?!MainWindowHelpers)(\w+)</name>", r"<name>\1</name>", s)
open(p, "w").write(s)
EOF

lupdate -no-obsolete src/app src/ui -ts "$TS"

python3 - "$TS" qualify <<'EOF'
import re, sys
p = sys.argv[1]
s = open(p).read()
s = re.sub(r"<name>(?!astro::)(\w+)</name>", r"<name>astro::\1</name>", s)
open(p, "w").write(s)
print("contexts qualified:", len(set(re.findall(r"<name>(astro::\w+)</name>", s))))
EOF
