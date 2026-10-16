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
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/serializer/CMemorySerializer.h"

#include <algorithm>
#include <map>
#include "../../AI/BattleAI/HypotheticBattle.h"

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
	// A historical v2 capture must not contain later v3-only policies.
	for(const auto * key : {"protectedAdventureBarriers", "creatureAbilities", "morale"})
		rules.Struct().erase(key);
	rules["adventureSpells"]["core:waterWalk"].Struct().erase("requireLegalDayEnd");
	for(auto & [name, spell] : rules["spells"].Struct())
	{
		(void)name;
		spell.Struct().erase("selectedPlacement");
		spell.Struct().erase("heroAccess");
		spell.Struct().erase("earthquake");
		spell.Struct().erase("structures");
		spell.Struct().erase("restoration");
		for(const auto * key : {"propagationLimit", "implosion", "ignoreInterveningBarriers",
			"temporaryMagicalEffectsOnly", "schoolRankDurations", "burnGroundedFlyers"})
			spell.Struct().erase(key);
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
			const auto offer = attackerSideHero->getPerkState().prepareOffer(rankLookup, seed, attackerSideHero->getPerkOfferWeightContext());
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
	// Advanced Light reaches two groups at SP100, below the unranked SP120 threshold.
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
	ASSERT_EQ(newHorizonsPurify::maximumSpellEffectChoices(battle()->getMagicRules(), attackerSideHero, 100), 2);
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
	EXPECT_EQ(selectedPerStack[static_cast<int32_t>(firstAlly->unitId())], 2u)
		<< "Advanced School rank must let the useful AI plan cleanse two groups below SP120";

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

	const auto manaBefore = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore - 15);
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

class NewHorizonsPurifyAllegianceAITest : public NewHorizonsPurifyAITest
{
protected:
	bool historicalHypnotize = false;

