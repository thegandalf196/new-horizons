/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "../../battleAI/HeroQueueSelectorFixture.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/ConsecratedArms.h"
#include "../../../lib/battle/ConsecratedArmsChoice.h"
#include "../../../lib/entities/hero/CHero.h"
#include "../../../lib/entities/hero/NewHorizonsCastleSpecialties.h"
#include "../../../lib/modding/CModHandler.h"

#include <initializer_list>
#include <string>

/// Castle specialty battles on the shipped New Horizons rules, with one
/// attacking specialist. `stripped` erases those Castle rows to reproduce a
/// 1164/1165 capture, which keeps every historical specialty.
class CastleSpecialtyTest : public heroQueueTest::HeroQueueSelectorTest
{
protected:
	std::vector<std::string> stripped;

	void SetUp() override
	{
		HeroQueueSelectorTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons test profile";
	}

	void mapLoaded(CMap * map) override
	{
		useRevisedOrders = true;
		HeroQueueSelectorTest::mapLoaded(map);
		if(stripped.empty())
			return;
		JsonNode heroes(JsonPath::builtin("config/newHorizonsHeroes"));
		for(const auto & key : stripped)
			heroes["revisedSpecialties"].Struct().erase(key);
		heroes.setOverrideFlag(true); // Replace, never merge with the shipped rows.
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, heroes);
	}

	/// Starts the world and makes the attacking hero the named Castle specialist.
	void specialist(const char * key, int level = 1, bool consecrated = true)
	{
		ASSERT_NO_FATAL_FAILURE(world());
		attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode(key)));
		attackerSideHero->level = level;
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(consecratedArms::SKILL)),
			consecrated ? MasteryLevel::BASIC : MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	}

	/// An empty battlefield; each test adds its own stacks, then calls beginCombat().
	void emptyField()
	{
		ASSERT_NO_FATAL_FAILURE(startBattle());
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
	}

	CStack * place(BattleSide side, const char * creature, BattleHex hex, int count, int initiative)
	{
		auto * stack = addStack(side, creatureByName(creature), hex, count);
		if(stack)
		{
			effect(stack, Bonus(BonusDuration::ONE_BATTLE, BonusType::STACKS_INITIATIVE_BASE, BonusSource::OTHER,
				initiative, BonusSourceID(), BonusSubtypeID(), BonusValueType::BASE_NUMBER));
			effect(stack, Bonus(BonusDuration::ONE_BATTLE, BonusType::NO_MORALE, BonusSource::OTHER, 1, BonusSourceID()));
		}
		return stack;
	}

	/// Fills an empty Chosen Prayers choice with the first prayer and eligible recipient.
	BattleAction withPrayer(BattleAction action, consecratedArms::Prayer prayer = consecratedArms::Prayer::DIVINE_PURPOSE,
		std::optional<uint32_t> recipient = std::nullopt) const
	{
		const auto request = consecratedArms::choiceRequest(*battle(), action);
		if(!request.prayers)
			return action;
		action.prayerChoices = {static_cast<uint8_t>(prayer)};
		for(size_t index = 1; index < request.prayers; ++index)
			action.prayerChoices.push_back(static_cast<uint8_t>((static_cast<int>(prayer) + index) % 6));
		action.prayerRecipient = recipient.value_or(request.recipients.front());
		return action;
	}

	bool submit(const BattleAction & action, PlayerColor player = PlayerColor(0))
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), player, action);
	}

	BattleAction spellAt(BattleSide side, SpellID spell, const battle::Unit * target) const
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = side;
		action.spell = spell;
		action.aimToUnit(target);
		return action;
	}

	BattleAction spellAtHex(BattleSide side, SpellID spell, BattleHex hex) const
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = side;
		action.spell = spell;
		action.aimToHex(hex);
		return action;
	}
};
