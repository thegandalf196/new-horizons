/*
 * NewHorizonsMassShadowTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"

#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/CStack.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/entities/hero/NewHorizonsPerkState.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/NewHorizonsSpellAvailability.h"
#include <vcmi/Environment.h>

#include <memory>
#include <set>
#include <string>
#include <utility>

namespace
{
constexpr auto massCurseKey = "new-horizons:massCurse";
constexpr auto massSorrowKey = "new-horizons:massSorrow";
constexpr auto shadowSkillKey = "new-horizons:shadowMagic";
constexpr auto grandMaledictionKey = "new-horizons:shadowMagic.grandMalediction";
constexpr auto maledictionKey = "new-horizons:shadowMagic.malediction";
constexpr auto soulBinderKey = "new-horizons:shadowMagic.soulBinder";

SpellID spellNamed(const std::string & name)
{
	return SpellID(SpellID::decode(name));
}

SpellID massCurseSpell()
{
	return spellNamed(massCurseKey);
}

SpellID massSorrowSpell()
{
	return spellNamed(massSorrowKey);
}

JsonNode olderMagicSnapshotWithoutMassVariants()
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	rules["rulesetVersion"].Integer() = newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION;
	rules.Struct().erase("schoolRankPowerCoefficientPercent");
	rules.Struct().erase("spellcraftEfficiencyPercent");
	for(auto & [identity, row] : rules["spells"].Struct())
	{
		(void)identity;
		if(row.isStruct())
			row.Struct().erase("selectedPlacement");
			row.Struct().erase("earthquake");
			row.Struct().erase("structures");
	}
	for(auto it = rules["spells"].Struct().begin(); it != rules["spells"].Struct().end();)
	{
		if(it->second.Struct().contains("variant"))
			it = rules["spells"].Struct().erase(it);
		else
			++it;
	}
	return rules;
}

class MassShadowPredictionEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit MassShadowPredictionEnvironment(std::shared_ptr<CGameState> state)
		: state(std::move(state))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
}

class NewHorizonsMassShadowTest : public HeroCommandFixture
{
protected:
	bool oldSnapshot = false;
	CStack * friendly = nullptr;
	CStack * firstEnemy = nullptr;
	CStack * secondEnemy = nullptr;
	CStack * immuneEnemy = nullptr;
	CStack * massImmuneEnemy = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
		ASSERT_NE(massCurseSpell(), SpellID::NONE);
		ASSERT_NE(massSorrowSpell(), SpellID::NONE);
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);

		JsonNode magicRules = oldSnapshot
			? olderMagicSnapshotWithoutMassVariants()
			: JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
		if(!oldSnapshot)
		{
			magicRules["spells"][massCurseKey]["active"] = JsonNode(true);
			magicRules["spells"][massSorrowKey]["active"] = JsonNode(true);
		}
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magicRules);

		JsonNode perkRules(JsonPath::builtin("config/newHorizonsPerks"));
		for(auto & perk : perkRules["skills"][shadowSkillKey]["perks"].Vector())
			if(perk["id"].String() == grandMaledictionKey)
				perk["effect"]["status"] = JsonNode("active");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perkRules);
	}

	void prepareHero(bool selectGrandMalediction, int shadowRank = MasteryLevel::EXPERT,
		int spellPower = 100, int wisdomRank = MasteryLevel::NONE)
	{
		startGame();
		attackerSideHero->removeAllSpells();
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(SpellID::CURSE);
		attackerSideHero->addSpellToSpellbook(SpellID::SORROW);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		const auto shadow = SecondarySkill(SecondarySkill::decode(shadowSkillKey));
		ASSERT_TRUE(shadow.hasValue());
		attackerSideHero->setSecSkillLevel(shadow, shadowRank, ChangeValueMode::ABSOLUTE);
		if(wisdomRank > MasteryLevel::NONE)
		{
			const SecondarySkill wisdom(SecondarySkill::decode("new-horizons:wisdom"));
			ASSERT_TRUE(wisdom.hasValue());
			attackerSideHero->setSecSkillLevel(wisdom, wisdomRank, ChangeValueMode::ABSOLUTE);
		}
		setTestSpellPointTotal(attackerSideHero, 1000);

		if(selectGrandMalediction)
			selectGrandMaledictionWithPrerequisites();
	}

	void selectGrandMaledictionWithPrerequisites()
	{
		const SecondarySkill shadow(SecondarySkill::decode(shadowSkillKey));
		attackerSideHero->setSecSkillLevel(shadow, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({shadowSkillKey, maledictionKey});
		attackerSideHero->setSecSkillLevel(shadow, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({shadowSkillKey, soulBinderKey});
		attackerSideHero->setSecSkillLevel(shadow, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({shadowSkillKey, grandMaledictionKey});
		ASSERT_TRUE(attackerSideHero->hasActivePerk(shadowSkillKey, grandMaledictionKey));
	}

	void prepareBattle()
	{
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		if(!remove.changedStacks.empty())
			gameHandler->sendAndApply(remove);

		friendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 100);
		firstEnemy = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 100);
		secondEnemy = addStack(BattleSide::DEFENDER, creatureByName("core:archer"), BattleHex(14, 5), 100);
		immuneEnemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(16, 5), 100);
		massImmuneEnemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(14, 7), 100);
		ASSERT_NE(friendly, nullptr);
		ASSERT_NE(firstEnemy, nullptr);
		ASSERT_NE(secondEnemy, nullptr);
		ASSERT_NE(immuneEnemy, nullptr);
		ASSERT_NE(massImmuneEnemy, nullptr);
		for(const SpellID family : {SpellID::CURSE, SpellID::SORROW})
			immuneEnemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
				BonusType::SPELL_IMMUNITY, BonusSource::OTHER, 1, BonusSourceID(), BonusSubtypeID(family)));
		for(const SpellID spell : {massCurseSpell(), massSorrowSpell()})
			massImmuneEnemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
				BonusType::SPELL_IMMUNITY, BonusSource::OTHER, 1, BonusSourceID(), BonusSubtypeID(spell)));
		beginCombat();
	}

	spells::Target massAim() const
	{
		return {spells::Destination(BattleHex::INVALID)};
	}

	bool castSpell(SpellID spell, const CStack * selectedTarget = nullptr)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = spell;
		if(selectedTarget)
			action.aimToUnit(selectedTarget);
		else
			action.aimToHex(BattleHex::INVALID);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	const Bonus * effect(const battle::Unit * unit, SpellID family, BonusType type) const
	{
		if(!unit)
			return nullptr;
		const auto bonuses = unit->getBonuses(Selector::source(
			BonusSource::SPELL_EFFECT, BonusSourceID(family)).And(Selector::type()(type)));
		return bonuses && !bonuses->empty() ? bonuses->front().get() : nullptr;
	}

	size_t familyEffectCount(const battle::Unit * unit, SpellID family, BonusType type) const
	{
		if(!unit)
			return 0;
		const auto bonuses = unit->getBonuses(Selector::source(
			BonusSource::SPELL_EFFECT, BonusSourceID(family)).And(Selector::type()(type)));
		return bonuses ? bonuses->size() : 0;
	}

	std::set<uint32_t> affectedIds(const spells::Mechanics & mechanics, const spells::Target & aim) const
	{
		std::set<uint32_t> result;
		for(const auto * unit : mechanics.getAffectedStacks(aim))
			if(unit)
				result.insert(unit->unitId());
		return result;
	}

	void expectDetachedAndAcceptedMassEffect(SpellID spell, SpellID family, BonusType type,
		int expectedValue, int expectedDuration, int expectedListedCost, int expectedWisdomCost)
	{
		const auto * definition = spell.toSpell();
		ASSERT_NE(definition, nullptr);
		const auto & rules = battle()->getBattle()->getMagicRules();
		EXPECT_EQ(newHorizonsMagic::spellCost(rules, spell, MasteryLevel::EXPERT), expectedListedCost);
		const auto manaCost = battle()->battleGetSpellCost(definition, attackerSideHero);
		EXPECT_EQ(manaCost, expectedWisdomCost);

		const auto realManaBefore = attackerSideHero->getManaAvailable();
		auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		MassShadowPredictionEnvironment environment(gameState());
		HypotheticBattle projected(&environment, callback);
		const auto * projectedSpell = spell.toSpell();
		spells::BattleCast projectedCast(&projected, attackerSideHero, spells::Mode::HERO, projectedSpell);
		auto projectedMechanics = projectedSpell->battleMechanics(&projectedCast);
		ASSERT_TRUE(projectedMechanics->isMassive());
		Bonus priorFamilyEffect(BonusDuration::N_TURNS, type, BonusSource::SPELL_EFFECT,
			type == BonusType::MORALE ? -1 : 0, BonusSourceID(family));
		priorFamilyEffect.turnsRemain = 1;
		projected.addUnitBonus(firstEnemy->unitId(), {priorFamilyEffect});
		const auto aim = massAim();
		projectedMechanics->castEval(projected.getServerCallback(), aim);

		const auto * projectedFriendly = projected.battleGetUnitByID(friendly->unitId());
		const auto * projectedFirstEnemy = projected.battleGetUnitByID(firstEnemy->unitId());
		const auto * projectedSecondEnemy = projected.battleGetUnitByID(secondEnemy->unitId());
		const auto * projectedImmuneEnemy = projected.battleGetUnitByID(immuneEnemy->unitId());
		const auto * projectedMassImmuneEnemy = projected.battleGetUnitByID(massImmuneEnemy->unitId());
		ASSERT_NE(projectedFriendly, nullptr);
		ASSERT_NE(projectedFirstEnemy, nullptr);
		ASSERT_NE(projectedSecondEnemy, nullptr);
		ASSERT_NE(projectedImmuneEnemy, nullptr);
		ASSERT_NE(projectedMassImmuneEnemy, nullptr);
		EXPECT_EQ(effect(projectedFriendly, family, type), nullptr);
		EXPECT_EQ(effect(projectedImmuneEnemy, family, type), nullptr);
		EXPECT_EQ(effect(projectedMassImmuneEnemy, family, type), nullptr);
		const auto * forecastFirst = effect(projectedFirstEnemy, family, type);
		const auto * forecastSecond = effect(projectedSecondEnemy, family, type);
		ASSERT_NE(forecastFirst, nullptr);
		ASSERT_NE(forecastSecond, nullptr);
		EXPECT_EQ(familyEffectCount(projectedFirstEnemy, family, type), 1u)
			<< "The family-scoped forecast replaces its previous effect rather than stacking a duplicate";
		EXPECT_EQ(forecastFirst->val, expectedValue);
		EXPECT_EQ(forecastSecond->val, expectedValue);
		EXPECT_EQ(forecastFirst->turnsRemain, expectedDuration);
		EXPECT_EQ(forecastSecond->turnsRemain, expectedDuration);
		EXPECT_EQ(attackerSideHero->getManaAvailable(), realManaBefore)
			<< "Detached forecast must not spend the real hero's Mana";

		ASSERT_TRUE(castSpell(spell));
		EXPECT_EQ(attackerSideHero->getManaAvailable(), realManaBefore - expectedWisdomCost);
		EXPECT_EQ(effect(friendly, family, type), nullptr);
		EXPECT_EQ(effect(immuneEnemy, family, type), nullptr);
		EXPECT_EQ(effect(massImmuneEnemy, family, type), nullptr);
		for(const CStack * target : {firstEnemy, secondEnemy})
		{
			const auto * applied = effect(target, family, type);
			ASSERT_NE(applied, nullptr);
			EXPECT_EQ(applied->val, forecastFirst->val);
			EXPECT_EQ(applied->turnsRemain, forecastFirst->turnsRemain);
			EXPECT_EQ(applied->val, expectedValue);
			EXPECT_EQ(applied->turnsRemain, expectedDuration);
		}
	}
};

TEST_F(NewHorizonsMassShadowTest, GrandMaledictionIsAnExpertOfferAndAddsThenLosesBothVirtualSpellSources)
{
	prepareHero(false, MasteryLevel::BASIC);
	const SecondarySkill shadow(SecondarySkill::decode(shadowSkillKey));
	ASSERT_TRUE(shadow.hasValue());
	const auto rankLookup = [this](const std::string & skillId)
	{
		return skillId == shadowSkillKey
			? static_cast<int>(attackerSideHero->getSecSkillLevel(SecondarySkill(SecondarySkill::decode(shadowSkillKey))))
			: 0;
	};

	auto basicOffer = attackerSideHero->getPerkState().prepareOffer(rankLookup, 1337);
	EXPECT_TRUE(std::ranges::any_of(basicOffer, [](const auto & offer)
	{
		return offer.selection.perkId == maledictionKey && offer.requiredRank == MasteryLevel::BASIC;
	}));
	attackerSideHero->applyPerkSelection({shadowSkillKey, maledictionKey});
	attackerSideHero->setSecSkillLevel(shadow, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	auto advancedOffer = attackerSideHero->getPerkState().prepareOffer(rankLookup, 1337);
	EXPECT_FALSE(std::ranges::any_of(advancedOffer, [](const auto & offer)
	{
		return offer.selection.perkId == grandMaledictionKey;
	}));
	attackerSideHero->applyPerkSelection({shadowSkillKey, soulBinderKey});
	EXPECT_THROW(attackerSideHero->applyPerkSelection({shadowSkillKey, grandMaledictionKey}), std::runtime_error);

	attackerSideHero->setSecSkillLevel(shadow, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	auto expertOffer = attackerSideHero->getPerkState().prepareOffer(rankLookup, 1337);
	const auto grandMalediction = std::ranges::find_if(expertOffer, [](const auto & offer)
	{
		return offer.selection.perkId == grandMaledictionKey;
	});
	ASSERT_NE(grandMalediction, expertOffer.end());
	EXPECT_EQ(grandMalediction->requiredRank, MasteryLevel::EXPERT);
	attackerSideHero->applyPerkSelection(grandMalediction->selection);
	ASSERT_TRUE(attackerSideHero->hasActivePerk(shadowSkillKey, grandMaledictionKey));

	for(const SpellID variant : {massCurseSpell(), massSorrowSpell()})
	{
		EXPECT_TRUE(attackerSideHero->getSpellsInSpellbook().contains(variant));
		EXPECT_TRUE(attackerSideHero->getInscribedSpellsForCasting().contains(variant));
		EXPECT_TRUE(attackerSideHero->canCastThisSpell(variant.toSpell()));
		EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(variant))
			<< "Grand Malediction provides a virtual source without serializing the variant";
		EXPECT_FALSE(newHorizonsMagic::spellAvailableForOrdinaryAcquisition(
			attackerSideHero->getMagicRules(), variant));
		EXPECT_FALSE(attackerSideHero->canLearnSpell(variant.toSpell(), true));
		EXPECT_FALSE(attackerSideHero->getSourcesForSpell(variant).empty());
	}

	attackerSideHero->setSecSkillLevel(shadow, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	EXPECT_FALSE(attackerSideHero->hasActivePerk(shadowSkillKey, grandMaledictionKey));
	for(const SpellID variant : {massCurseSpell(), massSorrowSpell()})
	{
		EXPECT_FALSE(attackerSideHero->getSpellsInSpellbook().contains(variant));
		EXPECT_FALSE(attackerSideHero->getInscribedSpellsForCasting().contains(variant));
		EXPECT_FALSE(attackerSideHero->canCastThisSpell(variant.toSpell()));
		EXPECT_TRUE(attackerSideHero->getSourcesForSpell(variant).empty());
		EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(variant));
	}
}

TEST_F(NewHorizonsMassShadowTest, PhysicalSpellbookRemovalRevokesVirtualMassSpellsWithoutRemovingThePerk)
{
	prepareHero(true);
	ASSERT_TRUE(attackerSideHero->hasSpellbook());
	ASSERT_TRUE(attackerSideHero->hasActivePerk(shadowSkillKey, grandMaledictionKey));
	for(const SpellID variant : {massCurseSpell(), massSorrowSpell()})
		ASSERT_TRUE(attackerSideHero->getInscribedSpellsForCasting().contains(variant));

	attackerSideHero->removeArtifact(ArtifactPosition::SPELLBOOK);
	EXPECT_FALSE(attackerSideHero->hasSpellbook());
	EXPECT_TRUE(attackerSideHero->hasActivePerk(shadowSkillKey, grandMaledictionKey));
	for(const SpellID variant : {massCurseSpell(), massSorrowSpell()})
	{
		EXPECT_FALSE(attackerSideHero->getSpellsInSpellbook().contains(variant));
		EXPECT_FALSE(attackerSideHero->getInscribedSpellsForCasting().contains(variant));
		EXPECT_FALSE(attackerSideHero->canCastThisSpell(variant.toSpell()));
		EXPECT_TRUE(attackerSideHero->getSourcesForSpell(variant).empty());
	}
}

TEST_F(NewHorizonsMassShadowTest, ManualAndScrollSourcesCannotGrantVariantsButKnownBaseCurseCastsWithoutSchoolRank)
{
	prepareHero(false, MasteryLevel::EXPERT);
	const auto massCurse = massCurseSpell();
	const auto massSorrow = massSorrowSpell();
	for(const SpellID variant : {massCurse, massSorrow})
	{
		attackerSideHero->addSpellToSpellbook(variant);
		ASSERT_TRUE(gameHandler->giveHeroNewScroll(attackerSideHero, variant, ArtifactPosition::FIRST_AVAILABLE));
		EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(variant));
		EXPECT_TRUE(attackerSideHero->getSourcesForSpell(variant).empty());
		EXPECT_FALSE(attackerSideHero->getSpellsInSpellbook().contains(variant));
		EXPECT_FALSE(attackerSideHero->canCastThisSpell(variant.toSpell()));
	}

	const SecondarySkill shadow(SecondarySkill::decode(shadowSkillKey));
	attackerSideHero->setSecSkillLevel(shadow, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(attackerSideHero->getSecSkillLevel(shadow), MasteryLevel::NONE);
	EXPECT_TRUE(attackerSideHero->canCastThisSpell(SpellID(SpellID::CURSE).toSpell()))
		<< "A known, physically inscribed base combat spell remains castable without School rank";
	prepareBattle();
	ASSERT_TRUE(castSpell(SpellID::CURSE, firstEnemy));
	EXPECT_NE(effect(firstEnemy, SpellID::CURSE, BonusType::ALWAYS_MINIMUM_DAMAGE), nullptr);
}

TEST_F(NewHorizonsMassShadowTest, ExpertBaseSpellsStaySingleTargetWhileVariantsCoverOnlyEligibleEnemies)
{
	prepareHero(true);
	prepareBattle();

	for(const SpellID base : {SpellID(SpellID::CURSE), SpellID(SpellID::SORROW)})
	{
		spells::BattleCast baseCast(battle(), attackerSideHero, spells::Mode::HERO, base.toSpell());
		const auto baseMechanics = base.toSpell()->battleMechanics(&baseCast);
		EXPECT_FALSE(baseMechanics->isMassive()) << base.toSpell()->getJsonKey();
		EXPECT_EQ(affectedIds(*baseMechanics, {spells::Destination(firstEnemy)}),
			(std::set<uint32_t>{firstEnemy->unitId()}));
	}

	const auto aim = massAim();
	const std::set<uint32_t> expected{firstEnemy->unitId(), secondEnemy->unitId()};
	for(const SpellID variant : {massCurseSpell(), massSorrowSpell()})
	{
		spells::BattleCast variantCast(battle(), attackerSideHero, spells::Mode::HERO, variant.toSpell());
		const auto variantMechanics = variant.toSpell()->battleMechanics(&variantCast);
		EXPECT_TRUE(variantMechanics->isMassive()) << variant.toSpell()->getJsonKey();
		EXPECT_EQ(affectedIds(*variantMechanics, aim), expected)
			<< "The Mass variant must exclude friendly, base-family-immune and variant-immune stacks";
	}
	const auto curseDescription = newHorizonsMagic::spellDescriptionForHero(
		attackerSideHero, massCurseSpell().toSpell(), MasteryLevel::EXPERT);
	EXPECT_NE(curseDescription.find("Targets every eligible enemy stack for 4 rounds"), std::string::npos);
	EXPECT_NE(curseDescription.find("Granted by Grand Malediction"), std::string::npos);
	const auto sorrowDescription = newHorizonsMagic::spellDescriptionForHero(
		attackerSideHero, massSorrowSpell().toSpell(), MasteryLevel::EXPERT);
	EXPECT_NE(sorrowDescription.find("Targets every eligible enemy stack for 4 rounds"), std::string::npos);
	EXPECT_NE(sorrowDescription.find("The Mass cast affects every eligible enemy stack"), std::string::npos);
}

TEST_F(NewHorizonsMassShadowTest, AcceptedMassCurseMatchesDetachedForecastAndKeepsItsMinimumDamageEndpoint)
{
	prepareHero(true, MasteryLevel::EXPERT, 100, MasteryLevel::EXPERT);
	prepareBattle();
	expectDetachedAndAcceptedMassEffect(massCurseSpell(), SpellID::CURSE,
		BonusType::ALWAYS_MINIMUM_DAMAGE, 0, 4, 9,
		newHorizonsMagic::wisdomAdjustedCost(9, 1, MasteryLevel::EXPERT));
}

TEST_F(NewHorizonsMassShadowTest, AcceptedMassSorrowMatchesDetachedForecastWithRankedPenaltyAndMalediction)
{
	prepareHero(true, MasteryLevel::EXPERT, 100, MasteryLevel::EXPERT);
	prepareBattle();
	const auto rules = battle()->getBattle()->getMagicRules();
	const auto penalty = newHorizonsMagic::sorrowMoralePenalty(
		rules, attackerSideHero, massSorrowSpell(), 100);
	ASSERT_TRUE(penalty.has_value());
	EXPECT_EQ(*penalty, 3)
		<< "Expert Shadow scales 100 Spell Power to 145 before floor(scaled power / 70)";
	const auto duration = newHorizonsMagic::sorrowDurationRounds(
		rules, attackerSideHero, massSorrowSpell());
	ASSERT_TRUE(duration.has_value());
	EXPECT_EQ(*duration, 4);
	expectDetachedAndAcceptedMassEffect(massSorrowSpell(), SpellID::SORROW,
		BonusType::MORALE, -*penalty, *duration, 12,
		newHorizonsMagic::wisdomAdjustedCost(12, 1, MasteryLevel::EXPERT));
}

TEST_F(NewHorizonsMassShadowTest, OlderMagicSnapshotWithoutVariantRowsDoesNotFallBackToInstalledVariants)
{
	oldSnapshot = true;
	prepareHero(true);
	const auto & rules = attackerSideHero->getMagicRules();
	EXPECT_EQ(rules["rulesetVersion"].Integer(), newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION);
	for(const SpellID variant : {massCurseSpell(), massSorrowSpell()})
	{
		EXPECT_EQ(newHorizonsMagic::spellVariantBase(rules, variant), variant);
		EXPECT_FALSE(newHorizonsMagic::spellAllowedBySavedRoster(rules, variant));
		EXPECT_FALSE(newHorizonsMagic::variantGrantAvailable(rules, attackerSideHero, variant));
		EXPECT_FALSE(attackerSideHero->isSpellInscribedForCasting(variant));
		EXPECT_TRUE(attackerSideHero->getSourcesForSpell(variant).empty());
		EXPECT_FALSE(attackerSideHero->canCastThisSpell(variant.toSpell()));
	}
}

TEST_F(NewHorizonsMassShadowTest, NonFullPowerSavedVariantMetadataFailsValidationAndFamilyResolution)
{
	prepareHero(true);
	JsonNode malformedRules = attackerSideHero->getMagicRules();
	malformedRules["spells"][massCurseKey]["variant"]["powerPercent"].Integer() = 60;
	EXPECT_THROW(newHorizonsMagic::validateRules(malformedRules), std::runtime_error);
	EXPECT_EQ(newHorizonsMagic::spellVariantBase(malformedRules, massCurseSpell()), massCurseSpell());
	EXPECT_FALSE(newHorizonsMagic::variantGrantAvailable(malformedRules, attackerSideHero, massCurseSpell()));
}
