#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Configure an explicitly selected TTWVR neural test INI; restart to apply."""
import argparse
import configparser
from pathlib import Path
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--config', required=True, type=Path, help='Private layer INI path')
parser.add_argument('action', choices=['status', 'on', 'off', 'all-off', 'quality',
                                     'strength', 'tone', 'detail', 'model', 'dlaa'])
parser.add_argument('value', nargs='?')
args = parser.parse_args()
if not args.config.is_file():
    parser.error('Configuration does not exist')
values = {'quality', 'strength', 'tone', 'detail', 'model', 'dlaa'}
if (args.action in values) != (args.value is not None):
    parser.error('This action requires one value' if args.action in values else 'Unexpected value')
if args.action in ('quality', 'strength', 'tone', 'detail'):
    try:
        args.value = int(args.value)
    except ValueError:
        parser.error('A whole-number percentage is required')
    if args.action == 'quality' and args.value not in (25, 50, 75, 100):
        parser.error('quality must be 25, 50, 75 or 100 percent per dimension')
    if args.action != 'quality' and not 0 <= args.value <= 100:
        parser.error('Percentage must be between 0 and 100')
if args.action == 'model' and args.value.upper() not in ('A', 'B'):
    parser.error('model must be A or B')
if args.action == 'dlaa' and args.value not in ('on', 'off'):
    parser.error('dlaa requires on or off')
config = configparser.ConfigParser()
config.read(args.config)
for section in ('neural_rendering', 'dlaa', 'capture'):
    if not config.has_section(section):
        config.add_section(section)
nr = config['neural_rendering']
if args.action == 'on':
    nr.update(enabled='1', experimental_temporal='1', experimental_spatial='0', transport_identity='0')
elif args.action == 'off':
    nr['enabled'] = '0'
elif args.action == 'all-off':
    nr['enabled'] = config['dlaa']['enabled'] = config['dlaa']['jitter_probe'] = '0'
    config['capture']['native_motion_probe'] = '0'
elif args.action in ('quality', 'strength', 'tone', 'detail'):
    nr[{'quality': 'resolution_percent', 'strength': 'intensity_percent',
        'tone': 'tone_percent', 'detail': 'detail_percent'}[args.action]] = str(args.value)
elif args.action == 'model':
    nr['model'] = args.value.upper()
elif args.action == 'dlaa':
    config['dlaa']['enabled'] = '1' if args.value == 'on' else '0'
    if args.value == 'on':
        nr['transport_identity'] = '0'
if args.action != 'status':
    temp = None
    try:
        with tempfile.NamedTemporaryFile('w', dir=args.config.parent,
                                          prefix=args.config.name + '.', delete=False) as f:
            temp = Path(f.name)
            config.write(f)
        temp.replace(args.config)
    finally:
        if temp and temp.exists():
            temp.unlink()
    print('Settings saved. Restart the test game to apply changes.')
print(f'Config: {args.config}')
print(f'NR configured: {nr.get("enabled", "0")} | model {nr.get("model", "B")} | '
      f'{nr.get("resolution_percent", "100")}% per dimension | '
      f'strength {nr.get("intensity_percent", "50")}% | '
      f'tone {nr.get("tone_percent", "100")}% | detail {nr.get("detail_percent", "100")}%')
print(f'DLAA configured: {config["dlaa"].get("enabled", "0")} | native resolution')
print('Configuration alone does not prove either feature evaluated successfully.')
