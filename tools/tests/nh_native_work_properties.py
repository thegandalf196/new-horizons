# SPDX-License-Identifier: GPL-2.0-or-later
"""Pure classification of native fixture properties; unknown names stay semantic.

Work counts may differ between equivalent implementations. Diagnostic controls
are excluded only after support, finite completion, profile and overflow checks.
"""
SIDES = ('side0', 'side1')
LEGACY_WORK = {side + suffix for side in SIDES for suffix in
               ('MovementExchanges', 'NormalExchanges', 'WaitedExchanges', 'TargetProbes')}
LEGACY_WORK |= {prefix + suffix for prefix in ('movement', 'normal', 'waited')
                for suffix in ('ExchangeCount', 'TargetProbes')}
FORECAST_WORK = ('ForecastRootCaptures', 'ForecastRootAdmissions', 'ForecastSourceFallbacks',
                 'ForecastLifecycleFallbacks', 'ForecastReferenceOverrides', 'ForecastForks',
                 'ForecastCommittedTransitions', 'ForecastRefusedTransitions', 'ForecastDetachedHealthUpdates',
                 'ForecastNeighborhoodInvalidations', 'ForecastCompactFactHits', 'ForecastCompactFactMisses',
                 'ForecastDerivedRefreshes')
BUDGET_WORK = tuple('Budget' + prefix + kind for prefix in ('Spent', 'Refusals')
                    for kind in ('Generation', 'Scout', 'Continuation', 'Node', 'Movement'))
NATIVE_WORK = ('ForecastNativeReadMaterializations', 'ForecastNativeOwningLoans',
               'ForecastNativeQueryRebindings')
CONTROLS = ('DiagnosticsVersion', 'DiagnosticsSupported', 'BudgetStartedDecisions',
            'BudgetCompletedFiniteDecisions', 'BudgetUnsupportedDecisions', 'BudgetMissingRootDecisions',
            'BudgetFullDecisions', 'BudgetBeamOneDecisions', 'BudgetFastDecisions', 'BudgetLegacyDecisions',
            'BudgetProfileMask', 'BudgetSaturated', 'ForecastSaturated')
SCHEMA_FIELDS = {side + suffix for side in SIDES
                 for suffix in FORECAST_WORK + BUDGET_WORK + NATIVE_WORK + CONTROLS}


def number(props, key):
    value = props.get(key, '')
    if not value.isdecimal():
        raise ValueError('Missing or invalid native work/control property: ' + key)
    return int(value)


def validate_side(props, side, policy):
    for suffix in FORECAST_WORK + BUDGET_WORK + CONTROLS:
        number(props, side + suffix)
    if any(props[side + suffix] != '1' for suffix in ('DiagnosticsVersion', 'DiagnosticsSupported')):
        raise ValueError('Unsupported native diagnostic version/support')
    if any(number(props, side + suffix) != 0 for suffix in
           ('BudgetUnsupportedDecisions', 'BudgetMissingRootDecisions', 'BudgetSaturated', 'ForecastSaturated')):
        raise ValueError('Missing, unsupported or saturated native diagnostic evidence')
    started = number(props, side + 'BudgetStartedDecisions')
    if started != number(props, side + 'BudgetCompletedFiniteDecisions'):
        raise ValueError('Incomplete finite native decisions')
    expected_mask = (4 if policy == 'Fast' else 1) if started else 0
    if number(props, side + 'BudgetProfileMask') != expected_mask:
        raise ValueError('Native diagnostic profile does not match selected policy')
    for profile in ('Full', 'BeamOne', 'Fast', 'Legacy'):
        if number(props, side + 'Budget' + profile + 'Decisions') != (started if profile == policy else 0):
            raise ValueError('Native diagnostic profile decision count mismatch')
    return started


def property_classes(props, policy):
    """Return exact observed work names and separately validated control values.

    Older fixtures without Diagnostic1 remain supported. Its original schema may
    omit the later three native-work metrics; partial extension emission rejects.
    """
    if policy not in ('Full', 'Fast'):
        raise ValueError('Unknown selected native policy')
    work = LEGACY_WORK.intersection(props)
    for key in work:
        number(props, key)
    if not SCHEMA_FIELDS.intersection(props):
        return work, {}
    expected_policy = 'Fast/BeamOneTwoActors' if policy == 'Fast' else 'Full'
    if 'policy' in props and props['policy'] != expected_policy:
        raise ValueError('Native diagnostic policy label does not match selected policy')
    if sum(validate_side(props, side, policy) for side in SIDES) <= 0:
        raise ValueError('No completed finite native decision was observed')
    controls = {side + suffix: props[side + suffix] for side in SIDES for suffix in CONTROLS}
    work |= {side + suffix for side in SIDES for suffix in FORECAST_WORK + BUDGET_WORK}
    native_fields = {side + suffix for side in SIDES for suffix in NATIVE_WORK}
    if native_fields.intersection(props):
        for key in native_fields:
            number(props, key)
        work |= native_fields
    return work, controls
