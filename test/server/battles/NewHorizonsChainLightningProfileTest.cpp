/*
 * NewHorizonsChainLightningProfileTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../SpellPointTestUtils.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"

namespace
{
JsonNode magicRulesForVersion(int version)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	if(version == newHorizonsMagic::CURRENT_RULESET_VERSION)
		return rules;

	rules["rulesetVersion"].Integer() = version;
	rules.Struct().erase("schoolRankPowerCoefficientPercent");
	rules.Struct().erase("spellcraftEfficiencyPercent");
	if(version == newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION)
	{
		newHorizonsMagic::validateRules(rules);
		return rules;
	}

	rules.Struct().erase("spellPoints");
	rules.Struct().erase("mageGuildGeneration");
	rules.Struct().erase("physicalDamageReductionCapPercent");
	rules.Struct().erase("warcasting");
	auto & spells = rules["spells"].Struct();
	for(auto it = spells.begin(); it != spells.end();)
	{
		if(it->first.starts_with(GameConstants::NEW_HORIZONS_MOD_SCOPE + ':'))
			it = spells.erase(it);
		else
			++it;
	}
	for(auto & [factionId, faction] : rules["factions"].Struct())
	{
		(void)factionId;
		faction["major"] = faction["preferredA"];
		faction["minor"] = faction["preferredB"];
		faction.Struct().erase("preferredA");
		faction.Struct().erase("preferredB");
	}
	for(auto & [name, spell] : rules["spells"].Struct())
	{
		(void)name;
		spell.Struct().erase("active");
		spell.Struct().erase("directDamage");
		spell.Struct().erase("cureAfflictions");
	}
	newHorizonsMagic::validateRules(rules);
	return rules;
}
}

class NewHorizonsChainLightningProfileTest : public HeroCommandFixture
{
protected:
	int magicVersion = newHorizonsMagic::CURRENT_RULESET_VERSION;
	const CSpell * chainLightning = nullptr;
	std::vector<CStack *> enemyTargets;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magicRulesForVersion(magicVersion));
	}

	void prepare()
	{
		startGame();
		chainLightning = SpellID(SpellID::CHAIN_LIGHTNING).toSpell();
		ASSERT_NE(chainLightning, nullptr);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(SpellID(SpellID::CHAIN_LIGHTNING));
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 999);
		startBattle();

		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);

		ASSERT_NE(addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(2, 5), 1), nullptr);
		for(int x = 12; x <= 16; ++x)
			enemyTargets.push_back(addStack(BattleSide::DEFENDER,
				creatureByName("core:peasant"), BattleHex(x, 5), 10000));
		beginCombat();

		ASSERT_EQ(enemyTargets.size(), newHorizonsMagic::CHAIN_LIGHTNING_FIXED_TARGET_COUNT_V3);
		for(const auto * target : enemyTargets)
			ASSERT_NE(target, nullptr);
		ASSERT_EQ(battle()->getMagicRules()["rulesetVersion"].Integer(), magicVersion);
	}

	void expectTargetCounts(const std::array<int, 4> & expected)
	{
		spells::Target aim;
		aim.emplace_back(enemyTargets.front());
		for(int rank = MasteryLevel::NONE; rank <= MasteryLevel::EXPERT; ++rank)
		{
			spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, chainLightning);
			cast.setSpellLevel(rank);
			const auto mechanics = chainLightning->battleMechanics(&cast);
			EXPECT_EQ(mechanics->getAffectedStacks(aim).size(), static_cast<size_t>(expected[rank]))
				<< "mastery rank " << rank;
		}
	}

	void expectAuthoritativeCastAffects(int expectedTargets)
	{
		std::vector<int64_t> healthBefore;
		for(const auto * target : enemyTargets)
			healthBefore.push_back(target->getAvailableHealth());

		spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, chainLightning);
		cast.setSpellLevel(MasteryLevel::NONE);
		spells::Target aim;
		aim.emplace_back(enemyTargets.front());
		cast.cast(gameHandler->spellEnv.get(), aim);

		int damagedTargets = 0;
		for(size_t index = 0; index < enemyTargets.size(); ++index)
			if(enemyTargets[index]->getAvailableHealth() < healthBefore[index])
				++damagedTargets;
		EXPECT_EQ(damagedTargets, expectedTargets);
	}
};

TEST_F(NewHorizonsChainLightningProfileTest, V1PreservesRankDependentPreviewAndServerCast)
{
	magicVersion = newHorizonsMagic::RULESET_VERSION;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_EQ(newHorizonsMagic::spellDescriptionForHero(attackerSideHero, chainLightning, MasteryLevel::BASIC)
		.find("up to five different stacks at every mastery rank"), std::string::npos);
	expectTargetCounts({4, 4, 5, 5});
	expectAuthoritativeCastAffects(4);
}

TEST_F(NewHorizonsChainLightningProfileTest, V2PreservesRankDependentPreviewAndServerCast)
{
	magicVersion = newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_EQ(newHorizonsMagic::spellDescriptionForHero(attackerSideHero, chainLightning, MasteryLevel::BASIC)
		.find("up to five different stacks at every mastery rank"), std::string::npos);
	expectTargetCounts({4, 4, 5, 5});
	expectAuthoritativeCastAffects(4);
}

TEST_F(NewHorizonsChainLightningProfileTest, V3UsesFiveTargetsForEveryMasteryInPreviewAndServerCast)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto description = newHorizonsMagic::spellDescriptionForHero(
		attackerSideHero, chainLightning, MasteryLevel::BASIC);
	EXPECT_NE(description.find("up to five different stacks at every mastery rank"), std::string::npos);
	EXPECT_NE(description.find("including friendly stacks"), std::string::npos);
	expectTargetCounts({5, 5, 5, 5});
	expectAuthoritativeCastAffects(5);
}
