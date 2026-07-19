#!/usr/bin/env bash
# Fail CI if legacy deploy scripts or hardcoded credential patterns reappear.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

echo "Scanning for forbidden deploy scripts and hardcoded secrets..."

if [[ -f deploy_old.py ]]; then
  echo "::error::deploy_old.py must not be committed (use deploy.py with env-based credentials)"
  exit 1
fi

FAIL=0

scan_file() {
  local file="$1"
  [[ -f "$file" ]] || return 0

  # Hardcoded token/password assignments (exclude env lookups and argparse help).
  while IFS= read -r match; do
    [[ -z "$match" ]] && continue
    if [[ "$match" == *"os.environ"* ]] || [[ "$match" == *"add_argument"* ]] || [[ "$match" == *"help="* ]]; then
      continue
    fi
    echo "::error file=${file}::${match}"
    FAIL=1
  done < <(
    grep -nE '(^|[^.])\b(DEPLOY_TOKEN|SFTP_PASSWORD|password)\s*=\s*["'"'"'][^"'"'"']{4,}["'"'"']' "$file" || true
  )
}

scan_file deploy.py
for script in scripts/*.py; do
  scan_file "$script"
done

# Inline SFTP/SSH password literals in Python call sites.
while IFS= read -r match; do
  [[ -z "$match" ]] && continue
  if [[ "$match" == *"args.password"* ]] || [[ "$match" == *"add_argument"* ]]; then
    continue
  fi
  echo "::error::${match}"
  FAIL=1
done < <(
  grep -rnE 'password\s*=\s*["'"'"'][^"'"'"']{4,}["'"'"']' deploy.py scripts/ --include='*.py' || true
)

if [[ "$FAIL" -ne 0 ]]; then
  echo "Secret scan failed."
  exit 1
fi

echo "No forbidden secrets or legacy deploy scripts found."
