#!/usr/bin/env python3
"""Download a set of Peff ReBirth .rbm files and upload them to a remote SFTP host."""

import argparse
import os
import pathlib
import sys
import urllib.request

import paramiko

DEFAULT_RBM_URLS = [
    "https://cdn.peff.com/rebirth/downloads/mods/Alpha2.rbm",
    "https://cdn.peff.com/rebirth/downloads/mods/R-SP%201200.rbm",
    "https://cdn.peff.com/rebirth/downloads/mods/Polar.rbm",
    "https://cdn.peff.com/rebirth/downloads/mods/RedStripe-dnb.rbm",
    "https://cdn.peff.com/rebirth/downloads/mods/ReVerse808.rbm",
    "https://cdn.peff.com/rebirth/downloads/mods/RB-SEMx.rbm",
    "https://cdn.peff.com/rebirth/downloads/mods/HouseMOD.rbm",
]


def parse_args():
    parser = argparse.ArgumentParser(
        description="Download selected ReBirth .rbm files and upload them to an SFTP server."
    )
    parser.add_argument("--host", required=True, help="SFTP host, e.g. storage.1ink.us")
    parser.add_argument("--port", type=int, default=22, help="SFTP port (default: 22)")
    parser.add_argument("--user", required=True, help="SSH username")
    parser.add_argument("--password", help="SSH password (use either password or key-file)")
    parser.add_argument("--key-file", help="Private key file for SSH auth")
    parser.add_argument("--remote-base", required=True, help="Remote base directory for uploads")
    parser.add_argument("--tmp-dir", default="./tmp_rebirth_mods", help="Local temporary download directory")
    parser.add_argument("--force", action="store_true", help="Re-download existing files")
    parser.add_argument("--dry-run", action="store_true", help="Show actions without uploading")
    parser.add_argument(
        "--urls-file",
        help="Optional file containing additional URLs to download, one URL per line",
    )
    return parser.parse_args()


def load_urls(urls_file=None):
    urls = list(DEFAULT_RBM_URLS)
    if urls_file:
        if not os.path.isfile(urls_file):
            raise FileNotFoundError(f"URLs file not found: {urls_file}")
        with open(urls_file, "r", encoding="utf-8") as fh:
            for line in fh:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                urls.append(line)
    return urls


def download_file(url, local_path, force=False):
    local_path.parent.mkdir(parents=True, exist_ok=True)
    if local_path.exists() and not force:
        print(f"Skipping existing file: {local_path}")
        return True
    print(f"Downloading {url} -> {local_path}")
    request = urllib.request.Request(
        url,
        headers={
            "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
            "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0 Safari/537.36"
        },
    )
    try:
        with urllib.request.urlopen(request, timeout=60) as response:
            with open(local_path, "wb") as fh:
                fh.write(response.read())
        return True
    except urllib.error.HTTPError as exc:
        print(f"HTTP error downloading {url}: {exc.code} {exc.reason}")
        return False
    except urllib.error.URLError as exc:
        print(f"URL error downloading {url}: {exc.reason}")
        return False
    except Exception as exc:
        print(f"Unexpected error downloading {url}: {exc}")
        return False


def sftp_mkdir_p(sftp, remote_directory):
    if remote_directory == "" or remote_directory == "/":
        return
    try:
        sftp.stat(remote_directory)
        return
    except IOError as exc:
        if exc.errno == 13:
            raise PermissionError(
                f"Permission denied creating remote directory: {remote_directory}. "
                "Use a writable path under your home directory or ask the server admin to create the parent directory."
            )
        parent = os.path.dirname(remote_directory.rstrip("/"))
        if parent:
            sftp_mkdir_p(sftp, parent)
        print(f"Creating remote directory: {remote_directory}")


def upload_file(sftp, local_path, remote_path):
    remote_dir = os.path.dirname(remote_path)
    sftp_mkdir_p(sftp, remote_dir)
    print(f"Uploading {local_path} -> {remote_path}")
    sftp.put(str(local_path), remote_path)


def connect_sftp(host, port, username, password=None, key_file=None):bash git.sh

    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    if key_file:
        client.connect(hostname=host, port=port, username=username, key_filename=key_file)
    else:
        client.connect(hostname=host, port=port, username=username, password=password)
    return client.open_sftp()


def main():
    args = parse_args()
    urls = load_urls(args.urls_file)
    tmp_path = pathlib.Path(args.tmp_dir)
    tmp_path.mkdir(parents=True, exist_ok=True)

    local_files = []
    for url in urls:
        filename = pathlib.Path(urllib.request.urlparse(url).path).name
        if not filename:
            raise ValueError(f"Unable to derive filename from URL: {url}")
        local_file = tmp_path / filename
        success = download_file(url, local_file, force=args.force)
        if success:
            local_files.append(local_file)
        else:
            print(f"Skipping upload for failed download: {filename}")

    if args.dry_run:
        print("Dry run complete. No upload performed.")
        return

    if not args.password and not args.key_file:
        raise ValueError("Either --password or --key-file is required for SFTP authentication.")

    with connect_sftp(args.host, args.port, args.user, args.password, args.key_file) as sftp:
        for local_file in local_files:
            remote_path = os.path.join(args.remote_base, local_file.name)
            upload_file(sftp, local_file, remote_path)

    print("All files uploaded successfully.")


if __name__ == "__main__":
    main()
