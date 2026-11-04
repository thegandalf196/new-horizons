/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroSpellVariants.h"
#include "PossibleSpellcast.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/NewHorizonsPurify.h"
#include "../../lib/spells/Problem.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/battle/NewHorizonsShadowGift.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/NewHorizonsSpellAvailability.h"
namespace heroSpellVariants
{
Prepared prepare(const CBattleInfoCallback & battle, const CGHeroInstance * hero, const CSpell * spell, BattleSide side)
{
	const int maxOvercharge = newHorizonsMagic::magicArrowMaxOvercharge(
		battle.getBattle()->getMagicRules(), spell->getId(), hero->getEffectPower(spell),
		newHorizonsMagic::magicArrowOverchargeModifiers(hero));
	const bool canUseSelectiveDispel = spell->getId() == SpellID::DISPEL
		&& hero->hasActivePerk("new-horizons:sorceryMagic", "new-horizons:sorceryMagic.selectiveDispel");
	const bool canUseTemporalField = spell->getId() == SpellID::SLOW
		&& !newHorizonsMagic::hasDistinctMassSlow(battle.getBattle()->getMagicRules())
		&& hero->hasActivePerk("new-horizons:sorceryMagic", "new-horizons:sorceryMagic.temporalField")
		&& !battle.battleWasTemporalFieldUsed(side);
	std::vector<std::pair<SpellID, std::string>> cureAfflictionChoices{{SpellID::NONE, {}}};
	const auto & magicRules = battle.getBattle()->getMagicRules();
	if(newHorizonsMagic::cureEnabled(magicRules, spell->getId()))
	{
		for(const auto * unit : battle.battleGetAllUnits(false))
		{
			for(const auto affliction : newHorizonsMagic::cureAfflictions(magicRules, unit))
				if(!vstd::contains(cureAfflictionChoices, std::make_pair(affliction, std::string{})))
					cureAfflictionChoices.emplace_back(affliction, std::string{});
			const auto frozenChoice = std::make_pair(SpellID(SpellID::NONE), std::string("frozen"));
			if(newHorizonsFrozen::isFrozen(*unit) && !vstd::contains(cureAfflictionChoices, frozenChoice))
				cureAfflictionChoices.push_back(frozenChoice);
		}
		std::sort(cureAfflictionChoices.begin(), cureAfflictionChoices.end(), [](const auto & lhs, const auto & rhs)
		{
			return std::make_pair(lhs.first.getNum(), lhs.second) < std::make_pair(rhs.first.getNum(), rhs.second);
		});
	}

	std::vector<int32_t> shadowGiftChoices = (spell && spell->getJsonKey() == newHorizonsShadowGift::SPELL_ID
	&& newHorizonsMagic::shadowGiftEnabled(battle.getBattle()->getMagicRules(), spell->getId()))
		? std::vector<int32_t>{10, 20, 30} : std::vector<int32_t>{0};
	return {maxOvercharge, canUseSelectiveDispel, canUseTemporalField,
		std::move(cureAfflictionChoices), std::move(shadowGiftChoices)};
}
bool targetAdmitted(const PossibleSpellcast & ps, spells::BattleCast & cast,
	std::unique_ptr<spells::Mechanics> & mechanics)
{
	if(!mechanics->getMetamagicCastProfile()) return true;
	auto purify = ps.spellPurifyChoices;
	for(const auto id : ps.spellPurifyPhysicalTargets)
		purify.emplace_back(id, newHorizonsPurify::physicalPoisonChoiceID());
	cast.setPurifyChoices(purify);
	if(ps.spell->getId() == newHorizonsPurify::spellID()) mechanics = ps.spell->battleMechanics(&cast);
	spells::detail::ProblemImpl problem;
	return mechanics->canBeCastAt(ps.dest, problem);
}

}
