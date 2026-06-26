"""Convert .po translation files to GNU .mo binary format."""
import os
import re
import struct
import sys


def parse_po(path):
    """Return list of (msgid, msgstr) pairs from a .po file."""
    pairs = []
    msgid = None
    msgstr = None

    def flush():
        if msgid is not None:
            pairs.append((msgid, msgstr or ""))

    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if line.startswith("msgid "):
                flush()
                msgid = _parse_string(line)
                msgstr = None
            elif line.startswith("msgstr "):
                msgstr = _parse_string(line)
            elif line.startswith('"') and msgid is not None:
                val = _parse_string(line)
                if msgstr is not None:
                    msgstr += val
                else:
                    msgid += val
    flush()
    return pairs


def _parse_string(line):
    """Extract the quoted content from a line like msgid "foo" or '"foo"'."""
    m = re.search(r'"((?:[^"\\]|\\.)*)"', line)
    if m:
        return m.group(1).replace('\\n', '\n').replace('\\t', '\t').replace('\\"', '"')
    return ""


def write_mo(pairs, output_path):
    """Write GNU MO file from list of (msgid, msgstr) pairs."""

    # Prepare sorted lists (original order as in .po file)
    origs = [p[0].encode("utf-8") for p in pairs]
    trans = [p[1].encode("utf-8") for p in pairs]
    n = len(pairs)

    # Build string data (concatenated, null-terminated)
    orig_data = b"\x00".join(origs) + b"\x00"
    trans_data = b"\x00".join(trans) + b"\x00"

    # Offsets for descriptor tables
    # Header = 7 * 4 = 28 bytes
    # Descriptors = 2 * n * 8 bytes  (each descriptor is 2 * uint32)
    desc_offset = 28
    orig_desc_offset = desc_offset
    trans_desc_offset = desc_offset + n * 8
    orig_string_offset = trans_desc_offset + n * 8
    trans_string_offset = orig_string_offset + len(orig_data)

    with open(output_path, "wb") as f:
        # Header
        f.write(struct.pack("<IIIIIII",
                            0x950412de,   # magic
                            0,            # revision
                            n,            # nstrings
                            orig_desc_offset,
                            trans_desc_offset,
                            0,            # hash table size
                            0,            # hash table offset
                            ))

        # Original strings descriptors
        off = orig_string_offset
        for s in origs:
            f.write(struct.pack("<II", len(s), off))
            off += len(s) + 1

        # Translation strings descriptors
        off = trans_string_offset
        for s in trans:
            f.write(struct.pack("<II", len(s), off))
            off += len(s) + 1

        # Actual string data
        f.write(orig_data)
        f.write(trans_data)


def main():
    if len(sys.argv) < 3:
        print("Usage: po2mo.py input.po output.mo", file=sys.stderr)
        sys.exit(1)

    pairs = parse_po(sys.argv[1])
    write_mo(pairs, sys.argv[2])
    print(f"  {sys.argv[2]}  ({len(pairs)} strings)")


if __name__ == "__main__":
    main()
