/*
 * NewHorizonsTomeAITest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/Nullkiller2/AIUtility.h"
#include "../../lib/GameConstants.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/GameSettings.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/CSpellHandler.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/NewHorizonsSpellAvailability.h"
#include "../../lib/entities/artifact/CArtifactInstance.h"

namespace
{
using TomeTestCase = std::pair<std::string, SpellSchool>;

bool hasNewHorizonsModule()
{
	return vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE);
}

std::optional<SpellID> rankFreeSpellForLegacySchool(CGHeroInstance * hero, SpellSchool school)
{
	for(const auto spellID : LIBRARY->spellh->getDefaultAllowed())
	{
		const auto * spell = spellID.toSpell();
		const auto level = hero->getSpellLevel(spell);
		if(!spell->hasSchool(school) || level < 1 || level > 2)
			continue;
		if(!newHorizonsMagic::spellAllowedBySavedRoster(hero->getMagicRules(), spellID)
			|| !newHorizonsMagic::hasSchoolProficiency(hero, spellID))
			continue;

		const auto activeSchools = hero->getSpellSchools(spell);
		if(!activeSchools.empty() && !vstd::contains(activeSchools, school))
			return spellID;
	}
	return std::nullopt;
}

}

class NewHorizonsTomeAITest : public HeroCommandFixture,
	public ::testing::WithParamInterface<TomeTestCase>
{
protected:
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!hasNewHorizonsModule())
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}
};

TEST_P(NewHorizonsTomeAITest, LegacyTomeDoesNotGrantUnknownNewHorizonsSpell)
{
	prepareCommands(true);
	attackerSideHero->removeAllSpells();
	const auto [name, school] = GetParam();
	SCOPED_TRACE(name);
	ASSERT_FALSE(vstd::contains(gameState()->getActiveSpellSchools(), school));
	const auto spellID = rankFreeSpellForLegacySchool(attackerSideHero, school);
	ASSERT_TRUE(spellID.has_value()) << "No active New Horizons level 1/2 spell uses this legacy school";
	const auto * spell = spellID->toSpell();
	ASSERT_TRUE(attackerSideHero->getSourcesForSpell(*spellID).empty());
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(spell));

	const ArtifactID tomeID(ArtifactID::decode(name));
	const auto * tome = gameState()->createArtifact(tomeID);
	ASSERT_NE(tome, nullptr);
	giveArtifact(attackerSideHero, tomeID, ArtifactPosition::MISC1);
	EXPECT_TRUE(attackerSideHero->getSourcesForSpell(*spellID).empty());
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(spell));
	EXPECT_EQ(NK2AI::getArtifactScoreForHero(attackerSideHero, tome), 0);

	attackerSideHero->addSpellToSpellbook(*spellID);
	EXPECT_TRUE(attackerSideHero->canCastThisSpell(spell));
	const auto knownScore = NK2AI::getArtifactScoreForHero(attackerSideHero, tome);
	EXPECT_EQ(knownScore, 0);
	RecordProperty("allKnownSpellScore", std::to_string(knownScore));
}

INSTANTIATE_TEST_SUITE_P(AllOriginalTomes, NewHorizonsTomeAITest, ::testing::Values(
	std::make_pair(std::string("core:tomeOfAirMagic"), SpellSchool::AIR),
	std::make_pair(std::string("core:tomeOfFireMagic"), SpellSchool::FIRE),
	std::make_pair(std::string("core:tomeOfWaterMagic"), SpellSchool::WATER),
	std::make_pair(std::string("core:tomeOfEarthMagic"), SpellSchool::EARTH)));

class LegacyTomeCompatibilityTest : public HeroCommandFixture,
	public ::testing::WithParamInterface<TomeTestCase>
{
protected:
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!hasNewHorizonsModule())
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode());
	}
};

TEST_P(LegacyTomeCompatibilityTest, LegacyTomeStillGrantsOriginalSchool)
{
	prepareCommands(true);
	attackerSideHero->removeAllSpells();
	const auto [name, school] = GetParam();
	SCOPED_TRACE(name);
	std::vector<SpellID> schoolSpells;
	for(const auto spellID : LIBRARY->spellh->getDefaultAllowed())
		if(spellID.toSpell()->hasSchool(school))
			schoolSpells.push_back(spellID);
	ASSERT_FALSE(schoolSpells.empty()) << "No legacy spells use this school";
	ASSERT_TRUE(attackerSideHero->getSourcesForSpell(schoolSpells.front()).empty());
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(schoolSpells.front().toSpell()));

	const ArtifactID tomeID(ArtifactID::decode(name));
	const auto * tome = gameState()->createArtifact(tomeID);
	ASSERT_NE(tome, nullptr);
	giveArtifact(attackerSideHero, tomeID, ArtifactPosition::MISC1);
	for(const auto spellID : schoolSpells)
	{
		EXPECT_FALSE(attackerSideHero->getSourcesForSpell(spellID).empty());
		EXPECT_TRUE(attackerSideHero->canCastThisSpell(spellID.toSpell()));
	}
	const auto unknownScore = NK2AI::getArtifactScoreForHero(attackerSideHero, tome);
	EXPECT_GT(unknownScore, 0);

	for(const auto spellID : schoolSpells)
		attackerSideHero->addSpellToSpellbook(spellID);
	for(const auto spellID : schoolSpells)
		EXPECT_TRUE(attackerSideHero->canCastThisSpell(spellID.toSpell()));
	EXPECT_EQ(NK2AI::getArtifactScoreForHero(attackerSideHero, tome), 0);
}

INSTANTIATE_TEST_SUITE_P(AllOriginalTomes, LegacyTomeCompatibilityTest, ::testing::Values(
	std::make_pair(std::string("core:tomeOfAirMagic"), SpellSchool::AIR),
	std::make_pair(std::string("core:tomeOfFireMagic"), SpellSchool::FIRE),
	std::make_pair(std::string("core:tomeOfWaterMagic"), SpellSchool::WATER),
	std::make_pair(std::string("core:tomeOfEarthMagic"), SpellSchool::EARTH)));
