/*
 * NewHorizonsMassRegenerationTest.cpp, part of VCMI engine
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
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/CUnitState.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/entities/hero/NewHorizonsPerkState.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/NewHorizonsSorcery.h"
#include "../../../lib/spells/NewHorizonsSpellAvailability.h"
#include <vcmi/Environment.h>

#include <memory>
#include <set>
#include <string>
#include <utility>

namespace
{
constexpr auto massRegenerationKey = "new-horizons:massRegeneration";
constexpr auto natureMagicSkill = "new-horizons:natureMagic";
constexpr auto rootcallerPerk = "new-horizons:natureMagic.rootcaller";
constexpr auto verdantCommunionPerk = "new-horizons:natureMagic.verdantCommunion";

SpellID spellNamed(const std::string & name)
{
	return SpellID(SpellID::decode(name));
}

SpellID massRegenerationSpell()
{
	return spellNamed(massRegenerationKey);
}

SpellID regenerationSpell()
{
	return spellNamed(std::string(newHorizonsMagic::NATURE_REGENERATION_SPELL));
}

SpellID spellLockSpell()
{
	return spellNamed("new-horizons:spellLock");
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
	}
	for(auto it = rules["spells"].Struct().begin(); it != rules["spells"].Struct().end();)
	{
		if(it->second.isStruct() && it->second.Struct().contains("variant"))
			it = rules["spells"].Struct().erase(it);
		else
			++it;
	}
	return rules;
}

class MassRegenerationPredictionEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit MassRegenerationPredictionEnvironment(std::shared_ptr<CGameState> state)
		: state(std::move(state))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
}

class NewHorizonsMassRegenerationTest : public HeroCommandFixture
{
protected:
	bool oldSnapshot = false;
	CStack * firstFriendly = nullptr;
	CStack * secondFriendly = nullptr;
	CStack * undeadFriendly = nullptr;
	CStack * nonLivingFriendly = nullptr;
	CStack * mechanicalFriendly = nullptr;
	CStack * spellLockedFriendly = nullptr;
	CStack * baseImmuneFriendly = nullptr;
	CStack * massImmuneFriendly = nullptr;
	CStack * cloneFriendly = nullptr;
	CStack * phantomFriendly = nullptr;
	CStack * siegeFriendly = nullptr;
	CStack * enemy = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
		ASSERT_NE(massRegenerationSpell(), SpellID::NONE);
		ASSERT_NE(regenerationSpell(), SpellID::NONE);
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);

		JsonNode magicRules = oldSnapshot
			? olderMagicSnapshotWithoutMassVariants()
			: JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
		if(!oldSnapshot)
			magicRules["spells"][massRegenerationKey]["active"] = JsonNode(true);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magicRules);

		JsonNode perkRules(JsonPath::builtin("config/newHorizonsPerks"));
		for(auto & perk : perkRules["skills"][natureMagicSkill]["perks"].Vector())
			if(perk["id"].String() == verdantCommunionPerk)
				perk["effect"]["status"] = JsonNode("active");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perkRules);
	}

	void prepareHero(bool selectCommunion, int natureRank = MasteryLevel::ADVANCED,
		int spellPower = 40, int wisdomRank = MasteryLevel::NONE)
	{
		startGame();
		attackerSideHero->removeAllSpells();
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(regenerationSpell());
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		const SecondarySkill nature(SecondarySkill::decode(natureMagicSkill));
		ASSERT_TRUE(nature.hasValue());
		attackerSideHero->setSecSkillLevel(nature, natureRank, ChangeValueMode::ABSOLUTE);
		if(wisdomRank > MasteryLevel::NONE)
		{
			const SecondarySkill wisdom(SecondarySkill::decode("new-horizons:wisdom"));
			ASSERT_TRUE(wisdom.hasValue());
			attackerSideHero->setSecSkillLevel(wisdom, wisdomRank, ChangeValueMode::ABSOLUTE);
		}
		setTestSpellPointTotal(attackerSideHero, 1000);

		if(selectCommunion)
			selectCommunionAtAdvancedRank();
	}

	void selectCommunionAtAdvancedRank()
	{
		const SecondarySkill nature(SecondarySkill::decode(natureMagicSkill));
		attackerSideHero->setSecSkillLevel(nature, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({natureMagicSkill, rootcallerPerk});
		ASSERT_TRUE(attackerSideHero->hasActivePerk(natureMagicSkill, rootcallerPerk));
		attackerSideHero->setSecSkillLevel(nature, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({natureMagicSkill, verdantCommunionPerk});
		ASSERT_TRUE(attackerSideHero->hasActivePerk(natureMagicSkill, verdantCommunionPerk));
	}

	void prepareBattle(bool includeEligibilityExclusions = true)
	{
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		if(!remove.changedStacks.empty())
			gameHandler->sendAndApply(remove);

		firstFriendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 20);
		secondFriendly = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(6, 5), 20);
		if(includeEligibilityExclusions)
		{
			undeadFriendly = addStack(BattleSide::ATTACKER, creatureByName("core:skeleton"), BattleHex(3, 7), 20);
			nonLivingFriendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(6, 7), 20);
			mechanicalFriendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(9, 7), 20);
			spellLockedFriendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 9), 20);
			baseImmuneFriendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(6, 9), 20);
			massImmuneFriendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(9, 9), 20);
			cloneFriendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(12, 7), 20);
			phantomFriendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(12, 9), 20);
			siegeFriendly = addStack(BattleSide::ATTACKER, creatureByName("core:ballista"), BattleHex(14, 3), 1);
		}
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 20);
		ASSERT_NE(firstFriendly, nullptr);
		ASSERT_NE(secondFriendly, nullptr);
		ASSERT_NE(enemy, nullptr);
		if(includeEligibilityExclusions)
		{
			ASSERT_NE(undeadFriendly, nullptr);
			ASSERT_NE(nonLivingFriendly, nullptr);
			ASSERT_NE(mechanicalFriendly, nullptr);
			ASSERT_NE(spellLockedFriendly, nullptr);
			ASSERT_NE(baseImmuneFriendly, nullptr);
			ASSERT_NE(massImmuneFriendly, nullptr);
			ASSERT_NE(cloneFriendly, nullptr);
			ASSERT_NE(phantomFriendly, nullptr);
			ASSERT_NE(siegeFriendly, nullptr);
			ASSERT_TRUE(undeadFriendly->hasBonusOfType(BonusType::UNDEAD));
			ASSERT_TRUE(siegeFriendly->hasBonusOfType(BonusType::SIEGE_WEAPON));
			addTrait(nonLivingFriendly, BonusType::NON_LIVING);
			addTrait(mechanicalFriendly, BonusType::MECHANICAL);
			markAsClone(cloneFriendly);
			markAsPhantom(phantomFriendly);
			addSpellLock(spellLockedFriendly);
			addSpellImmunity(baseImmuneFriendly, regenerationSpell());
			addSpellImmunity(massImmuneFriendly, massRegenerationSpell());
		}
		beginCombat();
	}

	void addTrait(CStack * unit, BonusType type)
	{
		unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			type, BonusSource::OTHER, 1, BonusSourceID()));
	}

	void addSpellImmunity(CStack * unit, SpellID spell)
	{
		unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::SPELL_IMMUNITY, BonusSource::OTHER, 1, BonusSourceID(), BonusSubtypeID(spell)));
	}

	void addSpellLock(CStack * unit)
	{
		Bonus lock(BonusDuration::N_TURNS, BonusType::MAGIC_RESISTANCE,
			BonusSource::SPELL_EFFECT, 100, BonusSourceID(spellLockSpell()));
		lock.turnsRemain = 1;
		unit->addNewBonus(std::make_shared<Bonus>(lock));
	}

	void applyState(CStack * unit, std::shared_ptr<battle::CUnitState> state)
	{
		UnitChanges update(unit->unitId(), UnitChanges::EOperation::UPDATE);
		update.data = state->save();
		BattleUnitsChanged changed;
		changed.battleID = BattleID(0);
		changed.changedStacks.push_back(std::move(update));
		gameHandler->sendAndApply(changed);
	}

	void markAsClone(CStack * unit)
	{
		auto state = unit->acquireState();
		state->cloned = true;
		applyState(unit, std::move(state));
		ASSERT_TRUE(unit->isClone());
	}

	void markAsPhantom(CStack * unit)
	{
		auto state = unit->acquireState();
		state->summoned = true;
		state->initializePhantomProfile(unit->getAvailableHealth(),
			newHorizonsSorcery::PHANTOM_ARMY_DURATION_ROUNDS);
		applyState(unit, std::move(state));
		ASSERT_GT(unit->getPhantomInitialIntegrity(), 0);
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

	std::shared_ptr<const Bonus> regenerationEffect(const battle::Unit * unit,
		SpellID source = regenerationSpell()) const
	{
		if(!unit)
			return {};
		const auto effects = unit->getBonuses(Selector::source(
			BonusSource::SPELL_EFFECT, BonusSourceID(source)).And(Selector::type()(BonusType::HP_REGENERATION)));
		return effects && !effects->empty() ? effects->front() : std::shared_ptr<const Bonus>{};
	}

	size_t regenerationEffectCount(const battle::Unit * unit, SpellID source = regenerationSpell()) const
	{
		if(!unit)
			return 0;
		const auto effects = unit->getBonuses(Selector::source(
			BonusSource::SPELL_EFFECT, BonusSourceID(source)).And(Selector::type()(BonusType::HP_REGENERATION)));
		return effects ? effects->size() : 0;
	}

	void expectVariantSource(bool present)
	{
		const auto variant = massRegenerationSpell();
		EXPECT_EQ(attackerSideHero->getSpellsInSpellbook().contains(variant), present);
		EXPECT_EQ(attackerSideHero->getInscribedSpellsForCasting().contains(variant), present);
		EXPECT_EQ(attackerSideHero->canCastThisSpell(variant.toSpell()), present);
		EXPECT_EQ(!attackerSideHero->getSourcesForSpell(variant).empty(), present);
		EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(variant))
			<< "Verdant Communion supplies an ephemeral source and must not durably inscribe Mass Regeneration";
	}

	void injure(CStack * stack, int64_t amount)
	{
		auto state = stack->acquireState();
		state->damage(amount);
		UnitChanges update(stack->unitId(), UnitChanges::EOperation::UPDATE);
		update.data = state->save();
		update.healthDelta = -amount;
		BattleUnitsChanged changed;
		changed.battleID = BattleID(0);
		changed.changedStacks.push_back(std::move(update));
		gameHandler->sendAndApply(changed);
	}

	bool advanceUntilNextActivation(const CStack * stack)
	{
		for(int attempt = 0; attempt < 32; ++attempt)
		{
			const auto * active = battle()->battleActiveUnit();
			if(!active)
				return false;
			const auto player = battle()->sideToPlayer(active->unitSide());
			const auto action = BattleAction::makeDefend(active);
			if(!gameHandler->battles->makePlayerBattleAction(BattleID(0), player, action))
				return false;
			if(battle()->battleActiveUnit() == stack)
				return true;
		}
		return false;
	}

	bool advanceRoundBounded(int maxActions = 64)
	{
		const int32_t startingRound = battle()->getRound();
		for(int attempt = 0; attempt < maxActions && battle()->getRound() == startingRound; ++attempt)
		{
			const auto * active = battle()->battleActiveUnit();
			if(!active)
				return false;
			const auto player = battle()->sideToPlayer(active->unitSide());
			const auto action = BattleAction::makeDefend(active);
			if(!gameHandler->battles->makePlayerBattleAction(BattleID(0), player, action))
				return false;
		}
		return battle()->getRound() == startingRound + 1;
	}

	void setNearlyExpiredFamilyMarker(CStack * unit)
	{
		const auto familyMarker = Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(regenerationSpell())).And(Selector::type()(BonusType::HP_REGENERATION));
		unit->removeBonusesRecursive(familyMarker);
		Bonus marker(BonusDuration::N_TURNS, BonusType::HP_REGENERATION,
			BonusSource::SPELL_EFFECT, 0, BonusSourceID(regenerationSpell()));
		marker.turnsRemain = 1;
		unit->addNewBonus(std::make_shared<Bonus>(marker));
	}
};

TEST_F(NewHorizonsMassRegenerationTest, AdvancedCommunionIsOfferedAndItsVirtualGrantRevokesWhenRankFalls)
{
	prepareHero(false, MasteryLevel::BASIC);
	attackerSideHero->applyPerkSelection({natureMagicSkill, rootcallerPerk});
	ASSERT_TRUE(attackerSideHero->hasActivePerk(natureMagicSkill, rootcallerPerk));
	const auto variant = massRegenerationSpell();
	const SecondarySkill nature(SecondarySkill::decode(natureMagicSkill));
	const auto rankLookup = [this](const std::string & skillId)
	{
		return skillId == natureMagicSkill
			? static_cast<int>(attackerSideHero->getSecSkillLevel(SecondarySkill(SecondarySkill::decode(natureMagicSkill))))
			: 0;
	};

	EXPECT_FALSE(newHorizonsMagic::variantGrantAvailable(
		attackerSideHero->getMagicRules(), attackerSideHero, variant));
	expectVariantSource(false);
	EXPECT_FALSE(newHorizonsMagic::spellAvailableForOrdinaryAcquisition(attackerSideHero->getMagicRules(), variant));
	EXPECT_FALSE(attackerSideHero->canLearnSpell(variant.toSpell(), true));
	attackerSideHero->addSpellToSpellbook(variant);
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(variant));
	EXPECT_TRUE(attackerSideHero->getSourcesForSpell(variant).empty());
	EXPECT_FALSE(attackerSideHero->getSpellsInSpellbook().contains(variant));
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(variant.toSpell()));
	attackerSideHero->removeSpellFromSpellbook(variant);
	ASSERT_TRUE(gameHandler->giveHeroNewScroll(
		attackerSideHero, variant, ArtifactPosition::FIRST_AVAILABLE));
	EXPECT_TRUE(attackerSideHero->getSourcesForSpell(variant).empty());
	EXPECT_FALSE(attackerSideHero->getSpellsInSpellbook().contains(variant));
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(variant.toSpell()));

	const auto hasCommunionOffer = [](const auto & candidateOffers)
	{
		return std::ranges::any_of(candidateOffers, [](const auto & offer)
		{
			return offer.selection.perkId == verdantCommunionPerk
				&& offer.requiredRank == MasteryLevel::ADVANCED;
		});
	};
	attackerSideHero->setSecSkillLevel(nature, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	auto offers = attackerSideHero->getPerkState().prepareOffer(rankLookup, 0);
	for(int seed = 1; seed <= 4095 && !hasCommunionOffer(offers); ++seed)
		offers = attackerSideHero->getPerkState().prepareOffer(rankLookup, seed);
	const auto communionOffer = std::ranges::find_if(offers, [](const auto & offer)
	{
		return offer.selection.perkId == verdantCommunionPerk
			&& offer.requiredRank == MasteryLevel::ADVANCED;
	});
	ASSERT_NE(communionOffer, offers.end())
		<< "A deterministic bounded seed search finds the Advanced Verdant Communion offer";
	attackerSideHero->applyPerkSelection(communionOffer->selection);
	ASSERT_TRUE(attackerSideHero->hasActivePerk(natureMagicSkill, verdantCommunionPerk));
	EXPECT_TRUE(newHorizonsMagic::variantGrantAvailable(
		attackerSideHero->getMagicRules(), attackerSideHero, variant));
	expectVariantSource(true);

	attackerSideHero->setSecSkillLevel(nature, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	EXPECT_FALSE(attackerSideHero->hasActivePerk(natureMagicSkill, verdantCommunionPerk));
	EXPECT_FALSE(newHorizonsMagic::variantGrantAvailable(
		attackerSideHero->getMagicRules(), attackerSideHero, variant));
	expectVariantSource(false);
}

TEST_F(NewHorizonsMassRegenerationTest, ActiveCommunionRequiresThePhysicalSpellbook)
{
	prepareHero(true);
	ASSERT_TRUE(attackerSideHero->hasSpellbook());
	ASSERT_TRUE(attackerSideHero->hasActivePerk(natureMagicSkill, verdantCommunionPerk));
	expectVariantSource(true);

	attackerSideHero->removeArtifact(ArtifactPosition::SPELLBOOK);
	EXPECT_FALSE(attackerSideHero->hasSpellbook());
	EXPECT_TRUE(attackerSideHero->hasActivePerk(natureMagicSkill, verdantCommunionPerk));
	EXPECT_FALSE(newHorizonsMagic::variantGrantAvailable(
		attackerSideHero->getMagicRules(), attackerSideHero, massRegenerationSpell()));
	expectVariantSource(false);
}

TEST_F(NewHorizonsMassRegenerationTest, AcceptedMassCastUsesThreeTimesTheListedCostAndOnlyReachesEligibleLivingAllies)
{
	prepareHero(true, MasteryLevel::ADVANCED, 40, MasteryLevel::EXPERT);
	prepareBattle();
	const auto variant = massRegenerationSpell();
	const auto base = regenerationSpell();
	const auto * variantDefinition = variant.toSpell();
	const auto * baseDefinition = base.toSpell();
	ASSERT_NE(variantDefinition, nullptr);
	ASSERT_NE(baseDefinition, nullptr);
	ASSERT_TRUE(newHorizonsMagic::variantGrantAvailable(
		attackerSideHero->getMagicRules(), attackerSideHero, variant));
	EXPECT_FALSE(newHorizonsMagic::spellAvailableForOrdinaryAcquisition(
		attackerSideHero->getMagicRules(), variant));
	EXPECT_FALSE(attackerSideHero->canLearnSpell(variantDefinition, true));

	const auto & rules = battle()->getBattle()->getMagicRules();
	ASSERT_EQ(newHorizonsMagic::spellVariantBase(rules, variant), base);
	const auto & variantRow = rules["spells"][std::string(massRegenerationKey)];
	const auto & baseRow = rules["spells"][std::string(newHorizonsMagic::NATURE_REGENERATION_SPELL)];
	ASSERT_EQ(variantRow["schools"].Vector().size(), baseRow["schools"].Vector().size());
	ASSERT_EQ(variantRow["schools"].Vector().size(), 1u);
	EXPECT_EQ(variantRow["schools"].Vector().front().String(), baseRow["schools"].Vector().front().String());
	EXPECT_EQ(variantRow["level"].Integer(), baseRow["level"].Integer());
	for(const int mastery : {MasteryLevel::NONE, MasteryLevel::BASIC,
		MasteryLevel::ADVANCED, MasteryLevel::EXPERT})
	{
		EXPECT_EQ(newHorizonsMagic::spellCost(rules, variant, mastery),
			3 * newHorizonsMagic::spellCost(rules, base, mastery))
			<< "listed cost before Wisdom at mastery rank " << mastery;
	}
	const int listedCost = newHorizonsMagic::spellCost(rules, variant, MasteryLevel::ADVANCED);
	const int expectedManaCost = newHorizonsMagic::wisdomAdjustedCost(
		listedCost, 1, MasteryLevel::EXPERT);
	EXPECT_EQ(battle()->battleGetSpellCost(variantDefinition, attackerSideHero), expectedManaCost);

	spells::BattleCast baseCast(battle(), attackerSideHero, spells::Mode::HERO, baseDefinition);
	const auto baseMechanics = baseDefinition->battleMechanics(&baseCast);
	EXPECT_FALSE(baseMechanics->isMassive())
		<< "Advanced Nature proficiency does not transform ordinary Regeneration into a Mass cast";
	EXPECT_EQ(affectedIds(*baseMechanics, {spells::Destination(firstFriendly)}),
		(std::set<uint32_t>{firstFriendly->unitId()}));

	spells::BattleCast variantCast(battle(), attackerSideHero, spells::Mode::HERO, variantDefinition);
	const auto variantMechanics = variantDefinition->battleMechanics(&variantCast);
	ASSERT_TRUE(variantMechanics->isMassive());
	const std::set<uint32_t> eligible{firstFriendly->unitId(), secondFriendly->unitId()};
	EXPECT_EQ(affectedIds(*variantMechanics, massAim()), eligible)
		<< "Mass Regeneration excludes undead, non-living, mechanical, Spell Locked, immune, clone, phantom, siege and enemy stacks";

	const int manaBefore = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(castSpell(variant));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore - expectedManaCost);
	for(const CStack * unit : {firstFriendly, secondFriendly})
	{
		ASSERT_NE(regenerationEffect(unit), nullptr);
		EXPECT_EQ(regenerationEffectCount(unit), 1u);
		EXPECT_EQ(unit->regenerationRateMillionths, 328'000)
			<< "Advanced Nature scales only Regeneration's Spell Power term";
		EXPECT_EQ(unit->regenerationPendingMicroHealth, 0);
	}
	for(const CStack * unit : {undeadFriendly, nonLivingFriendly, mechanicalFriendly,
		spellLockedFriendly, baseImmuneFriendly, massImmuneFriendly, cloneFriendly,
		phantomFriendly, siegeFriendly, enemy})
	{
		EXPECT_EQ(regenerationEffect(unit), nullptr);
		EXPECT_EQ(unit->regenerationRateMillionths, 0);
	}

	const int32_t initialCount = firstFriendly->getCount();
	const int64_t maxCreatureHealth = firstFriendly->getMaxHealth();
	injure(firstFriendly, maxCreatureHealth + 2);
	ASSERT_EQ(firstFriendly->getCount(), initialCount - 1);
	EXPECT_EQ(firstFriendly->getFirstHPleft(), maxCreatureHealth - 2);
	injure(firstFriendly, 4);
	const int32_t survivorsBeforeActivation = firstFriendly->getCount();
	const int64_t expectedHeal = firstFriendly->regenerationProjectedHeal();
	ASSERT_GT(expectedHeal, 0);
	const int64_t healthBeforeActivation = firstFriendly->getAvailableHealth();
	ASSERT_TRUE(advanceUntilNextActivation(firstFriendly));
	EXPECT_EQ(firstFriendly->getAvailableHealth(), healthBeforeActivation + expectedHeal);
	EXPECT_EQ(firstFriendly->getCount(), survivorsBeforeActivation)
		<< "The activation restores marked wounds on survivors without resurrecting the casualty";
	EXPECT_EQ(firstFriendly->regenerationPendingMicroHealth, 0);
}

TEST_F(NewHorizonsMassRegenerationTest, MassRefreshMatchesDetachedMaterializedForecastAndPreservesPendingWounds)
{
	prepareHero(true);
	prepareBattle(false);
	const auto base = regenerationSpell();
	const auto variant = massRegenerationSpell();
	ASSERT_TRUE(castSpell(base, firstFriendly));
	ASSERT_EQ(firstFriendly->regenerationRateMillionths, 328'000);

	// Start the mass cast with a real family effect near expiry and actual
	// pending wounds. The projected battle below is copied after both exist.
	const int32_t startingRound = battle()->getRound();
	ASSERT_TRUE(advanceRoundBounded());
	ASSERT_EQ(battle()->getRound(), startingRound + 1);
	setNearlyExpiredFamilyMarker(firstFriendly);
	injure(firstFriendly, 4);
	injure(secondFriendly, 3);
	const int64_t pendingBeforeForecast = firstFriendly->regenerationPendingMicroHealth;
	ASSERT_EQ(pendingBeforeForecast, INT64_C(1312000));
	const int64_t firstHealthBeforeForecast = firstFriendly->getAvailableHealth();
	const int64_t secondHealthBeforeForecast = secondFriendly->getAvailableHealth();
	const int manaBeforeForecast = attackerSideHero->getManaAvailable();

	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	MassRegenerationPredictionEnvironment environment(gameState());
	HypotheticBattle projected(&environment, callback);
	const auto projectedMaterializedFirstState = projected.getForUpdate(firstFriendly->unitId());
	int64_t projectedOnlyDamage = 2;
	projectedMaterializedFirstState->damage(projectedOnlyDamage);
	ASSERT_EQ(projectedOnlyDamage, 2);
	const int64_t projectedOnlyPending = projectedMaterializedFirstState->regenerationPendingMicroHealth;
	const int64_t projectedOnlyHealth = projectedMaterializedFirstState->getAvailableHealth();
	EXPECT_GT(projectedOnlyPending, pendingBeforeForecast);
	EXPECT_EQ(projectedOnlyHealth, firstHealthBeforeForecast - 2);
	EXPECT_EQ(firstFriendly->regenerationPendingMicroHealth, pendingBeforeForecast)
		<< "Materializing and damaging the detached copy must leave the live battle untouched";
	EXPECT_EQ(firstFriendly->getAvailableHealth(), firstHealthBeforeForecast);
	const auto * definition = variant.toSpell();
	ASSERT_NE(definition, nullptr);
	spells::BattleCast projectedCast(&projected, attackerSideHero, spells::Mode::HERO, definition);
	const auto projectedMechanics = definition->battleMechanics(&projectedCast);
	ASSERT_TRUE(projectedMechanics->isMassive());
	projectedMechanics->castEval(projected.getServerCallback(), massAim());

	const auto * projectedFirst = projected.battleGetUnitByID(firstFriendly->unitId());
	const auto * projectedSecond = projected.battleGetUnitByID(secondFriendly->unitId());
	const auto * projectedFirstState = dynamic_cast<const battle::CUnitState *>(projectedFirst);
	const auto * projectedSecondState = dynamic_cast<const battle::CUnitState *>(projectedSecond);
	ASSERT_NE(projectedFirstState, nullptr);
	ASSERT_NE(projectedSecondState, nullptr);
	const auto projectedFirstEffect = regenerationEffect(projectedFirst);
	const auto projectedSecondEffect = regenerationEffect(projectedSecond);
	ASSERT_NE(projectedFirstEffect, nullptr);
	ASSERT_NE(projectedSecondEffect, nullptr);
	EXPECT_EQ(regenerationEffectCount(projectedFirst), 1u)
		<< "The variant refreshes the existing base-family marker instead of duplicating it";
	EXPECT_EQ(regenerationEffectCount(projectedSecond), 1u);
	EXPECT_EQ(regenerationEffectCount(projectedFirst, variant), 0u)
		<< "Regeneration state remains keyed to the base spell family";
	EXPECT_GT(projectedFirstEffect->turnsRemain, 1)
		<< "Mass Regeneration refreshes the near-expired family duration";
	EXPECT_EQ(projectedFirstEffect->turnsRemain, projectedSecondEffect->turnsRemain);
	EXPECT_EQ(projectedFirstState->regenerationRateMillionths, 328'000);
	EXPECT_EQ(projectedSecondState->regenerationRateMillionths, 328'000);
	EXPECT_EQ(projectedFirstState->regenerationPendingMicroHealth, projectedOnlyPending)
		<< "Mass refresh preserves marks added to the already-materialized detached unit";
	EXPECT_EQ(projectedSecondState->regenerationPendingMicroHealth, 0)
		<< "Existing wounds are not retroactively marked before the accepted Mass cast";
	EXPECT_EQ(projectedFirstState->getAvailableHealth(), projectedOnlyHealth)
		<< "Forecast refresh must not heal existing or forecast-only wounds at cast time";
	EXPECT_EQ(projectedSecondState->getAvailableHealth(), secondHealthBeforeForecast);
	EXPECT_EQ(firstFriendly->regenerationPendingMicroHealth, pendingBeforeForecast)
		<< "Forecasting an already-materialized battle must not mutate the source battle";
	EXPECT_EQ(firstFriendly->getAvailableHealth(), firstHealthBeforeForecast);
	EXPECT_EQ(secondFriendly->getAvailableHealth(), secondHealthBeforeForecast);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBeforeForecast);

	ASSERT_TRUE(castSpell(variant));
	const auto actualFirstEffect = regenerationEffect(firstFriendly);
	const auto actualSecondEffect = regenerationEffect(secondFriendly);
	ASSERT_NE(actualFirstEffect, nullptr);
	ASSERT_NE(actualSecondEffect, nullptr);
	EXPECT_EQ(regenerationEffectCount(firstFriendly), 1u);
	EXPECT_EQ(regenerationEffectCount(secondFriendly), 1u);
	EXPECT_GT(actualFirstEffect->turnsRemain, 1);
	EXPECT_EQ(actualFirstEffect->turnsRemain, projectedFirstEffect->turnsRemain);
	EXPECT_EQ(actualSecondEffect->turnsRemain, projectedSecondEffect->turnsRemain);
	EXPECT_EQ(firstFriendly->regenerationRateMillionths, projectedFirstState->regenerationRateMillionths);
	EXPECT_EQ(secondFriendly->regenerationRateMillionths, projectedSecondState->regenerationRateMillionths);
	EXPECT_EQ(firstFriendly->regenerationPendingMicroHealth, pendingBeforeForecast);
	EXPECT_EQ(secondFriendly->regenerationPendingMicroHealth, projectedSecondState->regenerationPendingMicroHealth);
	EXPECT_EQ(firstFriendly->getAvailableHealth(), firstHealthBeforeForecast);
	EXPECT_EQ(secondFriendly->getAvailableHealth(), secondHealthBeforeForecast);
}

TEST_F(NewHorizonsMassRegenerationTest, OlderRosterKeepsBaseRegenerationSingleTargetAndDoesNotGainTheVariant)
{
	oldSnapshot = true;
	prepareHero(true);
	const auto variant = massRegenerationSpell();
	const auto base = regenerationSpell();
	const auto & rules = attackerSideHero->getMagicRules();
	EXPECT_EQ(rules["rulesetVersion"].Integer(), newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION);
	EXPECT_EQ(newHorizonsMagic::spellVariantBase(rules, variant), variant);
	EXPECT_FALSE(newHorizonsMagic::spellAllowedBySavedRoster(rules, variant));
	EXPECT_FALSE(newHorizonsMagic::variantGrantAvailable(rules, attackerSideHero, variant));
	EXPECT_FALSE(attackerSideHero->isSpellInscribedForCasting(variant));
	EXPECT_TRUE(attackerSideHero->getSourcesForSpell(variant).empty());
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(variant.toSpell()));
	EXPECT_TRUE(attackerSideHero->canCastThisSpell(base.toSpell()));

	prepareBattle();
	spells::BattleCast baseCast(battle(), attackerSideHero, spells::Mode::HERO, base.toSpell());
	const auto baseMechanics = base.toSpell()->battleMechanics(&baseCast);
	EXPECT_FALSE(baseMechanics->isMassive());
	EXPECT_EQ(affectedIds(*baseMechanics, {spells::Destination(firstFriendly)}),
		(std::set<uint32_t>{firstFriendly->unitId()}));
	ASSERT_TRUE(castSpell(base, firstFriendly));
	EXPECT_NE(regenerationEffect(firstFriendly), nullptr);
	EXPECT_EQ(regenerationEffect(secondFriendly), nullptr);
	EXPECT_EQ(firstFriendly->regenerationRateMillionths, 310'000)
		<< "The direct-damage-era snapshot retains the unranked 100% Spell Power coefficient";
}
