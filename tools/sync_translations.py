#!/usr/bin/env python3
"""
sync_translations.py — Sync tr() strings from C++ source into Qt .ts files.

Usage:
    python tools/sync_translations.py [--dry-run]

For each tr("...") found in src/, inserts a <message> block into every
translations/zipfx_*.ts file that is missing it:
  - English (zipfx_en.ts):  translation = source text (identity)
  - All others:             translation is left as type="unfinished"
"""

import argparse
import os
import re
import sys
from pathlib import Path
from xml.etree import ElementTree as ET

ROOT = Path(__file__).parent.parent
SRC  = ROOT / "src"
TS   = ROOT / "translations"

# Matches:  tr("some string")  or  tr( "some string" )
# Captures the raw string content (may contain escaped chars).
TR_RE = re.compile(r'\btr\s*\(\s*"((?:[^"\\]|\\.)*)"\s*[,\)]')

# Matches class declaration to determine Qt context name.
CLASS_RE = re.compile(r'\bclass\s+(\w+)\s*(?:final\s*)?(?:Q_DECL_FINAL\s*)?(?::\s*[^{;]+)?\s*\{')

# Map filename stem → context name (matches what lupdate produces).
# Files not listed here use the first Q_OBJECT class found in the file.
FILENAME_CONTEXT: dict[str, str] = {
    "MainWindow":            "MainWindow",
    "CreateArchiveDialog":   "CreateArchiveDialog",
    "FileListModel":         "FileListModel",
    "FindFilesDialog":       "FindFilesDialog",
    "DragProgressDialog":    "DragProgressDialog",
    "ArchiveTreeView":       "ArchiveTreeView",
    "ProgressDialog":        "ProgressDialog",
}


def context_for_file(path: Path, text: str) -> str:
    """Determine the Qt Linguist context name for a source file."""
    stem = path.stem
    if stem in FILENAME_CONTEXT:
        return FILENAME_CONTEXT[stem]
    # Fall back to first Q_OBJECT class found in file.
    for m in CLASS_RE.finditer(text):
        name = m.group(1)
        # Skip obvious non-UI helper classes.
        if name not in ("Q_DECL_FINAL",):
            return name
    return "QObject"


def collect_tr_strings(src_root: Path) -> dict[str, set[str]]:
    """Return {context_name: {source_string, ...}} from all .cpp/.h files."""
    result: dict[str, set[str]] = {}

    for path in sorted(src_root.rglob("*")):
        if path.suffix not in (".cpp", ".h", ".mm"):
            continue

        text = path.read_text(encoding="utf-8", errors="replace")
        # Join adjacent C++ string literals split across lines:
        #   "foo"\n    "bar"  →  "foobar"
        text = re.sub(r'"\s*\n\s*"', '', text)
        matches = list(TR_RE.finditer(text))
        if not matches:
            continue

        ctx = context_for_file(path, text)

        for m in matches:
            raw = m.group(1)
            # Unescape common C escape sequences to match Qt's source text.
            src = (raw
                   .replace("\\n", "\n")
                   .replace("\\t", "\t")
                   .replace('\\"', '"')
                   .replace("\\\\", "\\"))
            result.setdefault(ctx, set()).add(src)

    return result


def parse_ts(path: Path) -> ET.ElementTree:
    ET.register_namespace("", "")
    tree = ET.parse(path)
    return tree


def get_existing_sources(root: ET.Element, context_name: str) -> set[str]:
    """Return all <source> texts already in a given context."""
    for ctx in root.findall("context"):
        name_el = ctx.find("name")
        if name_el is not None and name_el.text == context_name:
            return {m.findtext("source", "") for m in ctx.findall("message")}
    return set()


def find_or_create_context(root: ET.Element, context_name: str) -> ET.Element:
    for ctx in root.findall("context"):
        name_el = ctx.find("name")
        if name_el is not None and name_el.text == context_name:
            return ctx
    # Create new context block.
    ctx = ET.SubElement(root, "context")
    name_el = ET.SubElement(ctx, "name")
    name_el.text = context_name
    return ctx


