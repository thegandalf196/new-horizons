#!/usr/bin/env python3
"""Source guard for the event-driven Chain Lightning battlefield preview.

The guard checks client wiring and shared-mechanics reuse. It is not a render
test; the focused native spell tests are the separate mechanics/prediction
control, and a live display remains necessary to review appearance.
"""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ACTIONS_H = (ROOT / "client/battle/BattleActionsController.h").read_text(encoding="utf-8")
ACTIONS_CPP = (ROOT / "client/battle/BattleActionsController.cpp").read_text(encoding="utf-8")
FIELD_H = (ROOT / "client/battle/BattleFieldController.h").read_text(encoding="utf-8")
FIELD_CPP = (ROOT / "client/battle/BattleFieldController.cpp").read_text(encoding="utf-8")
COMBAT_TEXTS = (ROOT / "config/newHorizonsCombatTexts.json").read_text(encoding="utf-8")

NATIVE_CONTROL_FILTER = (
    "NewHorizonsChainLightningFalloffTest.*:"
    "NewHorizonsDirectDamageMechanicsTest.ConductorUsesAuthoredPerJumpMultipliersInPredictionAndCast:"
    "NewHorizonsDirectDamageMechanicsTest.ConductorNeverReplacesBetterLevelScaledMasterChainRetention"
)


def function_body(source: str, signature: str) -> str:
    """Return one C++ function body, skipping braces inside comments/strings."""
    signature_start = source.index(signature)
    opening = source.index("{", signature_start)
    depth = 0
    index = opening
    while index < len(source):
        current = source[index]
        following = source[index:index + 2]
        if following == "//":
            newline = source.find("\n", index + 2)
            index = len(source) if newline < 0 else newline + 1
            continue
        if following == "/*":
            end = source.find("*/", index + 2)
            if end < 0:
                raise AssertionError(f"unterminated comment after {signature}")
            index = end + 2
            continue
        if current in ('"', "'"):
            quote = current
            index += 1
            while index < len(source):
                if source[index] == "\\":
                    index += 2
                    continue
                if source[index] == quote:
                    index += 1
                    break
                index += 1
            continue
        if current == "{":
            depth += 1
        elif current == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
        index += 1
    raise AssertionError(f"unterminated function body: {signature}")


def require(source: str, token: str, context: str) -> None:
    if token not in source:
        raise AssertionError(f"missing {context}: {token}")


