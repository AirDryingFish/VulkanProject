#!/usr/bin/env python3
"""Fetch or verify pinned Khronos scenes without overwriting existing assets.

Run from any directory. Uses only the Python standard library. Git blob SHA-1
and byte lengths come from the official GitHub tree at the pinned revision.
Image files remain subject to the repository's Git LFS attributes.
"""

import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import sys
import time
from urllib.parse import unquote, urlsplit
from urllib.request import Request, urlopen


ROOT = Path(__file__).resolve().parents[1] / "assets" / "models" / "gltf"
MANIFEST = ROOT / "scene-assets.json"


def verify_bytes(data, entry):
    if len(data) != entry["bytes"]:
        raise ValueError(f"{entry['path']}: incorrect byte count")
    header = f"blob {len(data)}\0".encode("ascii")
    if hashlib.sha1(header + data).hexdigest() != entry["git_blob_sha1"]:
        raise ValueError(f"{entry['path']}: Git blob checksum mismatch")


def process(entry, verify_only, url_prefix):
    relative = Path(entry["path"])
    if relative.is_absolute() or ".." in relative.parts:
        raise ValueError(f"Unsafe asset path: {relative}")
    target = ROOT / relative
    if not target.resolve().is_relative_to(ROOT.resolve()):
        raise ValueError(f"Asset path escapes asset directory: {relative}")
    if not entry["url"].startswith(url_prefix):
        raise ValueError(f"Unexpected upstream URL: {entry['url']}")
    if target.exists():
        verify_bytes(target.read_bytes(), entry)
        return f"verified {relative}"
    if verify_only:
        raise FileNotFoundError(f"Missing asset: {relative}")

    for attempt in range(3):
        try:
            request = Request(entry["url"], headers={"User-Agent": "VulkanProject-SceneAssets"})
            with urlopen(request, timeout=45) as response:
                data = response.read(entry["bytes"] + 1)
            verify_bytes(data, entry)
            break
        except Exception:
            if attempt == 2:
                raise
            time.sleep(attempt + 1)

    target.parent.mkdir(parents=True, exist_ok=True)
    # Exclusive creation deliberately refuses to replace any user file.
    with target.open("xb") as output:
        output.write(data)
    return f"downloaded {relative}"


def verify_scene_dependencies(files):
    for entry in files:
        path = ROOT / entry["path"]
        if path.suffix != ".gltf":
            continue
        document = json.loads(path.read_text(encoding="utf-8"))
        for category in ("buffers", "images"):
            for resource in document.get(category, []):
                uri = resource.get("uri", "")
                if not uri or uri.startswith("data:"):
                    continue
                parsed = urlsplit(uri)
                if parsed.scheme or parsed.netloc:
                    raise ValueError(f"{path.name}: non-local dependency {uri}")
                dependency = (path.parent / unquote(parsed.path)).resolve()
                if not dependency.is_relative_to(ROOT.resolve()) or not dependency.is_file():
                    raise ValueError(f"{path.name}: missing or unsafe dependency {uri}")
                if category == "buffers" and dependency.stat().st_size < resource["byteLength"]:
                    raise ValueError(f"{path.name}: truncated buffer {uri}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify-only", action="store_true", help="Check all files offline; download nothing")
    args = parser.parse_args()
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    if manifest["format"] != 1:
        raise ValueError("Unsupported scene manifest format")
    prefix = "https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/" + manifest["revision"] + "/"
    files = manifest["files"]
    with ThreadPoolExecutor(max_workers=6) as pool:
        for message in pool.map(lambda entry: process(entry, args.verify_only, prefix), files):
            print(message)
    verify_scene_dependencies(files)
    total = sum(entry["bytes"] for entry in files)
    print(f"OK: {len(files)} files, {total:,} bytes, revision {manifest['revision']}")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"scene-assets: {error}", file=sys.stderr)
        sys.exit(1)
