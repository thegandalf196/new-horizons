/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../client/battle/PassiveFactionSkillPresentation.h"

TEST(HeroMetamagicPointPresentation, ActualRankAndPointsDetermineBothBonuses)
{
	newHorizonsMetamagic::Profile profile;
	profile.rank = 2;
	profile.pointPolicy = true;
	profile.arcaneAttunement = true;
	MetamagicPointState state;
	state.points = 7;
	const auto status = passiveFactionSkillPresentation::metamagicPoints(profile, state);
	EXPECT_EQ(status.value, "7/12");
	EXPECT_NE(status.details.find("Spell effectiveness: +14%."), std::string::npos);
	EXPECT_NE(status.details.find("Spell penetration: +14 percentage points."), std::string::npos);
	EXPECT_NE(status.details.find("Army Initiative: +1."), std::string::npos);
	EXPECT_TRUE(status.replacesProviderDescription);
}

TEST(HeroMetamagicPointPresentation, SingularityReadbackUsesTheLatchAfterPointsFall)
{
	newHorizonsMetamagic::Profile profile;
	profile.rank = 3;
	profile.pointPolicy = true;
	profile.arcaneSingularity = true;
	MetamagicPointState state;
	state.points = 10;
	state.singularityReached = true;
	const auto active = passiveFactionSkillPresentation::metamagicPoints(profile, state);
	EXPECT_NE(active.details.find("Spell effectiveness: +30%."), std::string::npos);
	EXPECT_NE(active.details.find("Mana costs reduced by 25%"), std::string::npos);
	state.singularityReached = false;
	const auto waiting = passiveFactionSkillPresentation::metamagicPoints(profile, state);
	EXPECT_EQ(waiting.details.find("Mana costs reduced by 25%"), std::string::npos);
	EXPECT_NE(waiting.details.find("activates upon reaching 12 points"), std::string::npos);
}
