"""
Translate all remaining unfinished translations for all 15 languages.
"""
import re, os, sys, time
from deep_translator import GoogleTranslator

LANGS = {
    'ar': 'arabic', 'da': 'danish', 'de': 'german', 'es': 'spanish',
    'fi': 'finnish', 'fr': 'french', 'it': 'italian', 'ja': 'japanese',
    'ko': 'korean', 'nl': 'dutch', 'no': 'norwegian', 'pt': 'portuguese',
    'ru': 'russian', 'sv': 'swedish', 'zh': 'chinese (simplified)',
}

TS_DIR = os.path.join(os.path.dirname(__file__), 'translations')
UNFINISHED_TAG = '<translation type="unfinished" />'

def log(msg):
    sys.stdout.buffer.write((msg + '\n').encode('utf-8', errors='replace'))

total_translated = 0
total_errors = 0

for fname in sorted(os.listdir(TS_DIR)):
    if not fname.endswith('.ts'):
        continue
    lang_code = fname.replace('zipfx_', '').replace('.ts', '')
    if lang_code == 'en':
        continue
    target = LANGS.get(lang_code)
    if not target:
        continue

    fpath = os.path.join(TS_DIR, fname)
    with open(fpath, 'r', encoding='utf-8') as f:
        content = f.read()

    if UNFINISHED_TAG not in content:
        log(f"OK {fname}: no unfinished")
        continue

    # Split by unfinished tag
    parts = content.split(UNFINISHED_TAG)
    num_unfinished = len(parts) - 1
    log(f"{fname}: {num_unfinished} unfinished")

    translator = GoogleTranslator(source='en', target=target)
    new_parts = []
    errors = 0
    translated = 0

    for i, part in enumerate(parts):
        if i < num_unfinished:
            # Find the last <source> in this part
            src_tags = re.findall(r'<source>(.*?)</source>', part, re.DOTALL)
            if not src_tags:
                log(f"  ERROR #{i+1}: no source found")
                new_parts.append(part + UNFINISHED_TAG)
                errors += 1
                continue
            src = src_tags[-1]

            try:
                result = translator.translate(src)
                safe = result.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')
                new_parts.append(part + f'<translation>{safe}</translation>')
                translated += 1
            except Exception as e:
                log(f"  ERROR #{i+1}: {e}")
                new_parts.append(part + UNFINISHED_TAG)
                errors += 1

            if i > 0 and i % 10 == 0:
                time.sleep(0.5)  # rate limit

        else:
            new_parts.append(part)

    new_content = ''.join(new_parts)
    with open(fpath, 'w', encoding='utf-8') as f:
        f.write(new_content)

    remaining = new_content.count(UNFINISHED_TAG)
    log(f"  -> {translated} translated, {errors} errors, {remaining} remaining")
    total_translated += translated
    total_errors += errors

log(f"\nDone: {total_translated} translated, {total_errors} errors")
