/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../client/battle/ThunarRangedPresentation.h"

using namespace thunarRangedPresentation;

TEST(ThunarRangedPresentation, FallbackNeedsCanonicalAdmissionAndInspectedAttackFrames)
{
	EXPECT_FALSE(fallbackPose({}, Direction::FRONT, 0, 7));
	EXPECT_FALSE(fallbackPose({true, std::nullopt}, Direction::FRONT, 0, 7));
	EXPECT_FALSE(fallbackPose({true, static_cast<Family>(2)}, Direction::FRONT, 0, 7));
	const Admission accepted{true, Family::EARTH};
	EXPECT_FALSE(fallbackPose(accepted, Direction::FRONT, 0, 0));
	EXPECT_FALSE(fallbackPose(accepted, Direction::FRONT, 7, 7));
}

TEST(ThunarRangedPresentation, MissingShootingGroupsMapOnlyToMatchingAttackGroups)
{
	for(const auto family : {Family::EARTH, Family::MAGMA})
	{
		const Admission accepted{true, family};
		const std::array expected{ECreatureAnimType::ATTACK_UP,
			ECreatureAnimType::ATTACK_FRONT, ECreatureAnimType::ATTACK_DOWN};
		for(std::size_t index = 0; index < expected.size(); ++index)
		{
			const auto pose = fallbackPose(accepted, static_cast<Direction>(index), 0, 7);
			ASSERT_TRUE(pose);
			EXPECT_EQ(pose->group, expected[index]);
			EXPECT_EQ(pose->climaxFrame, 3);
		}
	}
}

TEST(ThunarRangedPresentation, AllTwelveProvisionalOriginsAreInNativeCanvasAndMirrorNormally)
{
	for(const auto family : {Family::EARTH, Family::MAGMA})
		for(const auto direction : {Direction::UP, Direction::FRONT, Direction::DOWN})
		{
			const auto right = provisionalOrigin(family, direction, true, 450);
			const auto left = provisionalOrigin(family, direction, false, 450);
			ASSERT_TRUE(right);
			ASSERT_TRUE(left);
			EXPECT_EQ(right->x + left->x, 450);
			EXPECT_EQ(right->y, left->y);
			EXPECT_GT(right->x, 0);
			EXPECT_LT(right->x, 450);
			EXPECT_GT(right->y, 0);
			EXPECT_LT(right->y, 400);
		}
}

TEST(ThunarRangedPresentation, UnknownGeometryDoesNotBorrowAnotherCreatureOrigin)
{
	EXPECT_FALSE(provisionalOrigin(Family::EARTH, Direction::FRONT, true, 300));
	EXPECT_FALSE(provisionalOrigin(static_cast<Family>(2), Direction::FRONT, true, 450));
	EXPECT_FALSE(provisionalOrigin(Family::MAGMA, static_cast<Direction>(3), false, 450));
}

TEST(ThunarRangedPresentation, VariantNonemptyAttackGroupsClampEmissionBeforeRecovery)
{
	const Admission accepted{true, Family::EARTH};
	for(std::size_t count = 1; count <= 8; ++count)
	{
		const auto pose = fallbackPose(accepted, Direction::FRONT, 0, count);
		ASSERT_TRUE(pose);
		EXPECT_EQ(pose->group, ECreatureAnimType::ATTACK_FRONT);
		EXPECT_LT(pose->climaxFrame, count);
		EXPECT_EQ(pose->climaxFrame, std::min<std::size_t>(3, count - 1));
	}
}
