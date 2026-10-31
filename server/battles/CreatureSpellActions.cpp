/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleActionProcessor.h"
#include "../CGameHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/NewHorizonsPuppetMaster.h"
#include "../../lib/entities/hero/NewHorizonsGenieSage.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/ISpellMechanics.h"

bool BattleActionProcessor::prepareUnitSpellAction(const CBattleInfoCallback & battle, BattleAction & ba)
{
	const CStack * stack = battle.battleGetStackByID(ba.stackNumber);
	battle::Target target = ba.getTarget(&battle);
	SpellID spellID = ba.spell;

	if (!canStackAct(battle, stack))
		return false;
	if(!stack->canCast())
	{
		gameHandler->complain("That creature has no spell casts available.");
		return false;
	}

	std::shared_ptr<const Bonus> randSpellcaster = stack->getBonus(Selector::type()(BonusType::RANDOM_SPELLCASTER));
	std::shared_ptr<const Bonus> spellcaster = stack->getBonus(Selector::typeSubtype(BonusType::SPELLCASTER, BonusSubtypeID(spellID)));

	if (!spellcaster && !randSpellcaster)
	{
		gameHandler->complain("That stack can't cast spells!");
		return false;
	}

	if(newHorizonsHeroes::isGenieLuckSpell(spellID) && !newHorizonsHeroes::isExplicitGenieLuckCast(*stack, spellID))
	{
		gameHandler->complain("That stack cannot use the selected Genie ability.");
		return false;
	}

	if (randSpellcaster && !newHorizonsHeroes::isExplicitGenieLuckCast(*stack, spellID))
	{
		if (target.size() != 1)
		{
			gameHandler->complain("Invalid target for random spellcaster!");
			return false;
		}

		const battle::Unit * subject = target[0].unitValue;
		if (target[0].unitValue == nullptr)
			subject = battle.battleGetStackByPos(target[0].hexValue, true);

		if (subject == nullptr)
		{
			gameHandler->complain("Invalid target for random spellcaster!");
			return false;
		}

		spellID = battle.getRandomCreatureSpell(gameHandler->getRandomGenerator(), stack, subject);

		if (spellID == SpellID::NONE)
		{
			gameHandler->complain("That stack can't cast spells!");
			return false;
		}
		spellcaster = stack->getBonus(Selector::typeSubtype(BonusType::SPELLCASTER, BonusSubtypeID(spellID)));
	}

	const CSpell * spell = SpellID(spellID).toSpell();
	newHorizonsPuppetMaster::ActionControllerCaster actionCaster(stack,
		battle.battleGetActionController(stack));
	spells::BattleCast parameters(&battle, &actionCaster, spells::Mode::CREATURE_ACTIVE, spell);
	int32_t spellLvl = 0;
	if(spellcaster)
		vstd::amax(spellLvl, spellcaster->val);
	if(randSpellcaster)
		vstd::amax(spellLvl, randSpellcaster->val);
	//Magic Plains raises level of spells cast by creatures; must match the client-side preview in BattleActionsController
	if(spell->getLevel() > 0)
		vstd::amax(spellLvl, stack->valOfBonuses(BonusType::MAGIC_SCHOOL_SKILL, BonusSubtypeID(SpellSchool::ANY)));
	parameters.setSpellLevel(spellLvl);
	const auto mechanics = spell->battleMechanics(&parameters);
	if(!mechanics->canBeCastAt(target))
	{
		gameHandler->complain("That creature cannot cast this spell at the requested target.");
		return false;
	}
	if(spells::targetsSanctifiedStackDirectly(*mechanics, target))
	{
		gameHandler->complain("A protected stack cannot be selected by a hostile single-target spell.");
		return false;
	}
	// Resolve random spellcasters exactly once, before publishing StartAction.
	ba.spell = spellID;
	return true;
}

bool BattleActionProcessor::doUnitSpellAction(const CBattleInfoCallback & battle, const BattleAction & ba)
{
	// prepareUnitSpellAction validated the caster, charge and final target before
	// StartAction marks this activation as spent. Do not reroll random casters here.
	const CStack * stack = battle.battleGetStackByID(ba.stackNumber);
	const auto target = ba.getTarget(&battle);
	const CSpell * spell = ba.spell.toSpell();
	const auto spellcaster = stack->getBonus(Selector::typeSubtype(BonusType::SPELLCASTER, BonusSubtypeID(ba.spell)));
	const auto randSpellcaster = stack->getBonus(Selector::type()(BonusType::RANDOM_SPELLCASTER));
	newHorizonsPuppetMaster::ActionControllerCaster actionCaster(stack,
		battle.battleGetActionController(stack));
	spells::BattleCast parameters(&battle, &actionCaster, spells::Mode::CREATURE_ACTIVE, spell);
	int32_t spellLvl = 0;
	if(spellcaster)
		vstd::amax(spellLvl, spellcaster->val);
	if(randSpellcaster)
		vstd::amax(spellLvl, randSpellcaster->val);
	if(spell->getLevel() > 0)
		vstd::amax(spellLvl, stack->valOfBonuses(BonusType::MAGIC_SCHOOL_SKILL, BonusSubtypeID(SpellSchool::ANY)));
	parameters.setSpellLevel(spellLvl);
	if(spell->isOffensive() || spell->isNegative() || spell->isDamage())
		breakSanctuary(battle, stack);
	parameters.cast(gameHandler->spellcastEnvironment(), target);

	processBattleEventTriggers(battle, CombatEventType::UNIT_SPELLCAST, stack, nullptr);
	return true;
}
