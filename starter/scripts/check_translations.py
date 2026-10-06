#!/usr/bin/env python3
"""Fail when a committed translations/*.ts no longer lists the strings the sources use.

Run after `cmake --build build --target update_translations`, which rewrites the files in place from the
sources. Each file is compared with its committed version by content, not bytes, so lupdate versions that
format differently still agree. Untranslated (unfinished) entries are fine: they show the English text.
"""
from pathlib import Path
import subprocess
import sys
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parent.parent


def messages(xml):
    found = set()
    for context in ET.fromstring(xml).iter("context"):
        name = context.findtext("name")
        for message in context.iter("message"):
            translation = message.find("translation")
            if translation is not None and translation.get("type") in ("vanished", "obsolete"):
                continue
            found.add((name, message.findtext("source"), message.findtext("comment") or ""))
    return found


failed = False
for path in sorted((root / "translations").glob("*.ts")):
    relative = path.relative_to(root.parent)
    committed = subprocess.run(["git", "show", f"HEAD:{relative.as_posix()}"], cwd=root,
                               capture_output=True, check=True).stdout
    added = messages(path.read_bytes()) - messages(committed)
    removed = messages(committed) - messages(path.read_bytes())
    for context, source, _ in sorted(added):
        print(f"{path.name}: new string not in the committed file: {context}: {source!r}")
    for context, source, _ in sorted(removed):
        print(f"{path.name}: committed string no longer in the sources: {context}: {source!r}")
    failed = failed or bool(added or removed)
if failed:
    print("Run `cmake --build build --target update_translations`, translate what you can, and commit the .ts files.")
sys.exit(1 if failed else 0)
