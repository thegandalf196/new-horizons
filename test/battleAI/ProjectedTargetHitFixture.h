/*
 * ProjectedTargetHitFixture.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/ProjectedTargetHit.h"
#include "../../AI/BattleAI/HypotheticBattle.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/GameConstants.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/NewHorizonsArmorer.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/serializer/CMemorySerializer.h"

namespace projectedHitTest
{
class Environment final : public ::Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit Environment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

template<typename T> std::vector<std::byte> serialized(const T & value)
{
	CMemorySerializer memory;
	memory.oser & value;
	return memory.extractBuffer();
}
}

class ProjectedTargetHitFixture : public HeroCommandFixture
{
protected:
	bool captureLegacyLuckPolicy = false;
	CStack * braced = nullptr;
	CStack * incoming = nullptr;
	std::unique_ptr<projectedHitTest::Environment> environment;
	std::shared_ptr<CBattleInfoCallback> live;
	std::shared_ptr<HypotheticBattle> child;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		if(captureLegacyLuckPolicy)
		{
			auto magic = JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
			magic.Struct().erase("heroLuck");
			magic.Struct().erase("heroMorale");
			magic.setOverrideFlag(true);
			loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magic);
		}
	}

	void selectArmorer(CGHeroInstance * hero, MasteryLevel::Type rank, const std::string & perk)
	{
		const int skill = SecondarySkill::decode("new-horizons:armorer");
		ASSERT_GE(skill, 0);
		hero->setSecSkillLevel(SecondarySkill(skill), rank, ChangeValueMode::ABSOLUTE);
		ASSERT_TRUE(hero->getPerkState().canAcquirePerk("new-horizons:armorer", perk,
			hero->getPerkSkillRank("new-horizons:armorer")));
		hero->applyPerkSelection({"new-horizons:armorer", perk});
		ASSERT_TRUE(hero->hasActivePerk("new-horizons:armorer", perk));
	}

	void activate(uint32_t id)
	{
		BattleSetActiveStack active;
		active.battleID = BattleID(0);
		active.stack = id;
		active.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(active);
	}

	void resetChild()
	{
		child = std::make_shared<HypotheticBattle>(environment.get(), live);
	}

	void prepare(bool lastStand = false, int bracedCount = 10, int incomingCount = 100)
	{
		ASSERT_NO_FATAL_FAILURE(prepareCommands());
		braced = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(93), bracedCount);
		incoming = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(89), incomingCount);
		if(lastStand)
		{
			ASSERT_NO_FATAL_FAILURE(selectArmorer(defenderSideHero, MasteryLevel::BASIC, "new-horizons:armorer.pavise"));
			ASSERT_NO_FATAL_FAILURE(selectArmorer(defenderSideHero, MasteryLevel::ADVANCED, "new-horizons:armorer.veteran"));
			ASSERT_NO_FATAL_FAILURE(selectArmorer(defenderSideHero, MasteryLevel::EXPERT, "new-horizons:armorer.lastStand"));
		}
		activate(incoming->unitId());
		environment = std::make_unique<projectedHitTest::Environment>(gameState());
		live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		resetChild();
	}

	projectedTargetHit::Context context(bool damageRetaliation = false, bool reactive = true) const
	{
		return {incoming->unitId(), true, damageRetaliation, reactive};
	}
};