	void mapLoaded(CMap * loaded) override
	{
		// Keep current hero development/weighted capture and commands. Only the
		// explicit historical-v3 case omits the retired spell's access marker.
		useCommands = true;
		NewHorizonsPurifyAITest::mapLoaded(loaded);
		// Same current-command neutralizer as MagicAITest: preserve captured
		// Hero rules/typed actions, isolate this spell-selection oracle from Orders.
		const JsonNode combatRules(JsonPath::builtin("config/newHorizonsCombat"));
		JsonNode commands = combatRules["combat"]["heroCommands"];
		for(auto & command : commands["commands"].Struct())
			for(auto & effect : command.second["effects"].Struct())
			{
				effect.second["base"].Float() = 0;
				effect.second["attack"].Float() = 0;
				effect.second["defense"].Float() = 0;
			}
		heroCommands::validateRules(commands);
		loaded->overrideGameSetting(EGameSettings::COMBAT_HERO_COMMANDS, commands);
		JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
		ASSERT_FALSE(rules["spells"]["core:hypnotize"]["heroAccess"].Bool());
		ASSERT_FALSE(rules["spells"]["core:hypnotize"]["ordinaryAcquisition"].Bool());
		if(historicalHypnotize)
		{
			const auto canonical = rules;
			rules["spells"]["core:hypnotize"].Struct().erase("heroAccess");
			auto restored = rules;
			restored["spells"]["core:hypnotize"]["heroAccess"] = canonical["spells"]["core:hypnotize"]["heroAccess"];
			ASSERT_EQ(restored, canonical);
			newHorizonsMagic::validateRules(rules);
		}
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, rules);
	}

	void activateNormal(const CStack * unit)
	{
		BattleSetActiveStack activation;
		activation.battleID = BattleID(0);
		activation.stack = unit->unitId();
		activation.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activation);
		ASSERT_EQ(battle()->battleActiveUnit()->unitId(), unit->unitId());
	}

	void paidCast(CGHeroInstance * hero, SpellID spell, CStack * target)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = hero == attackerSideHero ? BattleSide::ATTACKER : BattleSide::DEFENDER;
		action.spell = spell;
		action.aimToUnit(target);
		ASSERT_TRUE(battle()->battleGetSpellActionAllowance(action.side, spell))
			<< "paid cast needs a current typed spell allowance";
		spells::BattleCast preflight(battle(), hero, spells::Mode::HERO, spell.toSpell());
		const auto mechanics = spell.toSpell()->battleMechanics(&preflight);
		spells::detail::ProblemImpl problem;
		ASSERT_TRUE(mechanics->canBeCast(problem));
		ASSERT_TRUE(mechanics->canBeCastAt(spells::Target{spells::Destination(target)}, problem));
		const auto before = hero->getManaAvailable();
		const auto count = battle()->battleCastSpells(action.side);
		const auto cost = battle()->battleGetSpellCost(spell.toSpell(), hero);
		ASSERT_GT(cost, 0);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), hero->getOwner(), action));
		EXPECT_EQ(hero->getManaAvailable(), before - cost);
		EXPECT_EQ(battle()->battleCastSpells(action.side), count + 1);
	}

	void checkTransferredAllegiance(bool reverse, bool puppet)
	{
		historicalHypnotize = !puppet;
		ASSERT_NO_FATAL_FAILURE(startGame());
		const SpellID puppetSpell(SpellID::decode("new-horizons:puppetMaster"));
		ASSERT_NE(puppetSpell, SpellID::NONE);
		for(auto * hero : {attackerSideHero, defenderSideHero})
		{
			giveArtifact(hero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
			for(const auto known : hero->getSpellsInSpellbook())
				hero->removeSpellFromSpellbook(known);
			for(const auto spell : {purifySpell(), SpellID(SpellID::HYPNOTIZE), SpellID(SpellID::SLOW), puppetSpell})
				hero->addSpellToSpellbook(spell);
			hero->setPrimarySkill(PrimarySkill::SPELL_POWER, 120, ChangeValueMode::ABSOLUTE);
			hero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
			setTestSpellPointTotal(hero, 1000);
			// Actual Basic Temporalist keeps the fixed two-round Slow alive through
			// the two ordinary round budgets needed by this three-spell scenario.
			const SecondarySkill sorcery(SecondarySkill::decode("new-horizons:sorceryMagic"));
			hero->setSecSkillLevel(sorcery, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
			hero->applyPerkSelection({"new-horizons:sorceryMagic", "new-horizons:sorceryMagic.temporalist"});
			ASSERT_TRUE(hero->hasActivePerk("new-horizons:sorceryMagic", "new-horizons:sorceryMagic.temporalist"));
		}
		startBattle();
		removeInitialStacks();
		auto * first = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(1, 1), 1);
		auto * second = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(1, 3), 1);
		auto * active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(1, 5), 1);
		auto * opposition = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(15, 5), 1000);
		auto * secondOpposition = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(15, 7), 1);
		auto * target = addStack(reverse ? BattleSide::ATTACKER : BattleSide::DEFENDER,
			creatureByName("core:pikeman"), BattleHex(7, 5), 20);
		ASSERT_NE(target, nullptr);
		beginCombat();
		ASSERT_TRUE(battle()->battleUsesHeroCommands());
		ASSERT_EQ(newHorizonsPurify::maximumSpellEffectChoices(battle()->getMagicRules(), attackerSideHero, 120), 2);
		const auto slowDuration = [&]
		{
			int remaining = 0;
			for(const auto & bonus : newHorizonsPurify::spellEffectGroupBonuses(target, SpellID::SLOW))
				if(Bonus::NTurns(&bonus))
					remaining = std::max<int>(remaining, bonus.turnsRemain);
			return remaining;
		};

		// Generic single-creature casting still uses original-side targeting.
		// Slow is paid while this is an original enemy, BEFORE control changes.
		ASSERT_NO_FATAL_FAILURE(activateNormal(reverse ? opposition : first));
		ASSERT_NO_FATAL_FAILURE(paidCast(reverse ? defenderSideHero : attackerSideHero,
			SpellID::SLOW, target));
		ASSERT_EQ(slowDuration(), 3);
		const auto slowRound = battle()->battleGetRound();
		ASSERT_NO_FATAL_FAILURE(advanceRound()); // A new stack alone does not grant another Hero Action.
		ASSERT_EQ(battle()->battleGetRound(), slowRound + 1);
		ASSERT_EQ(slowDuration(), 2);
		ASSERT_TRUE(hasSpellEffect(target, SpellID::SLOW));
		ASSERT_NO_FATAL_FAILURE(activateNormal(reverse ? secondOpposition : second));
		ASSERT_NO_FATAL_FAILURE(paidCast(reverse ? defenderSideHero : attackerSideHero,
			puppet ? puppetSpell : SpellID(SpellID::HYPNOTIZE), target));
		const auto expectedOwner = reverse || puppet ? PlayerColor(1) : PlayerColor(0);
		ASSERT_EQ(battle()->battleGetOwner(target), expectedOwner);
		if(puppet)
		{
			ASSERT_EQ(target->unitSide(), BattleSide::DEFENDER);
			ASSERT_EQ(battle()->battleGetActionController(target), PlayerColor(0));
			ASSERT_FALSE(target->hasBonusOfType(BonusType::HYPNOTIZED));
		}
		else
		{
			ASSERT_TRUE(target->hasBonusOfType(BonusType::HYPNOTIZED));
			ASSERT_NE(battle()->playerToSide(battle()->battleGetOwner(target)), target->unitSide());
		}
		ASSERT_NO_FATAL_FAILURE(advanceRound());
		ASSERT_EQ(battle()->battleGetRound(), slowRound + 2);
		ASSERT_EQ(slowDuration(), 1);
		ASSERT_TRUE(hasSpellEffect(target, SpellID::SLOW));
		ASSERT_EQ(battle()->battleGetOwner(target), expectedOwner);
		ASSERT_EQ(target->hasBonusOfType(BonusType::HYPNOTIZED), !puppet);
		if(puppet)
		{
			ASSERT_EQ(battle()->battleGetActionController(target), PlayerColor(0));
		}
		ASSERT_NO_FATAL_FAILURE(activateNormal(active));
		for(const auto known : attackerSideHero->getSpellsInSpellbook())
			if(known != purifySpell())
				attackerSideHero->removeSpellFromSpellbook(known);
		ASSERT_FALSE(newHorizonsPurify::spellEffectGroupBonuses(target, SpellID::SLOW).empty());

		const auto snapshot = [&]
		{
			JsonNode result;
			result["round"].Integer() = battle()->battleGetRound();
			result["activationSerial"].Integer() = battle()->getActivationSerial();
			result["activeUnit"].Integer() = battle()->battleActiveUnit()->unitId();
			for(const auto * unit : battle()->battleGetAllUnits(false))
			{
				JsonNode entry;
				entry["id"].Integer() = unit->unitId();
				entry["state"] = unit->acquireState()->save();
				entry["owner"].Integer() = battle()->battleGetOwner(unit).getNum();
				entry["controller"].Integer() = battle()->battleGetActionController(unit).getNum();
				const auto bonuses = unit->getAllBonuses(CSelector([](const Bonus *) { return true; }));
				for(const auto & bonus : *bonuses)
					entry["bonuses"].Vector().push_back(bonus->toJsonNode());
				result["units"].Vector().push_back(std::move(entry));
			}
			for(auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
			{
				const auto * hero = side == BattleSide::ATTACKER ? attackerSideHero : defenderSideHero;
				JsonNode entry;
				entry["normal"].Integer() = hero->getNormalSpellPoints();
				entry["buffer"].Integer() = hero->getBufferSpellPoints();
				entry["temporaryBufferRemaining"].Integer() = battle()->getSide(side).temporaryBufferRemaining;
				entry["casts"].Integer() = battle()->battleCastSpells(side);
				result["sides"].Vector().push_back(std::move(entry));
			}
			return result;
		};
		const auto rng = [&]
		{
			CMemorySerializer wire;
			wire.oser & *gameHandler->randomizer;
			return wire.extractBuffer();
		};
		// total() intentionally latches the observed retaliation cap for this round
		// (including after Dispel). Warm that normal query before the reference:
		// assert no additional mutation by AI, not cold-cache bitwise immutability.
		for(const auto * unit : battle()->battleGetAllUnits(false))
		{
			const auto * state = dynamic_cast<const battle::CUnitState *>(unit);
			ASSERT_NE(state, nullptr);
			(void)state->counterAttacks.total();
		}
		const auto before = snapshot();
		const auto rngBefore = rng();
		const auto attackerAllowances = battle()->getHeroActionAllowances(BattleSide::ATTACKER);
		const auto defenderAllowances = battle()->getHeroActionAllowances(BattleSide::DEFENDER);
		const auto attackerUsedSpells = battle()->getUsedSpells(BattleSide::ATTACKER);
		const auto defenderUsedSpells = battle()->getUsedSpells(BattleSide::DEFENDER);
		const auto center = target->getPosition();
		const auto eligible = newHorizonsPurify::eligibleStacks(*battle(), BattleSide::ATTACKER,
			center, 120, false);
		const bool friendly = !reverse && !puppet;
		ASSERT_EQ(eligible.size(), friendly ? 1u : 0u);
		if(friendly)
		{
			ASSERT_EQ(eligible.front().unitId, target->unitId());
			ASSERT_TRUE(vstd::contains(eligible.front().spellEffectGroups, SpellID(SpellID::HYPNOTIZE)));
			ASSERT_TRUE(vstd::contains(eligible.front().spellEffectGroups, SpellID(SpellID::SLOW)));
		}
		spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, purifySpell().toSpell());
		const auto mechanics = purifySpell().toSpell()->battleMechanics(&cast);
		ASSERT_TRUE(battle()->battleGetSpellActionAllowance(BattleSide::ATTACKER, purifySpell()));
		spells::detail::ProblemImpl purifyProblem;
		ASSERT_TRUE(mechanics->canBeCast(purifyProblem))
			<< "negative controls must not decline because the Hero Action is spent";
		const spells::Target aim{spells::Destination(center)};
		const auto selection = SpellTargetEvaluator::purifySelection(mechanics.get(), aim);
		const auto viable = SpellTargetEvaluator::getViableTargets(mechanics.get());
		if(friendly)
		{
			ASSERT_TRUE(mechanics->canBeCastAt(aim));
			EXPECT_GT(selection.value, 0);
			EXPECT_EQ(selection.spellEffectGroups,
				(std::vector<std::pair<int32_t, SpellID>>{{static_cast<int32_t>(target->unitId()), SpellID::SLOW}}));
			EXPECT_FALSE(viable.empty());
		}
		else
		{
			EXPECT_EQ(selection.value, 0);
			EXPECT_TRUE(selection.spellEffectGroups.empty());
			EXPECT_TRUE(viable.empty());
		}
		auto callback = makeCallback();
		EXPECT_EQ(runPurifyAI(active, callback), friendly);
		const auto afterAI = snapshot();
		EXPECT_EQ(afterAI, before) << "\nBEFORE " << before.toCompactString()
			<< "\nAFTER " << afterAI.toCompactString();
		EXPECT_EQ(rng(), rngBefore);
		EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER), attackerAllowances);
		EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::DEFENDER), defenderAllowances);
		EXPECT_EQ(battle()->getUsedSpells(BattleSide::ATTACKER), attackerUsedSpells);
		EXPECT_EQ(battle()->getUsedSpells(BattleSide::DEFENDER), defenderUsedSpells);
		if(friendly)
		{
			ASSERT_EQ(callback->submitted.size(), 1u);
			const auto action = callback->submitted.front();
			ASSERT_EQ(action.spell, purifySpell());
			EXPECT_EQ(action.spellPurifyChoices, selection.spellEffectGroups);
			const auto mana = attackerSideHero->getManaAvailable();
			const auto cost = battle()->battleGetSpellCost(purifySpell().toSpell(), attackerSideHero);
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
			EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - cost);
			EXPECT_TRUE(newHorizonsPurify::spellEffectGroupBonuses(target, SpellID::SLOW).empty());
			EXPECT_TRUE(target->hasBonusOfType(BonusType::HYPNOTIZED));
			EXPECT_EQ(battle()->battleGetOwner(target), PlayerColor(0));
		}
		else
		{
			EXPECT_TRUE(callback->submitted.empty());
		}
		const JsonNode canonical(JsonPath::builtin("config/newHorizonsMagic"));
		EXPECT_FALSE(canonical["spells"]["core:hypnotize"]["heroAccess"].Bool());
		EXPECT_FALSE(canonical["spells"]["core:hypnotize"]["ordinaryAcquisition"].Bool());
	}
};

TEST_F(NewHorizonsPurifyAllegianceAITest, HistoricalSlowThenHypnotizeSelectsOnlySlowAndRetainsTransferredAlly)
{
	checkTransferredAllegiance(false, false);
}

TEST_F(NewHorizonsPurifyAllegianceAITest, HistoricalHypnotizedOriginalAllyIsExcludedWhileEffectivelyHostile)
{
	checkTransferredAllegiance(true, false);
}

TEST_F(NewHorizonsPurifyAllegianceAITest, PuppetControlledOriginalEnemyIsExcludedWithoutAllegianceTransfer)
{
	checkTransferredAllegiance(false, true);
}
