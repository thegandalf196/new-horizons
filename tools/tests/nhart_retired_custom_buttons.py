"""Finite retirement of the custom NH Spells and Cancel buttons (2026-11-23).

Every window that used them now uses the original Heroes III IOKAY/ICANCEL
buttons, and the hero action window uses the battle's own Cast Spell button.
"""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RECORD = 'assets/new-horizons/retired-custom-buttons.json'
RECORD_SHA256 = '2e63a657773e796144537f9696d58f18b56629b1b6ba6c66990a7f57703b02f7'
WAVE_SHA256 = '1cf14dfe3d8bf00e3718eb733a016acf4872b89f32f04877c5cf76893cc50bf4'
RESOURCES = tuple(sorted('SPRITES/NH_%s_%s' % (button, suffix) for button in ('cancel', 'spells')
                         for suffix in ('button.json', 'normal.png', 'pressed.png', 'disabled.png', 'highlighted.png')))
CONSUMERS = ('client/battle/BattleHeroActionWindow.cpp', 'client/windows/CBlacksmithDialog.cpp',
             'client/windows/CCastleInterface.cpp', 'client/windows/HeroGrowthWindow.cpp',
             'client/windows/NewHorizonsPerkBrowser.cpp')


def validate_wave(wave):
    payload = json.dumps(wave, sort_keys=True, separators=(',', ':')).encode()
    if hashlib.sha256(payload).hexdigest() != WAVE_SHA256:
        raise ValueError('Retired custom button wave changed')


def custom_button_retirement_wave(waves):
    matches = [wave for wave in waves if 'retiredCustomButtons' in wave]
    if len(matches) != 1:
        raise ValueError('Missing or duplicate retired custom button wave')
    validate_wave(matches[0])
    return matches[0]


def validate_custom_button_retirement(wave, entries, index):
    validate_wave(wave)
    payload = (ROOT / RECORD).read_bytes()
    if hashlib.sha256(payload).hexdigest() != RECORD_SHA256:
        raise ValueError('Retired custom button record changed')
    record = {row['resource']: row for row in json.loads(payload)['entries']}
    names = [row['resource'] for row in wave['retiredEntries']]
    if (wave['resources'] or tuple(names) != RESOURCES or set(record) != set(names)
            or wave['entryCount'] != wave['baselineCount'] - len(names)
            or wave['retiredCustomButtons'] != [{'retired': names, 'selectionRecordSha256': RECORD_SHA256}]
            or {row['resource'] for row in entries} & set(names) or any(name in index for name in names)):
        raise ValueError('Retired custom buttons are incomplete or still shipped')
    for row in wave['retiredEntries']:
        kept = record[row['resource']]
        if (row['sha256'], row['size']) != (kept['sha256'], kept['size']) or not kept['reason']:
            raise ValueError('Retired custom button record mismatch')
    for path in CONSUMERS:
        source = (ROOT / path).read_text(encoding='utf-8')
        if 'NH_cancel_button' in source or 'NH_spells_button' in source:
            raise ValueError('A retired custom button is still bound: ' + path)
