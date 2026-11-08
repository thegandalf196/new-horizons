/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "HeroCommandFixture.h"
#include "../../../lib/battle/NewHorizonsSpecialtyEvents.h"
#include "../../../lib/battle/NewHorizonsSpecialtyRoundEffects.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../server/battles/BattlePrimaryStrikeAftermath.h"

class NewHorizonsSpecialtyEventFixture : public HeroCommandFixture
{
protected:
	CStack * actor = nullptr;
	CStack * enemy = nullptr;
	void mapLoaded(CMap * map) override
	{
		useRevisedOrders = true;
		HeroCommandFixture::mapLoaded(map);
	}
	void prepare(const char * hero, const char * creature, int count = 5)
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(hero)), PlayerColor(0)).heroExperience(0)
			.heroGarrison({{CreatureID(CreatureID::decode(creature)), count}})
			.hero({7, 7, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PlayerColor(1)).heroExperience(0)
			.heroGarrison({{CreatureID(CreatureID::decode("core:pikeman")), 100}});
		startWithMap(std::move(builder));
		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		defenderSideHero = findHeroByOwner(PlayerColor(1));
		startBattle();
		for(const auto & unit : battle()->stacks)
			if(unit->unitSlot() == SlotID(0))
				(unit->unitSide() == BattleSide::ATTACKER ? actor : enemy) = unit.get();
		ASSERT_NE(actor, nullptr); ASSERT_NE(enemy, nullptr);
		actor->setPosition(BattleHex(3, 5)); enemy->setPosition(BattleHex(4, 5));
		for(const auto bound : {BonusValueType::INDEPENDENT_MIN, BonusValueType::INDEPENDENT_MAX})
			actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::STACKS_INITIATIVE_BASE,
				BonusSource::OTHER, 1000, BonusSourceID(), BonusSubtypeID(), bound));
	}
	void apply(const std::vector<newHorizonsOrdinaryStrike::EffectPlan> & plans)
	{
		for(const auto & plan : plans) battlePrimaryStrikeAftermath::applyOrdinaryEffect(*gameHandler, *battle(), plan);
	}
	void publish(battle::CUnitState & state)
	{
		BattleUnitsChanged changed;
		changed.battleID = BattleID(0);
		UnitChanges update(actor->unitId(), UnitChanges::EOperation::UPDATE);
		update.data = state.save();
		changed.changedStacks.push_back(std::move(update));
		gameHandler->sendAndApply(changed);
	}
};
