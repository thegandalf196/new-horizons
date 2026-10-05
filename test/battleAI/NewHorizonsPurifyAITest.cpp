/*
 * NewHorizonsPurifyAITest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"

#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../AI/BattleAI/SpellTargetsEvaluator.h"
#include "../../lib/CStack.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/HeroActionAllowanceState.h"
#include "../../lib/battle/NewHorizonsDivineMandate.h"
#include "../../lib/battle/NewHorizonsBulwark.h"
#include "../../lib/battle/PhysicalAffliction.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/NewHorizonsPurify.h"
#include "../../lib/spells/Problem.h"

#include <algorithm>
#include <map>

namespace
{
class PurifyEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit PurifyEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class PurifyCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> submitted;

	PurifyCallback() : CBattleCallback(PlayerColor(0), nullptr) {}
	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override
	{
		submitted.push_back(action);
	}
};

JsonNode savedV2MagicRules()
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	rules["rulesetVersion"].Integer() = newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION;
	rules.Struct().erase("schoolRankPowerCoefficientPercent");
	rules.Struct().erase("spellcraftEfficiencyPercent");
	for(auto & [name, spell] : rules["spells"].Struct())
	{
		(void)name;
		spell.Struct().erase("selectedPlacement");
		spell.Struct().erase("heroAccess");
		spell.Struct().erase("earthquake");
		spell.Struct().erase("structures");
		spell.Struct().erase("restoration");
		if(spell.Struct().contains("variant"))
		{
			spell.Struct().erase("variant");
			spell["active"].Bool() = false;
		}
	}
	if(newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION
		< newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION)
	{
		auto & sorrow = rules["spells"]["core:sorrow"];
		sorrow.Struct().erase("level");
		sorrow.Struct().erase("costs");
		sorrow["schools"].Vector().clear();
		sorrow["schools"].Vector().emplace_back(std::string("new-horizons:chaos"));
	}
	newHorizonsMagic::validateRules(rules);
	return rules;
}

SpellID purifySpell()
{
	return newHorizonsPurify::spellID();
}

void addSpellEffect(CStack * unit, SpellID source, int32_t strength, int32_t turns)
{
	Bonus effect(BonusDuration::N_TURNS, BonusType::STACKS_SPEED, BonusSource::SPELL_EFFECT,
		strength, BonusSourceID(source));
	effect.turnsRemain = turns;
	unit->addNewBonus(std::make_shared<Bonus>(effect));
}

void addPhysicalAffliction(CStack * unit, const std::string & kind, const int64_t applicationOrder,
	const int32_t sourceId)
{
	const BonusSourceID id(BonusCustomSource(static_cast<int32_t>(sourceId)));
	Bonus effect(BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::OTHER, -1, id);
	unit->addNewBonus(std::make_shared<Bonus>(effect));

	Bonus marker(BonusDuration::PERMANENT, BonusType::PHYSICAL_AFFLICTION, BonusSource::OTHER, 0, id);
	JsonNode parameters;
	parameters["kind"].String() = kind;
	parameters["applicationOrder"].Integer() = applicationOrder;
	marker.parameters = std::make_shared<BonusParameters>(parameters);
	unit->addNewBonus(std::make_shared<Bonus>(marker));
}

bool hasPhysicalAffliction(const battle::Unit * unit, const std::string & kind)
{
	if(!unit)
		return false;
	const auto afflictions = physicalAfflictions::enumerate(*unit);
	return std::any_of(afflictions.begin(), afflictions.end(), [&](const auto & affliction)
	{
		return affliction.kind == kind;
	});
}

bool hasSpellEffect(const battle::Unit * unit, SpellID source)
{
	return unit->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(source)));
}
}

class NewHorizonsPurifyAITest : public HeroCommandFixture
{
protected:
	bool useSavedV2Rules = false;
	static constexpr auto divineMandateSkill = "new-horizons:divineMandate";
	static constexpr auto sacredCommandPerk = "new-horizons:divineMandate.sacredCommand";
	static constexpr auto purifyingMandatePerk = "new-horizons:divineMandate.purifyingMandate";

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			useSavedV2Rules ? savedV2MagicRules()
				: JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void prepareCaster(bool purifier, bool savedV2 = false)
	{
		prepareCaster(purifier, savedV2, false);
	}

	void selectMandatePerk(const MasteryLevel::Type rank, const std::string & perkId)
	{
		const int skillId = SecondarySkill::decode(divineMandateSkill);
		ASSERT_GE(skillId, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(skillId), rank, ChangeValueMode::ABSOLUTE);
		const auto rankLookup = [this](const std::string & id)
		{
			return attackerSideHero->getPerkSkillRank(id);
		};

		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offer = attackerSideHero->getPerkState().prepareOffer(rankLookup, seed);
			const auto selected = std::find_if(offer.begin(), offer.end(), [&](const auto & candidate)
			{
				return candidate.selection.skillId == divineMandateSkill
					&& candidate.selection.perkId == perkId
					&& candidate.requiredRank == rank;
			});
			if(selected == offer.end())
				continue;

			const auto choice = static_cast<size_t>(std::distance(offer.begin(), selected));
			gameHandler->levelUpHero(attackerSideHero, offer, choice, seed, false);
			ASSERT_TRUE(attackerSideHero->hasActivePerk(divineMandateSkill, perkId));
			return;
		}

		FAIL() << "Could not find a legal " << perkId << " offer at rank " << rank;
	}

	void prepareCaster(bool purifier, bool savedV2, bool withPurifyingMandate)
	{
		useCommands = withPurifyingMandate;
		useSavedV2Rules = savedV2;
		ASSERT_NO_FATAL_FAILURE(startGame());
		if(withPurifyingMandate)
		{
			ASSERT_EQ(attackerSideHero->getFactionID(), FactionID::CASTLE);
			selectMandatePerk(MasteryLevel::BASIC, sacredCommandPerk);
			selectMandatePerk(MasteryLevel::ADVANCED, purifyingMandatePerk);
			ASSERT_TRUE(newHorizonsDivineMandate::hasPurifyingMandatePerk(attackerSideHero));
		}
		if(purifier)
		{
			const auto lightMagic = SecondarySkill::decode(std::string(newHorizonsPurify::LIGHT_MAGIC_SKILL));
			ASSERT_GE(lightMagic, 0);
			attackerSideHero->setSecSkillLevel(SecondarySkill(lightMagic), MasteryLevel::BASIC,
				ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({std::string(newHorizonsPurify::LIGHT_MAGIC_SKILL),
				"new-horizons:lightMagic.healer"});
			attackerSideHero->setSecSkillLevel(SecondarySkill(lightMagic), MasteryLevel::ADVANCED,
				ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({std::string(newHorizonsPurify::LIGHT_MAGIC_SKILL),
				std::string(newHorizonsPurify::PURIFIER_PERK)});
			ASSERT_TRUE(newHorizonsPurify::hasPurifierPerk(attackerSideHero));
		}
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		for(const auto known : attackerSideHero->getSpellsInSpellbook())
			attackerSideHero->removeSpellFromSpellbook(known);
		attackerSideHero->addSpellToSpellbook(purifySpell());
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 120, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);
		startBattle();
		beginCombat();
	}

	void removeInitialStacks()
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
	}

	void addPurifyAiStacks(CStack *& active, CStack *& afflicted, const bool magicalAffliction)
	{
		removeInitialStacks();
		active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(1, 5), 1);
		afflicted = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(6, 5), 5);
		auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(15, 5), 1000);
		ASSERT_NE(active, nullptr);
		ASSERT_NE(afflicted, nullptr);
		ASSERT_NE(enemy, nullptr);
		if(magicalAffliction)
			addSpellEffect(afflicted, SpellID::SLOW, -2, 3);
		addPhysicalAffliction(afflicted, "other", 1, 9401);
		addPhysicalAffliction(afflicted, "disease", 2, 9402);

		BattleSetActiveStack activate;
		activate.battleID = BattleID(0);
		activate.stack = active->unitId();
		activate.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activate);
	}

	std::shared_ptr<PurifyCallback> makeCallback()
	{
		auto callback = std::make_shared<PurifyCallback>();
		callback->onBattleStarted(battle());
		return callback;
	}

	bool runPurifyAI(CStack * active, const std::shared_ptr<PurifyCallback> & callback)
	{
		auto environment = std::make_shared<PurifyEnvironment>(gameState());
		BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
			BattleSide::ATTACKER, 1.0f, 2);
		evaluator.selectStackAction(active);
		return evaluator.attemptCastingSpell(active);
	}
};

TEST_F(NewHorizonsPurifyAITest, SelectsBestAreaAndProjectsOnlyChosenGroupsWithoutMutatingTheBattle)
{
	ASSERT_NO_FATAL_FAILURE(prepareCaster(true));
	removeInitialStacks();
	auto * active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(1, 5), 1);
	auto * firstAlly = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(5, 5), 5);
	auto * secondAlly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(9, 5), 20);
	// Keep the enemy alive through the evaluator's tactical lookahead; otherwise
	// it correctly declines all further hero actions once the battle is predicted
	// to end before the enemy gets a turn.
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(15, 5), 1000);
	ASSERT_NE(active, nullptr);
	ASSERT_NE(firstAlly, nullptr);
	ASSERT_NE(secondAlly, nullptr);
	ASSERT_NE(enemy, nullptr);

	addSpellEffect(firstAlly, SpellID::SLOW, -2, 3);
	addSpellEffect(firstAlly, SpellID::CURSE, -1, 1);
	addSpellEffect(firstAlly, SpellID::BLIND, -1, 2);
	addSpellEffect(firstAlly, SpellID::HASTE, 1, 3);
	addSpellEffect(secondAlly, SpellID::SLOW, -1, 2);

	auto poisonedState = firstAlly->acquireState();
	ASSERT_TRUE(newHorizonsBulwark::applyPhysicalPoison(poisonedState.get(), 40,
		static_cast<int32_t>(enemy->unitId())));
	BattleUnitsChanged updatePoison;
	updatePoison.battleID = BattleID(0);
	updatePoison.changedStacks.emplace_back(firstAlly->unitId(), UnitChanges::EOperation::UPDATE);
	updatePoison.changedStacks.back().data = poisonedState->save();
	gameHandler->sendAndApply(updatePoison);

	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = active->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);

	ASSERT_TRUE(newHorizonsPurify::enabled(battle()->getMagicRules(), purifySpell()));
	ASSERT_TRUE(purifySpell().toSpell()->canBeCast(battle(), spells::Mode::HERO, attackerSideHero, false))
		<< "Purify must pass BattleEvaluator's initial possible-spell population gate";
	const auto eligibleAtMidpoint = newHorizonsPurify::eligibleStacks(*battle(), BattleSide::ATTACKER,
		BattleHex(7, 5), attackerSideHero->getPrimSkillLevel(PrimarySkill::SPELL_POWER), true);
	ASSERT_EQ(eligibleAtMidpoint.size(), 2u)
		<< "the two afflicted friendly stacks should be eligible around the midpoint";
	spells::BattleCast preflightCast(battle(), attackerSideHero, spells::Mode::HERO, purifySpell().toSpell());
	const auto preflightMechanics = purifySpell().toSpell()->battleMechanics(&preflightCast);
	spells::detail::ProblemImpl preflightProblem;
	ASSERT_TRUE(preflightMechanics->canBeCast(preflightProblem));
	const auto preflightTargets = SpellTargetEvaluator::getViableTargets(preflightMechanics.get());
	ASSERT_FALSE(preflightTargets.empty()) << "a legal center should be generated for the eligible stacks";
	float bestPreflightValue = 0.0f;
	for(const auto & center : preflightTargets)
		bestPreflightValue = std::max(bestPreflightValue,
			SpellTargetEvaluator::purifySelection(preflightMechanics.get(), center).value);
	ASSERT_GT(bestPreflightValue, 0.0f) << "the selected negative groups should have positive cleanse value";

	auto environment = std::make_shared<PurifyEnvironment>(gameState());
	auto callback = std::make_shared<PurifyCallback>();
	callback->onBattleStarted(battle());
	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	ASSERT_TRUE(evaluator.attemptCastingSpell(active));
	ASSERT_EQ(callback->submitted.size(), 1u);
	const auto & action = callback->submitted.front();
	ASSERT_EQ(action.actionType, EActionType::HERO_SPELL);
	EXPECT_EQ(action.spell, purifySpell());
	EXPECT_FALSE(action.spellPurifyChoices.empty());
	EXPECT_TRUE(std::none_of(action.spellPurifyChoices.begin(), action.spellPurifyChoices.end(), [](const auto & choice)
	{
		return choice.second == newHorizonsPurify::physicalPoisonChoiceID();
	}));

	const auto selectedTarget = action.getTarget(battle());
	ASSERT_EQ(selectedTarget.size(), 1u);
	ASSERT_EQ(selectedTarget.front().unitValue, nullptr);
	ASSERT_TRUE(selectedTarget.front().hexValue.isValid());
	spells::BattleCast preview(battle(), attackerSideHero, spells::Mode::HERO, purifySpell().toSpell());
	const auto mechanics = purifySpell().toSpell()->battleMechanics(&preview);
	const auto candidateCenters = SpellTargetEvaluator::getViableTargets(mechanics.get());
	ASSERT_FALSE(candidateCenters.empty());
	float bestCenterValue = 0.0f;
	for(const auto & center : candidateCenters)
		bestCenterValue = std::max(bestCenterValue,
			SpellTargetEvaluator::purifySelection(mechanics.get(), center).value);
	const auto selectedValue = SpellTargetEvaluator::purifySelection(mechanics.get(), selectedTarget).value;
	EXPECT_NEAR(selectedValue, bestCenterValue, 0.001f);
	const auto selectedPlan = SpellTargetEvaluator::purifySelection(mechanics.get(), selectedTarget);
	EXPECT_EQ(action.spellPurifyChoices, selectedPlan.spellEffectGroups);

	std::map<int32_t, size_t> selectedPerStack;
	std::map<int32_t, std::vector<SpellID>> selectedGroups;
	const std::map<int32_t, std::vector<SpellID>> negativeGroupsByStack = {
		{static_cast<int32_t>(firstAlly->unitId()), {SpellID::SLOW, SpellID::CURSE, SpellID::BLIND}},
		{static_cast<int32_t>(secondAlly->unitId()), {SpellID::SLOW}}
	};
	for(const auto & [unitId, source] : action.spellPurifyChoices)
	{
		++selectedPerStack[unitId];
		selectedGroups[unitId].push_back(source);
		ASSERT_TRUE(negativeGroupsByStack.contains(unitId));
		EXPECT_TRUE(vstd::contains(negativeGroupsByStack.at(unitId), source));
	}
	const auto eligibleAtSelectedCenter = newHorizonsPurify::eligibleStacks(*battle(), BattleSide::ATTACKER,
		selectedTarget.front().hexValue, attackerSideHero->getPrimSkillLevel(PrimarySkill::SPELL_POWER), true);
	std::map<int32_t, int> choiceCaps;
	for(const auto & eligible : eligibleAtSelectedCenter)
		choiceCaps.emplace(eligible.unitId, eligible.maximumSpellEffectChoices);
	for(const auto & [unitId, count] : selectedPerStack)
	{
		ASSERT_TRUE(choiceCaps.contains(unitId));
		EXPECT_LE(count, static_cast<size_t>(choiceCaps.at(unitId)));
	}

	// Exercise the same detached unit projection used during AI ranking. The live
	// state remains untouched until the authoritative action is submitted.
	auto projected = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	for(const auto unitId : selectedPlan.physicalPoisonStackIds)
		selectedGroups.try_emplace(unitId);
	for(const auto & [unitId, groups] : selectedGroups)
	{
		auto projectedUnit = projected->getForUpdate(static_cast<uint32_t>(unitId));
		ASSERT_NE(projectedUnit, nullptr);
		ASSERT_TRUE(projectedUnit->applyPurifySelection(groups,
			vstd::contains(selectedPlan.physicalPoisonStackIds, unitId)));
	}
	for(const auto & [unitId, negativeGroups] : negativeGroupsByStack)
	{
		const auto * projectedUnit = projected->battleGetUnitByID(static_cast<uint32_t>(unitId));
		ASSERT_NE(projectedUnit, nullptr);
		for(const auto source : negativeGroups)
			EXPECT_EQ(hasSpellEffect(projectedUnit, source), !vstd::contains(selectedGroups[unitId], source));
	}
	const auto * projectedFirstAlly = projected->battleGetUnitByID(firstAlly->unitId());
	ASSERT_NE(projectedFirstAlly, nullptr);
	EXPECT_FALSE(newHorizonsPurify::hasPhysicalPoison(projectedFirstAlly));
	for(const auto & [unitId, negativeGroups] : negativeGroupsByStack)
	{
		const auto * liveUnit = battle()->battleGetUnitByID(static_cast<uint32_t>(unitId));
		ASSERT_NE(liveUnit, nullptr);
		for(const auto source : negativeGroups)
			EXPECT_TRUE(hasSpellEffect(liveUnit, source))
				<< "detached projection must not mutate the live stack before server submission";
	}
	EXPECT_TRUE(hasSpellEffect(firstAlly, SpellID::HASTE));
	EXPECT_TRUE(newHorizonsPurify::hasPhysicalPoison(firstAlly));

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	for(const auto & [unitId, source] : action.spellPurifyChoices)
	{
		const auto * unit = battle()->battleGetUnitByID(static_cast<uint32_t>(unitId));
		ASSERT_NE(unit, nullptr);
		EXPECT_FALSE(hasSpellEffect(unit, source));
	}
	for(const auto & [unitId, negativeGroups] : negativeGroupsByStack)
	{
		const auto * unit = battle()->battleGetUnitByID(static_cast<uint32_t>(unitId));
		ASSERT_NE(unit, nullptr);
		for(const auto source : negativeGroups)
			EXPECT_EQ(hasSpellEffect(unit, source), !vstd::contains(selectedGroups[unitId], source));
	}
	EXPECT_FALSE(newHorizonsPurify::hasPhysicalPoison(firstAlly));
	EXPECT_TRUE(hasSpellEffect(firstAlly, SpellID::HASTE));
}

TEST_F(NewHorizonsPurifyAITest, DivineMandateFollowupProjectsAndValuesOnePriorityPhysicalAffliction)
{
	ASSERT_NO_FATAL_FAILURE(prepareCaster(false, false, true));
	CStack * active = nullptr;
	CStack * afflicted = nullptr;
	addPurifyAiStacks(active, afflicted, true);
	ASSERT_TRUE(newHorizonsPurify::isMagicalSpellEffectGroup(afflicted, SpellID::SLOW));
	ASSERT_TRUE(hasPhysicalAffliction(afflicted, "disease"));
	ASSERT_TRUE(hasPhysicalAffliction(afflicted, "other"));

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE)));
	const auto status = battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER);
	ASSERT_TRUE(status.pendingFollowup);
	EXPECT_EQ(status.pendingFollowup->source, HeroActionAllowanceState::GrantSource::DIVINE_MANDATE);
	EXPECT_EQ(status.pendingFollowup->allowance, HeroActionAllowanceState::AllowanceKind::SPELL);

	auto callback = makeCallback();
	ASSERT_TRUE(runPurifyAI(active, callback));
	ASSERT_EQ(callback->submitted.size(), 1u);
	const auto action = callback->submitted.front();
	ASSERT_EQ(action.actionType, EActionType::HERO_SPELL);
	EXPECT_EQ(action.spell, purifySpell());
	ASSERT_TRUE(std::any_of(action.spellPurifyChoices.begin(), action.spellPurifyChoices.end(),
		[&](const auto & choice)
		{
			return choice.first == static_cast<int32_t>(afflicted->unitId()) && choice.second == SpellID::SLOW;
		}));

	std::vector<SpellID> selectedGroups;
	for(const auto & [unitId, sourceSpell] : action.spellPurifyChoices)
		if(unitId == static_cast<int32_t>(afflicted->unitId()))
			selectedGroups.push_back(sourceSpell);
	ASSERT_FALSE(selectedGroups.empty());
	const bool removesMagicalGroup = std::any_of(selectedGroups.begin(), selectedGroups.end(), [&](const SpellID sourceSpell)
	{
		return newHorizonsPurify::isMagicalSpellEffectGroup(afflicted, sourceSpell);
	});
	ASSERT_TRUE(removesMagicalGroup);

	// The same detached operation used for action ranking removes only the first
	// remaining physical affliction; it must not change live state before action
	// submission. Disease outranks the older generic affliction by shared policy.
	auto environment = std::make_shared<PurifyEnvironment>(gameState());
	auto detached = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	auto projected = detached->getForUpdate(afflicted->unitId());
	ASSERT_NE(projected, nullptr);
	ASSERT_TRUE(projected->applyPurifySelection(selectedGroups, false,
		newHorizonsDivineMandate::hasPurifyingMandatePerk(attackerSideHero) && removesMagicalGroup));
	EXPECT_FALSE(hasSpellEffect(projected.get(), SpellID::SLOW));
	EXPECT_FALSE(hasPhysicalAffliction(projected.get(), "disease"));
	EXPECT_TRUE(hasPhysicalAffliction(projected.get(), "other"));
	EXPECT_TRUE(hasSpellEffect(afflicted, SpellID::SLOW));
	EXPECT_TRUE(hasPhysicalAffliction(afflicted, "disease"));
	EXPECT_TRUE(hasPhysicalAffliction(afflicted, "other"));

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_FALSE(hasSpellEffect(afflicted, SpellID::SLOW));
	EXPECT_FALSE(hasPhysicalAffliction(afflicted, "disease"));
	EXPECT_TRUE(hasPhysicalAffliction(afflicted, "other"));
}

TEST_F(NewHorizonsPurifyAITest, OrdinaryPurifyDoesNotProjectPurifyingMandateWithoutSelectedGrant)
{
	ASSERT_NO_FATAL_FAILURE(prepareCaster(false, false, true));
	CStack * active = nullptr;
	CStack * afflicted = nullptr;
	addPurifyAiStacks(active, afflicted, true);
	ASSERT_TRUE(battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER).active);
	EXPECT_FALSE(battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER).pendingFollowup);

	const auto allowance = battle()->battleGetSpellActionAllowance(BattleSide::ATTACKER, purifySpell());
	ASSERT_TRUE(allowance.has_value());
	EXPECT_EQ(allowance->source, HeroActionAllowanceState::GrantSource::ROUND);

	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, purifySpell().toSpell());
	auto mechanics = purifySpell().toSpell()->battleMechanics(&cast);
	spells::detail::ProblemImpl problem;
	ASSERT_TRUE(mechanics->canBeCast(problem));
	const auto centers = SpellTargetEvaluator::getViableTargets(mechanics.get());
	const auto selectedTarget = std::find_if(centers.begin(), centers.end(), [&](const auto & center)
	{
		const auto selection = SpellTargetEvaluator::purifySelection(mechanics.get(), center);
		return std::ranges::any_of(selection.spellEffectGroups, [&](const auto & choice)
		{
			return choice.first == static_cast<int32_t>(afflicted->unitId()) && choice.second == SpellID::SLOW;
		});
	});
	ASSERT_NE(selectedTarget, centers.end());
	const auto selection = SpellTargetEvaluator::purifySelection(mechanics.get(), *selectedTarget);

	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = purifySpell();
	action.aimToHex(selectedTarget->front().hexValue);
	action.spellPurifyChoices = selection.spellEffectGroups;
	ASSERT_TRUE(std::ranges::any_of(action.spellPurifyChoices, [&](const auto & choice)
	{
		return choice.first == static_cast<int32_t>(afflicted->unitId()) && choice.second == SpellID::SLOW;
	}));

	std::vector<SpellID> selectedGroups;
	for(const auto & [unitId, sourceSpell] : action.spellPurifyChoices)
		if(unitId == static_cast<int32_t>(afflicted->unitId()))
			selectedGroups.push_back(sourceSpell);
	ASSERT_FALSE(selectedGroups.empty());
	auto callback = makeCallback();
	auto environment = std::make_shared<PurifyEnvironment>(gameState());
	auto detached = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	auto projected = detached->getForUpdate(afflicted->unitId());
	ASSERT_NE(projected, nullptr);
	ASSERT_TRUE(projected->applyPurifySelection(selectedGroups, false, false));
	EXPECT_FALSE(hasSpellEffect(projected.get(), SpellID::SLOW));
	EXPECT_TRUE(hasPhysicalAffliction(projected.get(), "disease"));
	EXPECT_TRUE(hasPhysicalAffliction(projected.get(), "other"));
	EXPECT_TRUE(hasSpellEffect(afflicted, SpellID::SLOW));
	EXPECT_TRUE(hasPhysicalAffliction(afflicted, "disease"));

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_FALSE(hasSpellEffect(afflicted, SpellID::SLOW));
	EXPECT_TRUE(hasPhysicalAffliction(afflicted, "disease"));
	EXPECT_TRUE(hasPhysicalAffliction(afflicted, "other"));
}

TEST_F(NewHorizonsPurifyAITest, PhysicalOnlyAfflictionsDoNotCreateAMandatePurifyTarget)
{
	ASSERT_NO_FATAL_FAILURE(prepareCaster(false, false, true));
	CStack * active = nullptr;
	CStack * afflicted = nullptr;
	addPurifyAiStacks(active, afflicted, false);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE)));
	ASSERT_TRUE(battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER).pendingFollowup);
	EXPECT_TRUE(newHorizonsPurify::eligibleSpellEffectGroups(battle()->getMagicRules(), afflicted).empty());
	EXPECT_FALSE(newHorizonsPurify::hasPhysicalPoison(afflicted));

	auto callback = makeCallback();
	EXPECT_FALSE(runPurifyAI(active, callback));
	EXPECT_TRUE(callback->submitted.empty());
	EXPECT_TRUE(hasPhysicalAffliction(afflicted, "disease"));
	EXPECT_TRUE(hasPhysicalAffliction(afflicted, "other"));
}

TEST_F(NewHorizonsPurifyAITest, SavedV2RulesDoNotExposeTheCanonicalSpellToBattleAI)
{
	ASSERT_NO_FATAL_FAILURE(prepareCaster(false, true));
	EXPECT_FALSE(newHorizonsPurify::enabled(battle()->getMagicRules(), purifySpell()));
	removeInitialStacks();
	auto * active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(2, 5), 1);
	auto * ally = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(6, 5), 5);
	addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(14, 5), 5);
	ASSERT_NE(active, nullptr);
	ASSERT_NE(ally, nullptr);
	addSpellEffect(ally, SpellID::SLOW, -2, 3);

	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = active->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);
	auto environment = std::make_shared<PurifyEnvironment>(gameState());
	auto callback = std::make_shared<PurifyCallback>();
	callback->onBattleStarted(battle());
	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	EXPECT_FALSE(evaluator.attemptCastingSpell(active));
	EXPECT_TRUE(callback->submitted.empty());
	EXPECT_TRUE(hasSpellEffect(ally, SpellID::SLOW));
}
