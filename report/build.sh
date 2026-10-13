#!/usr/bin/env bash
set -euo pipefail
REPORT_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
REPORT_BUILD="$REPORT_ROOT/tmp/coder/report-build"
mkdir -p "$REPORT_BUILD"
cd "$REPORT_ROOT"
for pass in 1 2; do
    if ! xelatex -interaction=nonstopmode -halt-on-error \
        -output-directory="$REPORT_BUILD" report/main.tex \
        > "$REPORT_BUILD/pass-$pass.log" 2>&1; then
        cat "$REPORT_BUILD/pass-$pass.log"
        exit 1
    fi
done
python3 - "$REPORT_BUILD" <<'PY'
from pathlib import Path
import re
import subprocess
import sys
directory = Path(sys.argv[1])
log = (directory / 'main.log').read_text()
if re.search(r'Overfull|Missing character|undefined references', log):
    raise SystemExit('Inspect report-build/main.log: overflow, missing glyph or unresolved reference')
info = subprocess.check_output(['pdfinfo', str(directory / 'main.pdf')], text=True)
pages = int(re.search(r'^Pages:\s+(\d+)', info, re.M).group(1))
if pages > 3:
    raise SystemExit(f'Report exceeds the three-page limit: {pages}')
print(f'Report compiled: {pages} A4 pages; no overflow, missing glyphs or unresolved references.')
PY
cp "$REPORT_BUILD/main.pdf" "$REPORT_ROOT/report/main.pdf"
