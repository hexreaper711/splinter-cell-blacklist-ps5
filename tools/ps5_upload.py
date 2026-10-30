#!/usr/bin/env python3
"""Uploads a local folder to the PS5 through its web file manager (port 8888), the way the
manager's own page does: /api/upload/prepare, one /api/upload-file per file, /api/upload/finish.

  export PS5_HOST=<your PS5's IP>:8888
  python3 tools/ps5_upload.py dist/PPSA90827 /data/homebrew --overwrite
  python3 tools/ps5_upload.py disc-files/disc2 /data/homebrew/PPSA90827/game --as disc2

The folder lands as <dest>/<folder name>, or <dest>/<--as name>. Without --overwrite it stops if that name is already
there. The PS5's address comes from --host or PS5_HOST. Standard library only.
"""
from __future__ import annotations

import argparse
import os
import json
import sys
import time
import urllib.parse
import urllib.request
from pathlib import Path


def call(host: str, path: str, params: dict, post: bool = False) -> dict:
    data = urllib.parse.urlencode(params).encode()
    url = f"http://{host}{path}" + ("" if post else "?" + data.decode())
    req = urllib.request.Request(url, data=data if post else None)
    if post:
        req.add_header("Content-Type", "application/x-www-form-urlencoded")
    with urllib.request.urlopen(req, timeout=60) as r:
        reply = json.load(r)
    if not reply.get("ok"):
        raise SystemExit(f"{path}: {reply.get('error') or reply}")
    return reply


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("folder", type=Path, help="the local folder to upload")
    ap.add_argument("dest", help="the PS5 folder it goes into, e.g. /data/homebrew")
    ap.add_argument("--host", default=os.environ.get("PS5_HOST"),
                    help="the PS5 file manager's address, ip:port (default: $PS5_HOST)")
    ap.add_argument("--overwrite", action="store_true", help="replace files that are already there")
    ap.add_argument("--as", dest="name", help="the folder's name on the PS5 (default: its own)")
    args = ap.parse_args()
    if not args.host:
        raise SystemExit("No PS5 address: pass --host ip:port or set PS5_HOST")

    folder = args.folder.resolve()
    name = args.name or folder.name
    files = sorted(p for p in folder.rglob("*") if p.is_file() and p.name != ".DS_Store")
    if not files:
        raise SystemExit(f"{folder} has no files")
    rels = [f"{name}/{p.relative_to(folder).as_posix()}" for p in files]
    sizes = [p.stat().st_size for p in files]

    there = {e["name"] for e in call(args.host, "/api/list", {"path": args.dest})["entries"]}
    if name in there and not args.overwrite:
        raise SystemExit(f"{args.dest}/{name} is already on the PS5; pass --overwrite to replace its files")

    def prepare(first: int) -> str:
        return call(args.host, "/api/upload/prepare", {
            "path": args.dest, "src": rels[first], "total": sum(sizes[first:]),
            "count": len(files) - first, "rels": "\n".join(rels[first:]),
            "sizes": "\n".join(map(str, sizes[first:])),
            "overwrite": "1" if args.overwrite else "0"}, post=True)["task_id"]

    def send(task: str, path: Path, rel: str, size: int) -> None:
        # Streamed from the file: macOS refuses a single socket write over 2 GB (Errno 22).
        with path.open("rb") as f:
            req = urllib.request.Request(f"http://{args.host}/api/upload-file", data=f)
            for key, value in (("Content-Type", "application/octet-stream"), ("Content-Length", str(size)),
                               ("X-WFM-Task-ID", str(task)),
                               ("X-WFM-Path", urllib.parse.quote(args.dest, safe="")),
                               ("X-WFM-Rel", urllib.parse.quote(rel, safe="")), ("X-WFM-Size", str(size)),
                               ("X-WFM-Overwrite", "1" if args.overwrite else "0")):
                req.add_header(key, value)
            with urllib.request.urlopen(req, timeout=600) as r:
                body = r.read()
        try:
            reply = json.loads(body)
        except ValueError:
            return
        if isinstance(reply, dict) and reply.get("ok") is False:
            raise OSError(f"the PS5 refused it: {reply.get('error') or reply}")

    # The file manager sometimes stops answering for a while (the console on Wi-Fi): a failed file
    # is sent again after a pause, under a new upload task in case the server lost the old one.
    task = prepare(0)
    start = time.monotonic()
    for i, (path, rel, size) in enumerate(zip(files, rels, sizes)):
        for attempt in range(6):
            try:
                send(task, path, rel, size)
                break
            except (OSError, urllib.error.URLError) as e:
                if attempt == 5:
                    # Ended the way the page's Cancel button does, or the server refuses every
                    # later upload with "another task is running".
                    for end, params in (("/api/cancel", {"id": task}), ("/api/upload/finish", {"task_id": task})):
                        try:
                            call(args.host, end, params)
                        except (OSError, urllib.error.URLError, SystemExit):
                            pass
                    raise SystemExit(f"{rel}: {e} (gave up; run again with --overwrite)")
                wait = 10 * 2**attempt
                print(f"  ({rel}: {e}; trying again in {wait}s)", file=sys.stderr, flush=True)
                time.sleep(wait)
                try:
                    task = prepare(i)
                except (OSError, urllib.error.URLError, SystemExit):
                    pass  # (still away: the next try finds out)
        print(f"  {size:>12,}  {rel}", flush=True)
    call(args.host, "/api/upload/finish", {"task_id": task})
    print(f"{len(files)} files, {sum(sizes) / 2**20:.1f} MB into {args.dest}/{name} "
          f"in {time.monotonic() - start:.1f}s")

    # Every file's size on the PS5 against the local one.
    remote: dict[str, int] = {}
    def walk(path: str, rel: str) -> None:
        for e in call(args.host, "/api/list", {"path": path})["entries"]:
            if e["type"] == "d":
                walk(f"{path}/{e['name']}", f"{rel}/{e['name']}")
            else:
                remote[f"{rel}/{e['name']}"] = e["size"]
    walk(f"{args.dest}/{name}", name)
    bad = [rel for rel, size in zip(rels, sizes) if remote.get(rel) != size]
    for rel in bad:
        print(f"  size differs: {rel} (PS5 {remote.get(rel, 'missing')}, here "
              f"{sizes[rels.index(rel)]})", file=sys.stderr)
    print(f"checked: {len(files) - len(bad)} of {len(files)} files match")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
