"""Exact released Authority aliases and additive-art bridges; no Git or private inputs.

The two Wisp records certify approved replacements, not reconstructed old pixels.
All other historical payloads retain their ordinary byte-for-byte checks.
"""
import copy
import hashlib

from .nhart_test_resources import entries_before_wave
from .nhart_force_lance import force_lance_wave
from .nhart_skill_rank_art import skill_rank_wave
from .nhart_release_presentation import presentation_wave
from .nhart_living_armor_backdrop import backdrop_wave
from .nhart_creature_ability_art import ability_wave
from .nhart_interface_status_art import interface_status_wave
from .nhart_status_icon_repaint import repaint_wave
from .nhart_flat_interface_repaint import flat_repaint_wave
from .nhart_retired_interface_icons import retirement_wave
from .nhart_retired_custom_buttons import custom_button_retirement_wave
from .nhart_academy_town_animation import town_animation_wave
from .nhart_academy_town_masks import town_mask_wave
from .nhart_sylvan_ambush_art import sylvan_ambush_wave
from .nhart_spell_school_badges import school_badge_wave

AUTHORITY_CURRENT = (
    ('massedRanks', 'offensive_drills'), ('veteranCadre', 'defensive_drills'),
    ('familiarStandards', 'conditioning'), ('foreignLegion', 'battle_readiness'),
    ('officerCorps', 'officer_corps'), ('missileCommand', 'maneuver_training'),
    ('regimentalOrganization', 'counterattack_training'), ('combinedArms', 'protective_training'),
    ('grandMarshal', 'grand_marshal'), ('masterOfLegions', 'veteran_corps'),
    ('offensiveDrills', 'offensive_drills'), ('defensiveDrills', 'defensive_drills'),
    ('conditioning', 'conditioning'), ('battleReadiness', 'battle_readiness'),
    ('maneuverTraining', 'maneuver_training'), ('counterattackTraining', 'counterattack_training'),
    ('protectiveTraining', 'protective_training'), ('veteranCorps', 'veteran_corps'),
)
BRIDGE_PREVIOUS_ARCHIVE = '8ca8449c7d45c0ee60621ca6e2756bbd966c68370a227be950430e5f0e3e2df1'
BRIDGE_NEXT_ARCHIVE = '37028037a0086b1f6f1937af9c2f21a08e6513ab47c83293af6e0386c1549171'
BRIDGE_NEXT_ENTRIES = '17a55971ad4715c29b240f9603f00be7a56cdda2632117efb6e9dc5d240ec030'
BRIDGE_PREVIOUS_ENTRIES = '2c4651072efa1bf2265c98e4e2c0119a7f5c9686aef33584147465df5c9f8a97'
BRIDGE_GROUPS = (
    ({'mummy-runtime': 102, 'royal-mummy-runtime': 39, 'seth-portrait-runtime': 2},
     'b63e1d62a35be23ec55a062d64bf2054e1bb7a917debdf100bc1bc863f7a0b22'),
    ({'conflux-hero-portraits': 10, 'conflux-water-ice-runtime': 245},
     'ab47c2d4e4d8f086741cb78083dd530d0d60f27b9d9aa9c6505ee9f8ad5a895a'),
)
WISP_REPLACEMENTS = {
    'SPRITES/Wisp/icons/wisp-icon-58x64.png': (
        '52f21093935c3b0a3a08cbbf9e3874c5a2038542aa8329f8cb50b0e38a746c21', 8557,
        '9b44a210a069b5057908cf6fdd7e5251220091c72e7ab94f3a22ed7c1a6fd5a8', 9416),
    'SPRITES/WispUpgrade/icons/icon-58x64.png': (
        '49caa6d21421b75f2b5ea7461ad710a0f7ec8167ada1fe0fe3786238a0d60d40', 8709,
        'cfab9d491c5eadc5fa6b532a679944b17c0f98a19bf58e2338eed4f2d793c15b', 9604),
}
OLD_WISP_APPROVAL = 'user-selected-runtime-art-bundle-2026-10-08; not blanket-original-archive-authorization'
NEW_WISP_APPROVAL = 'conflux-twcrport-scenery-fix-2026-11-13; provisional visual fix'


def authority_before_leadership_rework(header, original_block):
    """Reverse only the released eighteen-row block, preserving unrelated bytes."""
    current = ''.join('\t\t{"new-horizons:authority.' + identity + '", "NH_perk_authority_' + stem + '"},\n'
                      for identity, stem in AUTHORITY_CURRENT)
    if header.count(original_block) == 1:
        revised = [identity for identity, _ in AUTHORITY_CURRENT[:10]
                   if identity not in ('officerCorps', 'grandMarshal')]
        if any('new-horizons:authority.' + identity + '"' in header for identity in revised):
            raise ValueError('Mixed historical and revised Authority blocks')
        return header
    if header.count(current) != 1 or any(header.count(line) != 1 for line in current.splitlines(keepends=True)):
        raise ValueError('Unrecognized released Authority alias block')
    return header.replace(current, original_block, 1)


