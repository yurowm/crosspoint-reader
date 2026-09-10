#!/usr/bin/env python3
import hashlib
import json
import os
import shutil
import sys
import tempfile
from pathlib import Path


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    source = Path(sys.argv[1] if len(sys.argv) > 1 else "/opt/media/ebooks").resolve()
    output = Path(sys.argv[2] if len(sys.argv) > 2 else "/var/lib/crosspoint-sync").resolve()
    books_dir = output / "books"
    output.mkdir(parents=True, exist_ok=True)
    books_dir.mkdir(parents=True, exist_ok=True)

    old = {}
    manifest_path = output / "manifest.json"
    try:
        old = {item["path"]: item for item in json.loads(manifest_path.read_text())["books"]}
    except (OSError, KeyError, TypeError, json.JSONDecodeError):
        pass

    books = []
    used_assets = set()
    for path in sorted(source.rglob("*")):
        if not path.is_file() or path.suffix.lower() != ".epub":
            continue
        stat = path.stat()
        relative = path.relative_to(source).as_posix()
        previous = old.get(relative, {})
        if previous.get("size") == stat.st_size and previous.get("mtime_ns") == stat.st_mtime_ns:
            digest = previous.get("sha256", "")
        else:
            digest = sha256(path)
        if len(digest) != 64:
            digest = sha256(path)
        asset = f"{digest}.epub"
        destination = books_dir / asset
        if not destination.exists() or destination.stat().st_size != stat.st_size or sha256(destination) != digest:
            temporary = books_dir / f".{asset}.part"
            shutil.copy2(path, temporary)
            os.replace(temporary, destination)
        used_assets.add(asset)
        books.append({
            "path": relative,
            "size": stat.st_size,
            "mtime_ns": stat.st_mtime_ns,
            "sha256": digest,
            "url": f"books/{asset}",
        })

    for asset in books_dir.glob("*.epub"):
        if asset.name not in used_assets:
            asset.unlink()

    payload = {"version": 1, "books": books}
    fd, temporary_name = tempfile.mkstemp(prefix=".manifest-", dir=output, text=True)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            json.dump(payload, stream, ensure_ascii=False, separators=(",", ":"))
            stream.write("\n")
        os.replace(temporary_name, manifest_path)
    finally:
        if os.path.exists(temporary_name):
            os.unlink(temporary_name)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
