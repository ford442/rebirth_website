#!/usr/bin/env python3
"""Download a list of Peff ReBirth .rbm files from a Wayback Machine mirror of peff.com."""

import argparse
import os
import pathlib
import sys
import urllib.error
import urllib.parse
import urllib.request

SCRIPT_DIR = pathlib.Path(__file__).resolve().parent
DEFAULT_LIST_FILE = SCRIPT_DIR / "peff_rbm_filenames.txt"
DEFAULT_BASE_URL = (
    "https://web.archive.org/web/20040129012338/http://peff.com/rebirth/downloads/mods/"
)


def parse_args():
    parser = argparse.ArgumentParser(
        description="Download a list of ReBirth .rbm files from a common base URL."
    )
    parser.add_argument(
        "--base-url",
        default=DEFAULT_BASE_URL,
        help="Base URL for the remote files (default: Wayback peff.com mods snapshot)",
    )
    parser.add_argument(
        "--list-file",
        default=str(DEFAULT_LIST_FILE),
        help="Text file with one filename per line",
    )
    parser.add_argument(
        "--output-dir",
        default="./peff_rbm_downloads",
        help="Local directory to save downloaded files",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Re-download files even if they already exist locally",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print the URLs without downloading",
    )
    return parser.parse_args()


def load_names(list_file):
    path = pathlib.Path(list_file)
    if not path.exists():
        raise FileNotFoundError(f"List file not found: {path}")

    names = []
    with path.open("r", encoding="utf-8") as fh:
        for line in fh:
            filename = line.strip()
            if not filename or filename.startswith("#"):
                continue
            names.append(filename)
    return names


def build_url(base_url, filename):
    if not base_url.endswith("/"):
        base_url = base_url + "/"
    encoded = urllib.parse.quote(filename, safe="")
    return base_url + encoded


def download_url(url, dest_path):
    headers = {
        "User-Agent": (
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
            "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0 Safari/537.36"
        )
    }
    request = urllib.request.Request(url, headers=headers)
    try:
        with urllib.request.urlopen(request, timeout=60) as response:
            dest_path.parent.mkdir(parents=True, exist_ok=True)
            with open(dest_path, "wb") as fh:
                fh.write(response.read())
        return True
    except urllib.error.HTTPError as exc:
        print(f"ERROR {exc.code} downloading {url}")
    except urllib.error.URLError as exc:
        print(f"ERROR downloading {url}: {exc.reason}")
    except Exception as exc:
        print(f"ERROR downloading {url}: {exc}")
    return False


def main():
    args = parse_args()
    names = load_names(args.list_file)
    output_dir = pathlib.Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    if args.dry_run:
        print(f"Base URL: {args.base_url}")
        for name in names:
            print(build_url(args.base_url, name))
        return

    failed = []
    for name in names:
        url = build_url(args.base_url, name)
        dest_path = output_dir / name
        if dest_path.exists() and not args.force:
            print(f"Skipping existing file: {dest_path}")
            continue
        print(f"Downloading {name}")
        success = download_url(url, dest_path)
        if not success:
            failed.append(name)

    print("\nDownload complete.")
    if failed:
        print("Failed to download the following files:")
        for name in failed:
            print(f"- {name}")
        sys.exit(1)


if __name__ == "__main__":
    main()
