#!/usr/bin/env bash
# Render docs/**/*.md to PDF next to the source (pandoc -> HTML -> headless Chromium).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CHROME="${CHROME:-$(command -v chromium || echo /opt/pw-browsers/chromium-1194/chrome-linux/chrome)}"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
for md in "$@"; do
  name="$(basename "${md%.md}")"
  title="$(grep -m1 '^# ' "$md" | sed 's/^# //')"
  pandoc "$md" -f gfm -t html5 --standalone --embed-resources \
    --css "$ROOT/tools/pdf.css" --metadata pagetitle="$title" -o "$TMP/$name.html"
  "$CHROME" --headless --no-sandbox --disable-gpu --no-pdf-header-footer \
    --print-to-pdf="${md%.md}.pdf" "file://$TMP/$name.html" 2>/dev/null
  echo "wrote ${md%.md}.pdf"
done
