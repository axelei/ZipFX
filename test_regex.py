import re

fname = 'translations/zipfx_de.ts'
with open(fname, 'r', encoding='utf-8') as f:
    content = f.read()

pattern = r'<source>(.*?)</source>\s*<translation type="unfinished"\s*/>'
m = re.search(pattern, content, re.DOTALL)
if m:
    print('MATCHED:', repr(m.group(1)))
else:
    print('NO MATCH')
