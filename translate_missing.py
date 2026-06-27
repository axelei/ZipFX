"""Translate the single unfinished string in each .ts file."""
import re, os, sys
from deep_translator import GoogleTranslator

LANGS = {
    'ar': 'arabic', 'da': 'danish', 'de': 'german', 'es': 'spanish',
    'fi': 'finnish', 'fr': 'french', 'it': 'italian', 'ja': 'japanese',
    'ko': 'korean', 'nl': 'dutch', 'no': 'norwegian', 'pt': 'portuguese',
    'ru': 'russian', 'sv': 'swedish', 'zh': 'chinese (simplified)',
}

def log(msg):
    """Print safely to console."""
    sys.stdout.buffer.write((msg + '\n').encode('utf-8', errors='replace'))

TS_DIR = os.path.join(os.path.dirname(__file__), 'translations')

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

    idx = content.find('<translation type="unfinished" />')
    if idx == -1:
        log(f"SKIP {fname}: no unfinished")
        continue

    prefix = content[:idx]
    src_tags = re.findall(r'<source>(.*?)</source>', prefix, re.DOTALL)
    if not src_tags:
        log(f"SKIP {fname}: no source found")
        continue
    src = src_tags[-1]

    log(f"  [{lang_code}] Translating: {src[:80]}...")
    translated = GoogleTranslator(source='en', target=target).translate(src)
    log(f"    Got: {translated[:80]}...")

    safe = translated.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')
    content = content.replace('<translation type="unfinished" />', f'<translation>{safe}</translation>', 1)

    with open(fpath, 'w', encoding='utf-8') as f:
        f.write(content)
    log(f"  OK {fname}")

log("\nDone!")