def make_message(source: str, translation: str, unfinished: bool) -> ET.Element:
    msg = ET.Element("message")
    src_el = ET.SubElement(msg, "source")
    src_el.text = source
    tr_el = ET.SubElement(msg, "translation")
    if unfinished:
        tr_el.set("type", "unfinished")
    else:
        tr_el.text = translation
    return msg


def indent_tree(elem: ET.Element, level: int = 0) -> None:
    """Add pretty-print indentation in-place (4-space, matching Qt Linguist)."""
    pad = "\n" + "    " * level
    child_pad = "\n" + "    " * (level + 1)
    if len(elem):
        if not elem.text or not elem.text.strip():
            elem.text = child_pad
        for child in elem:
            indent_tree(child, level + 1)
            if not child.tail or not child.tail.strip():
                child.tail = child_pad
        # Last child gets parent-level tail.
        if not child.tail or not child.tail.strip():
            child.tail = pad
    if level and (not elem.tail or not elem.tail.strip()):
        elem.tail = pad


def count_unfinished(root: ET.Element) -> int:
    """Count messages with type="unfinished" across all contexts."""
    return sum(
        1 for tr in root.iter("translation")
        if tr.get("type") == "unfinished"
    )


def sync_file(ts_path: Path, tr_map: dict[str, set[str]], is_english: bool,
              dry_run: bool) -> tuple[int, int]:
    """Insert missing messages. Returns (added, pending_unfinished)."""
    tree = ET.parse(ts_path)
    root = tree.getroot()
    added = 0

    for context_name, sources in sorted(tr_map.items()):
        existing = get_existing_sources(root, context_name)
        missing = sorted(sources - existing)
        if not missing:
            continue

        ctx_el = find_or_create_context(root, context_name)
        for src in missing:
            translation = src if is_english else ""
            msg = make_message(src, translation, unfinished=not is_english)
            ctx_el.append(msg)
            added += 1
            flag = "" if is_english else " [unfinished]"
            print(f"  + [{context_name}] {src!r:.80}{flag}")

    if added and not dry_run:
        # Re-indent and write back preserving XML declaration.
        indent_tree(root)
        xml_bytes = ET.tostring(root, encoding="unicode", xml_declaration=False)
        ts_path.write_text(
            "<?xml version='1.0' encoding='utf-8'?>\n"
            "<!DOCTYPE TS>\n"
            + xml_bytes + "\n",
            encoding="utf-8"
        )

    pending = count_unfinished(root)
    return added, pending


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dry-run", action="store_true",
                        help="Print what would change without writing files")
    args = parser.parse_args()

    print("Scanning source files…")
    tr_map = collect_tr_strings(SRC)
    total_strings = sum(len(v) for v in tr_map.values())
    print(f"Found {total_strings} unique tr() strings across {len(tr_map)} contexts.\n")

    ts_files = sorted(TS.glob("zipfx_*.ts"))
    if not ts_files:
        print("No .ts files found in translations/", file=sys.stderr)
        sys.exit(1)

    grand_total = 0
    print(f"{'File':<22} {'Added':>7}  {'Pending':>7}")
    print("-" * 40)
    for ts_path in ts_files:
        is_english = ts_path.name == "zipfx_en.ts"
        added, pending = sync_file(ts_path, tr_map, is_english, args.dry_run)
        grand_total += added
        added_col = f"+{added}" if added else "-"
        pending_col = str(pending) if not is_english else "n/a"
        print(f"{ts_path.name:<22} {added_col:>7}  {pending_col:>7}")

    print("-" * 40)
    action = "would add" if args.dry_run else "added"
    print(f"\n{action.capitalize()} {grand_total} message(s) total.")
    if args.dry_run:
        print("(dry-run — no files written)")


if __name__ == "__main__":
    main()
