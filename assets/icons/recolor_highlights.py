"""Recolor all highlight SVGs - halfway saturation between original and current"""
import glob
import os

base = os.path.dirname(os.path.abspath(__file__))
patterns = [
    os.path.join(base, '*_highlight.svg'),
    os.path.join(base, 'svg', '*_highlight.svg')
]

files = []
for p in patterns:
    files.extend(glob.glob(p))

print(f'Found {len(files)} highlight files')

# Halfway between original and current desaturated:
# Original: #1e1b4b, #ec4899
# Current:  #3d3a5c, #c87a9d
# Halfway:  #2e2b54, #da619b
for f in files:
    with open(f, 'r', encoding='utf-8') as file:
        content = file.read()

    content = content.replace('#3d3a5c', '#2e2b54')
    content = content.replace('#c87a9d', '#da619b')

    with open(f, 'w', encoding='utf-8') as file:
        file.write(content)

    print(f'  Updated: {os.path.basename(f)}')

print('Done!')
