/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleActionsController.h"
#include "BattleInterface.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/PossiblePlayerBattleAction.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/ISpellMechanics.h"
#include "../../lib/spells/RecipientSpellEffectPreview.h"
#include "../../lib/spells/NewHorizonsSorcerySpells.h"
#include "../../lib/spells/NewHorizonsMetamagicOverflow.h"
#include "../../lib/texts/MetaString.h"
#include "../windows/SpellbookEntryPresentation.h"
#include "../GameInstance.h"

namespace
{
/// Present the shared, read-only spill estimate without extending selection.
std::string overflowStatus(const CBattleInfoCallback & battle,
	const spells::Mechanics & mechanics, const spells::Target & target)
{
	const auto preview = newHorizonsMetamagic::previewOverflow(mechanics, target);
	if(!preview || preview->damageIfLands <= 0)
		return {};
	const auto * recipient = battle.battleGetStackByID(preview->recipient, false);
	if(!recipient)
		return {};
	const bool conditional = preview->primaryResistancePercent > 0
		|| preview->secondaryResistancePercent > 0 || preview->conditionalRebirth;
	const auto damage = (conditional ? "0 - " : std::string())
		+ std::to_string(preview->damageIfLands);
	auto text = MetaString::createFromTextID("new-horizons.metamagic.arcaneOverflow.preview");
	text.replaceRawString(recipient->getName());
	text.replaceRawString(damage);
	return text.toString(&GAME->translator());
}

/// Preview the established native effect without repeating scripted formulas.
std::string recipientEffectStatus(const spells::Mechanics & mechanics,
	const battle::Unit * recipient)
{
	const auto preview = newHorizonsMagic::recipientSpellEffectPreview(mechanics, recipient);
	if(!preview)
		return {};
	const auto effect = spellbookPresentation::effectSummary(preview->effect, GAME->translator());
	std::string result = effect.empty() ? "" : " | " + effect;
	if(preview->durationRounds)
		result += " | " + std::to_string(*preview->durationRounds) + " rounds";
	return result;
}
}

std::string BattleActionsController::spellTargetManaStatus(PossiblePlayerBattleAction action,
	const BattleHex & targetHex)
{
	const auto * hero = owner.currentHero();
	const auto battle = owner.getBattle();
	if(!heroSpellToCast || !hero || !battle)
		return {};
	switch(action.get())
	{
		case PossiblePlayerBattleAction::SACRIFICE:
		case PossiblePlayerBattleAction::TELEPORT:
			if(!selectedStack) // The first click has not specified the full cast.
				return {};
			break;
		case PossiblePlayerBattleAction::AIMED_SPELL_CREATURE:
		case PossiblePlayerBattleAction::ANY_LOCATION:
			break;
		default:
			return {};
	}
	const auto * spell = heroSpellToCast->spell.toSpell();
	if(!spell)
		return {};
	// Mirror the ordinary submission path on a copy. Staged targets and cast
	// options survive; neither the pending action nor gameplay state is changed.
	BattleAction preview = *heroSpellToCast;
	if(action.get() == PossiblePlayerBattleAction::SACRIFICE)
		preview.aimToUnit(getStackForHex(targetHex));
	else
		preview.aimToHex(targetHex);
	const auto target = preview.getTarget(battle.get());
	spells::BattleCast cast(battle.get(), hero, spells::Mode::HERO, spell);
	cast.setOvercharge(preview.spellOvercharge);
	cast.setSelectiveDispel(preview.spellSelectiveDispel);
	cast.setCureAffliction(preview.spellCureAffliction);
	cast.setCurePhysicalAffliction(preview.spellCurePhysicalAffliction);
	cast.setPurifyChoices(preview.spellPurifyChoices);
	cast.setMassSlow(preview.spellMassSlow);
	cast.setShadowGiftSacrificePercent(preview.spellShadowGiftSacrificePercent);
	const auto mechanics = cast.mechanicsForTarget(target);
	if(!mechanics)
		return {};
	mechanics->setStormOfDaggersTargetCount(static_cast<int32_t>(preview.target.size()));
	return " | Mana: " + std::to_string(mechanics->getTargetAwareManaCost(target))
		+ recipientEffectStatus(*mechanics, getStackForHex(targetHex))
		+ overflowStatus(*battle, *mechanics, target);
}

bool BattleActionsController::isForcedRecallSpell(const CSpell * spell)
{
	return spell && spell->getJsonKey() == newHorizonsSorcerySpells::FORCED_RECALL;
}

BattleHexArray BattleActionsController::getForcedRecallTargetHexes(const CSpell * spell, const BattleHex & targetHex)
{
	const auto battle = owner.getBattle();
	if(!isForcedRecallSpell(spell) || !battle || !heroSpellcastingModeActive()
		|| !targetHex.isValid() || !isCastingPossibleHere(spell, nullptr, targetHex))
		return {};
	const auto * target = getStackForHex(targetHex);
	if(!target)
		return {};
	const auto destination = newHorizonsSorcerySpells::forcedRecallDestination(*battle, *target);
	return destination ? target->getHexes(*destination) : BattleHexArray{};
}
