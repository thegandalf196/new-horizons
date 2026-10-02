/*
 * NewHorizonsMassBlessTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
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
constexpr auto massBlessKey = "new-horizons:massBless";
constexpr auto lightMagicSkill = "new-horizons:lightMagic";
constexpr auto benedictionPerk = "new-horizons:lightMagic.benediction";
constexpr auto litanyPerk = "new-horizons:lightMagic.litany";

SpellID massBlessSpell()
{
	return SpellID(SpellID::decode(massBlessKey));
}

JsonNode olderMagicSnapshotWithoutMassBless()
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	rules["rulesetVersion"].Integer() = newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION;
	rules.Struct().erase("schoolRankPowerCoefficientPercent");
	rules.Struct().erase("spellcraftEfficiencyPercent");
	auto & spells = rules["spells"].Struct();
	for(auto it = spells.begin(); it != spells.end();)
	{
		if(it->second.isStruct())
		{
			it->second.Struct().erase("selectedPlacement");
			it->second.Struct().erase("earthquake");
			if(it->second.Struct().contains("variant"))
			{
				it = spells.erase(it);
				continue;
			}
		}
		++it;
	}
	return rules;
}

class MassBlessPredictionEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit MassBlessPredictionEnvironment(std::shared_ptr<CGameState> state)
		: state(std::move(state))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
}

class NewHorizonsMassBlessTest : public HeroCommandFixture
{
protected:
	bool oldSnapshot = false;
	CStack * firstFriendly = nullptr;
	CStack * secondFriendly = nullptr;
	CStack * blessImmuneFriendly = nullptr;
	CStack * massBlessImmuneFriendly = nullptr;
	CStack * enemy = nullptr;
	const CSpell * bless = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
		ASSERT_NE(massBlessSpell(), SpellID::NONE);
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);

		JsonNode magicRules = oldSnapshot
			? olderMagicSnapshotWithoutMassBless()
			: JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
		if(!oldSnapshot)
			magicRules["spells"][massBlessKey]["active"] = JsonNode(true);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magicRules);

		JsonNode perkRules(JsonPath::builtin("config/newHorizonsPerks"));
		for(auto & perk : perkRules["skills"][lightMagicSkill]["perks"].Vector())
			if(perk["id"].String() == litanyPerk)
				perk["effect"]["status"] = JsonNode("active");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perkRules);
	}

	void prepareHero(bool selectLitany, int lightRank = MasteryLevel::ADVANCED,
		int spellPower = 100, int wisdomRank = MasteryLevel::NONE)
	{
		startGame();
		bless = SpellID(SpellID::BLESS).toSpell();
		ASSERT_NE(bless, nullptr);
		attackerSideHero->removeAllSpells();
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(SpellID::BLESS);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		const SecondarySkill light(SecondarySkill::decode(lightMagicSkill));
		ASSERT_TRUE(light.hasValue());
		attackerSideHero->setSecSkillLevel(light, lightRank, ChangeValueMode::ABSOLUTE);
		if(wisdomRank > MasteryLevel::NONE)
		{
			const SecondarySkill wisdom(SecondarySkill::decode("new-horizons:wisdom"));
			ASSERT_TRUE(wisdom.hasValue());
			attackerSideHero->setSecSkillLevel(wisdom, wisdomRank, ChangeValueMode::ABSOLUTE);
		}
		setTestSpellPointTotal(attackerSideHero, 1000);

		if(selectLitany)
			selectLitanyWithPrerequisites();
		attackerSideHero->setSecSkillLevel(light, lightRank, ChangeValueMode::ABSOLUTE);
	}

	void selectLitanyWithPrerequisites()
	{
		const SecondarySkill light(SecondarySkill::decode(lightMagicSkill));
		attackerSideHero->setSecSkillLevel(light, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({lightMagicSkill, benedictionPerk});
		attackerSideHero->setSecSkillLevel(light, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({lightMagicSkill, litanyPerk});
		ASSERT_TRUE(attackerSideHero->hasActivePerk(lightMagicSkill, litanyPerk));
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

		firstFriendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 100);
		secondFriendly = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(6, 5), 100);
		blessImmuneFriendly = addStack(BattleSide::ATTACKER, creatureByName("core:peasant"), BattleHex(3, 8), 100);
		massBlessImmuneFriendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(6, 8), 100);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 100);
		ASSERT_NE(firstFriendly, nullptr);
		ASSERT_NE(secondFriendly, nullptr);
		ASSERT_NE(blessImmuneFriendly, nullptr);
		ASSERT_NE(massBlessImmuneFriendly, nullptr);
		ASSERT_NE(enemy, nullptr);
		addSpellImmunity(blessImmuneFriendly, SpellID(SpellID::BLESS));
		addSpellImmunity(massBlessImmuneFriendly, massBlessSpell());
		beginCombat();
	}

	void addSpellImmunity(CStack * unit, SpellID spell)
	{
		unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::SPELL_IMMUNITY, BonusSource::OTHER, 1, BonusSourceID(), BonusSubtypeID(spell)));
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

	std::set<uint32_t> affectedIds(const spells::Mechanics & mechanics, const spells::Target & aim) const
	{
		std::set<uint32_t> result;
		for(const auto * unit : mechanics.getAffectedStacks(aim))
			if(unit)
				result.insert(unit->unitId());
		return result;
	}

	std::shared_ptr<const Bonus> blessEffect(const battle::Unit * unit) const
	{
		if(!unit)
			return {};
		const auto bonuses = unit->getBonuses(Selector::source(
			BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::BLESS)))
			.And(Selector::type()(BonusType::ALWAYS_MAXIMUM_DAMAGE)));
		return bonuses && !bonuses->empty() ? bonuses->front() : std::shared_ptr<const Bonus>{};
	}

	std::shared_ptr<const Bonus> curseEffect(const battle::Unit * unit) const
	{
		if(!unit)
			return {};
		const auto bonuses = unit->getBonuses(Selector::source(
			BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::CURSE)))
			.And(Selector::type()(BonusType::ALWAYS_MINIMUM_DAMAGE)));
		return bonuses && !bonuses->empty() ? bonuses->front() : std::shared_ptr<const Bonus>{};
	}

	size_t blessEffectCount(const battle::Unit * unit) const
	{
		if(!unit)
			return 0;
		const auto bonuses = unit->getBonuses(Selector::source(
			BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::BLESS)))
			.And(Selector::type()(BonusType::ALWAYS_MAXIMUM_DAMAGE)));
		return bonuses ? bonuses->size() : 0;
	}

	void expectVariantSource(bool present)
	{
		const auto variant = massBlessSpell();
		EXPECT_EQ(attackerSideHero->getSpellsInSpellbook().contains(variant), present);
		EXPECT_EQ(attackerSideHero->getInscribedSpellsForCasting().contains(variant), present);
		EXPECT_EQ(attackerSideHero->canCastThisSpell(variant.toSpell()), present);
		EXPECT_EQ(!attackerSideHero->getSourcesForSpell(variant).empty(), present);
		EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(variant))
			<< "Litany supplies an ephemeral source and must not durably inscribe Mass Bless";
	}
};

TEST_F(NewHorizonsMassBlessTest, AdvancedLitanyOfferGrantsAndRankDowngradeRevokesVirtualMassBless)
{
	prepareHero(false, MasteryLevel::BASIC);
	const SecondarySkill light(SecondarySkill::decode(lightMagicSkill));
	EXPECT_FALSE(newHorizonsMagic::variantGrantAvailable(
		attackerSideHero->getMagicRules(), attackerSideHero, massBlessSpell()));
	const auto rankLookup = [this](const std::string & skillId)
	{
		return skillId == lightMagicSkill
			? static_cast<int>(attackerSideHero->getSecSkillLevel(
				SecondarySkill(SecondarySkill::decode(lightMagicSkill))))
			: 0;
	};

	const auto hasBenediction = [](const auto & offers)
	{
		return std::ranges::any_of(offers, [](const auto & offer)
		{
			return offer.selection.perkId == benedictionPerk && offer.requiredRank == MasteryLevel::BASIC;
		});
	};
	auto basicOffer = attackerSideHero->getPerkState().prepareOffer(rankLookup, 0);
	for(int seed = 1; seed < 4096 && !hasBenediction(basicOffer); ++seed)
		basicOffer = attackerSideHero->getPerkState().prepareOffer(rankLookup, seed);
	ASSERT_TRUE(hasBenediction(basicOffer))
		<< "A deterministic bounded seed search finds the Basic Benediction offer in the full active pool";
	attackerSideHero->applyPerkSelection({lightMagicSkill, benedictionPerk});
	attackerSideHero->setSecSkillLevel(light, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	const auto hasLitany = [](const auto & offers)
	{
		return std::ranges::any_of(offers, [](const auto & offer)
		{
			return offer.selection.perkId == litanyPerk && offer.requiredRank == MasteryLevel::ADVANCED;
		});
	};
	auto advancedOffer = attackerSideHero->getPerkState().prepareOffer(rankLookup, 0);
	for(int seed = 1; seed < 4096 && !hasLitany(advancedOffer); ++seed)
		advancedOffer = attackerSideHero->getPerkState().prepareOffer(rankLookup, seed);
	const auto litanyOffer = std::ranges::find_if(advancedOffer, [](const auto & offer)
	{
		return offer.selection.perkId == litanyPerk && offer.requiredRank == MasteryLevel::ADVANCED;
	});
	ASSERT_NE(litanyOffer, advancedOffer.end())
		<< "A deterministic bounded seed search finds the Advanced Litany offer in the full active pool";
	attackerSideHero->applyPerkSelection(litanyOffer->selection);
	ASSERT_TRUE(attackerSideHero->hasActivePerk(lightMagicSkill, litanyPerk));
	EXPECT_TRUE(newHorizonsMagic::variantGrantAvailable(
		attackerSideHero->getMagicRules(), attackerSideHero, massBlessSpell()));
	expectVariantSource(true);

	attackerSideHero->setSecSkillLevel(light, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	EXPECT_FALSE(attackerSideHero->hasActivePerk(lightMagicSkill, litanyPerk));
	expectVariantSource(false);
}

TEST_F(NewHorizonsMassBlessTest, ActiveLitanyNeedsThePhysicalSpellbook)
{
	prepareHero(true);
	ASSERT_TRUE(attackerSideHero->hasSpellbook());
	ASSERT_TRUE(attackerSideHero->hasActivePerk(lightMagicSkill, litanyPerk));
	expectVariantSource(true);

	attackerSideHero->removeArtifact(ArtifactPosition::SPELLBOOK);
	EXPECT_FALSE(attackerSideHero->hasSpellbook());
	EXPECT_TRUE(attackerSideHero->hasActivePerk(lightMagicSkill, litanyPerk));
	EXPECT_FALSE(newHorizonsMagic::variantGrantAvailable(
		attackerSideHero->getMagicRules(), attackerSideHero, massBlessSpell()));
	expectVariantSource(false);
}

TEST_F(NewHorizonsMassBlessTest, ExpertBlessRemainsSingleTargetEvenWhenLitanyGrantsMassBless)
{
	prepareHero(true, MasteryLevel::EXPERT);
	prepareBattle();
	const auto massBless = massBlessSpell();
	spells::BattleCast baseCast(battle(), attackerSideHero, spells::Mode::HERO, bless);
	const auto baseMechanics = bless->battleMechanics(&baseCast);
	EXPECT_EQ(baseMechanics->getRangeLevel(), 2);
	EXPECT_EQ(baseMechanics->getEffectLevel(), MasteryLevel::EXPERT);
	EXPECT_FALSE(baseMechanics->isMassive());
	EXPECT_EQ(affectedIds(*baseMechanics, {spells::Destination(firstFriendly)}),
		(std::set<uint32_t>{firstFriendly->unitId()}));

	const auto * massBlessDefinition = massBless.toSpell();
	ASSERT_NE(massBlessDefinition, nullptr);
	spells::BattleCast variantCast(battle(), attackerSideHero, spells::Mode::HERO, massBlessDefinition);
	const auto variantMechanics = massBlessDefinition->battleMechanics(&variantCast);
	EXPECT_TRUE(variantMechanics->isMassive());
	EXPECT_EQ(variantMechanics->getEffectLevel(), baseMechanics->getEffectLevel());
	EXPECT_EQ(affectedIds(*variantMechanics, massAim()),
		(std::set<uint32_t>{firstFriendly->unitId(), secondFriendly->unitId()}));

	ASSERT_TRUE(castSpell(SpellID(SpellID::BLESS), firstFriendly));
	ASSERT_NE(blessEffect(firstFriendly), nullptr);
	EXPECT_EQ(blessEffect(firstFriendly)->val, 0);
	EXPECT_EQ(blessEffect(secondFriendly), nullptr)
		<< "Expert Light mastery does not turn the ordinary Bless entry into a Mass cast";
}

TEST_F(NewHorizonsMassBlessTest, MassBlessUsesBlessPowerCapBenedictionWisdomCostAndDetachedProjection)
{
	prepareHero(true, MasteryLevel::ADVANCED, 500, MasteryLevel::EXPERT);
	prepareBattle();
	const auto variant = massBlessSpell();
	ASSERT_EQ(newHorizonsMagic::spellVariantBase(attackerSideHero->getMagicRules(), variant),
		SpellID(SpellID::BLESS));
	ASSERT_FALSE(newHorizonsMagic::spellAvailableForOrdinaryAcquisition(
		attackerSideHero->getMagicRules(), variant));
	EXPECT_FALSE(attackerSideHero->canLearnSpell(variant.toSpell(), true));

	const auto & rules = battle()->getBattle()->getMagicRules();
	const auto & baseRow = rules["spells"]["core:bless"];
	const auto & variantRow = rules["spells"][massBlessKey];
	ASSERT_EQ(baseRow["schools"].Vector().size(), 1u);
	ASSERT_EQ(variantRow["schools"].Vector().size(), 1u);
	EXPECT_EQ(variantRow["schools"].Vector().front().String(), baseRow["schools"].Vector().front().String());
	EXPECT_EQ(variantRow["level"].Integer(), baseRow["level"].Integer());
	for(const int mastery : {MasteryLevel::NONE, MasteryLevel::BASIC,
		MasteryLevel::ADVANCED, MasteryLevel::EXPERT})
	{
		EXPECT_EQ(newHorizonsMagic::spellCost(rules, variant, mastery),
			3 * newHorizonsMagic::spellCost(rules, SpellID(SpellID::BLESS), mastery))
			<< "listed cost before Wisdom at mastery rank " << mastery;
	}

	const auto * massBlessDefinition = variant.toSpell();
	ASSERT_NE(massBlessDefinition, nullptr);
	const int listedCost = newHorizonsMagic::spellCost(rules, variant, MasteryLevel::ADVANCED);
	const int expectedManaCost = newHorizonsMagic::wisdomAdjustedCost(
		listedCost, 1, MasteryLevel::EXPERT);
	EXPECT_EQ(battle()->battleGetSpellCost(massBlessDefinition, attackerSideHero), expectedManaCost);

	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 62, ChangeValueMode::ABSOLUTE);
	spells::BattleCast thresholdBaseCast(battle(), attackerSideHero, spells::Mode::HERO, bless);
	spells::BattleCast thresholdVariantCast(battle(), attackerSideHero, spells::Mode::HERO, massBlessDefinition);
	const int thresholdDuration = newHorizonsMagic::BLESS_BASE_DURATION + 2;
	EXPECT_EQ(bless->battleMechanics(&thresholdBaseCast)->getEffectDuration(), thresholdDuration)
		<< "Advanced Light at 62 Spell Power gives three ordinary rounds plus Benediction";
	EXPECT_EQ(massBlessDefinition->battleMechanics(&thresholdVariantCast)->getEffectDuration(), thresholdDuration)
		<< "Mass Bless uses the same uncapped Bless duration term";
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 500, ChangeValueMode::ABSOLUTE);

	Bonus priorCurse(BonusDuration::N_TURNS, BonusType::ALWAYS_MINIMUM_DAMAGE,
		BonusSource::SPELL_EFFECT, 0, BonusSourceID(SpellID(SpellID::CURSE)));
	priorCurse.turnsRemain = 2;
	secondFriendly->addNewBonus(std::make_shared<Bonus>(priorCurse));
	ASSERT_NE(curseEffect(secondFriendly), nullptr);

	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	MassBlessPredictionEnvironment environment(gameState());
	HypotheticBattle projected(&environment, callback);
	spells::BattleCast projectedCast(&projected, attackerSideHero, spells::Mode::HERO, massBlessDefinition);
	auto projectedMechanics = massBlessDefinition->battleMechanics(&projectedCast);
	ASSERT_TRUE(projectedMechanics->isMassive());
	EXPECT_EQ(projectedMechanics->getEffectDuration(), newHorizonsMagic::BLESS_MAX_DURATION + 1)
		<< "The high Spell Power term is capped before Benediction adds one round";
	EXPECT_EQ(affectedIds(*projectedMechanics, massAim()),
		(std::set<uint32_t>{firstFriendly->unitId(), secondFriendly->unitId()}));
	const int manaBefore = attackerSideHero->getManaAvailable();
	Bonus priorBless(BonusDuration::N_TURNS, BonusType::ALWAYS_MAXIMUM_DAMAGE,
		BonusSource::SPELL_EFFECT, 0, BonusSourceID(SpellID(SpellID::BLESS)));
	priorBless.turnsRemain = 1;
	firstFriendly->addNewBonus(std::make_shared<Bonus>(priorBless));
	projected.addUnitBonus(firstFriendly->unitId(), {priorBless});
	projectedMechanics->castEval(projected.getServerCallback(), massAim());

	const auto projectedFirst = blessEffect(projected.battleGetUnitByID(firstFriendly->unitId()));
	const auto projectedSecond = blessEffect(projected.battleGetUnitByID(secondFriendly->unitId()));
	ASSERT_NE(projectedFirst, nullptr);
	ASSERT_NE(projectedSecond, nullptr);
	const int projectedValue = projectedFirst->val;
	const int projectedDuration = projectedFirst->turnsRemain;
	EXPECT_EQ(projectedValue, 0);
	EXPECT_EQ(projectedSecond->val, projectedValue);
	EXPECT_EQ(blessEffectCount(projected.battleGetUnitByID(firstFriendly->unitId())), 1u)
		<< "The already-materialized AI stack refreshes its family-scoped Bless instead of duplicating it";
	EXPECT_EQ(projectedDuration, newHorizonsMagic::BLESS_MAX_DURATION + 1);
	EXPECT_EQ(projectedSecond->turnsRemain, projectedDuration);
	EXPECT_EQ(blessEffect(projected.battleGetUnitByID(blessImmuneFriendly->unitId())), nullptr);
	EXPECT_EQ(blessEffect(projected.battleGetUnitByID(massBlessImmuneFriendly->unitId())), nullptr);
	EXPECT_EQ(blessEffect(projected.battleGetUnitByID(enemy->unitId())), nullptr);
	EXPECT_EQ(curseEffect(projected.battleGetUnitByID(secondFriendly->unitId())), nullptr)
		<< "Mass Bless removes the existing Curse in the detached forecast";
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore)
		<< "The detached AI projection evaluates the already-materialized battle without spending real Mana";

	ASSERT_TRUE(castSpell(variant));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore - expectedManaCost);
	for(const CStack * unit : {firstFriendly, secondFriendly})
	{
		const auto applied = blessEffect(unit);
		ASSERT_NE(applied, nullptr);
		EXPECT_EQ(applied->val, projectedValue);
		EXPECT_EQ(applied->turnsRemain, projectedDuration);
	}
	EXPECT_EQ(blessEffectCount(firstFriendly), 1u);
	EXPECT_EQ(blessEffect(blessImmuneFriendly), nullptr);
	EXPECT_EQ(blessEffect(massBlessImmuneFriendly), nullptr);
	EXPECT_EQ(blessEffect(enemy), nullptr);
	EXPECT_EQ(curseEffect(secondFriendly), nullptr)
		<< "The accepted Mass Bless removes the same Curse as the detached forecast";
}

TEST_F(NewHorizonsMassBlessTest, ManualAndScrollAcquisitionCannotGrantMassBless)
{
	prepareHero(false);
	const auto variant = massBlessSpell();
	ASSERT_FALSE(newHorizonsMagic::spellAvailableForOrdinaryAcquisition(
		attackerSideHero->getMagicRules(), variant));
	EXPECT_FALSE(attackerSideHero->canLearnSpell(variant.toSpell(), true));

	attackerSideHero->addSpellToSpellbook(variant);
	ASSERT_TRUE(gameHandler->giveHeroNewScroll(
		attackerSideHero, variant, ArtifactPosition::FIRST_AVAILABLE));
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(variant));
	EXPECT_TRUE(attackerSideHero->getSourcesForSpell(variant).empty());
	EXPECT_FALSE(attackerSideHero->getSpellsInSpellbook().contains(variant));
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(variant.toSpell()));
}

TEST_F(NewHorizonsMassBlessTest, OlderSavedRosterWithoutMassBlessCannotResolveOrGrantTheVariant)
{
	oldSnapshot = true;
	prepareHero(true);
	const auto variant = massBlessSpell();
	const auto & rules = attackerSideHero->getMagicRules();
	EXPECT_EQ(rules["rulesetVersion"].Integer(), newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION);
	EXPECT_EQ(newHorizonsMagic::spellVariantBase(rules, variant), variant);
	EXPECT_FALSE(newHorizonsMagic::spellAllowedBySavedRoster(rules, variant));
	EXPECT_FALSE(newHorizonsMagic::variantGrantAvailable(rules, attackerSideHero, variant));
	EXPECT_FALSE(attackerSideHero->isSpellInscribedForCasting(variant));
	EXPECT_TRUE(attackerSideHero->getSourcesForSpell(variant).empty());
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(variant.toSpell()));
}
