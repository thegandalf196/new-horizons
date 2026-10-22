#!/usr/bin/env python3
"""Source guard for Blink's shared-geometry hover preview."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CONTROLLER = (ROOT / "client/battle/BattleActionsController.cpp").read_text(encoding="utf-8")
CONTROLLER_H = (ROOT / "client/battle/BattleActionsController.h").read_text(encoding="utf-8")
FIELD = (ROOT / "client/battle/BattleFieldController.cpp").read_text(encoding="utf-8")


def between(source: str, start: str, end: str) -> str:
    left = source.index(start)
    right = source.index(end, left)
    return source[left:right]


def main() -> None:
    assert '#include "lib/spells/NewHorizonsBlink.h"' in CONTROLLER
    assert "std::optional<newHorizonsBlink::Preview> getBlinkDestinationPreview" in CONTROLLER_H
    assert "spell->getJsonKey() == newHorizonsBlink::SPELL_ID" in CONTROLLER

    preview = between(
        CONTROLLER,
        "std::optional<newHorizonsBlink::Preview> BattleActionsController::getBlinkDestinationPreview",
        "bool BattleActionsController::isValidTransfigureMatterTarget",
    )
    for token in (
        "heroSpellcastingModeActive()",
        "battleGetStackByPos(targetHex, true)",
        "isCastingPossibleHere(spell, nullptr, targetHex)",
        "cast.setMetamagicFollowup(heroSpellToCast->metamagicFollowup)",
        "spell->battleMechanics(&cast)",
        "newHorizonsBlink::preview(*mechanics, target)",
    ):
        assert token in preview, token
    for forbidden in (
        "CRandomGenerator",
        "battleMakeSpellAction",
        "giveCommand(",
        "aimToHex(",
        "aimToUnit(",
    ):
        assert forbidden not in preview, forbidden

    field_preview = between(
        FIELD,
        "BattleHexArray BattleFieldController::getHighlightedHexesForSpellRange",
        "BattleHexArray BattleFieldController::getHighlightedHexesForMovementTarget",
    )
    assert "BattleActionsController::isBlinkSpell(spell)" in field_preview
    assert "getBlinkDestinationPreview(spell, hoveredHex)" in field_preview
    assert "preview->legalDestinations" in field_preview
    assert field_preview.index("isBlinkSpell(spell)") < field_preview.index("rangeInHexes(hoveredHex)")
    assert "CRandomGenerator" not in field_preview

    status = between(
        CONTROLLER,
        "std::string BattleActionsController::actionGetStatusMessage(",
        "std::string BattleActionsController::actionGetStatusMessageBlocked(",
    )
    for token in (
        "isBlinkSpell(spell)",
        "preview->radius",
        "preview->legalDestinations.size()",
        "preview->blinkmaster",
        "actual landing remains random",
        "farther of two random legal destinations",
    ):
        assert token in status, token

    # Blink remains an ordinary single-stack spell cast. There is no Blink-only
    # cast selector or click path that lets the player choose a landing hex.
    cast = between(
        CONTROLLER,
        "void BattleActionsController::castThisSpell",
        "bool BattleActionsController::continueOrdinarySpellcast",
    )
    click = between(
        CONTROLLER,
        "void BattleActionsController::onHexLeftClicked",
        "void BattleActionsController::tryActivateStackSpellcasting",
    )
    realization = between(
        CONTROLLER,
        "void BattleActionsController::actionRealize",
        "PossiblePlayerBattleAction BattleActionsController::selectAction",
    )
    assert "getCasterAction(spellID.toSpell(), castingHero, spells::Mode::HERO)" in cast
    assert "isBlinkSpell" not in cast
    assert "isBlinkSpell" not in click and "getBlinkDestinationPreview" not in click
    assert "auto action = selectAction(clickedHex)" in click
    assert "case PossiblePlayerBattleAction::AIMED_SPELL_CREATURE:" in realization
    assert "heroSpellToCast->aimToHex(targetHex)" in realization
    assert "if(submitHeroSpellAction(*heroSpellToCast))" in realization
    assert realization.index("heroSpellToCast->aimToHex(targetHex)") < realization.index(
        "if(submitHeroSpellAction(*heroSpellToCast))"
    )
    assert "battleMakeSpellAction(" not in realization
    submit = between(
        CONTROLLER,
        "bool BattleActionsController::submitHeroSpellAction(",
        "namespace\n{\nconst char * heroOrderTargetName",
    )
    immediate = between(submit, "const auto preview = buildFriendlyFirePreview", "const auto snapshot = preview->snapshot")
    assert "if(!preview || (preview->recipients.empty() && preview->pandemoniumText.empty()))" in immediate
    assert "battleMakeSpellAction(owner.getBattleID(), action)" in immediate
    assert "return true;" in immediate
    assert "isBlinkSpell" not in submit  # Shared ordinary routing, no Blink-specific selector.
    assert "ConfirmationGate" in submit and "weakLifetime.lock()" in submit
    assert "gate->confirm(current->snapshot)" in submit
    assert submit.count("battleMakeSpellAction(owner.getBattleID(), action)") == 2
    assert "createAndPushWindow" not in preview
    assert "updateBattleTargetSelectionControls" not in preview

    print("PASS: Blink preview uses shared legal geometry, shows random radius/count/Blinkmaster hint, and retains the ordinary stack-target click path")
    print("Source wiring only; rendered appearance and a live battle remain unverified")


if __name__ == "__main__":
    main()
