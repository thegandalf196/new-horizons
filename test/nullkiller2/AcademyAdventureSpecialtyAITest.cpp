/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "nullkiller2/NullkillerTest.h"
#include "AI/Nullkiller2/Engine/Nullkiller.h"
#include "AI/Nullkiller2/Engine/PriorityEvaluator.h"
#include "lib/GameConstants.h"
#include "lib/entities/hero/NewHorizonsAcademySpecialties.h"
#include "lib/mapObjects/CGCreature.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/modding/CModHandler.h"

namespace
{
const PlayerColor ACADEMY_AI_PLAYER(0);
CreatureID academyCreature(const char * name) { return CreatureID(CreatureID::decode(name)); }

/// The adventure AI values Rissa's Spoils of Alchemy as Mercury on the fights it ranks.
class AcademyAdventureSpecialtyAITest : public NullkillerTest
{
protected:
	CGHeroInstance * specialist = nullptr;
	CGHeroInstance * other = nullptr;
	CGCreature * guard = nullptr;
	std::unique_ptr<NK2AI::AIGateway> gateway;

	void SetUp() override
	{
		NullkillerTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires curated New Horizons preset";
	}

	void mapLoaded(CMap * loaded) override
	{
		NullkillerTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
	}

	void prepare()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36).playerActive(ACADEMY_AI_PLAYER)
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:rissa")), ACADEMY_AI_PLAYER)
				.heroGarrison({{academyCreature("core:stoneGargoyle"), 1}})
			.hero({24, 24, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), ACADEMY_AI_PLAYER)
				.heroGarrison({{academyCreature("core:pikeman"), 1}})
			.monster({10, 10, 0}, academyCreature("core:angel"), 20, static_cast<int8_t>(CGCreature::Character::HOSTILE));
		startWithMap(std::move(builder));
		specialist = findHeroAt({5, 5, 0});
		other = findHeroAt({24, 24, 0});
		guard = findFirst<CGCreature>();
		ASSERT_TRUE(specialist && other && guard);
		revealMap(ACADEMY_AI_PLAYER);
		gateway = makeGateway(ACADEMY_AI_PLAYER);
	}
};
}

TEST_F(AcademyAdventureSpecialtyAITest, RissaValuesTheMercuryAFightWouldBring)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	NK2AI::RewardEvaluator reward(gateway->nullkiller.get());
	const int mercury = newHorizonsHeroes::spoilsOfAlchemyMercury(specialist, newHorizonsHeroes::spoilsRecruitmentGold(*guard));
	ASSERT_EQ(mercury, 10) << "20 Angels are worth the per-combat maximum";
	EXPECT_EQ(reward.getGoldReward(guard, specialist), mercury * 100) << "Valued like any other Mercury reward";
	EXPECT_EQ(reward.getGoldReward(guard, other), 0) << "Only Rissa distils the fallen";
}
