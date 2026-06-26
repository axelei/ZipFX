# Translate ZipFX

Scan all C++ source files for `tr()` strings, sync missing keys into `translations/zipfx_en.ts`, then translate every new key into all other 15 language `.ts` files.

## Step 1 — Collect all tr() sources

Search `src/` recursively for `tr("...")` calls (including multi-line). For each unique string, note its enclosing class name (the Q_OBJECT class or the nearest `class Foo` declaration in the file) — this becomes the Qt Linguist context name.

## Step 2 — Identify missing keys in zipfx_en.ts

Parse `translations/zipfx_en.ts`. For every `<source>` text found in Step 1 that has **no** `<message>` block in the matching `<context>` in the English file, it is a missing key.

## Step 3 — Add missing keys to zipfx_en.ts

For each missing key, insert a `<message>` block into the correct `<context>` in `zipfx_en.ts`. The `<translation>` should equal the source text (English is the source language). Example:

```xml
        <message>
            <source>Install RAR</source>
            <translation>Install RAR</translation>
        </message>
```

If the context doesn't exist yet in the file, create it before the closing `</TS>` tag:

```xml
    <context>
        <name>ContextName</name>
        <message>
            <source>...</source>
            <translation>...</translation>
        </message>
    </context>
```

## Step 4 — Propagate to all other language files

The other language files are:
`zipfx_ar.ts` (Arabic), `zipfx_da.ts` (Danish), `zipfx_de.ts` (German), `zipfx_es.ts` (Spanish), `zipfx_fi.ts` (Finnish), `zipfx_fr.ts` (French), `zipfx_it.ts` (Italian), `zipfx_ja.ts` (Japanese), `zipfx_ko.ts` (Korean), `zipfx_nl.ts` (Dutch), `zipfx_no.ts` (Norwegian), `zipfx_pt.ts` (Portuguese), `zipfx_ru.ts` (Russian), `zipfx_sv.ts` (Swedish), `zipfx_zh.ts` (Chinese Simplified).

For **each** language file, for **each** missing key identified in Step 2:
- Locate the matching `<context>` (same `<name>`), or create it.
- Check if a `<message>` for that `<source>` already exists.
- If it doesn't, insert a new `<message>` block with a proper translation in that language. Use your knowledge of the language to provide accurate translations — do not leave `type="unfinished"` unless you genuinely cannot translate it.

For RTL language (Arabic `ar`), ensure translations are correct right-to-left text.

## Step 5 — Report

After all edits, print a summary table:
- How many new keys were added
- Which contexts they belong to
- Confirmation that all 16 files were updated

## Important rules

- Preserve the existing XML structure exactly — indentation is 4 spaces, self-closing tags for empty translations use `<translation type="unfinished" />`.
- Do **not** remove or modify existing `<message>` entries.
- Do **not** alter `<location>` lines — they are added by lupdate and may be stale; just leave existing ones as-is and omit location from new entries you add manually.
- Keep messages in the same order as they appear in the existing file (append new ones at the end of their context block).
- The `<TS version="2.1" language="...">` attribute at the top of each file must be preserved unchanged.
