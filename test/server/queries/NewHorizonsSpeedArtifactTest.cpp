/*
 * NewHorizonsSpeedArtifactTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "../battles/BattleTestFixture.h"

#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/bonuses/BonusEnum.h"
#include "../../../lib/bonuses/BonusSelector.h"
#include "../../../lib/battle/CBattleInfoCallback.h"
#include "../../../lib/entities/artifact/CArtifact.h"
#include "../../../lib/entities/artifact/CArtifactInstance.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/ArtifactLocation.h"
#include "../../../server/CGameHandler.h"

#include <algorithm>
#include <array>
#include <memory>
#include <utility>
#include <vector>

namespace
{
struct EquippedSpeedArtifact
{
	ArtifactID id;
	ArtifactPosition slot;
	int32_t amount;
};

class SpeedArtifactEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit SpeedArtifactEnvironment(std::shared_ptr<CGameState> state_)
		: state(std::move(state_))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

ArtifactID coreArtifact(const char * identifier)
{
	return ArtifactID(ArtifactID::decode(identifier));
}

ArtifactPosition firstFreeHeroSlot(const CGHeroInstance & hero, ArtifactID artifact)
{
	const auto * type = artifact.toArtifact();
	if(!type)
		return ArtifactPosition::PRE_FIRST;

	for(const auto slot : type->getPossibleSlots().at(ArtBearer::HERO))
		if(!hero.getArt(slot))
			return slot;

	return ArtifactPosition::PRE_FIRST;
}

class NewHorizonsSpeedArtifactTest : public BattleTestFixture
{
protected:
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";

		startGame();
	}
};
}

TEST_F(NewHorizonsSpeedArtifactTest, EquippedSpeedArtifactsAlsoGrantExplicitInitiativeWithoutDoubleCountingFallback)
{
	const std::array<std::pair<ArtifactID, int32_t>, 3> artifacts{{
		{coreArtifact("core:ringOfTheWayfarer"), 1},
		{coreArtifact("core:necklaceOfSwiftness"), 1},
		{coreArtifact("core:capeOfVelocity"), 2},
	}};
	std::vector<EquippedSpeedArtifact> equipped;

	for(const auto & [artifact, value] : artifacts)
	{
		ASSERT_NE(artifact.toArtifact(), nullptr);
		const auto slot = firstFreeHeroSlot(*attackerSideHero, artifact);
		ASSERT_NE(slot, ArtifactPosition::PRE_FIRST);

		giveArtifact(attackerSideHero, artifact, slot);
		const auto * loaded = attackerSideHero->getArt(slot);
		ASSERT_NE(loaded, nullptr);
		ASSERT_EQ(loaded->getTypeId(), artifact);
		ASSERT_TRUE(vstd::contains(artifact.toArtifact()->getPossibleSlots().at(ArtBearer::HERO), slot));
		equipped.push_back({artifact, slot, value});
	}

	startBattle();
	auto * mageWithArtifacts = addStack(BattleSide::ATTACKER, creatureByName("core:mage"), BattleHex(76), 1);
	auto * mageWithoutArtifacts = addStack(BattleSide::DEFENDER, creatureByName("core:mage"), BattleHex(106), 1);
	auto * archMageWithArtifacts = addStack(BattleSide::ATTACKER, creatureByName("core:archMage"), BattleHex(78), 1);
	auto * archMageWithoutArtifacts = addStack(BattleSide::DEFENDER, creatureByName("core:archMage"), BattleHex(108), 1);
	auto * genieWithArtifacts = addStack(BattleSide::ATTACKER, creatureByName("core:genie"), BattleHex(80), 1);
	auto * genieWithoutArtifacts = addStack(BattleSide::DEFENDER, creatureByName("core:genie"), BattleHex(110), 1);
	ASSERT_NE(mageWithArtifacts, nullptr);
	ASSERT_NE(mageWithoutArtifacts, nullptr);
	ASSERT_NE(archMageWithArtifacts, nullptr);
	ASSERT_NE(archMageWithoutArtifacts, nullptr);
	ASSERT_NE(genieWithArtifacts, nullptr);
	ASSERT_NE(genieWithoutArtifacts, nullptr);
	const auto creatureInitiative = Selector::type()(BonusType::STACKS_INITIATIVE_BASE)
		.And(Selector::sourceTypeSel(BonusSource::CREATURE_ABILITY));
	EXPECT_TRUE(mageWithoutArtifacts->hasBonus(creatureInitiative));
	EXPECT_TRUE(archMageWithoutArtifacts->hasBonus(creatureInitiative));
	EXPECT_FALSE(genieWithoutArtifacts->hasBonus(creatureInitiative));

	const std::array<std::pair<CStack *, CStack *>, 3> comparedStacks{{
		{mageWithArtifacts, mageWithoutArtifacts},
		{archMageWithArtifacts, archMageWithoutArtifacts},
		{genieWithArtifacts, genieWithoutArtifacts},
	}};
	const auto expectDeltas = [&comparedStacks](int32_t expected)
	{
		for(const auto & [equippedStack, controlStack] : comparedStacks)
		{
			EXPECT_EQ(static_cast<int32_t>(equippedStack->getMovementRange())
				- static_cast<int32_t>(controlStack->getMovementRange()), expected);
			EXPECT_EQ(equippedStack->getInitiative() - controlStack->getInitiative(), expected);
		}
	};

	expectDeltas(4);

	SpeedArtifactEnvironment environment(gameState());
	std::shared_ptr<CBattleInfoCallback> callback(gameState(), battle());
	HypotheticBattle projected(&environment, callback);
	for(const auto & [live, control] : comparedStacks)
	{
		const auto projectedLive = projected.getForUpdate(live->unitId());
		const auto projectedControl = projected.getForUpdate(control->unitId());
		ASSERT_NE(projectedLive, nullptr);
		ASSERT_NE(projectedControl, nullptr);
		EXPECT_EQ(static_cast<int32_t>(projectedLive->getMovementRange())
			- static_cast<int32_t>(projectedControl->getMovementRange()), 4);
		EXPECT_EQ(projectedLive->getInitiative() - projectedControl->getInitiative(), 4);
	}

	int32_t remainingBonus = 4;
	for(auto it = equipped.rbegin(); it != equipped.rend(); ++it)
	{
		const auto & [artifact, slot, value] = *it;
		const auto * installed = attackerSideHero->getArt(slot);
		ASSERT_NE(installed, nullptr);
		ASSERT_EQ(installed->getTypeId(), artifact);
		gameHandler->removeArtifact(ArtifactLocation(attackerSideHero->id, slot));
		remainingBonus -= value;
		expectDeltas(remainingBonus);
	}

	expectDeltas(0);
}
