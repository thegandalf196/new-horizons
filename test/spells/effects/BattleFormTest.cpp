/*
 * BattleFormTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "../../server/battles/BattleTestFixture.h"
#include "../../SpellPointTestUtils.h"
#include "../../../server/CGameHandler.h"

#include "../../../lib/CCreatureHandler.h"
#include "../../../lib/CRandomGenerator.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/battle/AccessibilityInfo.h"
#include "../../../lib/battle/CUnitState.h"
#include "../../../lib/battle/Unit.h"
#include "../../../lib/entities/creature/NewHorizonsCreatureCategoryRules.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/Problem.h"
#include "../../../lib/spells/effects/BattleForm.h"
#include "../../../server/battles/BattleProcessor.h"

#include "../../mock/mock_spells_Mechanics.h"

#include <vstd/RNG.h>

using namespace ::testing;

namespace
{
JsonNode battleFormCategoryRules()
{
	JsonNode rules;
	rules["schemaVersion"].Integer() = 1;
	rules["rulesetVersion"].Integer() = newHorizonsCreatures::CREATURE_CATEGORY_RULESET_VERSION;
	rules["sourceRulesetId"].String() = "new-horizons:battleFormTest";
	for(const std::string name : {"core", "elite", "champion"})
	{
		rules["categories"][name]["nameTextId"].String() = "new-horizons.category." + name + ".name";
		rules["categories"][name]["descriptionTextId"].String() = "new-horizons.category." + name + ".description";
	}
	// These two elite creatures deliberately come from different factions and have
	// different battlefield footprints. This is test data only, not an activated roster.
	rules["creatures"]["core:ogre"].String() = "elite";
	rules["creatures"]["core:griffin"].String() = "elite";
	rules["growthLines"]["core:ogre"]["weeklyBaseGrowth"].Integer() = 4;
	rules["growthLines"]["core:ogre"]["members"].Vector().emplace_back("core:ogre");
	rules["growthLines"]["core:griffin"]["weeklyBaseGrowth"].Integer() = 3;
	rules["growthLines"]["core:griffin"]["members"].Vector().emplace_back("core:griffin");
	return rules;
}

class InstalledCategoryOverride
{
	std::unique_ptr<GameSettings> previous;

public:
	explicit InstalledCategoryOverride(const JsonNode & rules)
	{
		auto config = LIBRARY->settingsHandler->getFullConfig();
		config["creatures"]["newHorizonsCategories"] = rules;
		auto replacement = std::make_unique<GameSettings>();
		replacement->loadBase(config);
		previous = std::move(LIBRARY->settingsHandler);
		LIBRARY->settingsHandler = std::move(replacement);
	}

	~InstalledCategoryOverride()	{ LIBRARY->settingsHandler = std::move(previous); }
};

JsonNode battleFormEffectConfig(bool includeDuration = false)
{
	JsonNode config;
	config["type"].String() = "core:battleForm";
	if(includeDuration)
		config["duration"].Integer() = 3;
	return config;
}

class BattleFormEffectCastTest : public BattleTestFixture
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		// The test creates an ordinary clone through the real spell action. Clone
		// is intentionally inactive in the installed New Horizons spell roster.
		if(allowLegacyCloneSpell)
			loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode());
	}

	void prepareBattle(bool prepareCloneCast = false)
	{
		allowLegacyCloneSpell = prepareCloneCast;
		InstalledCategoryOverride rules(battleFormCategoryRules());
		startGame();
		if(prepareCloneCast)
		{
			giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
			attackerSideHero->addSpellToSpellbook(SpellID::CLONE);
			const auto * cloneSpell = SpellID(SpellID::CLONE).toSpell();
			ASSERT_NE(cloneSpell, nullptr);
			attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER,
				3 * attackerSideHero->getEffectPowerDivisor(cloneSpell), ChangeValueMode::ABSOLUTE);
			setTestSpellPointTotal(attackerSideHero, 100);
		}
		startBattle();

		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);

		const auto origin = BattleHex(8, 5);
		target = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), origin, 4);
		const auto blockedTail = battle::Unit::occupiedHex(origin, true, BattleSide::DEFENDER);
		ASSERT_TRUE(blockedTail.isAvailable());
		blocker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), blockedTail, 1);
		if(prepareCloneCast)
			cloneSource = addStack(BattleSide::ATTACKER, creatureByName("core:ogre"), BattleHex(3, 3), 4);
		ASSERT_NE(target, nullptr);
		ASSERT_NE(blocker, nullptr);
		if(prepareCloneCast)
			ASSERT_NE(cloneSource, nullptr);
		beginCombat();
	}

	void saveState(CStack * stack, const std::function<void(battle::CUnitState &)> & mutate)
	{
		auto state = stack->acquireState();
		mutate(*state);

		BattleUnitsChanged update;
		update.battleID = BattleID(0);
		UnitChanges change(stack->unitId(), UnitChanges::EOperation::UPDATE);
		change.data = state->save();
		update.changedStacks.push_back(std::move(change));
		gameHandler->sendAndApply(update);
	}

	CStack * target = nullptr;
	CStack * blocker = nullptr;
	CStack * cloneSource = nullptr;
	bool allowLegacyCloneSpell = false;
};
}

TEST_F(BattleFormEffectCastTest, UsesUniformCapturedCategoryPoolAndRelocatesWithoutChangingHealthOrIdentity)
{
	prepareBattle();
	ASSERT_NE(target, nullptr);
	ASSERT_NE(blocker, nullptr);

	const auto sourceCreature = target->creatureId();
	const auto sourceCategory = battle()->battleGetCreatureCategory(sourceCreature);
	ASSERT_TRUE(sourceCategory);
	ASSERT_EQ(sourceCategory->category, newHorizonsCreatures::CreatureCategory::ELITE);

	std::vector<const Creature *> candidatePool;
	LIBRARY->creatures()->forEach([&](const Creature * creature, bool & stop)
	{
		(void)stop;
		if(!creature || creature->getBaseHitPoints() <= 0)
			return;
		const auto category = battle()->battleGetCreatureCategory(creature->getId());
		if(category && category->category == sourceCategory->category)
			candidatePool.push_back(creature);
	});
	ASSERT_EQ(candidatePool.size(), 2u);
	const auto griffin = creatureByName("core:griffin");
	ASSERT_TRUE(std::ranges::any_of(candidatePool, [griffin](const Creature * creature)
	{
		return creature->getId() == griffin;
	}));

	const auto originalUnitId = target->unitId();
	const auto originalOwner = target->unitOwner();
	const auto originalSide = target->unitSide();
	const auto originalInitiative = target->getInitiative();
	int64_t damage = 19;
	auto injured = target->acquireState();
	injured->damage(damage);
	ASSERT_EQ(damage, 19);
	const auto sourceHealth = injured->health.getCreatureHealthAvailable();
	ASSERT_GT(sourceHealth, 0);
	BattleUnitsChanged injury;
	injury.battleID = BattleID(0);
	UnitChanges injuryChange(target->unitId(), UnitChanges::EOperation::UPDATE);
	injuryChange.data = injured->save();
	injury.changedStacks.push_back(std::move(injuryChange));
	gameHandler->sendAndApply(injury);

	const auto origin = target->getPosition();
	const auto sourceAccessibility = battle()->getAccessibility(target);
	const auto expectedLargeFormPosition = sourceAccessibility.nearestLegalPosition(
		origin, true, target->unitSide());
	ASSERT_TRUE(expectedLargeFormPosition);
	ASSERT_NE(*expectedLargeFormPosition, origin)
		<< "The cross-faction Griffin form must not be discarded just because its footprint cannot fit at the source anchor";
	const auto expectedSmallFormPosition = sourceAccessibility.nearestLegalPosition(
		origin, false, target->unitSide());
	ASSERT_TRUE(expectedSmallFormPosition);

	spells::MechanicsMock mechanics;
	ON_CALL(mechanics, battle()).WillByDefault(Return(battle()));
	ON_CALL(mechanics, creatures()).WillByDefault(Return(LIBRARY->creatures()));
	ON_CALL(mechanics, isReceptive(_)).WillByDefault(Return(true));
	ON_CALL(mechanics, isSmart()).WillByDefault(Return(false));
	ON_CALL(mechanics, isNegativeSpell()).WillByDefault(Return(true));
	ON_CALL(mechanics, getBattleID()).WillByDefault(Return(BattleID(0)));

	spells::effects::BattleFormEffect effect;
	effect.init(battleFormEffectConfig()); // omitted duration defaults to two rounds
	EXPECT_EQ(effect.getDuration(), 2);
	const auto candidates = effect.formsForTarget(&mechanics, target);
	ASSERT_EQ(candidates.size(), candidatePool.size());
	for(size_t index = 0; index < candidatePool.size(); ++index)
	{
		const auto * expectedCreature = candidatePool[index];
		EXPECT_EQ(candidates[index].creature, expectedCreature->getId());
		EXPECT_EQ(candidates[index].landing,
			expectedCreature->isDoubleWide() ? *expectedLargeFormPosition : *expectedSmallFormPosition);
	}

	int selectedSeed = 0;
	const Creature * expectedForm = nullptr;
	for(int seed = 1; seed < 10000 && expectedForm == nullptr; ++seed)
	{
		CRandomGenerator expectedRandom(seed);
		const auto * candidate = *RandomGeneratorUtil::nextItem(candidatePool, expectedRandom);
		if(candidate->getId() == griffin)
		{
			selectedSeed = seed;
			expectedForm = candidate;
		}
	}
	ASSERT_NE(expectedForm, nullptr);
	ASSERT_GT(selectedSeed, 0);
	gameHandler->randomizer->setSeed(selectedSeed);
	spells::Target selectedTarget;
	selectedTarget.emplace_back(target);
	spells::detail::ProblemImpl problem;
	ASSERT_TRUE(effect.applicableTarget(problem, &mechanics, selectedTarget));
	effect.apply(gameHandler->spellcastEnvironment(), &mechanics, selectedTarget);

	EXPECT_EQ(target->unitId(), originalUnitId);
	EXPECT_EQ(target->unitOwner(), originalOwner);
	EXPECT_EQ(target->unitSide(), originalSide);
	EXPECT_EQ(target->getInitiative(), originalInitiative);
	EXPECT_EQ(target->creatureId(), expectedForm->getId());
	EXPECT_EQ(target->getPosition(), *expectedLargeFormPosition);
	const auto transformed = target->acquireState();
	EXPECT_TRUE(transformed->hasBattleForm());
	EXPECT_EQ(transformed->battleFormOriginalCreature(), sourceCreature);
	EXPECT_EQ(transformed->battleFormCreature(), expectedForm->getId());
	EXPECT_EQ(transformed->health.getCreatureHealthAvailable(), sourceHealth);
	EXPECT_EQ(transformed->save()["state"]["battleFormRoundsRemaining"].Integer(), 2);
}

TEST_F(BattleFormEffectCastTest, KeepsPhantomArmyInTargetFlowAndRejectsItExplicitly)
{
	prepareBattle();
	ASSERT_NE(target, nullptr);

	spells::MechanicsMock mechanics;
	ON_CALL(mechanics, battle()).WillByDefault(Return(battle()));
	ON_CALL(mechanics, creatures()).WillByDefault(Return(LIBRARY->creatures()));
	ON_CALL(mechanics, isReceptive(_)).WillByDefault(Return(true));
	ON_CALL(mechanics, isSmart()).WillByDefault(Return(false));
	ON_CALL(mechanics, isNegativeSpell()).WillByDefault(Return(true));

	spells::effects::BattleFormEffect effect;
	effect.init(battleFormEffectConfig(true));
	spells::Target target;
	target.emplace_back(this->target);

	saveState(this->target, [](battle::CUnitState & state)
	{
		state.summoned = true;
		state.initializePhantomProfile(state.getAvailableHealth(), 2);
	});

	spells::detail::ProblemImpl problem;
	const auto filtered = effect.filterTarget(&mechanics, target);
	ASSERT_EQ(filtered.size(), 1u) << "Phantom Army target must reach applicability for an explicit rejection";
	EXPECT_FALSE(effect.applicableTarget(problem, &mechanics, filtered));
	std::vector<std::string> messages;
	problem.getAll(messages);
	ASSERT_EQ(messages.size(), 1u);
	EXPECT_NE(messages.front().find("cannot currently affect Phantom Army stacks"), std::string::npos);
}

TEST_F(BattleFormEffectCastTest, AppliesThroughBattleStatePacketToARealClone)
{
	prepareBattle(true);
	ASSERT_NE(cloneSource, nullptr);

	BattleAction cloneCast;
	cloneCast.actionType = EActionType::HERO_SPELL;
	cloneCast.side = BattleSide::ATTACKER;
	cloneCast.spell = SpellID::CLONE;
	cloneCast.aimToUnit(cloneSource);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), cloneCast));
	ASSERT_EQ(server.castsOf(SpellID::CLONE).size(), 1u);

	const CStack * clone = nullptr;
	for(const auto * unit : battle()->battleGetAllStacks())
		if(unit->isClone() && unit->unitSide() == BattleSide::ATTACKER)
			clone = unit;
	ASSERT_NE(clone, nullptr);
	ASSERT_TRUE(clone->alive());
	const auto originalUnitId = clone->unitId();
	const auto originalOwner = clone->unitOwner();
	const auto originalSide = clone->unitSide();
	const auto sourceCreature = clone->creatureId();
	const auto originalHealth = clone->acquireState()->health.getCreatureHealthAvailable();

	spells::MechanicsMock mechanics;
	ON_CALL(mechanics, battle()).WillByDefault(Return(battle()));
	ON_CALL(mechanics, creatures()).WillByDefault(Return(LIBRARY->creatures()));
	ON_CALL(mechanics, isReceptive(_)).WillByDefault(Return(true));
	ON_CALL(mechanics, isSmart()).WillByDefault(Return(false));
	ON_CALL(mechanics, isNegativeSpell()).WillByDefault(Return(true));
	ON_CALL(mechanics, getBattleID()).WillByDefault(Return(BattleID(0)));

	spells::effects::BattleFormEffect effect;
	effect.init(battleFormEffectConfig());
	spells::Target selectedTarget;
	selectedTarget.emplace_back(clone);
	spells::detail::ProblemImpl problem;
	ASSERT_TRUE(effect.applicableTarget(problem, &mechanics, selectedTarget));
	effect.apply(gameHandler->spellcastEnvironment(), &mechanics, selectedTarget);

	const auto transformed = clone->acquireState();
	ASSERT_TRUE(transformed->hasBattleForm());
	EXPECT_TRUE(transformed->isClone());
	EXPECT_EQ(transformed->unitId(), originalUnitId);
	EXPECT_EQ(transformed->unitOwner(), originalOwner);
	EXPECT_EQ(transformed->unitSide(), originalSide);
	EXPECT_EQ(transformed->battleFormOriginalCreature(), sourceCreature);
	EXPECT_EQ(transformed->health.getCreatureHealthAvailable(), originalHealth);

	const auto saved = transformed->save();
	const auto restored = clone->acquireState();
	restored->load(saved);
	EXPECT_TRUE(restored->isClone());
	EXPECT_TRUE(restored->hasBattleForm());
	EXPECT_EQ(restored->battleFormOriginalCreature(), sourceCreature);
	EXPECT_EQ(restored->health.getCreatureHealthAvailable(), originalHealth);
}

TEST(BattleFormEffectConfigTest, RejectsUnknownParametersAndInvalidDurations)
{
	spells::effects::BattleFormEffect effect;
	auto invalid = battleFormEffectConfig();
	invalid["unsupported"].Bool() = true;
	EXPECT_THROW(effect.init(invalid), std::runtime_error);

	for(const auto duration : {0.0, -1.0, 1.5, 2147483648.0})
	{
		auto config = battleFormEffectConfig();
		config["duration"].Float() = duration;
		EXPECT_THROW(effect.init(config), std::runtime_error);
	}

	auto validDuration = battleFormEffectConfig(true);
	EXPECT_NO_THROW(effect.init(validDuration));
	EXPECT_EQ(effect.getDuration(), 3);
}
