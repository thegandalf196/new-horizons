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


def validate_perk_presentation(row, header, captured_header=None):
    if row.get('schemaVersion') == 2:
        from .nhart_sylvan_perk_presentations import validate_sylvan_presentation
        validate_sylvan_presentation(row, header, captured_header)
        return
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


def validate_perk_payload(row, read_resource):
    """Common selected conditional-perk PNG, frame, hash and alpha-state checks."""
    import hashlib
    from io import BytesIO
    from PIL import Image, ImageEnhance

    stem = 'SPRITES/' + row['icon44']
    states = ('normal', 'pressed', 'disabled', 'highlighted')
    expected = {'images': [{'group': 0, 'frame': index,
                           'file': row['icon44'] + '_' + state + '.png'}
                          for index, state in enumerate(states)]}
    if json.loads(read_resource(stem + '.json')) != expected:
        raise ValueError('Conditional perk frame descriptor changed')
    images = {}
    for name in row['resources']:
        if name.endswith('.json'):
            continue
        image = Image.open(BytesIO(read_resource(name)))
        image.load()
        size = 32 if name.endswith('_32.png') else 44
        if image.mode != 'RGBA' or image.info or image.size != (size, size):
            raise ValueError('Conditional perk requires clean native RGBA')
        alpha = image.getchannel('A')
        if alpha.getextrema() != (0, row['alphaMaximum' + str(size)]):
            raise ValueError('Selected conditional perk alpha changed')
        if row.get('schemaVersion') == 2:
            bounds = alpha.getbbox()
            if not bounds or min(bounds[0], bounds[1], size - bounds[2], size - bounds[3]) < 2:
                raise ValueError('Sylvan foreground requires clear two-pixel perimeter')
        images[name] = image
    for size in (44, 32):
        name = stem + ('_32' if size == 32 else '') + '.png'
        if hashlib.sha256(read_resource(name)).hexdigest() != row['native' + str(size) + 'Sha256']:
            raise ValueError('Selected conditional perk native hash changed')
    normal = images[stem + '.png']
    expected_states = (normal, ImageEnhance.Brightness(normal).enhance(0.82),
                       ImageEnhance.Brightness(ImageEnhance.Color(normal).enhance(0.15)).enhance(0.65),
                       ImageEnhance.Brightness(normal).enhance(1.13))
    for state, expected in zip(states, expected_states):
        actual = images[stem + '_' + state + '.png']
        if actual.tobytes() != expected.tobytes() or actual.getchannel('A').tobytes() != normal.getchannel('A').tobytes():
            raise ValueError('Conditional perk state recipe or alpha changed')
