"""Finite schema-3 contract for captured Sylvan creature-alliance artwork."""
import hashlib
import re

from .nhart_sylvan_perk_presentations import source_bound_sylvan_icon, SYLVAN_APPROVAL

ALLIANCE_ADAPTER_SHA256 = 'ffbbbad39d6138a0c12dde4cbcf1320c60267dd74c9c652ec52fcd90e483371c'
ALLIANCE_CONDITIONS = (
    ('elvenPrecision', 'Hunting Pack', 'wild_hunt', 'elven_precision'),
    ('forestSFavor', 'Dwarven Hardiness', 'dwarven_hardiness', 'forests_favor'),
    ('serendipity', 'Disarming Shot', 'covering_fire', 'serendipity'),
    ('luckyRecovery', 'Briar Grip', 'briar_grip', 'lucky_recovery'),
    ('sharedFortune', 'Evergreen Blessings', 'evergreen_blessings', 'shared_fortune'),
    ('natureSProvidence', 'Silver Wings', 'silver_wings', 'natures_providence'),
    ('fortunateAim', 'Sacred Grove', 'dappled_cover', 'fortunate_aim'),
    ('wildChance', 'Kindred Flame', 'kindred_flame', 'wild_chance'),
    ('perfectMoment', 'Perfect Moment', 'perfect_moment', 'perfect_moment'),
    ('cascadingFortune', 'Ancient Accord', 'ancient_accord', 'cascading_fortune'),
)
ALLIANCE_CONDITIONS = tuple(('new-horizons:sylvanLuck.' + identity, name,
                            'NH_perk_sylvan_tactics_' + icon, 'NH_perk_' + historical)
                           for identity, name, icon, historical in ALLIANCE_CONDITIONS)
ALLIANCE_ORIGIN = 'new-or-reused-own-perk-foreground-family'
ALLIANCE_SELECTION = 'root-selected-provisional-sylvan-alliance-foreground-family-2026-11-14'
ALLIANCE_APPROVAL = SYLVAN_APPROVAL


def source_bound_alliance_icon(header, captured_header, alliance_header, identity, name, rules):
    """Structural source witness; this does not execute C++ or prove GUI rendering."""
    if not isinstance(alliance_header, str) or hashlib.sha256(alliance_header.encode()).hexdigest() != ALLIANCE_ADAPTER_SHA256:
        raise ValueError('Unrecognized Sylvan alliance presentation wrapper')
    if identity.startswith('new-horizons:sylvanLuck.') and isinstance(rules, dict) and 'sylvanTacticsPolicy' in rules:
        policy = rules['sylvanTacticsPolicy']
        if type(policy) is not int or policy not in (1, 2):
            raise ValueError('Invalid Sylvan alliance captured policy')
        if policy == 2:
            # The immutable old adapter still handles unchanged Perfect Moment
            # and all fallback labels. Its policy-1 predicate also accepts v2.
            source_bound_sylvan_icon(header, captured_header, identity, name,
                                    {'sylvanTacticsPolicy': 1})
            for stable, authored, icon, _ in ALLIANCE_CONDITIONS:
                if (identity, name) == (stable, authored):
                    return icon
            rules = {'sylvanTacticsPolicy': 1}
    return source_bound_sylvan_icon(header, captured_header, identity, name, rules)


