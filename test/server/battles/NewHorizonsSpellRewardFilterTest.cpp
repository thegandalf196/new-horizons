/*
 * NewHorizonsSpellRewardFilterTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/json/JsonKeyExtractor.h"
#include "../../../lib/modding/ModScope.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/CSpellHandler.h"
#include "../../../lib/spells/SpellSchoolHandler.h"

class NewHorizonsSpellRewardFilterTest : public HeroCommandFixture {};

TEST_F(NewHorizonsSpellRewardFilterTest, RewardLevelsUseSavedClassificationWithoutChangingGlobalSpells)
{
	prepareCommands(true);
	JsonKeyExtractor extractor(gameState().get());
	const auto allowed = LIBRARY->spellh->getDefaultAllowed();
	int changes = 0;
	for(const auto spell : allowed)
		changes += gameState()->getSpellLevel(spell) != spell.toSpell()->getLevel();
	if(!gameState()->getMagicRules().isNull() && !gameState()->getMagicRules().Struct().empty())
		EXPECT_GT(changes, 0) << "Curated fixture must exercise actual reassigned levels";
	for(int level = 1; level <= 5; ++level)
	{
		SCOPED_TRACE(level);
		JsonNode filter;
		filter["level"].Integer() = level;
		std::set<SpellID> expected;
		for(const auto spell : allowed)
			if(gameState()->getSpellLevel(spell) == level)
				expected.insert(spell);
		EXPECT_EQ(extractor.filterKeys(filter, allowed), expected);
	}
}

TEST_F(NewHorizonsSpellRewardFilterTest, RewardSchoolsSupportActiveSchoolsAndOriginalAuthoredAffinity)
{
	prepareCommands(true);
	JsonKeyExtractor extractor(gameState().get());
	const auto allowed = LIBRARY->spellh->getDefaultAllowed();
	const auto active = gameState()->getActiveSpellSchools();
	std::set<SpellSchool> schools(active.begin(), active.end());
	schools.insert({SpellSchool::AIR, SpellSchool::FIRE, SpellSchool::EARTH, SpellSchool::WATER});
	for(const auto school : schools)
	{
		SCOPED_TRACE(school.serializationKey());
		JsonNode filter;
		filter["school"].String() = school.serializationKey();
		// Original filters belong to core; new-school filters belong to their
		// defining mod. Core must not acquire dependencies on an optional mod.
		const auto owner = school.toEntity(LIBRARY)->getModScope();
		filter.setModScope(owner);
		const auto resolved = LIBRARY->identifiers()->getIdentifier("spellSchool", filter["school"], true);
		ASSERT_TRUE(resolved.has_value());
		ASSERT_EQ(*resolved, school.getNum());
		if(owner != ModScope::scopeBuiltin())
			EXPECT_FALSE(LIBRARY->identifiers()->getIdentifier(ModScope::scopeBuiltin(), "spellSchool", school.serializationKey(), true).has_value());
		std::set<SpellID> expected;
		for(const auto spell : allowed)
			if(spell.toSpell()->hasSchool(school) || vstd::contains(gameState()->getSpellSchools(spell), school))
				expected.insert(spell);
		ASSERT_FALSE(expected.empty());
		EXPECT_EQ(extractor.filterKeys(filter, allowed), expected);
	}
}
