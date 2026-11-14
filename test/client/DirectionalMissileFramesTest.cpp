/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../client/battle/DirectionalMissileFrames.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/constants/StringConstants.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/json/JsonUtils.h"
#include "../../lib/modding/CModHandler.h"

TEST(DirectionalMissileFrames, StaticMissilesRetainEverySelectedAngle)
{
	EXPECT_EQ(directionalMissileFrames::phases(1, 12, 12), 1);
	for(std::size_t angle = 0; angle < 12; ++angle)
		for(const double elapsed : {0.0, 0.9, 1.0, 9.9, 1000.0})
			EXPECT_EQ(directionalMissileFrames::frame(angle, 1, elapsed), angle);
}

TEST(DirectionalMissileFrames, AllThreeDirectionsAnimateWithoutCrossingAngleBlocks)
{
	ASSERT_EQ(directionalMissileFrames::phases(4, 3, 12), 4);
	for(std::size_t angle = 0; angle < 3; ++angle)
		for(std::size_t phase = 0; phase < 4; ++phase)
		{
			EXPECT_EQ(directionalMissileFrames::frame(angle, 4, phase + 0.9), angle * 4 + phase);
			EXPECT_EQ(directionalMissileFrames::frame(angle, 4, phase + 4.0), angle * 4 + phase);
		}
}

TEST(DirectionalMissileFrames, IncompleteOrInvalidArtworkCannotSelectMissingPhaseFrames)
{
	EXPECT_EQ(directionalMissileFrames::phases(4, 3, 11), 1);
	EXPECT_EQ(directionalMissileFrames::phases(0, 3, 12), 1);
	EXPECT_EQ(directionalMissileFrames::phases(4, 0, 12), 1);
	EXPECT_EQ(directionalMissileFrames::phases(4, 3, 0), 1);
	EXPECT_EQ(directionalMissileFrames::phases(65, 1, 65), 1);
	EXPECT_EQ(directionalMissileFrames::phases(64, 3, 192), 64);
}

TEST(DirectionalMissileFrames, PhaseWrapHandlesLongFramesAndStartsAtFirstPose)
{
	EXPECT_EQ(directionalMissileFrames::frame(2, 4, -1.0), 8);
	EXPECT_EQ(directionalMissileFrames::frame(2, 4, 0.0), 8);
	EXPECT_EQ(directionalMissileFrames::frame(2, 4, 3.999), 11);
	EXPECT_EQ(directionalMissileFrames::frame(2, 4, 4.0), 8);
	EXPECT_EQ(directionalMissileFrames::frame(2, 4, 1001.25), 9);
}

TEST(DirectionalMissileFrames, LoadedRoyalUsesTemporalMissilesWhileOriginalShooterKeepsDefaults)
{
	if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
		GTEST_SKIP() << "Requires the New Horizons Mummy content";
	const auto royalId = CreatureID(CreatureID::decode("new-horizons:royalMummy"));
	ASSERT_TRUE(royalId.hasValue());
	const auto royal = royalId.toCreature();
	ASSERT_NE(royal, nullptr);
	EXPECT_EQ(royal->animation.projectileFramesPerAngle, 4);
	EXPECT_EQ(royal->animation.projectileImageName, AnimationPath::builtin("NH_ROYAL_MUMMY_PROJECTILE"));
	EXPECT_EQ(royal->animation.projectileImpactName, AnimationPath::builtin("NH_ROYAL_MUMMY_IMPACT"));
	EXPECT_EQ(royal->animation.attackClimaxFrame, 5);
	EXPECT_EQ(royal->animation.missileFrameAngles, (std::vector<double>{45, 0, -45}));
	const auto archer = CreatureID(CreatureID::ARCHER).toCreature();
	ASSERT_NE(archer, nullptr);
	EXPECT_EQ(archer->animation.projectileFramesPerAngle, 1);
	EXPECT_TRUE(archer->animation.projectileImpactName.empty());
}

TEST(DirectionalMissileFrames, SchemaAcceptsOnlyBoundedIntegralPhaseCounts)
{
	const auto schema = JsonUtils::getSchema("vcmi:creature")["properties"]["graphics"]
		["properties"]["missile"]["properties"]["framesPerAngle"];
	EXPECT_TRUE(JsonUtils::validate(JsonNode(1), schema, "ordinary missile phases"));
	EXPECT_TRUE(JsonUtils::validate(JsonNode(4), schema, "Royal missile phases"));
	EXPECT_TRUE(JsonUtils::validate(JsonNode(64), schema, "maximum missile phases"));
	for(const auto invalid : {0, -1, 65})
		EXPECT_FALSE(JsonUtils::validate(JsonNode(invalid), schema, "invalid missile phases"));
	EXPECT_FALSE(JsonUtils::validate(JsonNode(1.5), schema, "fractional missile phases"));
	EXPECT_FALSE(JsonUtils::validate(JsonNode(true), schema, "non-numeric missile phases"));
}
