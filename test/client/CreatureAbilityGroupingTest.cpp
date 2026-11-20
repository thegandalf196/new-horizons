/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../client/windows/CreatureAbilityGrouping.h"

TEST(CreatureAbilityGrouping, WaterElementalVulnerabilitiesShareOneCard)
{
	// Owner's Water Elemental screen: five separate +100% cards.
	const auto card = creatureAbilityGrouping::spellVulnerability({
		{100, "Armageddon"}, {100, "Fire Shield"}, {100, "Fire Wall"}, {100, "Fireball"}, {100, "Inferno"}});
	EXPECT_EQ(card.description, "{Spell Vulnerability}\n+100%: Armageddon, Fire Shield, Fire Wall, Fireball and Inferno");
	EXPECT_NE(card.tooltip.find("increased damage from these spells:\n+100%: Armageddon"), std::string::npos);
}

TEST(CreatureAbilityGrouping, PercentagesFormSeparateLinesHighestFirst)
{
	const auto card = creatureAbilityGrouping::hatred({{50, "Devils"}, {100, "Fire Elementals"}, {50, "Arch Devils"}});
	EXPECT_EQ(card.description, "{Hatred}\n+100%: Fire Elementals\n+50%: Devils and Arch Devils");
}

TEST(CreatureAbilityGrouping, SpellImmunitiesListEverySpell)
{
	const auto card = creatureAbilityGrouping::spellImmunity({"Frost Ring", "Ice Bolt"});
	EXPECT_EQ(card.description, "{Spell Immunity}\nUnaffected by Frost Ring and Ice Bolt.");
	EXPECT_EQ(card.tooltip, "{Spell Immunity}\n\nThis unit cannot be affected by Frost Ring and Ice Bolt.");
}
