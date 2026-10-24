"""Exact test-only conditional-perk presentation metadata and header history."""
import json
import re

OVERLOAD_SUFFIX = '\n\n/// Presentation-only distinction for a reused stable ID. Use the authored label\n/// from saved definitions or the immutable offer; never look up installed rules.\n/// Missing/renamed/translated labels conservatively retain the historical icon.\ninline const std::string & newHorizonsPerkIcon(const std::string & perkId, std::string_view capturedName)\n{\n\tif(perkId == "new-horizons:sorceryMagic.countermage" && capturedName == "Arcane Ballistics")\n\t{\n\t\tstatic const std::string arcaneBallistics = "NH_perk_arcane_ballistics";\n\t\treturn arcaneBallistics;\n\t}\n\treturn newHorizonsPerkIcon(perkId);\n}\n'
CONDITION = ('new-horizons:sorceryMagic.countermage', 'Arcane Ballistics',
             'NH_perk_arcane_ballistics', 'NH_perk_countermage')


def header_before_perk_presentation(header):
    if 'std::string_view' not in header and 'NH_perk_arcane_ballistics' not in header:
        return header
    if not header.endswith(OVERLOAD_SUFFIX) or header.count(OVERLOAD_SUFFIX) != 1:
        raise ValueError('Unrecognized conditional perk presentation overload')
    if header.count('#include <string_view>\n') != 1:
        raise ValueError('Unrecognized conditional perk presentation include')
    return header[:-len(OVERLOAD_SUFFIX)].replace('#include <string_view>\n', '', 1)


def source_bound_icon(header, identity, captured_name):
    # Structural source check only; native C++ behavior is covered separately.
    base = header_before_perk_presentation(header)
    if base == header:
        raise ValueError('Conditional presentation overload absent')
    bindings = dict(re.findall(r'\{"(new-horizons:[^"]+)", "([^"]+)"\}', base))
    stable_id, name, icon, historical = CONDITION
    if bindings.get(stable_id) != historical:
        raise ValueError('Historical ID-only binding changed')
    return icon if (identity, captured_name) == (stable_id, name) else bindings.get(identity, 'NH_perk_neutral')


def validate_perk_presentation(row, header):
    required = {'id', 'capturedName', 'icon44', 'historicalIcon44', 'resources',
                'native44Sha256', 'native32Sha256', 'alphaMaximum44', 'alphaMaximum32',
                'origin', 'selection', 'approval', 'selectionEvidenceSha256'}
    if set(row) != required or tuple(row[k] for k in ('id', 'capturedName', 'icon44', 'historicalIcon44')) != CONDITION:
        raise ValueError('Unsupported conditional perk presentation contract')
    if any(type(row[key]) is not int for key in ('alphaMaximum44', 'alphaMaximum32')) or (row['alphaMaximum44'], row['alphaMaximum32']) != (254, 253):
        raise ValueError('Selected44alpha254/32alpha253 must remain unchanged')
    for key in ('native44Sha256', 'native32Sha256', 'selectionEvidenceSha256'):
        if not re.fullmatch('[0-9a-f]{64}', row[key]):
            raise ValueError('Invalid conditional presentation hash')
    for key in ('origin', 'selection', 'approval'):
        if not isinstance(row[key], str) or not row[key].strip():
            raise ValueError('Missing conditional presentation provenance')
    stem = row['icon44']
    expected = {'SPRITES/' + stem + extension for extension in
                ('.json', '.png', '_32.png', '_normal.png', '_pressed.png', '_disabled.png', '_highlighted.png')}
    if len(row['resources']) != 7 or set(row['resources']) != expected:
        raise ValueError('Conditional presentation requires exact seven resources')
    if source_bound_icon(header, row['id'], row['capturedName']) != stem:
        raise ValueError('Selected condition does not match header')
    for wrong in ('Countermage', '', 'arcane ballistics', 'Translated Arcane Ballistics'):
        if source_bound_icon(header, row['id'], wrong) != row['historicalIcon44']:
            raise ValueError('Historical or wrong-label fallback changed')
