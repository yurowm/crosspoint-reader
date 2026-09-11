#!/usr/bin/env python3
import hashlib
import json
import os
import shutil
import sys
import tempfile
import zipfile
from pathlib import Path
from html.parser import HTMLParser
from xml.etree import ElementTree


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def local_name(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


class VisibleTextCounter(HTMLParser):
    HIDDEN_TAGS = {"script", "style", "head", "svg", "title", "meta", "link", "noscript"}
    BOUNDARY_TAGS = {
        "address", "article", "aside", "blockquote", "br", "div", "dl", "figcaption", "figure", "footer",
        "h1", "h2", "h3", "h4", "h5", "h6", "header", "hr", "li", "main", "nav", "ol", "p", "pre",
        "section", "table", "tr", "ul",
    }

    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.inside_body = False
        self.hidden_stack = []
        self.has_visible_text = False
        self.pending_whitespace = False
        self.count = 0

    def handle_starttag(self, tag: str, attrs) -> None:
        tag = tag.lower()
        if tag == "body":
            self.inside_body = True
        style = next((value for name, value in attrs if name.lower() == "style"), "") or ""
        hidden = bool(self.hidden_stack and self.hidden_stack[-1]) or tag in self.HIDDEN_TAGS or any(
            declaration.strip().lower().startswith("display:none")
            for declaration in style.replace(" ", "").split(";")
        )
        self.hidden_stack.append(hidden)
        if self.inside_body and not hidden and tag in self.BOUNDARY_TAGS and self.has_visible_text:
            self.pending_whitespace = True

    def handle_endtag(self, tag: str) -> None:
        tag = tag.lower()
        hidden = self.hidden_stack.pop() if self.hidden_stack else False
        if self.inside_body and not hidden and tag in self.BOUNDARY_TAGS and self.has_visible_text:
            self.pending_whitespace = True
        if tag == "body":
            self.inside_body = False

    def handle_startendtag(self, tag: str, attrs) -> None:
        self.handle_starttag(tag, attrs)
        self.handle_endtag(tag)

    def handle_data(self, data: str) -> None:
        if not self.inside_body or (self.hidden_stack and self.hidden_stack[-1]):
            return
        for character in data:
            if character.isspace():
                if self.has_visible_text:
                    self.pending_whitespace = True
                continue
            if self.pending_whitespace:
                self.count += 1
            self.count += 1
            self.has_visible_text = True
            self.pending_whitespace = False


def read_epub_metadata(path: Path) -> dict:
    result = {
        "title": path.stem,
        "authors": [],
        "series": "",
        "series_index": "",
        "year": "",
        "tags": [],
        "characters": 0,
    }
    try:
        with zipfile.ZipFile(path) as archive:
            container = ElementTree.fromstring(archive.read("META-INF/container.xml"))
            rootfile = next(
                element.attrib.get("full-path", "")
                for element in container.iter()
                if local_name(element.tag) == "rootfile"
            )
            opf = ElementTree.fromstring(archive.read(rootfile))
            opf_dir = Path(rootfile).parent
            metadata = next((element for element in opf if local_name(element.tag) == "metadata"), None)
            if metadata is not None:
                titles = [(element.text or "").strip() for element in metadata if local_name(element.tag) == "title"]
                authors = [
                    (element.text or "").strip() for element in metadata if local_name(element.tag) == "creator"
                ]
                tags = [(element.text or "").strip() for element in metadata if local_name(element.tag) == "subject"]
                dates = [(element.text or "").strip() for element in metadata if local_name(element.tag) == "date"]
                result["title"] = next((value for value in titles if value), result["title"])
                result["authors"] = list(dict.fromkeys(value for value in authors if value))
                result["tags"] = list(dict.fromkeys(value for value in tags if value))
                date = next((value for value in dates if value), "")
                if len(date) >= 4 and date[:4].isdigit():
                    result["year"] = date[:4]

                collection_id = ""
                refinements = {}
                for element in metadata:
                    if local_name(element.tag) != "meta":
                        continue
                    name = element.attrib.get("name", "").lower()
                    content = element.attrib.get("content", "").strip()
                    prop = element.attrib.get("property", "").lower()
                    refines = element.attrib.get("refines", "").lstrip("#")
                    text = (element.text or "").strip()
                    if name == "calibre:series":
                        result["series"] = content
                    elif name == "calibre:series_index":
                        result["series_index"] = content
                    elif prop == "belongs-to-collection" and text and not result["series"]:
                        result["series"] = text
                        collection_id = element.attrib.get("id", "")
                    elif refines:
                        refinements[(refines, prop)] = text
                if collection_id:
                    result["series_index"] = refinements.get(
                        (collection_id, "group-position"), result["series_index"]
                    )

            manifest = {}
            spine = []
            for element in opf.iter():
                name = local_name(element.tag)
                if name == "item":
                    manifest[element.attrib.get("id", "")] = element.attrib.get("href", "")
                elif name == "itemref":
                    spine.append(element.attrib.get("idref", ""))
            total = 0
            for item_id in spine:
                href = manifest.get(item_id, "")
                if not href:
                    continue
                item_path = (opf_dir / href.split("#", 1)[0]).as_posix()
                counter = VisibleTextCounter()
                counter.feed(archive.read(item_path).decode("utf-8", errors="ignore"))
                total += counter.count
            result["characters"] = min(total, 0xFFFFFFFF)
    except (KeyError, OSError, StopIteration, ValueError, zipfile.BadZipFile, ElementTree.ParseError):
        pass
    return result


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
        if previous.get("sha256") == digest and isinstance(previous.get("metadata"), dict):
            metadata = previous["metadata"]
        else:
            metadata = read_epub_metadata(path)
        asset = f"{digest}.epub"
        destination = books_dir / asset
        if not destination.exists() or destination.stat().st_size != stat.st_size or sha256(destination) != digest:
            temporary = books_dir / f".{asset}.part"
            shutil.copy2(path, temporary)
            temporary.chmod(0o644)
            os.replace(temporary, destination)
        destination.chmod(0o644)
        used_assets.add(asset)
        books.append({
            "path": relative,
            "size": stat.st_size,
            "mtime_ns": stat.st_mtime_ns,
            "sha256": digest,
            "url": f"books/{asset}",
            "metadata": metadata,
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
        os.chmod(temporary_name, 0o644)
        os.replace(temporary_name, manifest_path)
    finally:
        if os.path.exists(temporary_name):
            os.unlink(temporary_name)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
