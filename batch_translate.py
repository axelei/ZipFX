"""
Batch translate all unfinished translations in all .ts files.
Uses a robust approach: split on unfinished tag, find preceding <source>.
"""
import re, os, sys

from deep_translator import GoogleTranslator

LANGS = {
    'ar': 'arabic', 'da': 'danish', 'de': 'german', 'es': 'spanish',
    'fi': 'finnish', 'fr': 'french', 'it': 'italian', 'ja': 'japanese',
    'ko': 'korean', 'nl': 'dutch', 'no': 'norwegian', 'pt': 'portuguese',
    'ru': 'russian', 'sv': 'swedish', 'zh': 'chinese (simplified)',
}

TS_DIR = os.path.join(os.path.dirname(__file__), 'translations')

def log(msg):
    sys.stdout.buffer.write((msg + '\n').encode('utf-8', errors='replace'))

UNFINISHED_TAG = '<translation type="unfinished" />'

for fname in sorted(os.listdir(TS_DIR)):
    if not fname.endswith('.ts'):
        continue
    lang_code = fname.replace('zipfx_', '').replace('.ts', '')
    if lang_code == 'en':
        continue
    target = LANGS.get(lang_code)
    if not target:
        log(f"SKIP {fname}: unknown '{lang_code}'")
        continue

    fpath = os.path.join(TS_DIR, fname)
    with open(fpath, 'r', encoding='utf-8') as f:
        content = f.read()

    if UNFINISHED_TAG not in content:
        log(f"OK {fname}: no unfinished")
        continue

    parts = content.split(UNFINISHED_TAG)
    num_unfinished = len(parts) - 1

    translator = GoogleTranslator(source='en', target=target)
    new_parts = []

    for i, part in enumerate(parts):
        if i < num_unfinished:
            src_match = re.findall(r'<source>(.*?)</source>', part, re.DOTALL)
            if not src_match:
                log(f"  ERROR: no source found for unfinished #{i+1} in {fname}")
                new_parts.append(part + UNFINISHED_TAG)
                continue

            src = src_match[-1]
            try:
                translated = translator.translate(src)
                safe = translated.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')
                new_parts.append(part + f'<translation>{safe}</translation>')
                log(f"  [{lang_code} #{i+1}/{num_unfinished}] {src[:50]}... -> {translated[:50]}...")
            except Exception as e:
                log(f"  ERROR [{lang_code} #{i+1}]: {e}")
                new_parts.append(part + UNFINISHED_TAG)
        else:
            new_parts.append(part)

    new_content = ''.join(new_parts)

    with open(fpath, 'w', encoding='utf-8') as f:
        f.write(new_content)

    # Verify
    remaining = new_content.count(UNFINISHED_TAG)
    if remaining == 0:
        log(f"  OK {fname}: all {num_unfinished} done")
    else:
        log(f"  PARTIAL {fname}: {remaining} still unfinished")

log("\nDone!")
