/*
 * NewHorizonsLevelGrantAITest.cpp, part of VCMI engine
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

class NewHorizonsLevelGrantAITest : public HeroCommandFixture {};

TEST_F(NewHorizonsLevelGrantAITest, SpellbindersHatValuesMissingSpellsAtTheirSavedLevel)
{
	prepareCommands(true);
	attackerSideHero->removeAllSpells();
	const ArtifactID hatID(ArtifactID::decode("core:spellbindersHat"));
	const auto * hat = gameState()->createArtifact(hatID);
	std::vector<SpellID> granted;
	for(const auto spellID : LIBRARY->spellh->getDefaultAllowed())
		if(attackerSideHero->getSpellLevel(spellID.toSpell()) == 5)
			granted.push_back(spellID);
	ASSERT_FALSE(granted.empty());
	ASSERT_FALSE(attackerSideHero->canCastThisSpell(granted.front().toSpell()));
	const auto unknownScore = NK2AI::getArtifactScoreForHero(attackerSideHero, hat);
	giveArtifact(attackerSideHero, hatID, ArtifactPosition::HEAD);
	for(const auto spell : granted)
		EXPECT_TRUE(attackerSideHero->canCastThisSpell(spell.toSpell()));
	for(const auto spell : granted)
		attackerSideHero->addSpellToSpellbook(spell);
	const auto knownScore = NK2AI::getArtifactScoreForHero(attackerSideHero, hat);
	EXPECT_GT(unknownScore, knownScore);
	EXPECT_EQ(knownScore, 0);
	RecordProperty("activeSchools", static_cast<int>(gameState()->getActiveSpellSchools().size()));
	RecordProperty("unknownSpellScore", std::to_string(unknownScore));
	RecordProperty("allKnownSpellScore", std::to_string(knownScore));
}
