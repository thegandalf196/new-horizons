"""Finite captured-policy contract for the selected ten Sylvan Tactics perks."""
import hashlib
import re
from .nhart_perk_presentations import source_bound_icon

# Reviewed C++ adapter bytes, including exact policy API and legacy delegation.
SYLVAN_ADAPTER_SHA256 = '62964719ee80add20b2cb00b40da6bbeac9339d496f7707ed6f3db8934449d3f'
SYLVAN_CONDITIONS = (
    ('new-horizons:sylvanLuck.elvenPrecision', 'Fleetfoot', 'NH_perk_sylvan_tactics_fleetfoot', 'NH_perk_elven_precision'),
    ('new-horizons:sylvanLuck.forestSFavor', 'Patient Hunter', 'NH_perk_sylvan_tactics_patient_hunter', 'NH_perk_forests_favor'),
    ('new-horizons:sylvanLuck.serendipity', 'Dappled Cover', 'NH_perk_sylvan_tactics_dappled_cover', 'NH_perk_serendipity'),
    ('new-horizons:sylvanLuck.luckyRecovery', 'Measured Advance', 'NH_perk_sylvan_tactics_measured_advance', 'NH_perk_lucky_recovery'),
    ('new-horizons:sylvanLuck.sharedFortune', 'Covering Fire', 'NH_perk_sylvan_tactics_covering_fire', 'NH_perk_shared_fortune'),
    ('new-horizons:sylvanLuck.natureSProvidence', 'Crossfire', 'NH_perk_sylvan_tactics_crossfire', 'NH_perk_natures_providence'),
    ('new-horizons:sylvanLuck.fortunateAim', 'Disruptive Strike', 'NH_perk_sylvan_tactics_disruptive_strike', 'NH_perk_fortunate_aim'),
    ('new-horizons:sylvanLuck.wildChance', 'Quick Redeployment', 'NH_perk_sylvan_tactics_quick_redeployment', 'NH_perk_wild_chance'),
    ('new-horizons:sylvanLuck.perfectMoment', 'Perfect Moment', 'NH_perk_sylvan_tactics_perfect_moment', 'NH_perk_perfect_moment'),
    ('new-horizons:sylvanLuck.cascadingFortune', 'Wild Hunt', 'NH_perk_sylvan_tactics_wild_hunt', 'NH_perk_cascading_fortune'),
)
SYLVAN_ORIGIN = 'selected-model-generated-own-perk-foreground-family'
SYLVAN_SELECTION = 'root-selected-provisional-sylvan-tactics-foreground-family-2026-10-29'
SYLVAN_APPROVAL = 'Provisional; no user Final approval; rendered acceptance pending; no blanket license claim'


def source_bound_sylvan_icon(header, captured_header, identity, name, rules):
    """Structural source witness only; native C++ tests establish behavior."""
    if not isinstance(captured_header, str) or hashlib.sha256(captured_header.encode()).hexdigest() != SYLVAN_ADAPTER_SHA256:
        raise ValueError('Unrecognized captured Sylvan perk adapter')
    for stable, _, _, historical in SYLVAN_CONDITIONS:
        if source_bound_icon(header, stable, '') != historical:
            raise ValueError('Historical Sylvan binding changed')
    if not identity.startswith('new-horizons:sylvanLuck.') or not isinstance(rules, dict) or 'sylvanTacticsPolicy' not in rules:
        return source_bound_icon(header, identity, name)
    policy = rules['sylvanTacticsPolicy']
    if type(policy) is not int or policy != 1:
        raise ValueError('Invalid captured Sylvan Tactics policy')
    for stable, authored, icon, _ in SYLVAN_CONDITIONS:
        if (identity, name) == (stable, authored):
            return icon
    return source_bound_icon(header, identity, name)


def validate_sylvan_presentation(row, header, captured_header):
    required = {'schemaVersion', 'capturedPolicy', 'id', 'capturedName', 'icon44',
                'historicalIcon44', 'resources', 'native44Sha256', 'native32Sha256',
                'alphaMaximum44', 'alphaMaximum32', 'masterSha256',
                'exportManifestSha256', 'selectionEvidenceSha256', 'resolverSourceSha256',
                'origin', 'selection', 'approval'}
    if set(row) != required or type(row['schemaVersion']) is not int or row['schemaVersion'] != 2:
        raise ValueError('Unsupported Sylvan presentation schema')
    condition = tuple(row[k] for k in ('id', 'capturedName', 'icon44', 'historicalIcon44'))
    if condition not in SYLVAN_CONDITIONS:
        raise ValueError('Unrecognized Sylvan conditional identity')
    policy = row['capturedPolicy']
    if not isinstance(policy, dict) or set(policy) != {'sylvanTacticsPolicy'} or type(policy['sylvanTacticsPolicy']) is not int or policy['sylvanTacticsPolicy'] != 1:
        raise ValueError('Sylvan artwork requires captured policy integer1')
    for key in ('alphaMaximum44', 'alphaMaximum32'):
        if type(row[key]) is not int or row[key] != 255:
            raise ValueError('Selected Sylvan alpha maximum must remain255')
    for key in ('native44Sha256', 'native32Sha256', 'masterSha256', 'exportManifestSha256',
                'selectionEvidenceSha256', 'resolverSourceSha256'):
        if not isinstance(row[key], str) or not re.fullmatch('[0-9a-f]{64}', row[key]):
            raise ValueError('Invalid Sylvan provenance hash')
    if row['resolverSourceSha256'] != SYLVAN_ADAPTER_SHA256:
        raise ValueError('Sylvan metadata does not identify reviewed adapter')
    if (row['origin'], row['selection'], row['approval']) != (SYLVAN_ORIGIN, SYLVAN_SELECTION, SYLVAN_APPROVAL):
        raise ValueError('Selected Sylvan provenance or provisional status changed')
    expected = {'SPRITES/' + row['icon44'] + suffix for suffix in
                ('.json', '.png', '_32.png', '_normal.png', '_pressed.png', '_disabled.png', '_highlighted.png')}
    if not isinstance(row['resources'], list) or len(row['resources']) != 7 or set(row['resources']) != expected:
        raise ValueError('Sylvan presentation requires exact seven resources')
    if source_bound_sylvan_icon(header, captured_header, row['id'], row['capturedName'], policy) != row['icon44']:
        raise ValueError('Sylvan selected condition does not match source')
    if source_bound_sylvan_icon(header, captured_header, row['id'], row['capturedName'], {}) != row['historicalIcon44']:
        raise ValueError('Sylvan absent-policy fallback changed')
    for wrong in ('', 'Unknown revised perk', row['capturedName'].lower()):
        if source_bound_sylvan_icon(header, captured_header, row['id'], wrong, policy) != row['historicalIcon44']:
            raise ValueError('Sylvan unknown-label fallback changed')


def validate_sylvan_family(rows, header, captured_header):
    selected = [row for row in rows if row.get('schemaVersion') == 2]
    if not selected:
        return
    if len(selected) != 10 or {row['id'] for row in selected} != {row[0] for row in SYLVAN_CONDITIONS}:
        raise ValueError('Selected Sylvan wave requires all ten distinct identities')
    for row in selected:
        validate_sylvan_presentation(row, header, captured_header)
    resources = [name for row in selected for name in row['resources']]
    if len(resources) != 70 or len(set(resources)) != 70:
        raise ValueError('Selected Sylvan wave requires seventy distinct resources')
