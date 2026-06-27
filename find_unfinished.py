import re, os

files = [f for f in sorted(os.listdir('translations')) if f.endswith('.ts') and f != 'zipfx_en.ts']
sources_by_lang = {}
for fname in files:
    with open(f'translations/{fname}', 'r', encoding='utf-8') as f:
        c = f.read()
    matches = re.findall(r'<source>(.*?)</source>\s*<translation type="unfinished"\s*/>', c, re.DOTALL)
    sources_by_lang[fname] = matches

# Show unique sources per language
for fname, sources in sorted(sources_by_lang.items()):
    print(f'{fname}: {len(sources)} unfinished')
    for s in sources:
        print(f'  {s.encode("unicode_escape").decode("ascii")}')
    print()
