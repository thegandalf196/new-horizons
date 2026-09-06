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
#include "../../lib/GameLibrary.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/CSpellHandler.h"
#include "../../lib/entities/artifact/CArtifactInstance.h"

class NewHorizonsTomeAITest : public HeroCommandFixture,
	public ::testing::WithParamInterface<std::pair<std::string, SpellSchool>> {};

TEST_P(NewHorizonsTomeAITest, RealGrantedSpellsHaveValueUntilPermanentlyKnown)
{
	prepareCommands(true);
	attackerSideHero->removeAllSpells();
	const auto [name, school] = GetParam();
	SCOPED_TRACE(name);
	SCOPED_TRACE("active schools=" + std::to_string(gameState()->getActiveSpellSchools().size()));
	const ArtifactID tomeID(ArtifactID::decode(name));
	const auto * tome = gameState()->createArtifact(tomeID);
	ASSERT_NE(tome, nullptr);
	std::vector<SpellID> granted;
	for(const auto spellID : LIBRARY->spellh->getDefaultAllowed())
		if(spellID.toSpell()->hasSchool(school))
			granted.push_back(spellID);
	ASSERT_FALSE(granted.empty());
	ASSERT_FALSE(attackerSideHero->canCastThisSpell(granted.front().toSpell()));
	const auto unknownScore = NK2AI::getArtifactScoreForHero(attackerSideHero, tome);

	giveArtifact(attackerSideHero, tomeID, ArtifactPosition::MISC1);
	for(const auto spell : granted)
		EXPECT_TRUE(attackerSideHero->canCastThisSpell(spell.toSpell()));
	for(const auto spell : granted)
		attackerSideHero->addSpellToSpellbook(spell);
	const auto knownScore = NK2AI::getArtifactScoreForHero(attackerSideHero, tome);
	EXPECT_GT(unknownScore, knownScore);
	EXPECT_EQ(knownScore, 0);
	RecordProperty("activeSchools", static_cast<int>(gameState()->getActiveSpellSchools().size()));
	RecordProperty("unknownSpellScore", std::to_string(unknownScore));
	RecordProperty("allKnownSpellScore", std::to_string(knownScore));
}

INSTANTIATE_TEST_SUITE_P(AllOriginalTomes, NewHorizonsTomeAITest, ::testing::Values(
	std::make_pair(std::string("core:tomeOfAirMagic"), SpellSchool::AIR),
	std::make_pair(std::string("core:tomeOfFireMagic"), SpellSchool::FIRE),
	std::make_pair(std::string("core:tomeOfWaterMagic"), SpellSchool::WATER),
	std::make_pair(std::string("core:tomeOfEarthMagic"), SpellSchool::EARTH)));
