#!/usr/bin/env python3
"""
check-link-rot.py

HEAD-sweep remaining external archive URLs. Prints a report; exits 1 when
new dead links are found (so a scheduled workflow can open an issue).

Usage:
    python3 scripts/check-link-rot.py
    python3 scripts/check-link-rot.py --sample 40 --kind rbs
"""

from __future__ import annotations

import argparse
import json
import random
import sys
from pathlib import Path
from typing import Any

import requests

REPO = Path(__file__).resolve().parents[1]
INTEGRITY = REPO / "src" / "data" / "archive-integrity.json"
USER_AGENT = "rebirth-website-linkrot/1.0 (+https://github.com/ford442/rebirth_website)"


def head_ok(session: requests.Session, url: str) -> tuple[bool, str]:
    try:
        response = session.head(url, timeout=20, allow_redirects=True)
        if response.status_code == 405:
            response = session.get(url, timeout=20, stream=True)
            response.close()
        if response.status_code == 404:
            return False, "http_404"
        if response.status_code >= 400:
            return False, f"http_{response.status_code}"
        return True, f"http_{response.status_code}"
    except requests.RequestException as exc:
        return False, f"http_error:{exc}"


def main() -> int:
    parser = argparse.ArgumentParser(description="HEAD-sweep remote archive URLs.")
    parser.add_argument("--sample", type=int, default=40)
    parser.add_argument("--kind", choices=("rbs", "rbm", "all"), default="all")
    parser.add_argument("--seed", type=int, default=338)
    args = parser.parse_args()

    if not INTEGRITY.is_file():
        print(f"Missing {INTEGRITY}", file=sys.stderr)
        return 1

    payload = json.loads(INTEGRITY.read_text(encoding="utf-8"))
    entries: dict[str, Any] = payload.get("entries") or {}

    candidates: list[tuple[str, str]] = []
    for path, entry in entries.items():
        if entry.get("status") == "local-core":
            continue
        if args.kind != "all" and entry.get("kind") != args.kind:
            continue
        sources = entry.get("sources") or []
        if not sources:
            continue
        candidates.append((path, sources[0]))

    rng = random.Random(args.seed)
    rng.shuffle(candidates)
    sample = candidates[: max(0, args.sample)]

    # Always include already-dead rows so recoveries are visible.
    for path, entry in entries.items():
        if entry.get("status") != "dead":
            continue
        sources = entry.get("sources") or []
        if sources:
            sample.append((path, sources[0]))

    # Dedup while preserving order.
    seen: set[str] = set()
    unique: list[tuple[str, str]] = []
    for item in sample:
        if item[0] in seen:
            continue
        seen.add(item[0])
        unique.append(item)

    session = requests.Session()
    session.headers.update({"User-Agent": USER_AGENT})

    dead: list[str] = []
    ok = 0
    for path, url in unique:
        healthy, detail = head_ok(session, url)
        status = "OK" if healthy else "DEAD"
        print(f"{status} {path} {detail}", file=sys.stderr)
        if healthy:
            ok += 1
        else:
            dead.append(f"{path} ({detail})")

    print(f"Checked {len(unique)} URLs: ok={ok} dead={len(dead)}", file=sys.stderr)
    if dead:
        print("DEAD_LINKS:")
        for row in dead:
            print(row)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
