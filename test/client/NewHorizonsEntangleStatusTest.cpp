/*
 * NewHorizonsEntangleStatusTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "../StdInc.h"

#include "../../client/battle/NewHorizonsBattleStatus.h"
#include "../../lib/spells/CSpell.h"

namespace
{
using namespace newHorizonsBattleStatus;

std::shared_ptr<Bonus> makeMarker(SpellID spell, BonusType type = BonusType::BIND_EFFECT,
	BonusSource source = BonusSource::SPELL_EFFECT, BonusDuration::Type duration = BonusDuration::N_TURNS,
	int32_t rounds = 2, bool withParameters = false)
{
	auto bonus = std::make_shared<Bonus>(duration, type, source, 0, BonusSourceID(spell));
	bonus->turnsRemain = rounds;
	if(withParameters)
		bonus->parameters = std::make_shared<BonusParameters>(1);
	return bonus;
}
}

class NewHorizonsEntangleStatusTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		entangleId = SpellID::decode(std::string(ENTANGLE_SPELL_KEY));
		if(entangleId == SpellID::NONE)
			GTEST_SKIP() << "Requires the New Horizons Entangle spell registration";

		bindId = SpellID::decode("core:bind");
		ASSERT_NE(bindId, SpellID::NONE);
	}

	SpellID entangleId = SpellID::NONE;
	SpellID bindId = SpellID::NONE;
};

TEST_F(NewHorizonsEntangleStatusTest, ReadsTheCurrentTimedMarkerRounds)
{
	const auto status = entangleStatus(std::vector<std::shared_ptr<Bonus>>{
		makeMarker(entangleId, BonusType::BIND_EFFECT, BonusSource::SPELL_EFFECT, BonusDuration::N_TURNS, 2)});

	ASSERT_TRUE(status.active());
	EXPECT_EQ(status.remainingRounds, 2);
	EXPECT_EQ(entangleStatus(std::vector<std::shared_ptr<Bonus>>{
		makeMarker(entangleId, BonusType::BIND_EFFECT, BonusSource::SPELL_EFFECT,
			BonusDuration::N_TURNS, 1)}).remainingRounds, 1);
	EXPECT_EQ(entangleStatus(std::vector<std::shared_ptr<Bonus>>{
		makeMarker(entangleId, BonusType::BIND_EFFECT, BonusSource::SPELL_EFFECT,
			BonusDuration::N_TURNS, 3)}).remainingRounds, 3);
}

TEST_F(NewHorizonsEntangleStatusTest, DoesNotReadLegacyBindAsEntangle)
{
	const auto legacyBind = makeMarker(bindId, BonusType::BIND_EFFECT, BonusSource::SPELL_EFFECT,
		BonusDuration::PERMANENT, 3);
	EXPECT_FALSE(isEntangle("core:bind"));
	EXPECT_FALSE(entangleStatus(std::vector<std::shared_ptr<Bonus>>{legacyBind}).active());
}

TEST_F(NewHorizonsEntangleStatusTest, RequiresAnUnexpiredUnparameterizedTimedBindMarker)
{
	const std::vector<std::shared_ptr<Bonus>> invalidMarkers = {
		makeMarker(entangleId, BonusType::BIND_EFFECT, BonusSource::ARTIFACT),
		makeMarker(entangleId, BonusType::STACKS_SPEED),
		makeMarker(entangleId, BonusType::BIND_EFFECT, BonusSource::SPELL_EFFECT, BonusDuration::PERMANENT),
		makeMarker(entangleId, BonusType::BIND_EFFECT, BonusSource::SPELL_EFFECT, BonusDuration::N_TURNS, 0),
		makeMarker(entangleId, BonusType::BIND_EFFECT, BonusSource::SPELL_EFFECT, BonusDuration::N_TURNS, 2, true),
	};
	EXPECT_FALSE(entangleStatus(invalidMarkers).active());
}

TEST_F(NewHorizonsEntangleStatusTest, HelpExplainsRootingWithoutStoppingTheStack)
{
	const auto status = entangleStatus(std::vector<std::shared_ptr<Bonus>>{
		makeMarker(entangleId, BonusType::BIND_EFFECT, BonusSource::SPELL_EFFECT, BonusDuration::N_TURNS, 2)});
	const auto tooltip = entangleTooltip("Entangle", status);

	EXPECT_NE(tooltip.find("2 rounds remaining"), std::string::npos);
	EXPECT_NE(tooltip.find("Initiative and activation timing are unchanged"), std::string::npos);
	EXPECT_NE(tooltip.find("Unlike Time Stop"), std::string::npos);
	EXPECT_NE(tooltip.find("attack adjacent enemies, retaliate, shoot, Wait, Defend"), std::string::npos);
	EXPECT_NE(tooltip.find("do not require movement"), std::string::npos);
	EXPECT_NE(tooltip.find("Forced displacement or teleportation removes the roots"), std::string::npos);
}
