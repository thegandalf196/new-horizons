/*
 * NewHorizonsGuildLevelZeroTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/NewHorizonsSpellAvailability.h"

// Independent future guard test: NONE-only tests cannot discriminate the level
// check, because roster admission already rejects NONE before that check.
class NewHorizonsGuildLevelZeroTest : public HeroCommandFixture
{
protected:
	SpellID ability;
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		ability = SpellID(SpellID::decode("core:deathStare"));
		ASSERT_NE(ability, SpellID(SpellID::NONE));
		ASSERT_FALSE(ability.toSpell()->isCommonHeroSpell());
		ASSERT_EQ(ability.toSpell()->getLevel(), 0);
		ASSERT_TRUE(newHorizonsMagic::spellAllowedBySavedRoster(JsonNode(), ability));
	}
	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		for(auto * town : map->getObjects<CGTownInstance>())
		{
			town->obligatorySpells = {ability, SpellID(SpellID::MAGIC_ARROW)};
			town->possibleSpells.clear();
		}
	}
};

TEST_F(NewHorizonsGuildLevelZeroTest, AdmittedLevelZeroAbilityCannotIndexMandatoryGuildBuckets)
{
	startGame(true);
	ASSERT_TRUE(newHorizonsMagic::spellAllowedByWorldRoster(*gameState(), ability));
	ASSERT_EQ(gameState()->getSpellLevel(ability), 0);
	ASSERT_FALSE(gameState()->getMap().getAllTowns().empty());
	for(const auto townId : gameState()->getMap().getAllTowns())
	{
		const auto * town = gameState()->getTown(townId);
		bool foundArrow = false;
		for(const auto & level : town->spells)
		{
			EXPECT_FALSE(vstd::contains(level, ability));
			foundArrow |= vstd::contains(level, SpellID(SpellID::MAGIC_ARROW));
		}
		EXPECT_TRUE(foundArrow);
	}
}