def validate_alliance_presentation(row, header, captured_header, alliance_header):
    required = {'schemaVersion', 'capturedPolicy', 'id', 'capturedName', 'icon44',
                'historicalIcon44', 'resources', 'native44Sha256', 'native32Sha256',
                'alphaMaximum44', 'alphaMaximum32', 'masterSha256',
                'exportManifestSha256', 'selectionEvidenceSha256', 'resolverSourceSha256',
                'origin', 'selection', 'approval'}
    if set(row) != required or type(row['schemaVersion']) is not int or row['schemaVersion'] != 3:
        raise ValueError('Unsupported Sylvan alliance presentation schema')
    condition = tuple(row[k] for k in ('id', 'capturedName', 'icon44', 'historicalIcon44'))
    if condition not in ALLIANCE_CONDITIONS:
        raise ValueError('Unrecognized Sylvan alliance conditional identity')
    policy = row['capturedPolicy']
    if not isinstance(policy, dict) or set(policy) != {'sylvanTacticsPolicy'} or type(policy['sylvanTacticsPolicy']) is not int or policy['sylvanTacticsPolicy'] != 2:
        raise ValueError('Sylvan alliance artwork requires captured policy integer2')
    for key in ('alphaMaximum44', 'alphaMaximum32'):
        if type(row[key]) is not int or row[key] != 255:
            raise ValueError('Selected Sylvan alliance alpha maximum must remain255')
    for key in ('native44Sha256', 'native32Sha256', 'masterSha256', 'exportManifestSha256',
                'selectionEvidenceSha256', 'resolverSourceSha256'):
        if not isinstance(row[key], str) or not re.fullmatch('[0-9a-f]{64}', row[key]):
            raise ValueError('Invalid Sylvan alliance provenance hash')
    if row['resolverSourceSha256'] != ALLIANCE_ADAPTER_SHA256:
        raise ValueError('Sylvan alliance metadata does not identify reviewed wrapper')
    if (row['origin'], row['selection'], row['approval']) != (ALLIANCE_ORIGIN, ALLIANCE_SELECTION, ALLIANCE_APPROVAL):
        raise ValueError('Selected Sylvan alliance provenance or provisional status changed')
    expected = {'SPRITES/' + row['icon44'] + suffix for suffix in
                ('.json', '.png', '_32.png', '_normal.png', '_pressed.png', '_disabled.png', '_highlighted.png')}
    if not isinstance(row['resources'], list) or len(row['resources']) != 7 or set(row['resources']) != expected:
        raise ValueError('Sylvan alliance presentation requires exact seven resources')
    actual = source_bound_alliance_icon(header, captured_header, alliance_header,
                                       row['id'], row['capturedName'], policy)
    if actual != row['icon44']:
        raise ValueError('Sylvan alliance condition does not match reviewed source')
    for wrong in ('', 'Unknown revised perk', row['capturedName'].lower()):
        if source_bound_alliance_icon(header, captured_header, alliance_header, row['id'], wrong, policy) != row['historicalIcon44']:
            raise ValueError('Sylvan alliance unknown-label fallback changed')
    if source_bound_alliance_icon(header, captured_header, alliance_header, row['id'], row['capturedName'], {}) != row['historicalIcon44']:
        raise ValueError('Sylvan alliance absent-policy fallback changed')


def validate_alliance_family(rows, header, captured_header, alliance_header):
    selected = [row for row in rows if row.get('schemaVersion') == 3]
    if not selected:
        return
    if len(selected) != 10 or {row['id'] for row in selected} != {row[0] for row in ALLIANCE_CONDITIONS}:
        raise ValueError('Sylvan alliance wave requires all ten distinct identities')
    for row in selected:
        validate_alliance_presentation(row, header, captured_header, alliance_header)
    resources = [name for row in selected for name in row['resources']]
    if len(resources) != 70 or len(set(resources)) != 70:
        raise ValueError('Sylvan alliance wave requires seventy distinct resources')


def validate_alliance_resource_reuse(wave, declared, entries, waves, index, reader):
    """Distinguish the six new families from four exact existing byte reuses."""
    from .nhart_test_resources import entries_before_wave
    suffixes = ('.json', '.png', '_32.png', '_normal.png', '_pressed.png', '_disabled.png', '_highlighted.png')
    all_resources = {'SPRITES/' + row[2] + suffix for row in ALLIANCE_CONDITIONS for suffix in suffixes}
    reused_stems = ('wild_hunt', 'covering_fire', 'dappled_cover', 'perfect_moment')
    reused = {'SPRITES/NH_perk_sylvan_tactics_' + stem + suffix
              for stem in reused_stems for suffix in suffixes}
    if declared != all_resources or len(wave['resources']) != 42 or set(wave['resources']) != all_resources - reused:
        raise ValueError('Sylvan alliance wave must declare exactly42 additions and28 existing reuses')
    baseline = {row['resource']: row for row in entries_before_wave(entries, waves, index)}
    for name in reused:
        if name not in baseline:
            raise ValueError('Sylvan alliance reused resource did not exist before wave')
        payload = reader(name)
        if (len(payload), hashlib.sha256(payload).hexdigest()) != (baseline[name]['size'], baseline[name]['sha256']):
            raise ValueError('Sylvan alliance reused payload differs from exact predecessor')
