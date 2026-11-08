/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NativeRosterSpellLuaParityFixture.h"

namespace test
{
TEST_F(NativeRosterSpellLuaParity, ClonePreservesAddedCloneFlagsOriginalLinkAndLifetime)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	JsonNode config;
	config["maxTier"].Integer() = 7;
	ASSERT_NO_FATAL_FAILURE(compare("clone", "core:clone", config, source()));
}

TEST_F(NativeRosterSpellLuaParity, SummonPreservesPermanentAndTemporaryHealthBasedCreation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	for(const bool permanent : {false, true})
	{
		SCOPED_TRACE(permanent);
		JsonNode config;
		config["id"].String() = "core:airElemental";
		config["permanent"].Bool() = permanent;
		config["summonByHealth"].Bool() = true;
		ASSERT_NO_FATAL_FAILURE(compare("summon", "core:airElemental", config, destination()));
	}
}

TEST_F(NativeRosterSpellLuaParity, DemonSummonPreservesCorpseRetirementAndReplacementRoster)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto corpse = child->getForUpdate(braced->unitId());
	auto damage = corpse->getAvailableHealth();
	corpse->damage(damage);
	ASSERT_FALSE(corpse->alive());
	JsonNode config;
	config["id"].String() = "core:demon";
	config["permanent"].Bool() = true;
	ASSERT_NO_FATAL_FAILURE(compare("demonSummon", "core:summonDemons", config, source()));
}

TEST_F(NativeRosterSpellLuaParity, LivingArmorPreservesCapturedDurationAndFractionalFinalCreature)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	JsonNode config;
	config["id"].String() = "new-horizons:livingArmor";
	ASSERT_NO_FATAL_FAILURE(compare("timedSummon", "new-horizons:summonLivingArmor", config, destination()));
}

TEST_F(NativeRosterSpellLuaParity, TrollSummonPreservesNatureProvenanceAndAggregateHealthTrim)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	JsonNode config;
	config["id"].String() = "core:troll";
	ASSERT_NO_FATAL_FAILURE(compare("summonTrolls", "new-horizons:summonTrolls", config, destination()));
}

TEST_F(NativeRosterSpellLuaParity, ElementalConvergencePreservesTerrainChoiceAndAggregateHealthTrim)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(compare("elementalConvergence", "new-horizons:elementalConvergence", JsonNode(), destination()));
}

TEST_F(NativeRosterSpellLuaParity, PhantomArmyPreservesIntegrityDurationAndResolvedSourceReceipt)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(compare("phantomArmy", "new-horizons:phantomArmy", JsonNode(), source(), {braced->unitId()}));
}

TEST_F(NativeRosterSpellLuaParity, VerdantPrisonPreservesOrderedRingSharesAndPartialCreatureHealth)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto target = incoming->unitId();
	child->getForUpdate(target)->position = BattleHex(94);
	JsonNode config;
	config["id"].String() = "core:dendroidGuard";
	ASSERT_NO_FATAL_FAILURE(compare("verdantPrison", "new-horizons:verdantPrison", config,
		[target](const HypotheticBattle & battle) { return spells::Target{spells::Destination(battle.battleGetUnitByID(target))}; }));
}
}
