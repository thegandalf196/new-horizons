/*
 * NewHorizonsChainLightningFalloffTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"
#include "../../SpellPointTestUtils.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/CRandomGenerator.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/effects/Effect.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

#include <vcmi/Environment.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <numeric>
#include <string>

namespace
{
constexpr auto chainLightningKey = "core:chainLightning";
constexpr std::array<int32_t, 5> v3RetentionPercent = {100, 70, 50, 35, 25};
constexpr std::array<int64_t, 5> v3DamageFrom200 = {200, 140, 100, 70, 50};
constexpr std::array<int64_t, 5> v3DamageFrom90 = {90, 63, 45, 31, 22};

JsonNode magicRulesForVersion(const int version, const int64_t initialDamage)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	rules["spells"][chainLightningKey]["directDamage"]["base"].Integer() = initialDamage;
	rules["spells"][chainLightningKey]["directDamage"]["powerCoefficient"].Integer() = 0;
	if(version == newHorizonsMagic::CURRENT_RULESET_VERSION)
		return rules;

	rules["rulesetVersion"].Integer() = version;
	rules.Struct().erase("schoolRankPowerCoefficientPercent");
	rules.Struct().erase("spellcraftEfficiencyPercent");
	for(auto & [spellId, spell] : rules["spells"].Struct())
	{
		(void)spellId;
		spell.Struct().erase("heroAccess");
		spell.Struct().erase("restoration");
		spell.Struct().erase("selectedPlacement");
		spell.Struct().erase("earthquake");
		spell.Struct().erase("structures");
		if(spell.Struct().contains("variant"))
		{
			spell.Struct().erase("variant");
			spell["active"].Bool() = false;
		}
	}
	if(version == newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION)
	{
		newHorizonsMagic::validateRules(rules);
		return rules;
	}

	rules.Struct().erase("spellPoints");
	rules.Struct().erase("mageGuildGeneration");
	rules.Struct().erase("physicalDamageReductionCapPercent");
	rules.Struct().erase("warcasting");
	for(auto & [factionId, faction] : rules["factions"].Struct())
	{
		(void)factionId;
		faction["major"] = faction["preferredA"];
		faction["minor"] = faction["preferredB"];
		faction.Struct().erase("preferredA");
		faction.Struct().erase("preferredB");
	}
	for(auto & [spellId, spell] : rules["spells"].Struct())
	{
		(void)spellId;
		spell.Struct().erase("active");
		spell.Struct().erase("directDamage");
		spell.Struct().erase("cureAfflictions");
	}
	for(auto it = rules["spells"].Struct().begin(); it != rules["spells"].Struct().end();)
	{
		if(it->first.starts_with(GameConstants::NEW_HORIZONS_MOD_SCOPE + ':'))
			it = rules["spells"].Struct().erase(it);
		else
			++it;
	}
	rules.setModScope(GameConstants::NEW_HORIZONS_MOD_SCOPE);
	newHorizonsMagic::validateRules(rules);
	return rules;
}

class ChainLightningEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit ChainLightningEnvironment(std::shared_ptr<CGameState> value)
		: state(std::move(value))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

struct PrefixForecast
{
	std::vector<int64_t> damageByRecipient;
	std::vector<uint32_t> recipientIds;
	spells::effects::SpellEffectValue full;
	int64_t initialDamage = 0;
	int32_t effectCount = 0;
};
}

class NewHorizonsChainLightningFalloffTest : public HeroCommandFixture
{
protected:
	int magicVersion = newHorizonsMagic::CURRENT_RULESET_VERSION;
	int64_t initialDamage = 200;
	const CSpell * chainLightning = nullptr;
	CStack * attackerStack = nullptr;
	std::vector<CStack *> recipients;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			magicRulesForVersion(magicVersion, initialDamage));
	}

	void prepare()
	{
		startGame();
		chainLightning = SpellID(SpellID::decode(std::string(chainLightningKey))).toSpell();
		ASSERT_NE(chainLightning, nullptr);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(chainLightning->getId());
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 0, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);

		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		ASSERT_FALSE(remove.changedStacks.empty());
		gameHandler->sendAndApply(remove);

		attackerStack = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(2, 5), 1);
		ASSERT_NE(attackerStack, nullptr);
		for(int x = 10; x < 15; ++x)
			recipients.push_back(addStack(BattleSide::DEFENDER,
				creatureByName("core:peasant"), BattleHex(x, 5), 1000));
		ASSERT_EQ(recipients.size(), v3RetentionPercent.size());
		for(const auto * recipient : recipients)
			ASSERT_NE(recipient, nullptr);

		beginCombat();
		activateAttackerStack();
		ASSERT_EQ(battle()->getMagicRules()["rulesetVersion"].Integer(), magicVersion);
	}

	void activateAttackerStack()
	{
		BattleSetActiveStack activate;
		activate.battleID = BattleID(0);
		activate.stack = attackerStack->unitId();
		activate.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activate);
	}

	std::vector<std::byte> randomizerSnapshot() const
	{
		CMemorySerializer memory;
		memory.oser & *gameHandler->randomizer;
		return memory.extractBuffer();
	}

	PrefixForecast forecastPrefixes()
	{
		PrefixForecast result;
		spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, chainLightning);
		const auto mechanics = chainLightning->battleMechanics(&cast);
		result.initialDamage = mechanics->getEffectValue();
		spells::Target aim;
		aim.emplace_back(recipients.front());
		const auto canonicalAim = mechanics->canonicalizeTarget(aim);

		mechanics->forEachEffect([&](const spells::effects::Effect & effect)
		{
			++result.effectCount;
			const auto transformed = effect.transformTarget(mechanics.get(), aim, canonicalAim);
			result.damageByRecipient.resize(std::max(result.damageByRecipient.size(), transformed.size()));
			if(result.recipientIds.empty())
				for(const auto & destination : transformed)
					if(destination.unitValue)
						result.recipientIds.push_back(destination.unitValue->unitId());

			spells::Target prefix;
			spells::effects::SpellEffectValue previous;
			for(size_t index = 0; index < transformed.size(); ++index)
			{
				prefix.push_back(transformed[index]);
				const auto current = effect.getHealthChange(mechanics.get(), prefix);
				result.damageByRecipient[index] += previous.hpDelta - current.hpDelta;
				previous = current;
			}
			result.full += previous;
			return false;
		});
		return result;
	}

	void expectAcceptedCastMatchesForecast(const std::vector<int64_t> & expectedDamage,
		const bool checkV3RetentionApi)
	{
		ASSERT_EQ(expectedDamage.size(), recipients.size());
		const auto expectedPathLength = static_cast<size_t>(std::count_if(expectedDamage.begin(), expectedDamage.end(),
			[](const int64_t damage) { return damage > 0; }));
		const auto liveHealthBefore = [&]
		{
			std::vector<int64_t> result;
			for(const auto * recipient : recipients)
				result.push_back(recipient->getAvailableHealth());
			return result;
		}();
		const auto liveManaBefore = attackerSideHero->getManaAvailable();
		const auto liveActionCountsBefore = battle()->getHeroActionAllowances(BattleSide::ATTACKER)
			.remainingCounts(battle()->getRound());
		const auto rngBefore = randomizerSnapshot();

		const auto prefixForecast = forecastPrefixes();
		ASSERT_EQ(prefixForecast.damageByRecipient.size(), expectedPathLength);
		ASSERT_EQ(prefixForecast.recipientIds.size(), expectedPathLength);
		ASSERT_EQ(prefixForecast.effectCount, 1);
		EXPECT_EQ(prefixForecast.initialDamage, expectedDamage.front());
		EXPECT_EQ(prefixForecast.full.hpDelta,
			-std::accumulate(expectedDamage.begin(), expectedDamage.end(), int64_t{0}));
		for(size_t index = 0; index < expectedPathLength; ++index)
		{
			EXPECT_EQ(prefixForecast.recipientIds[index], recipients[index]->unitId())
				<< "the deterministic path reaches recipient " << index;
			EXPECT_EQ(prefixForecast.damageByRecipient[index], expectedDamage[index])
				<< "the difference between cumulative prefixes is hop " << index;
		}

		if(checkV3RetentionApi)
		{
			spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, chainLightning);
			const auto mechanics = chainLightning->battleMechanics(&cast);
			for(size_t index = 0; index < v3RetentionPercent.size(); ++index)
				EXPECT_EQ(mechanics->getNewHorizonsChainLightningRetentionPercent(static_cast<int32_t>(index)),
					v3RetentionPercent[index]);
			EXPECT_EQ(mechanics->getNewHorizonsChainLightningRetentionPercent(-1), -1);
			EXPECT_EQ(mechanics->getNewHorizonsChainLightningRetentionPercent(5), -1);

			const auto * unrelatedSpell = SpellID(SpellID::MAGIC_ARROW).toSpell();
			ASSERT_NE(unrelatedSpell, nullptr);
			spells::BattleCast unrelatedCast(battle(), attackerSideHero, spells::Mode::HERO, unrelatedSpell);
			const auto unrelatedMechanics = unrelatedSpell->battleMechanics(&unrelatedCast);
			ASSERT_NE(unrelatedMechanics, nullptr);
			EXPECT_EQ(unrelatedMechanics->getNewHorizonsChainLightningRetentionPercent(0), -1);
		}

		auto callback = std::make_shared<CPlayerBattleCallback>(battle(), attackerSideHero->getOwner());
		ChainLightningEnvironment environment(gameState());
		HypotheticBattle projected(&environment, callback);
		const auto * projectedPrimary = projected.battleGetUnitByID(recipients.front()->unitId());
		ASSERT_NE(projectedPrimary, nullptr);
		spells::Target projectedAim;
		projectedAim.emplace_back(projectedPrimary);
		spells::BattleCast projectedCast(&projected, attackerSideHero, spells::Mode::HERO, chainLightning);
		projectedCast.castEval(projected.getServerCallback(), projectedAim);
		for(size_t index = 0; index < recipients.size(); ++index)
		{
			const auto * projectedRecipient = projected.battleGetUnitByID(recipients[index]->unitId());
			ASSERT_NE(projectedRecipient, nullptr);
			EXPECT_EQ(liveHealthBefore[index] - projectedRecipient->getAvailableHealth(), expectedDamage[index])
				<< "detached full-cast forecast recipient " << index;
			EXPECT_EQ(recipients[index]->getAvailableHealth(), liveHealthBefore[index])
				<< "projection leaves live recipient " << index << " untouched";
		}
		EXPECT_EQ(attackerSideHero->getManaAvailable(), liveManaBefore)
			<< "a readonly spell forecast does not spend Mana";
		EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER)
			.remainingCounts(battle()->getRound()), liveActionCountsBefore)
			<< "a readonly spell forecast does not consume the Hero Action";
		EXPECT_EQ(randomizerSnapshot(), rngBefore)
			<< "prefix and detached forecasts do not advance authoritative combat RNG";

		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = chainLightning->getId();
		action.aimToUnit(recipients.front());
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
			attackerSideHero->getOwner(), action)) << "the ordinary shared Hero Spell path accepts Chain Lightning";

		for(size_t index = 0; index < recipients.size(); ++index)
			EXPECT_EQ(liveHealthBefore[index] - recipients[index]->getAvailableHealth(), expectedDamage[index])
				<< "authoritative recipient " << index;
		const int32_t acceptedCost = battle()->battleGetSpellCost(chainLightning, attackerSideHero);
		EXPECT_EQ(liveManaBefore - attackerSideHero->getManaAvailable(), acceptedCost);
		EXPECT_EQ(liveActionCountsBefore.heroActions,
			battle()->getHeroActionAllowances(BattleSide::ATTACKER)
				.remainingCounts(battle()->getRound()).heroActions + 1);
		EXPECT_EQ(server.castsOf(chainLightning->getId()).size(), 1u);
	}
};

TEST_F(NewHorizonsChainLightningFalloffTest,
	SavedV3OrdinaryFalloffMatchesPrefixForecastDetachedSimulationAndAcceptedCast)
{
	magicVersion = newHorizonsMagic::CURRENT_RULESET_VERSION;
	initialDamage = 200;
	ASSERT_NO_FATAL_FAILURE(prepare());
	expectAcceptedCastMatchesForecast(
		std::vector<int64_t>(v3DamageFrom200.begin(), v3DamageFrom200.end()), true);
}

TEST_F(NewHorizonsChainLightningFalloffTest, SavedV3UsesIntegerRetentionForNonDivisibleBaseDamage)
{
	magicVersion = newHorizonsMagic::CURRENT_RULESET_VERSION;
	initialDamage = 90;
	ASSERT_NO_FATAL_FAILURE(prepare());
	expectAcceptedCastMatchesForecast(
		std::vector<int64_t>(v3DamageFrom90.begin(), v3DamageFrom90.end()), true);
}

TEST_F(NewHorizonsChainLightningFalloffTest, RetentionHelperRejectsInactiveAndOffRosterSavedProfiles)
{
	magicVersion = newHorizonsMagic::CURRENT_RULESET_VERSION;
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto chainId = chainLightning->getId();
	auto inactiveRules = battle()->getMagicRules();
	inactiveRules["spells"][chainLightningKey]["active"].Bool() = false;
	EXPECT_EQ(newHorizonsMagic::chainLightningRetentionPercent(inactiveRules, chainId, 0), -1);

	auto offRosterRules = battle()->getMagicRules();
	offRosterRules["spells"].Struct().erase(std::string(chainLightningKey));
	EXPECT_EQ(newHorizonsMagic::chainLightningRetentionPercent(offRosterRules, chainId, 0), -1);
}

TEST_F(NewHorizonsChainLightningFalloffTest, V1PreservesLegacyGeometricHalving)
{
	magicVersion = newHorizonsMagic::RULESET_VERSION;
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto forecast = forecastPrefixes();
	ASSERT_EQ(forecast.effectCount, 1);
	ASSERT_EQ(forecast.damageByRecipient.size(), 4u);
	ASSERT_GT(forecast.initialDamage, 0);
	for(size_t index = 0; index < forecast.damageByRecipient.size(); ++index)
	{
		auto expected = forecast.initialDamage;
		for(size_t hop = 0; hop < index; ++hop)
			expected /= 2;
		EXPECT_EQ(forecast.damageByRecipient[index], expected) << "v1 recipient " << index;
	}
	std::vector<int64_t> expected(forecast.damageByRecipient.begin(), forecast.damageByRecipient.end());
	expected.resize(recipients.size(), 0); // Legacy Chain Lightning visits only four stacks.
	expectAcceptedCastMatchesForecast(expected, false);
}

TEST_F(NewHorizonsChainLightningFalloffTest, V2PreservesLegacyGeometricHalving)
{
	magicVersion = newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION;
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto forecast = forecastPrefixes();
	ASSERT_EQ(forecast.effectCount, 1);
	ASSERT_EQ(forecast.damageByRecipient.size(), 4u);
	ASSERT_GT(forecast.initialDamage, 0);
	for(size_t index = 0; index < forecast.damageByRecipient.size(); ++index)
	{
		auto expected = forecast.initialDamage;
		for(size_t hop = 0; hop < index; ++hop)
			expected /= 2;
		EXPECT_EQ(forecast.damageByRecipient[index], expected) << "v2 recipient " << index;
	}
	std::vector<int64_t> expected(forecast.damageByRecipient.begin(), forecast.damageByRecipient.end());
	expected.resize(recipients.size(), 0); // Legacy Chain Lightning visits only four stacks.
	expectAcceptedCastMatchesForecast(expected, false);
}
