"""Current Metamagic and Consecrated Arms artwork identities and exact wave scope."""
BINDINGS = {
    'new-horizons:metamagic.conservedFormula': 'NH_perk_metamagic_conserved_formula',
    'new-horizons:metamagic.disruptiveCasting': 'NH_perk_metamagic_disruptive_casting',
    'new-horizons:metamagic.unravelingMagic': 'NH_perk_metamagic_unraveling_magic',
    'new-horizons:metamagic.reconstitutingMagic': 'NH_perk_metamagic_reconstituting_magic',
    'new-horizons:metamagic.spellSculpting': 'NH_perk_metamagic_spell_sculpting',
    'new-horizons:metamagic.widenSpells': 'NH_perk_metamagic_widen_spells',
    'new-horizons:metamagic.levitation': 'NH_perk_metamagic_levitation',
    'new-horizons:metamagic.arcaneBallistics': 'NH_perk_metamagic_arcane_ballistics',
    'new-horizons:metamagic.arcaneCascade': 'NH_perk_metamagic_arcane_cascade',
    'new-horizons:metamagic.empoweredSpells': 'NH_perk_metamagic_empowered_spells',
    'new-horizons:divineMandate.enduringPrayer': 'NH_perk_consecrated_arms_enduring_prayer',
    'new-horizons:divineMandate.joyfulDevotion': 'NH_perk_consecrated_arms_joyful_devotion',
    'new-horizons:divineMandate.fearlessAdvance': 'NH_perk_consecrated_arms_fearless_advance',
    'new-horizons:divineMandate.unshakableFaith': 'NH_perk_consecrated_arms_unshakable_faith',
    'new-horizons:divineMandate.communionOfSaints': 'NH_perk_consecrated_arms_communion_of_saints',
    'new-horizons:divineMandate.righteousSentence': 'NH_perk_consecrated_arms_righteous_sentence',
    'new-horizons:divineMandate.ferventInvocation': 'NH_perk_consecrated_arms_fervent_invocation',
    'new-horizons:divineMandate.sacredAccord': 'NH_perk_consecrated_arms_sacred_accord',
    'new-horizons:divineMandate.antiphonalPrayer': 'NH_perk_consecrated_arms_antiphonal_prayer',
    'new-horizons:divineMandate.livingLiturgy': 'NH_perk_consecrated_arms_living_liturgy',
}


# Metamagic 5 identities stay bound to their selected wave art for captured
# policy-6 games; they are retired from the current roster.
RETIRED_METAMAGIC5_IDS = frozenset(identity for identity in BINDINGS
                                   if identity.startswith('new-horizons:metamagic.'))

# Metamagic 6 provisional stand-ins: selected historical paintings reused until
# purpose-made art is approved. They add no artwork wave or selected bytes.
METAMAGIC6_STANDINS = {
    'new-horizons:metamagic.arcaneWard': 'NH_perk_spell_buffer',
    'new-horizons:metamagic.manaSiphon': 'NH_perk_focused_pairing',
    'new-horizons:metamagic.deepReserves': 'NH_perk_arcane_reservoir',
    'new-horizons:metamagic.formulaRecovery': 'NH_perk_formula_reserve',
    'new-horizons:metamagic.arcaneCycle': 'NH_perk_spell_sequencing',
    'new-horizons:metamagic.afterglow': 'NH_perk_echoed_duration',
    'new-horizons:metamagic.crescendo': 'NH_perk_perfect_sequence',
    'new-horizons:metamagic.cleansingWeave': 'NH_perk_metamagic_reconstituting_magic',
    'new-horizons:metamagic.grandWeaving': 'NH_perk_grand_metamagic',
    'new-horizons:metamagic.spellstorm': 'NH_perk_arcane_economy',
}
CURRENT_BINDINGS = {**{identity: icon for identity, icon in BINDINGS.items()
                       if identity not in RETIRED_METAMAGIC5_IDS}, **METAMAGIC6_STANDINS}

def current_faction_perk_wave(wave):
    """Recognize only the complete twenty-perk selected artwork wave."""
    records = wave['perks']
    matching = [row for row in records if row['id'] in BINDINGS]
    if not matching:
        return False
    if len(records) != 20 or {row['id'] for row in records} != set(BINDINGS):
        raise ValueError('Incomplete or mixed current faction perk artwork wave')
    if any(row['icon44'] != BINDINGS[row['id']] for row in records):
        raise ValueError('Current faction perk artwork identity changed')
    return True


def current_faction_perk_wave_from(waves):
    matches = [wave for wave in waves if current_faction_perk_wave(wave)]
    if len(matches) != 1:
        raise ValueError('Missing or duplicate current faction perk artwork wave')
    return matches[0]
