# -*- coding: utf-8 -*-
import re
path = 'data/map planets.txt'
with open(path, 'r', encoding='utf-8') as f:
    content = f.read()
blocks = re.split(r'\nplanet\s+', content)
results = []
for i, block in enumerate(blocks):
    if i == 0 and not block.strip().startswith('planet'):
        continue
    first = block.strip().split('\n')[0].strip()
    if first.startswith('"'):
        mat = re.match(r'"([^"]+)"', first)
        name = mat.group(1) if mat else None
    else:
        name = first.split()[0] if first else None
    if not name:
        continue
    m = re.search(r'description\s+`([^`]*)`', block)
    if m:
        desc = m.group(1).strip()
        results.append((name, desc))
import json
# Load existing keys from language/ru/planets.json
from pathlib import Path
_ru_planets = Path('language/ru/planets.json')
if _ru_planets.is_file():
    with open(_ru_planets, 'r', encoding='utf-8') as f:
        ru = json.load(f)
    existing = {k for k in ru if k.startswith('planet.desc.')}
else:
    ru = {}
    existing = set()
# Planet name from key: "planet.desc.Big Sky" -> "Big Sky"
existing_names = {k.replace('planet.desc.', '', 1) for k in existing}
all_names = [name for name, _ in results]
missing = [name for name in all_names if name not in existing_names]
# Output missing as JSON for next step: list of (name, desc)
missing_data = [(name, desc) for name, desc in results if name not in existing_names]
with open('missing_planets.json', 'w', encoding='utf-8') as out:
    json.dump(missing_data, out, ensure_ascii=False, indent=0)
print('Total with description:', len(results))
print('Existing in language/ru (planet.desc.*):', len(existing_names))
print('Missing:', len(missing))
print('First 80 missing:', missing[:80])
