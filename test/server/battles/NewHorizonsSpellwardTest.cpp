/*
 * NewHorizonsSpellwardTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later; see license.txt file in main folder
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../hero/NewHorizonsHeroRulesFixture.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
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
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
#include <vcmi/Environment.h>

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace
{
constexpr std::string_view warcastingSkillId = "new-horizons:warcasting";
constexpr std::string_view spellwardPerkId = "new-horizons:warcasting.spellward";

bool setSpellwardStatus(JsonNode & rules, const std::string_view status)
{
	auto & perks = rules["skills"][std::string(warcastingSkillId)]["perks"].Vector();
	const auto found = std::find_if(perks.begin(), perks.end(), [](const JsonNode & perk)
	{
		return perk["id"].String() == spellwardPerkId;
	});
	if(found == perks.end())
		return false;

	(*found)["effect"]["status"].String() = status;
	return true;
}

std::shared_ptr<Bonus> spellDamageReduction(const int value)
{
	return std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::SPELL_DAMAGE_REDUCTION,
		BonusSource::OTHER, value, BonusSourceID(), BonusSubtypeID(SpellSchool::ANY));
}

class SpellwardPredictionEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit SpellwardPredictionEnvironment(std::shared_ptr<CGameState> value)
		: state(std::move(value))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

struct DetachedDamage
{
	int64_t forecast = 0;
	int64_t applied = 0;
};

class NewHorizonsSpellwardTest : public HeroCommandFixture
{
protected:
	bool spellwardActiveInFixture = true;
	CStack * attackerCaster = nullptr;
	CStack * attackerMelee = nullptr;
	CStack * protectedStack = nullptr;
	CStack * independentlyProtectedStack = nullptr;
	CStack * cappedStack = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, testHeroRules());
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));

		JsonNode perkRules(JsonPath::builtin("config/newHorizonsPerks"));
		if(!spellwardActiveInFixture && !setSpellwardStatus(perkRules, "planned"))
			throw std::runtime_error("Missing Spellward from the New Horizons perk registry");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perkRules));
	}

	SecondarySkill warcasting() const
	{
		const int decoded = SecondarySkill::decode(std::string(warcastingSkillId));
		EXPECT_GE(decoded, 0);
		return SecondarySkill(decoded);
	}

	void acceptSpellwardThroughBasicOffer(CGHeroInstance * hero)
	{
		const auto rankLookup = [hero](const std::string & skillId)
		{
			return hero->getPerkSkillRank(skillId);
		};

		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = hero->getPerkState().prepareOffer(rankLookup, seed);
			const auto selected = std::find_if(offers.begin(), offers.end(), [](const auto & offer)
			{
				return offer.selection.skillId == warcastingSkillId
					&& offer.selection.perkId == spellwardPerkId;
			});
			if(selected == offers.end())
				continue;

			ASSERT_EQ(selected->requiredRank, static_cast<int>(MasteryLevel::BASIC));
			const auto choice = static_cast<size_t>(std::distance(offers.begin(), selected));
			gameHandler->levelUpHero(hero, offers, choice, seed, false);
			ASSERT_TRUE(hero->hasActivePerk(std::string(warcastingSkillId), std::string(spellwardPerkId)));
			return;
		}

		FAIL() << "Spellward was not available as a Basic New Horizons perk offer";
	}

	void prepare(const bool selectSpellward = true, const bool perkIsActive = true)
	{
		spellwardActiveInFixture = perkIsActive;
		startGame();

		const auto arrow = SpellID(SpellID::MAGIC_ARROW);
		for(const auto hero : {attackerSideHero, defenderSideHero})
		{
			giveArtifact(hero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
			hero->removeAllSpells();
			hero->addSpellToSpellbook(arrow);
			hero->addSpellToSpellbook(SpellID::SLOW);
			hero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
			hero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 1000, ChangeValueMode::ABSOLUTE);
			setTestSpellPointTotal(hero, 1000);
		}

		defenderSideHero->setSecSkillLevel(warcasting(), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		if(selectSpellward)
			acceptSpellwardThroughBasicOffer(defenderSideHero);

		startBattle();
		removeDeployedUnits();
		attackerCaster = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 1000);
		attackerMelee = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"),
			BattleHex(rightHex - GameConstants::BFIELD_WIDTH), 1000);
		protectedStack = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(rightHex), 10000);
		independentlyProtectedStack = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 10000);
		cappedStack = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(14, 5), 10000);
		ASSERT_NE(attackerCaster, nullptr);
		ASSERT_NE(attackerMelee, nullptr);
		ASSERT_NE(protectedStack, nullptr);
		ASSERT_NE(independentlyProtectedStack, nullptr);
		ASSERT_NE(cappedStack, nullptr);
		independentlyProtectedStack->addNewBonus(spellDamageReduction(50));
		cappedStack->addNewBonus(spellDamageReduction(95));
		beginCombat();
		ASSERT_EQ(battle()->battleGetOwnerHero(protectedStack), defenderSideHero);
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

	int64_t rawDamage(CGHeroInstance * caster) const
	{
		const auto * spell = SpellID(SpellID::MAGIC_ARROW).toSpell();
		spells::BattleCast castEvent(battle(), caster, spells::Mode::HERO, spell);
		return spell->battleMechanics(&castEvent)->getEffectValue();
	}

	int64_t forecastDamage(CGHeroInstance * caster, const CBattleInfoCallback * selectedBattle,
		const CStack * target) const
	{
		const auto * spell = SpellID(SpellID::MAGIC_ARROW).toSpell();
		spells::BattleCast castEvent(selectedBattle, caster, spells::Mode::HERO, spell);
		return spell->battleMechanics(&castEvent)->adjustEffectValue(target);
	}

	DetachedDamage detachedDamage(CGHeroInstance * caster, const CStack * target) const
	{
		const auto * spell = SpellID(SpellID::MAGIC_ARROW).toSpell();
		auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		SpellwardPredictionEnvironment environment(gameState());
		HypotheticBattle projected(&environment, callback);
		const auto * projectedTarget = projected.battleGetUnitByID(target->unitId());
		EXPECT_NE(projectedTarget, nullptr);
		if(!projectedTarget)
			return {};
		EXPECT_EQ(projected.battleGetPerkMagicalReductionBasisPoints(projectedTarget),
			battle()->battleGetPerkMagicalReductionBasisPoints(target));

		spells::BattleCast castEvent(&projected, caster, spells::Mode::HERO, spell);
		const auto mechanics = spell->battleMechanics(&castEvent);
		DetachedDamage result;
		result.forecast = mechanics->adjustEffectValue(projectedTarget);
		const auto projectedHealthBefore = projectedTarget->getAvailableHealth();
		spells::Target aim{spells::Destination(projectedTarget)};
		mechanics->castEval(projected.getServerCallback(), aim);
		const auto * projectedAfter = projected.battleGetUnitByID(target->unitId());
		EXPECT_NE(projectedAfter, nullptr);
		if(projectedAfter)
			result.applied = projectedHealthBefore - projectedAfter->getAvailableHealth();
		return result;
	}

	bool castPaid(BattleSide side, const CStack * actor,
		SpellID spell, const CStack * target)
	{
		BattleSetActiveStack activation;
		activation.battleID = BattleID(0);
		activation.stack = actor->unitId();
		activation.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activation);

		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = side;
		action.spell = spell;
		action.aimToUnit(target);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->sideToPlayer(side), action);
	}

	void advanceRound()
	{
		BattleNextRound next;
		next.battleID = BattleID(0);
		gameHandler->sendAndApply(next);
	}
};
}

TEST_F(NewHorizonsSpellwardTest, AcceptedBasicOfferReducesPaidMagicalDamageAndMatchesDetachedCast)
{
	prepare();
	ASSERT_TRUE(defenderSideHero->hasActivePerk(std::string(warcastingSkillId), std::string(spellwardPerkId)));
	ASSERT_TRUE(SpellID(SpellID::MAGIC_ARROW).toSpell()->isMagical());

	const auto raw = rawDamage(attackerSideHero);
	ASSERT_GT(raw, 0);
	const auto expectedProtectedDamage = raw * 90 / 100;
	const auto expectedCombinedDamage = raw * 50 * 90 / 10000;
	const auto expectedCappedDamage = raw * 5 / 100;

	EXPECT_EQ(forecastDamage(attackerSideHero, battle(), protectedStack), expectedProtectedDamage);
	EXPECT_EQ(forecastDamage(attackerSideHero, battle(), independentlyProtectedStack), expectedCombinedDamage);
	EXPECT_EQ(forecastDamage(attackerSideHero, battle(), cappedStack), expectedCappedDamage)
		<< "A 95% independent source and Spellward cap total MDR at 95%";
	EXPECT_EQ(forecastDamage(defenderSideHero, battle(), attackerCaster), raw)
		<< "The attacker's stack is controlled by a hero without Spellward";

	const auto detached = detachedDamage(attackerSideHero, protectedStack);
	EXPECT_EQ(detached.forecast, expectedProtectedDamage);
	EXPECT_EQ(detached.applied, expectedProtectedDamage);
	const auto combinedDetached = detachedDamage(attackerSideHero, independentlyProtectedStack);
	EXPECT_EQ(combinedDetached.forecast, expectedCombinedDamage);
	EXPECT_EQ(combinedDetached.applied, expectedCombinedDamage);
	const auto cappedDetached = detachedDamage(attackerSideHero, cappedStack);
	EXPECT_EQ(cappedDetached.forecast, expectedCappedDamage);
	EXPECT_EQ(cappedDetached.applied, expectedCappedDamage);

	const auto healthBefore = protectedStack->getAvailableHealth();
	const auto manaBefore = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(castPaid(BattleSide::ATTACKER, attackerCaster,
		SpellID(SpellID::MAGIC_ARROW), protectedStack));
	EXPECT_EQ(healthBefore - protectedStack->getAvailableHealth(), expectedProtectedDamage);
	EXPECT_LT(attackerSideHero->getManaAvailable(), manaBefore)
		<< "The real server cast spends Mana while applying Spellward";

	advanceRound();
	const auto combinedSourceHealthBefore = independentlyProtectedStack->getAvailableHealth();
	const auto combinedManaBefore = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(castPaid(BattleSide::ATTACKER, attackerCaster,
		SpellID(SpellID::MAGIC_ARROW), independentlyProtectedStack));
	EXPECT_EQ(combinedSourceHealthBefore - independentlyProtectedStack->getAvailableHealth(), expectedCombinedDamage)
		<< "An independent 50% ANY reduction multiplies with Spellward";
	EXPECT_LT(attackerSideHero->getManaAvailable(), combinedManaBefore);
}

TEST_F(NewHorizonsSpellwardTest, PlannedPerkCannotBeOfferedOrForceSelectedAndAddsNoProtection)
{
	prepare(false, false);
	ASSERT_FALSE(defenderSideHero->hasActivePerk(std::string(warcastingSkillId), std::string(spellwardPerkId)));
	const auto rankLookup = [this](const std::string & skillId)
	{
		return defenderSideHero->getPerkSkillRank(skillId);
	};
	for(uint64_t seed = 0; seed < 512; ++seed)
	{
		const auto offers = defenderSideHero->getPerkState().prepareOffer(rankLookup, seed);
		EXPECT_EQ(std::find_if(offers.begin(), offers.end(), [](const auto & offer)
		{
			return offer.selection.skillId == warcastingSkillId && offer.selection.perkId == spellwardPerkId;
		}), offers.end());
	}
	EXPECT_THROW(defenderSideHero->applyPerkSelection({std::string(warcastingSkillId), std::string(spellwardPerkId)}),
		std::runtime_error);
	EXPECT_TRUE(defenderSideHero->getPerkState().selected.empty());

	const auto raw = rawDamage(attackerSideHero);
	EXPECT_EQ(forecastDamage(attackerSideHero, battle(), protectedStack), raw);
	const auto healthBefore = protectedStack->getAvailableHealth();
	ASSERT_TRUE(castPaid(BattleSide::ATTACKER, attackerCaster,
		SpellID(SpellID::MAGIC_ARROW), protectedStack));
	EXPECT_EQ(healthBefore - protectedStack->getAvailableHealth(), raw);
}

TEST_F(NewHorizonsSpellwardTest, RankLossReacquisitionAndHypnotizeUseCurrentControllerWithoutDuplicatingSelection)
{
	prepare();
	const auto raw = rawDamage(attackerSideHero);
	ASSERT_GT(raw, 0);
	EXPECT_EQ(forecastDamage(attackerSideHero, battle(), protectedStack), raw * 90 / 100);

	EXPECT_THROW(defenderSideHero->applyPerkSelection(
		{std::string(warcastingSkillId), std::string(spellwardPerkId)}), std::runtime_error);
	ASSERT_EQ(defenderSideHero->getPerkState().selected.size(), 1u);

	defenderSideHero->setSecSkillLevel(warcasting(), MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	EXPECT_FALSE(defenderSideHero->hasActivePerk(std::string(warcastingSkillId), std::string(spellwardPerkId)));
	EXPECT_EQ(forecastDamage(attackerSideHero, battle(), protectedStack), raw)
		<< "Dropping below Basic disables the selected perk immediately";

	defenderSideHero->setSecSkillLevel(warcasting(), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	EXPECT_TRUE(defenderSideHero->hasActivePerk(std::string(warcastingSkillId), std::string(spellwardPerkId)));
	EXPECT_EQ(forecastDamage(attackerSideHero, battle(), protectedStack), raw * 90 / 100);
	ASSERT_EQ(defenderSideHero->getPerkState().selected.size(), 1u)
		<< "Rank reacquisition reuses the saved selection instead of adding a duplicate";

	protectedStack->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID()));
	EXPECT_EQ(battle()->battleGetOwnerHero(protectedStack), attackerSideHero);
	EXPECT_EQ(forecastDamage(attackerSideHero, battle(), protectedStack), raw)
		<< "Spellward follows the current controlling hero after Hypnotize";
}

TEST_F(NewHorizonsSpellwardTest, ProjectedControlChangesUseProjectedOwnershipWithoutChangingLiveStacks)
{
	prepare();
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	SpellwardPredictionEnvironment environment(gameState());
	HypotheticBattle projected(&environment, callback);
	const auto target = projected.getForUpdate(protectedStack->unitId());
	ASSERT_NE(target, nullptr);
	EXPECT_EQ(projected.battleGetPerkMagicalReductionBasisPoints(target.get()), 1000);
	target->addUnitBonus({Bonus(BonusDuration::PERMANENT,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID())});
	ASSERT_TRUE(target->isHypnotized());
	EXPECT_FALSE(protectedStack->isHypnotized());
	EXPECT_EQ(projected.battleGetPerkMagicalReductionBasisPoints(target.get()), 0);
	EXPECT_EQ(battle()->battleGetPerkMagicalReductionBasisPoints(protectedStack), 1000);

	const auto attacker = projected.getForUpdate(attackerCaster->unitId());
	ASSERT_NE(attacker, nullptr);
	attacker->addUnitBonus({Bonus(BonusDuration::PERMANENT,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID())});
	ASSERT_TRUE(attacker->isHypnotized());
	EXPECT_FALSE(attackerCaster->isHypnotized());
	EXPECT_EQ(projected.battleGetPerkMagicalReductionBasisPoints(attacker.get()), 1000);
	EXPECT_EQ(battle()->battleGetPerkMagicalReductionBasisPoints(attackerCaster), 0);
}

TEST_F(NewHorizonsSpellwardTest, SpellwardDoesNotTurnIntoSpellResistanceOrReduceSlowOrPhysicalAttacks)
{
	prepare();
	const auto * slow = SpellID(SpellID::SLOW).toSpell();
	ASSERT_NE(slow, nullptr);
	spells::BattleCast slowEvent(battle(), attackerSideHero, spells::Mode::HERO, slow);
	const auto slowMechanics = slow->battleMechanics(&slowEvent);
	EXPECT_TRUE(slowMechanics->isReceptive(protectedStack));
	EXPECT_TRUE(slowMechanics->canBeCastAt(spells::Target{spells::Destination(protectedStack)}));
	EXPECT_EQ(protectedStack->valOfBonuses(BonusType::MAGIC_RESISTANCE), 0);
	EXPECT_EQ(protectedStack->valOfBonuses(BonusType::SPELL_RESISTANCE_AURA), 0);

	ASSERT_TRUE(castPaid(BattleSide::ATTACKER, attackerCaster,
		SpellID(SpellID::SLOW), protectedStack));
	EXPECT_TRUE(protectedStack->hasBonusOfType(BonusType::STACKS_SPEED))
		<< "A non-damaging hostile spell still applies to the protected stack";

	const auto ordinaryAttack = BattleAttackInfo(attackerMelee, protectedStack, 0, false);
	const auto ordinaryDamage = battle()->calculateDmgRange(ordinaryAttack).damage;
	ASSERT_GT(ordinaryDamage.max, 0);
	const auto healthBefore = protectedStack->getAvailableHealth();
	ASSERT_TRUE(attack(attackerMelee, protectedStack->getPosition()));
	const auto actualDamage = healthBefore - protectedStack->getAvailableHealth();
	EXPECT_GE(actualDamage, ordinaryDamage.min);
	EXPECT_LE(actualDamage, ordinaryDamage.max)
		<< "Spellward protects only magical damage; an ordinary creature attack stays in its normal range";
}
