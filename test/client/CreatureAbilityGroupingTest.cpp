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

TEST(CreatureAbilityGrouping, OnHitEffectsShareOneCardAndKeepDistinctChances)
{
	// Dragon Fly: Dispel and Weakness always; differing chances stay on their own lines.
	const auto card = creatureAbilityGrouping::onHitEffects({{100, "Dispel"}, {100, "Weakness"}, {20, "Acid"}});
	EXPECT_EQ(card.description, "{On-hit Effects}\nAlways: Dispel and Weakness\n20% chance: Acid");
}

#include "../../client/windows/SylvanStatusArt.h"

TEST(SylvanStatusArt, RowsGetPerkIconAndTimingHelp)
{
	const auto art = sylvanStatusArt::forRow("Dappled Cover: 20% ranged creature-damage protection (current).");
	EXPECT_EQ(art.image, "NH_perk_sylvan_tactics_dappled_cover.png");
	EXPECT_NE(art.help.find("ends when it leaves that state"), std::string::npos);
	EXPECT_EQ(sylvanStatusArt::forRow("Disruptive Strike: -15% Initiative until this stack's next activation.").image,
		"NH_perk_sylvan_tactics_disruptive_strike.png");
	EXPECT_TRUE(sylvanStatusArt::forRow("Unknown: something").image.empty());
}