def main() -> None:
    for field in (
        "int32_t hopNumber",
        "uint32_t unitId",
        "BattleHex position",
        "BattleHex occupiedHex",
        "int64_t cumulativeDamage",
        "int64_t projectedDamage",
        "int64_t estimatedKills",
    ):
        require(ACTIONS_H, field, f"ordered recipient preview field {field}")
    require(ACTIONS_H, "struct ChainLightningPreview", "cached preview value")
    require(ACTIONS_H, "bool assumesNoResistance", "resistance caveat state")
    require(ACTIONS_H, "const ChainLightningPreview & getChainLightningPreview() const", "read-only preview getter")
    require(FIELD_H, "void showChainLightningPreviewNumbers(Canvas &", "native-hex number renderer")

    getter = function_body(ACTIONS_CPP, "BattleActionsController::getChainLightningPreview() const")
    require(getter, "return", "cached preview getter")
    for forbidden in ("updateChainLightningPreview", "BattleCast", "getHealthChange", "CRandomGenerator"):
        if forbidden in getter:
            raise AssertionError(f"preview getter must only return cached data, found {forbidden}")

    preview = function_body(ACTIONS_CPP, "BattleActionsController::updateChainLightningPreview(")
    for token in (
        "PossiblePlayerBattleAction::AIMED_SPELL_CREATURE",
        "isChainLightningPreviewSpell(spell)",
        "newHorizonsMagic::rulesActive(savedRules)",
        "SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION",
        "newHorizonsMagic::spellAllowedBySavedRoster(savedRules, action.spell())",
        "chainLightningPreviewCacheKey",
        "if(chainLightningPreview.active && chainLightningPreviewCacheKey",
        "*chainLightningPreviewCacheKey == cacheKey",
        "chainLightningPreviewCacheKey = cacheKey",
        "BattleCast",
        "mechanics->canonicalizeTarget(aim)",
        "transformTarget",
        'effect.name != "directDamage"',
        "effect.indirect",
        "getHealthChange",
        "prefix.push_back",
        "previousCumulativeDamage",
        "projectedDamage",
        "cumulativeDamage",
        "estimatedKills",
        "assumesNoResistance",
        "consoleText",
    ):
        require(preview, token, f"shared ordered-preview calculation {token}")
    if not (preview.index("transformTarget") < preview.index("prefix.push_back") < preview.index("getHealthChange")):
        raise AssertionError("Chain Lightning must derive each hop from cumulative ordered target prefixes")
    if "std::sort" in preview or "std::ranges::sort" in preview:
        raise AssertionError("preview must preserve the spell mechanics' ordered transformed target sequence")
    for token in (
        'preview.consoleText += "\\n";',
        "std::to_string(recipient.hopNumber)",
        "std::to_string(recipient.projectedDamage)",
        "std::to_string(recipient.estimatedKills)",
    ):
        require(preview, token, f"compact per-hop status text {token}")
    for forbidden in (
        "CRandomGenerator",
        "getDefault()",
        "castEval(",
        "battleMakeSpellAction",
        "makePlayerBattleAction",
        "sendAndApply(",
        "giveCommand(",
    ):
        if forbidden in preview:
            raise AssertionError(f"hover preview must not roll RNG or submit/mutate an action: {forbidden}")

    invalidation = function_body(ACTIONS_CPP, "BattleActionsController::invalidateChainLightningPreview()")
    require(invalidation, "chainLightningPreview = {};", "cleared cached preview")
    require(invalidation, "chainLightningPreviewCacheKey.reset();", "cleared preview cache key")

    require(ACTIONS_CPP, '"core:chainLightning"', "ordinary Chain Lightning identity gate")
    require(ACTIONS_CPP, '"new-horizons:masterChainLightning"', "Master Chain identity gate")
    require(ACTIONS_CPP, "SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION", "saved-v3 applicability gate")
    require(ACTIONS_CPP, '"new-horizons.combat.chainLightning.previewHeader"', "translated preview header")
    require(COMBAT_TEXTS, '"new-horizons.combat.chainLightning.previewHeader": "%SPELL: damage/kills (no resistance assumed)"',
        "honest no-resistance caveat")

    for method in (
        "BattleActionsController::castThisSpell(",
        "BattleActionsController::endCastingSpell()",
        "BattleActionsController::activateStack()",
        "BattleActionsController::setPriorityActions(",
        "BattleActionsController::resetCurrentStackPossibleActions()",
        "BattleActionsController::onHoverEnded()",
    ):
        require(function_body(ACTIONS_CPP, method), "invalidateChainLightningPreview()", f"preview invalidation in {method}")

    hover = function_body(ACTIONS_CPP, "BattleActionsController::onHexHovered(")
    require(hover, "updateChainLightningPreview", "event-driven legal-hover refresh")
    require(hover, "invalidateChainLightningPreview", "blocked/unsupported-hover clear")
    for selector in (
        "repeatedPlacementModeActive()",
        "stormOfDaggersTargetSelectionModeActive()",
        "soulChainTargetSelectionModeActive()",
        "lifeDrainTargetSelectionModeActive()",
        "fireWallPlacementModeActive()",
        "vengefulVinesTargetSelectionModeActive()",
        "heroOrderTargetingModeActive()",
    ):
        require(hover, selector, f"non-chain selector invalidation path {selector}")
    blocked_status = function_body(ACTIONS_CPP, "BattleActionsController::actionGetStatusMessageBlocked(")
    require(blocked_status, "invalidateChainLightningPreview()", "blocked-target stale-preview clear")

    legal_hexes = function_body(FIELD_CPP, "BattleFieldController::getHighlightedHexesForSpellRange()")
    require(legal_hexes, "getChainLightningPreview()", "preview-backed ordered target highlights")
    require(legal_hexes, "recipient.position", "native hex highlight positions")
    require(legal_hexes, "recipient.occupiedHex", "double-wide native hex coverage")
    require(legal_hexes, "result.insert(recipient.position)", "recipient-ordered hex highlights")

    number_renderer = function_body(FIELD_CPP, "BattleFieldController::showChainLightningPreviewNumbers(")
    require(number_renderer, "getChainLightningPreview()", "post-render number source")
    require(number_renderer, "recipient.hopNumber", "numbered hop labels")
    require(number_renderer, "recipient.position", "labels anchored to native hex positions")
    for forbidden in ("updateChainLightningPreview", "transformTarget", "getHealthChange", "CRandomGenerator"):
        if forbidden in number_renderer:
            raise AssertionError(f"native-hex labels must consume cached data only, found {forbidden}")
    render = function_body(FIELD_CPP, "BattleFieldController::renderBattlefield(Canvas &")
    require(render, "renderer.execute", "post-battle-renderer hop-label path")
    require(render, "showChainLightningPreviewNumbers(clippedCanvas)", "post-render hop-label draw")
    if render.index("renderer.execute") > render.index("showChainLightningPreviewNumbers"):
        raise AssertionError("number labels must be drawn after the battlefield renderer executes")

    status = function_body(ACTIONS_CPP, "BattleActionsController::actionGetStatusMessage(")
    require(status, "getChainLightningPreview()", "compact hover console readback")
    require(status, "consoleText", "per-hop damage/kill and conditional resistance text")

    print("PASS: saved-v3 Chain Lightning preview has ordered mechanics wiring, event invalidation, and number/status presentation hooks")
    print(f"Mechanics/prediction controls: {NATIVE_CONTROL_FILTER}")
    print("Static wiring only; this does not prove rendered appearance or a live battle.")


if __name__ == "__main__":
    main()
