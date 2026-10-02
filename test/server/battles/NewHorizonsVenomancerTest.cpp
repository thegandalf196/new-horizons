/*
 * NewHorizonsVenomancerTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of the license available in license.txt file, in main folder
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"
#include "../../hero/NewHorizonsHeroRulesFixture.h"
#include "../../SpellPointTestUtils.h"

#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/NewHorizonsBulwark.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/NewHorizonsSpellAvailability.h"
#include "../../../lib/spells/Problem.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
#include <vcmi/Environment.h>

#include <algorithm>
#include <array>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace
{
constexpr std::string_view poisonSpellKey = newHorizonsMagic::NATURE_POISON_SPELL;
constexpr std::string_view natureMagicSkillKey = newHorizonsMagic::NATURE_MAGIC_SKILL;
constexpr std::string_view venomancerPerkKey = newHorizonsMagic::NATURE_VENOMANCER;
constexpr std::string_view spellcraftSkillKey = "new-horizons:spellcraft";
constexpr std::string_view spellPenetrationPerkKey = "new-horizons:spellcraft.spellPenetration";
constexpr std::string_view empowerSpellPerkKey = newHorizonsMagic::SPELLCRAFT_EMPOWER_SPELL;

SpellID poisonSpell()
{
	return SpellID(SpellID::decode(std::string(poisonSpellKey)));
}

bool activatePerkInFixture(JsonNode & rules, const std::string_view skillId,
	const std::string_view perkId)
{
	auto & perks = rules["skills"][std::string(skillId)]["perks"].Vector();
	const auto found = std::find_if(perks.begin(), perks.end(), [perkId](const JsonNode & perk)
	{
		return perk["id"].String() == perkId;
	});
	if(found == perks.end())
		return false;

	(*found)["effect"]["status"].String() = "active";
	return true;
}

JsonNode savedV2MagicRulesWithoutPoison()
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	rules["rulesetVersion"].Integer() = newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION;
	rules.Struct().erase("schoolRankPowerCoefficientPercent");
	rules.Struct().erase("spellcraftEfficiencyPercent");
	auto & spells = rules["spells"].Struct();
	for(auto it = spells.begin(); it != spells.end();)
	{
		if(std::string_view(it->first) == poisonSpellKey || it->second.Struct().contains("variant"))
			it = spells.erase(it);
		else
		{
			it->second.Struct().erase("selectedPlacement");
			it->second.Struct().erase("earthquake");
			it->second.Struct().erase("structures");
			++it;
		}
	}
	newHorizonsMagic::validateRules(rules);
	return rules;
}

class VenomancerPredictionEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit VenomancerPredictionEnvironment(std::shared_ptr<CGameState> value)
		: state(std::move(value))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsVenomancerTest : public HeroCommandFixture
{
protected:
	bool useSavedV2Rules = false;
	bool useEmpoweredPoison = false;
	CStack * casterStack = nullptr;
	CStack * poisonTarget = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
		ASSERT_NE(poisonSpell(), SpellID::NONE);
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, testHeroRules());

		JsonNode perkRules(JsonPath::builtin("config/newHorizonsPerks"));
		if(!activatePerkInFixture(perkRules, natureMagicSkillKey, venomancerPerkKey))
			throw std::runtime_error("Missing Venomancer from the New Horizons perk registry");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perkRules));

		JsonNode magicRules = useSavedV2Rules
			? savedV2MagicRulesWithoutPoison()
			: JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
		if(useEmpoweredPoison)
		{
			for(auto & cost : magicRules["spells"][std::string(poisonSpellKey)]["costs"].Vector())
				cost.Integer() = 12;
		}
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, std::move(magicRules));
	}

	SecondarySkill natureMagic() const
	{
		const int decoded = SecondarySkill::decode(std::string(natureMagicSkillKey));
		EXPECT_GE(decoded, 0);
		return SecondarySkill(decoded);
	}

	void acceptPerkThroughOffer(CGHeroInstance * hero, const std::string_view skillId,
		const std::string_view perkId, const int requiredRank)
	{
		const auto rankLookup = [hero](const std::string & candidateSkill)
		{
			return hero->getPerkSkillRank(candidateSkill);
		};

		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = hero->getPerkState().prepareOffer(rankLookup, seed);
			const auto selected = std::find_if(offers.begin(), offers.end(), [skillId, perkId](const auto & offer)
			{
				return offer.selection.skillId == skillId && offer.selection.perkId == perkId;
			});
			if(selected == offers.end())
				continue;

			ASSERT_EQ(selected->requiredRank, requiredRank);
			const auto choice = static_cast<size_t>(std::distance(offers.begin(), selected));
			gameHandler->levelUpHero(hero, offers, choice, seed, false);
			ASSERT_TRUE(hero->hasActivePerk(std::string(skillId), std::string(perkId)));
			return;
		}

		FAIL() << "No legal New Horizons perk offer contained " << perkId;
	}

	void prepare(const bool selectVenomancer, const bool selectEmpowerSpell = false,
		const bool includePoisonTarget = true)
	{
		startGame();
		attackerSideHero->setSecSkillLevel(natureMagic(), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->removeAllSpells();
		attackerSideHero->addSpellToSpellbook(poisonSpell());
		setTestSpellPointTotal(attackerSideHero, 1000);
		if(selectVenomancer)
			acceptPerkThroughOffer(attackerSideHero, natureMagicSkillKey,
				venomancerPerkKey, MasteryLevel::BASIC);

		if(selectEmpowerSpell)
		{
			const SecondarySkill spellcraft(SecondarySkill::decode(std::string(spellcraftSkillKey)));
			ASSERT_TRUE(spellcraft.hasValue());
			attackerSideHero->setSecSkillLevel(spellcraft, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
			acceptPerkThroughOffer(attackerSideHero, spellcraftSkillKey,
				spellPenetrationPerkKey, MasteryLevel::BASIC);
			attackerSideHero->setSecSkillLevel(spellcraft, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
			acceptPerkThroughOffer(attackerSideHero, spellcraftSkillKey,
				empowerSpellPerkKey, MasteryLevel::ADVANCED);
		}

		startBattle();
		removeDeployedUnits();
		casterStack = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 20);
		if(includePoisonTarget)
			poisonTarget = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(12, 5), 1000);
		beginCombat();
		ASSERT_NE(casterStack, nullptr);
		if(includePoisonTarget)
		{
			ASSERT_NE(poisonTarget, nullptr);
		}
		ASSERT_EQ(battle()->getMagicRules()["rulesetVersion"].Integer(), useSavedV2Rules
			? newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION
			: newHorizonsMagic::CURRENT_RULESET_VERSION);
		if(useSavedV2Rules)
		{
			EXPECT_FALSE(newHorizonsMagic::spellAllowedBySavedRoster(battle()->getMagicRules(), poisonSpell()));
			EXPECT_FALSE(newHorizonsMagic::physicalPoisonEnabled(battle()->getMagicRules(), poisonSpell()));
		}
		else
		{
			EXPECT_TRUE(newHorizonsMagic::spellAllowedBySavedRoster(battle()->getMagicRules(), poisonSpell()));
			EXPECT_TRUE(newHorizonsMagic::physicalPoisonEnabled(battle()->getMagicRules(), poisonSpell()));
		}
	}

	void removeDeployedUnits()
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		if(!remove.changedStacks.empty())
			gameHandler->sendAndApply(remove);
	}

	bool act(const CStack * stack, const BattleAction & action)
	{
		BattleSetActiveStack activation;
		activation.battleID = BattleID(0);
		activation.stack = stack->unitId();
		activation.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activation);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->battleGetOwner(stack), action);
	}

	bool castPoison(CStack * target)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = poisonSpell();
		action.aimToUnit(target);
		return act(casterStack, action);
	}

	bool advanceUntilPoisonActivation(CStack * stack, const int32_t remainingBefore)
	{
		const size_t maximumActions = battle()->stacks.size() * 8 + 8;
		for(size_t actionIndex = 0; actionIndex < maximumActions; ++actionIndex)
		{
			if(stack->physicalPoisonActivationsRemaining < remainingBefore)
				return true;

			const auto * active = battle()->battleActiveUnit();
			if(!active)
				return false;
			if(!gameHandler->battles->makePlayerBattleAction(BattleID(0),
				battle()->sideToPlayer(active->unitSide()), BattleAction::makeDefend(active)))
				return false;
		}
		return stack->physicalPoisonActivationsRemaining < remainingBefore;
	}
};
}

TEST_F(NewHorizonsVenomancerTest, ActiveBasicOfferSnapshotsTheWholeBaseBonusAndEscalatesForThreeActivations)
{
	prepare(true);
	ASSERT_TRUE(attackerSideHero->hasActivePerk(std::string(natureMagicSkillKey),
		std::string(venomancerPerkKey)));
	ASSERT_EQ(newHorizonsMagic::poisonBaseBonusPercent(attackerSideHero), 20);
	ASSERT_EQ(battle()->battleGetOwner(casterStack), PlayerColor(0));

	const auto initialHealth = poisonTarget->getAvailableHealth();
	const auto firstInjuryIndex = server.injuries.size();
	ASSERT_TRUE(castPoison(poisonTarget));
	// Nature Basic gives a normal integer Base of 77. Venomancer scales that
	// complete value once, flooring 77 * 1.2 to 92.
	ASSERT_EQ(poisonTarget->physicalPoisonBaseDamage, 92);
	ASSERT_EQ(poisonTarget->physicalPoisonActivationsRemaining, 3);
	constexpr std::array<int64_t, 3> expectedTicks{92, 138, 184};
	int64_t totalDamage = 0;
	for(size_t activation = 0; activation < expectedTicks.size(); ++activation)
	{
		ASSERT_TRUE(advanceUntilPoisonActivation(poisonTarget,
			static_cast<int32_t>(expectedTicks.size() - activation)));
		std::vector<int64_t> poisonHits;
		for(size_t injuryIndex = firstInjuryIndex; injuryIndex < server.injuries.size(); ++injuryIndex)
			for(const auto & hit : server.injuries[injuryIndex].stacks)
				if(hit.stackAttacked == poisonTarget->unitId())
					poisonHits.push_back(hit.damageAmount);
		ASSERT_EQ(poisonHits.size(), activation + 1);
		EXPECT_EQ(poisonHits.back(), expectedTicks[activation]);
		totalDamage += expectedTicks[activation];
		EXPECT_EQ(poisonTarget->getAvailableHealth(), initialHealth - totalDamage);
		EXPECT_EQ(poisonTarget->physicalPoisonActivationsRemaining,
			static_cast<int32_t>(expectedTicks.size() - activation - 1));
	}
	EXPECT_EQ(poisonTarget->physicalPoisonBaseDamage, 0);
}

TEST_F(NewHorizonsVenomancerTest, UnselectedPerkLeavesThePhysicalPoisonBaseUnchanged)
{
	prepare(false);
	ASSERT_FALSE(attackerSideHero->hasActivePerk(std::string(natureMagicSkillKey),
		std::string(venomancerPerkKey)));
	EXPECT_EQ(newHorizonsMagic::poisonBaseBonusPercent(attackerSideHero), 0);

	ASSERT_TRUE(castPoison(poisonTarget));
	EXPECT_EQ(poisonTarget->physicalPoisonBaseDamage, 77);
	EXPECT_EQ(poisonTarget->physicalPoisonActivationsRemaining, 3);
}

TEST_F(NewHorizonsVenomancerTest, SavedSchoolSpellcraftAndEmpowerScaleSpellPowerBeforeVenomancerScalesTheWholeBase)
{
	useEmpoweredPoison = true;
	prepare(true, true);
	ASSERT_EQ(newHorizonsMagic::spellPowerCoefficientBasisPoints(
		battle()->getMagicRules(), attackerSideHero, poisonSpell()), 13800);
	const auto * spell = poisonSpell().toSpell();
	ASSERT_NE(spell, nullptr);
	spells::BattleCast castEvent(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&castEvent);
	ASSERT_EQ(mechanics->getEmpowerSpellBonusPercent(), 25);

	ASSERT_TRUE(castPoison(poisonTarget));
	// floor(20 + floor(100 * 13800 * 1.25 / 20000)) = 106, then floor(106 * 1.2) = 127.
	EXPECT_EQ(poisonTarget->physicalPoisonBaseDamage, 127);
	EXPECT_EQ(poisonTarget->physicalPoisonActivationsRemaining, 3);
}

TEST_F(NewHorizonsVenomancerTest, SavedV2RosterCannotAcquireTheV3PoisonOrVenomancerEffect)
{
	useSavedV2Rules = true;
	prepare(true);
	EXPECT_TRUE(attackerSideHero->hasActivePerk(std::string(natureMagicSkillKey),
		std::string(venomancerPerkKey)));
	const auto manaBefore = attackerSideHero->getManaAvailable();

	EXPECT_FALSE(castPoison(poisonTarget));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(poisonTarget->physicalPoisonBaseDamage, 0);
	EXPECT_EQ(poisonTarget->physicalPoisonActivationsRemaining, 0);
}

TEST_F(NewHorizonsVenomancerTest, DetachedCastEvalPoisonsAnAlreadyMaterializedProjectedUnitOnly)
{
	prepare(true);
	ASSERT_EQ(poisonTarget->physicalPoisonBaseDamage, 0);
	const auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	VenomancerPredictionEnvironment environment(gameState());
	HypotheticBattle projected(&environment, callback);

	// Ensure the projected unit is materialized before castEval mutates its state.
	const auto materializationBonus = Bonus(BonusDuration::ONE_BATTLE, BonusType::MORALE,
		BonusSource::OTHER, 0, BonusSourceID());
	projected.addUnitBonus(poisonTarget->unitId(), {materializationBonus});
	const auto * projectedTarget = projected.battleGetUnitByID(poisonTarget->unitId());
	ASSERT_NE(projectedTarget, nullptr);
	const auto * spell = poisonSpell().toSpell();
	ASSERT_NE(spell, nullptr);
	spells::BattleCast projectedCast(&projected, attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&projectedCast);
	spells::Target projectedAim{spells::Destination(projectedTarget)};
	mechanics->castEval(projected.getServerCallback(), projectedAim);

	const auto * projectedTargetAfterCast = projected.battleGetUnitByID(poisonTarget->unitId());
	ASSERT_NE(projectedTargetAfterCast, nullptr);
	const auto projectedState = projectedTargetAfterCast->acquireState();
	ASSERT_NE(projectedState, nullptr);
	EXPECT_EQ(projectedState->physicalPoisonBaseDamage, 92);
	EXPECT_EQ(projectedState->physicalPoisonActivationsRemaining, 3);
	EXPECT_EQ(poisonTarget->physicalPoisonBaseDamage, 0);
	EXPECT_EQ(poisonTarget->physicalPoisonActivationsRemaining, 0);
}

TEST_F(NewHorizonsVenomancerTest, ToxicSpinesStillUsesActualReflectedDamageWithVenomancerOnTheAttacker)
{
	startGame();
	attackerSideHero->setSecSkillLevel(natureMagic(), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	acceptPerkThroughOffer(attackerSideHero, natureMagicSkillKey,
		venomancerPerkKey, MasteryLevel::BASIC);
	const SecondarySkill bulwark(SecondarySkill::decode(std::string(newHorizonsBulwark::SKILL_ID)));
	ASSERT_TRUE(bulwark.hasValue());
	defenderSideHero->setSecSkillLevel(bulwark, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	acceptPerkThroughOffer(defenderSideHero, newHorizonsBulwark::SKILL_ID,
		newHorizonsBulwark::TOXIC_SPINES_ID, MasteryLevel::BASIC);
	defenderSideHero->setSecSkillLevel(bulwark, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(defenderSideHero->hasActivePerk(std::string(newHorizonsBulwark::SKILL_ID),
		std::string(newHorizonsBulwark::TOXIC_SPINES_ID)));
	ASSERT_GT(newHorizonsBulwark::reflectionBasisPoints(
		static_cast<int>(defenderSideHero->getSecSkillLevel(bulwark)), false, false), 0);

	startBattle();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(80), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(81), 100);
	ASSERT_NE(attacker, nullptr);
	ASSERT_NE(defender, nullptr);
	forceMaximumDamage(attacker);
	blockRetaliation(attacker);
	blockRetaliation(defender);
	const auto attackerHealthBefore = attacker->getAvailableHealth();
	beginCombat();
	defender->defending = true;
	defender->bulwarkPreemptiveUsed = true;

	BattleAction melee = BattleAction::makeMeleeAttack(attacker, defender->getPosition(), attacker->getPosition());
	ASSERT_TRUE(act(attacker, melee));
	const int64_t actualReflectedLoss = attackerHealthBefore - attacker->getAvailableHealth();
	ASSERT_GT(actualReflectedLoss, 0);
	EXPECT_EQ(attacker->physicalPoisonBaseDamage,
		newHorizonsBulwark::toxicSpinesPoisonBase(actualReflectedLoss));
	EXPECT_EQ(attacker->physicalPoisonActivationsRemaining, 3);
}