def _reverse_bridge_records(entries):
    families = {family for group, _ in BRIDGE_GROUPS for family in group}
    result = [copy.deepcopy(row) for row in entries if row['family'] not in families]
    for row in result:
        if row['resource'] not in WISP_REPLACEMENTS:
            continue
        old_hash, old_size, new_hash, new_size = WISP_REPLACEMENTS[row['resource']]
        if (row['sha256'], row['size'], row['approval']) != (new_hash, new_size, NEW_WISP_APPROVAL):
            raise ValueError('Released Wisp replacement record changed')
        row.update(sha256=old_hash, size=old_size, approval=OLD_WISP_APPROVAL)
    return result


def released_predecessor_entries(entries, waves, digest):
    """Verify exact 143/255 additions and two replacements before reversing them."""
    # The new finite cohort is removed by ordinary wave reversal, never folded
    # into the released bridge's existing fingerprints or approval records.
    force_lance_wave(waves)
    skill_rank_wave(waves)
    presentation_wave(waves)
    backdrop_wave(waves)
    ability_wave(waves)
    interface_status_wave(waves)
    repaint_wave(waves)
    flat_repaint_wave(waves)
    retirement_wave(waves)
    custom_button_retirement_wave(waves)
    town_animation_wave(waves)
    town_mask_wave(waves)
    sylvan_ambush_wave(waves)
    school_badge_wave(waves)
    matches = [i for i, wave in enumerate(waves) if wave['baselineArchiveSha256'] == BRIDGE_NEXT_ARCHIVE]
    if len(matches) != 1 or matches[0] == 0:
        raise ValueError('Missing exact released-art bridge boundary')
    index = matches[0]
    if (waves[index - 1]['newArchiveSha256'], waves[index - 1]['entryCount'],
            waves[index]['baselineCount']) != (BRIDGE_PREVIOUS_ARCHIVE, 5810, 6208):
        raise ValueError('Released-art bridge archive/count predecessor changed')
    boundary = entries_before_wave(entries, waves, index)
    if len(boundary) != 6208 or digest(boundary) != BRIDGE_NEXT_ENTRIES:
        raise ValueError('Released-art bridge next manifest changed')
    for families, expected_hash in BRIDGE_GROUPS:
        selected = [row for row in boundary if row['family'] in families]
        counts = {family: sum(row['family'] == family for row in selected) for family in families}
        if counts != families or digest(selected) != expected_hash:
            raise ValueError('Released-art bridge additions/provenance changed')
    previous = _reverse_bridge_records(boundary)
    if len(previous) != 5810 or digest(previous) != BRIDGE_PREVIOUS_ENTRIES:
        raise ValueError('Released-art bridge original manifest changed')
    return _reverse_bridge_records(entries), index


def baseline_payload_proof(row, payload):
    """Return an explicit proof kind; never fabricate an unavailable old payload."""
    if payload is None:
        from .nhart_retired_interface_icons import retired_row_proof
        if retired_row_proof(row):
            return 'verified-retired-entry'
        raise ValueError('Historical baseline payload missing')
    actual_hash = hashlib.sha256(payload).hexdigest()
    replacement = WISP_REPLACEMENTS.get(row['resource'])
    if replacement and (row['sha256'], row['size'], row['approval']) == (replacement[0], replacement[1], OLD_WISP_APPROVAL):
        if (actual_hash, len(payload)) != (replacement[2], replacement[3]):
            raise ValueError('Released Wisp replacement payload changed')
        return 'verified-released-replacement'
    if (actual_hash, len(payload)) != (row['sha256'], row['size']):
        from .nhart_water_ice_restoration import portrait_replacement_proof
        if portrait_replacement_proof(row, payload):
            return 'verified-released-replacement'
        from .nhart_status_icon_repaint import repaint_replacement_proof
        if repaint_replacement_proof(row, payload):
            return 'verified-selected-replacement'
        from .nhart_flat_interface_repaint import flat_replacement_proof
        if flat_replacement_proof(row, payload):
            return 'verified-selected-replacement'
        from .nhart_academy_town_animation import town_animation_replacement_proof
        if town_animation_replacement_proof(row, payload):
            return 'verified-selected-replacement'
        from .nhart_academy_town_masks import town_mask_replacement_proof
        if town_mask_replacement_proof(row, payload):
            return 'verified-selected-replacement'
        raise ValueError('Historical baseline payload changed')
    return 'verified-matching-payload'


def initial_selection_history(entries, waves, digest):
    """Project initial selections through the exact released bridge and waves."""
    from .nhart_water_ice_restoration import restoration_wave
    legacy, _ = released_predecessor_entries(entries, waves, digest)
    restoration = restoration_wave(waves)
    history = [wave for wave in waves if wave is not restoration]
    return entries_before_wave(legacy, history, 0), legacy, {
        name for wave in history for name in wave['resources']}
