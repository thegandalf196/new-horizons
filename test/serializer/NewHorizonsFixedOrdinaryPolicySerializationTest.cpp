/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../server/battles/FullGameSnapshotTypes.h"
#include "CapturedSpecialtySnapshotFixture.h"
#include "../../lib/GameSettings.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <array>

namespace
{
struct FixedPolicy { const char * hero; const char * key; int value; };
constexpr std::array<FixedPolicy, 6> fixedPolicies{{
	{"core:fiona", "packPursuitSpeed", 5},
	{"core:lacus", "undertowDefensePerAttack", 1},
	{"core:gretchin", "goblinFrenzyDamagePercent", 50},
	{"core:korbac", "evasiveFlightReductionPercentPerHex", 3},
	{"core:kalt", "risingTideCreaturesPerRound", 1},
	{"core:straker", "undyingHealingHP", 100}}};
JsonNode & policy(JsonNode & rules)
{
	return rules["additionalSpecialties"]["ordinaryCreatureSpecialties"];
}
const JsonNode & policy(const JsonNode & rules)
{
	return rules["additionalSpecialties"]["ordinaryCreatureSpecialties"];
}
class FixedOrdinaryPolicySerializationTest : public HeroCommandFixture,
	public ::testing::WithParamInterface<int>
{
protected:
	const FixedPolicy & entry() const { return fixedPolicies.at(GetParam()); }
	void prepare()
	{
		useRevisedOrders = true;
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(entry().hero)), PlayerColor(0)).heroExperience(0)
			.heroGarrison({{CreatureID(0), 7}})
			.hero({7, 7, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PlayerColor(1)).heroExperience(0)
			.heroGarrison({{CreatureID(0), 7}});
		startWithMap(std::move(builder));
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		ASSERT_NE(attackerSideHero, nullptr);
	}
};
TEST_P(FixedOrdinaryPolicySerializationTest, CurrentWorldHeroPoolAndSettingsKeepExactCapturedRecipe)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto id = attackerSideHero->id;
	const auto & captured = attackerSideHero->getPrimaryGrowthRules();
	ASSERT_EQ(policy(captured)[entry().key].Integer(), entry().value);
	ASSERT_TRUE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(attackerSideHero));
	const auto presentation = attackerSideHero->getSpecialtyPresentation();
	const auto bonuses = attackerSideHero->getExportedBonusList().toJsonNode();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	const auto * hero = restored.getHero(id);
	ASSERT_NE(hero, nullptr);
	EXPECT_EQ(hero->getPrimaryGrowthRules(), captured);
	EXPECT_EQ(hero->getSpecialtyPresentation().name, presentation.name);
	EXPECT_EQ(hero->getExportedBonusList().toJsonNode(), bonuses);
	EXPECT_EQ(policy(restored.getHeroDevelopmentRules())[entry().key].Integer(), entry().value);
	EXPECT_EQ(policy(restored.getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS))[entry().key].Integer(), entry().value);
	const auto pool = restored.getMap().getHeroesInPool();
	ASSERT_FALSE(pool.empty());
	const auto * pooled = restored.getMap().tryGetFromHeroPool(pool.front());
	ASSERT_NE(pooled, nullptr);
	EXPECT_EQ(policy(pooled->getPrimaryGrowthRules())[entry().key].Integer(), entry().value);
}
TEST_P(FixedOrdinaryPolicySerializationTest, ExplicitZeroRejectsAtAdmissionReadAndWriterBeforePrefix)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto & captured = attackerSideHero->getPrimaryGrowthRules();
	const auto original = captured;
	auto invalid = captured;
	policy(invalid)[entry().key].Integer() = 0;
	GameSettings settings;
	settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS, original);
	const auto settingsSnapshot = [&settings]
	{
		CMemorySerializer serializer;
		serializer.oser & settings;
		return serializer.extractBuffer();
	};
	const auto before = settingsSnapshot();
	EXPECT_THROW(settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS, invalid), std::runtime_error);
	EXPECT_EQ(settingsSnapshot(), before);
	const auto bytes = capturedSpecialtySnapshotTest::constructedSnapshot(*attackerSideHero, captured, invalid);
	CMemorySerializer reader(bytes);
	reader.iser.loadingGamestate = true;
	reader.iser.cb = gameState().get();
	auto rejected = std::make_shared<CGHeroInstance>(gameState().get());
	EXPECT_THROW(rejected->serialize(reader.iser), std::runtime_error);
	EXPECT_EQ(captured, original);

	// Test-only corrupted owner: ordinary APIs cannot install this state.
	auto & corrupted = const_cast<JsonNode &>(captured);
	corrupted = invalid;
	CMemorySerializer writer;
	EXPECT_THROW(attackerSideHero->serialize(writer.oser), std::runtime_error);
	EXPECT_TRUE(writer.extractBuffer().empty());
	corrupted = original;
	EXPECT_EQ(captured, original);
}
INSTANTIATE_TEST_SUITE_P(SixCurrentFixedRecipes, FixedOrdinaryPolicySerializationTest, ::testing::Range(0, 6));
}
