/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include "BattleTestFixture.h"
#include "../../../lib/battle/NewHorizonsSerpentsEmbrace.h"
#include "../../../lib/entities/hero/NewHorizonsSerpentsEmbrace.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/BonusSelector.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
#include "../../../server/battles/NewHorizonsSerpentsEmbrace.h"
class NewHorizonsSerpentsEmbraceBattleTest : public BattleTestFixture
{
protected:
	bool captured = true;
	CStack * source = nullptr;
	CStack * target = nullptr;
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires curated New Horizons preset";
	}
	void mapLoaded(CMap * map) override
	{
		BattleTestFixture::mapLoaded(map);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		if(!captured)
			rules["additionalSpecialties"].Struct().erase("serpentsEmbrace");
		rules.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
		for(const auto & object : map->objects)
			if(auto * hero = dynamic_cast<CGHeroInstance *>(object.get()); hero && hero->getOwner() == PlayerColor(0))
				hero->setHeroType(HeroTypeID(HeroTypeID::decode("core:fafner")));
	}
	void prepare(bool queen = false)
	{
		startGame();
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		source = addStack(BattleSide::ATTACKER, CreatureID(CreatureID::decode(queen ? "core:nagaQueen" : "core:naga")), BattleHex(leftHex), 10);
		target = addStack(BattleSide::DEFENDER, CreatureID(CreatureID::decode("core:pikeman")), BattleHex(rightHex), 100);
		forceMaximumDamage(source);
		forceMaximumDamage(target);
		for(auto * unit : {source, target})
			unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
				BonusType::NO_MORALE, BonusSource::OTHER, 1, BonusSourceID()));
		beginCombat();
		battle()->activeStack = source->unitId();
	}
	CStack * preparePhoenix(bool ownHypnotized)
	{
		battle()->moveUnit(target->unitId(), BattleHex(12, 8));
		battle()->removeUnit(source->unitId());
		source = addStack(BattleSide::ATTACKER, creatureByName("core:naga"), BattleHex(leftHex), 1000);
		forceMaximumDamage(source);
		source->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::NO_MORALE, BonusSource::OTHER, 1, BonusSourceID()));
		const auto side = ownHypnotized ? BattleSide::ATTACKER : BattleSide::DEFENDER;
		const BattleHex anchor(ownHypnotized ? rightHex + 1 : rightHex);
		auto * phoenix = addStack(side, creatureByName("core:phoenix"), anchor, 10);
		if(ownHypnotized)
			phoenix->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
				BonusType::HYPNOTIZED, BonusSource::SPELL_EFFECT, 1, BonusSourceID(SpellID(SpellID::HYPNOTIZE))));
		battle()->activeStack = source->unitId();
		return phoenix;
	}
	void assertPhoenixMeleeGeometry(const CStack * phoenix)
	{
		ASSERT_NE(phoenix, nullptr);
		ASSERT_EQ(phoenix->getHexes().size(), 2u);
		for(const auto hex : source->getHexes())
			ASSERT_FALSE(vstd::contains(phoenix->getHexes(), hex));
		ASSERT_TRUE(battle()->isMeleeAttackPossible(source, phoenix));
	}
	void install(const CStack * binder, const CStack * victim)
	{
		SetStackEffect packet;
		packet.battleID = battle()->getBattleID();
		packet.toAdd.emplace_back(victim->unitId(), std::vector<Bonus>{
			newHorizonsSerpentsEmbrace::marker(binder->unitId(), binder->creatureId())});
		gameHandler->sendAndApply(packet);
	}
	void activate(const CStack * unit, BattleUnitTurnReason reason)
	{
		BattleSetActiveStack packet;
		packet.battleID = battle()->getBattleID();
		packet.stack = unit->unitId();
		packet.reason = reason;
		gameHandler->sendAndApply(packet);
	}
	bool heldBy(const CStack * binder) const
	{
		return !newHorizonsSerpentsEmbrace::releasePlan(*target, binder->unitId()).empty();
	}
};
