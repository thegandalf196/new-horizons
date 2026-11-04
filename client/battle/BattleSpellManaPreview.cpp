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
#include "../../lib/spells/NewHorizonsMetamagicMarks.h"
#include "../../lib/spells/RecipientSpellEffectPreview.h"
#include "../windows/SpellbookEntryPresentation.h"
#include "../GameInstance.h"

namespace
{
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
	if(!heroSpellToCast || !hero || !battle
		|| !newHorizonsMetamagic::usesArcaneMarks(hero->getPerkState().rules))
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
	cast.setMetamagicFollowup(preview.metamagicFollowup);
	cast.setMetamagicGrand(preview.metamagicGrand);
	cast.setMetamagicManaRefund(preview.metamagicManaRefund);
	const auto mechanics = cast.mechanicsForTarget(target);
	if(!mechanics)
		return {};
	mechanics->setStormOfDaggersTargetCount(static_cast<int32_t>(preview.target.size()));
	return " | Mana: " + std::to_string(mechanics->getTargetAwareManaCost(target))
		+ recipientEffectStatus(*mechanics, getStackForHex(targetHex));
}
