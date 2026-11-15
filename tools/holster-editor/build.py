#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build the portable offline editor with Python's standard library."""
from pathlib import Path
import argparse
import zipfile
root = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--check', action='store_true', help='fail if index.html is stale')
parser.add_argument('--package', type=Path, help='also create a portable ZIP at this path')
args = parser.parse_args()
if args.check and args.package:
    parser.error('--check and --package cannot be used together')
html = (root/'src/shell.html').read_text(encoding='utf-8')
for tag, name in [('STYLE', 'style.css'), ('STATE', 'state.js'), ('GEOMETRY', 'geometry.js'), ('APP', 'app.js')]:
    source = (root/'src'/name).read_text(encoding='utf-8')
    if tag != 'STYLE' and '</script' in source.lower():
        raise SystemExit('Unsafe inline script closing tag')
    assert html.count('{{'+tag+'}}') == 1
    html = html.replace('{{'+tag+'}}', source)
output = root/'index.html'
if args.check:
    if not output.exists() or output.read_bytes() != html.encode():
        raise SystemExit('index.html is stale: run build.py')
    print('Portable HTML matches its source.')
else:
    output.write_bytes(html.encode())
    print(output)

if args.package:
    args.package.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.package, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        archive.writestr('TTWVR-Holster-Editor.html', html.encode('utf-8'))
        archive.writestr('README.txt', (root/'PORTABLE.txt').read_bytes())
        archive.writestr('LICENSE.txt', (root.parents[1]/'LICENSE').read_bytes())
    print(args.package.resolve())
