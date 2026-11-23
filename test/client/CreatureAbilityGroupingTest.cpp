/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../client/windows/CreatureAbilityGrouping.h"
#include "../../client/windows/CreatureStatusCardGrouping.h"
#include "../../lib/battle/NewHorizonsRampartSpecialties.h"

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
#include "../OriginalDataRequirement.h"

TEST(SylvanStatusArt, RowsGetPerkIconAndTimingHelp)
{
	const auto art = sylvanStatusArt::forRow("Dappled Cover: 20% ranged creature-damage protection (current).");
	EXPECT_EQ(art.image, "NH_perk_sylvan_tactics_dappled_cover.png");
	EXPECT_NE(art.help.find("ends when it leaves that state"), std::string::npos);
	EXPECT_EQ(sylvanStatusArt::forRow("Disruptive Strike: -15% Initiative until this stack's next activation.").image,
		"NH_perk_sylvan_tactics_disruptive_strike.png");
	EXPECT_TRUE(sylvanStatusArt::forRow("Unknown: something").image.empty());
}

namespace
{
Bonus noneMarker(const char * stacking, const char * icon = "")
{
	Bonus marker(BonusDuration::ONE_BATTLE, BonusType::NONE, BonusSource::HERO_SPECIAL, 50, BonusSourceID());
	marker.stacking = stacking;
	if(*icon)
		marker.customIconPath = ImagePath::builtin(icon);
	return marker;
}
}

TEST(CreatureStatusCardGrouping, BrittleKeepsItsOwnCardBesideOtherNoneMarkers)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA(); // the prayer markers resolve mod identifiers
	const auto brittle = noneMarker(newHorizonsRampart::BRITTLE_STACKING, "NH_status_brittle.png");
	const auto other = noneMarker("new-horizons:arcane-inspiration");
	EXPECT_TRUE(creatureStatusCardGrouping::ownsCard(brittle));
	EXPECT_FALSE(creatureStatusCardGrouping::sameCard(brittle, other));
	EXPECT_FALSE(creatureStatusCardGrouping::sameCard(other, brittle));
	EXPECT_TRUE(creatureStatusCardGrouping::sameCard(brittle, brittle)) << "Copies of one marker stay one card";
	// Prayer markers and plain NONE markers keep their shared group.
	const auto prayers = consecratedArms::prayerBonuses(consecratedArms::Prayer::DIVINE_PURPOSE, 200,
		consecratedArms::Policy{1}, 3, 1);
	const auto prayer = std::find_if(prayers.begin(), prayers.end(), [](const Bonus & bonus)
		{ return bonus.type == BonusType::NONE && consecratedArms::identity(&bonus); });
	ASSERT_NE(prayer, prayers.end());
	EXPECT_FALSE(creatureStatusCardGrouping::ownsCard(*prayer));
	EXPECT_FALSE(creatureStatusCardGrouping::sameCard(*prayer, brittle));
	EXPECT_TRUE(creatureStatusCardGrouping::sameCard(*prayer, other));
	// Other bonus types still group by type and subtype (Living Remedy joins Regeneration).
	Bonus remedy(BonusDuration::N_TURNS, BonusType::HP_REGENERATION, BonusSource::HERO_SPECIAL, 30, BonusSourceID());
	remedy.customIconPath = ImagePath::builtin("NH_status_living_remedy.png");
	const Bonus innate(BonusDuration::PERMANENT, BonusType::HP_REGENERATION, BonusSource::CREATURE_ABILITY, 50, BonusSourceID());
	EXPECT_TRUE(creatureStatusCardGrouping::sameCard(remedy, innate));
}
