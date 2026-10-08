#!/bin/bash
# Uploads a release file to VirusTotal, waits for the scan, and writes:
#   <out>.json  the result, used as the predicate of a CI attestation
#   <out>.md    a short section for the release notes
# Usage: VT_API_KEY=... Tools/virustotal.sh <file> <out-prefix>
#
# A clean result means ~70 antivirus engines know nothing bad about the file.
# It cannot prove the file is safe; the build-provenance attestation and the
# open source are what tie the download to this repository.
#
# Free API tier: 4 requests a minute, so the poll is slow on purpose.
set -euo pipefail
FILE="$1"; OUT="$2"
: "${VT_API_KEY:?set VT_API_KEY}"
API=https://www.virustotal.com/api/v3
SHA=$(sha256sum "$FILE" | cut -d' ' -f1)
NAME=$(basename "$FILE")
REPORT="https://www.virustotal.com/gui/file/$SHA"

# Files up to 32 MB go straight to /files; the release DMG is ~20 MB.
size=$(stat -c %s "$FILE")
[ "$size" -le $((32 * 1024 * 1024)) ] || { echo "$NAME is over 32 MB; needs the upload_url flow" >&2; exit 1; }

analysis=$(curl -sS --fail-with-body -H "x-apikey: $VT_API_KEY" -F "file=@$FILE" "$API/files" \
           | jq -r '.data.id')
[ -n "$analysis" ] && [ "$analysis" != null ] || { echo "upload failed" >&2; exit 1; }
echo "uploaded $NAME, analysis $analysis"

status=queued; result='{}'
for _ in $(seq 1 45); do          # up to 15 minutes
  sleep 20
  result=$(curl -sS --fail-with-body -H "x-apikey: $VT_API_KEY" "$API/analyses/$analysis")
  status=$(jq -r '.data.attributes.status' <<<"$result")
  echo "  $status"
  [ "$status" = completed ] && break
done
[ "$status" = completed ] || { echo "scan did not complete in time" >&2; exit 1; }

stats=$(jq -c '.data.attributes.stats' <<<"$result")
malicious=$(jq -r '.malicious' <<<"$stats")
suspicious=$(jq -r '.suspicious' <<<"$stats")
clean=$(jq -r '.undetected + .harmless' <<<"$stats")
date=$(jq -r '.data.attributes.date | todate' <<<"$result")

jq -n --arg file "$NAME" --arg sha "$SHA" --arg analysis "$analysis" --arg report "$REPORT" \
      --arg date "$date" --argjson stats "$stats" \
      '{scanner: "VirusTotal", file: $file, sha256: $sha, analysis: $analysis,
        report: $report, scanned: $date, stats: $stats}' > "$OUT.json"

{
  echo "### Malware scan"
  if [ "$malicious" -eq 0 ] && [ "$suspicious" -eq 0 ]; then
    echo "VirusTotal, $date: **no engine flagged \`$NAME\`** ($clean engines checked it)."
  else
    echo "VirusTotal, $date: **$malicious malicious, $suspicious suspicious** of the engines that checked \`$NAME\`."
    echo "Unsigned apps sometimes draw false positives from individual engines; see the report."
  fi
  echo "[Full report]($REPORT). The result is also attested by CI:"
  echo '```bash'
  echo "gh attestation verify $NAME -R bizmar/exr-quicklook \\"
  echo "  --predicate-type https://github.com/bizmar/exr-quicklook/attestations/virustotal/v1"
  echo '```'
} > "$OUT.md"
cat "$OUT.md"

# A flag does not fail the job (the release is only a draft, and a human looks
# before publishing), but it must not go unnoticed.
if [ "$malicious" -gt 0 ] || [ "$suspicious" -gt 0 ]; then
  echo "::warning::VirusTotal flagged $NAME: $malicious malicious, $suspicious suspicious -- $REPORT"
fi
