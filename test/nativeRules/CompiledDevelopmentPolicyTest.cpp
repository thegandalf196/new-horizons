/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/nativeRules/CompiledDevelopmentRules.h"
#include "../../lib/entities/hero/NewHorizonsParalyzingGaze.h"
#include "../../lib/entities/hero/NewHorizonsSerpentsEmbrace.h"
#include "../../lib/entities/hero/NewHorizonsForestAmbush.h"
#include "../../lib/entities/hero/NewHorizonsAerialSupremacy.h"
#include "../../lib/entities/hero/NewHorizonsDreadCavalry.h"
#include "../../lib/entities/hero/NewHorizonsManaLeeches.h"
#include "../../lib/entities/hero/NewHorizonsGhostSpecialty.h"
#include "../../lib/entities/hero/NewHorizonsGenieSage.h"
#include "../../lib/entities/hero/NewHorizonsChosenSpecialties.h"
#include "../../lib/pathfinder/NewHorizonsMovement.h"
#include "../../lib/filesystem/ResourcePath.h"
TEST(CompiledDevelopmentPolicy, NumericalSpecialtiesBooleansMovementAndChosenIdentityMatchRaw)
{
	const JsonNode world(JsonPath::builtin("config/newHorizonsHeroes"));
	const auto current = newHorizonsHeroes::resolveHeroRules(world, HeroClassID(HeroClassID::decode("core:knight")));
	for(const auto & raw : {JsonNode(), current})
	{
		const nativeRules::CompiledDevelopmentRules typed(raw);
		EXPECT_EQ(typed.paralyzingGaze, newHorizonsHeroes::paralyzingGazePolicy(raw));
		EXPECT_EQ(typed.serpentsEmbrace, newHorizonsHeroes::serpentsEmbracePolicy(raw));
		EXPECT_EQ(typed.forestAmbush, newHorizonsHeroes::forestAmbushPolicy(raw));
		EXPECT_EQ(typed.aerialSupremacy, newHorizonsHeroes::aerialSupremacyPolicy(raw));
		EXPECT_EQ(typed.dreadCavalry, newHorizonsHeroes::dreadCavalryDamagePercent(raw));
		EXPECT_EQ(typed.manaLeeches, newHorizonsHeroes::manaLeechesAmount(raw));
		EXPECT_EQ(typed.ghostUpgrade, newHorizonsHeroes::usesGhostUpgrade(raw));
		EXPECT_EQ(typed.genieSage, newHorizonsHeroes::usesGenieSage(raw));
		const auto living = newHorizonsHeroes::livingInfernoRules(raw);
		ASSERT_EQ(typed.livingInferno.has_value(), living.has_value());
		if(living)
		{
			EXPECT_EQ(typed.livingInferno->damagePerCreature, living->damagePerCreature);
			EXPECT_EQ(typed.livingInferno->durationRounds, living->durationRounds);
		}
		const auto surgeon = newHorizonsHeroes::herbalSurgeonRules(raw);
		ASSERT_EQ(typed.herbalSurgeon.has_value(), surgeon.has_value());
		if(surgeon)
		{
			EXPECT_EQ(typed.herbalSurgeon->tentPercentPerLevel, surgeon->tentPercentPerLevel);
			EXPECT_EQ(typed.herbalSurgeon->tentMaximumPercent, surgeon->tentMaximumPercent);
			EXPECT_EQ(typed.herbalSurgeon->regenerationPercentPerLevel, surgeon->regenerationPercentPerLevel);
			EXPECT_EQ(typed.herbalSurgeon->regenerationMaximumPercent, surgeon->regenerationMaximumPercent);
		}
		for(uint32_t level : {0u, 1u, 2u, 50u}) EXPECT_EQ(typed.levelMovementBonus(level), newHorizonsMovement::levelMovementBonus(raw, level));
		for(const auto * key : {"core:inteus", "core:loynis", "core:halon"})
			for(const auto spell : {SpellID(SpellID::NONE), SpellID(SpellID::HASTE), SpellID(SpellID::decode("new-horizons:theChosen"))})
			{
				const HeroTypeID hero(HeroTypeID::decode(key));
				EXPECT_EQ(typed.chosenDurationBonus(hero, spell), newHorizonsHeroes::chosenSpecialtyDurationBonus(raw, key, spell));
				EXPECT_EQ(typed.chosenManaDiscount(hero, spell), newHorizonsHeroes::chosenSpecialtyManaDiscount(raw, key, spell));
			}
	}
}
