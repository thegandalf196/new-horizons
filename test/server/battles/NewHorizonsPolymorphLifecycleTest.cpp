/*
 * NewHorizonsPolymorphLifecycleTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsElementalTerrainFixture.h"
#include "../../../lib/battle/AccessibilityInfo.h"
#include "../../../lib/battle/BattleForm.h"
#include "../../../lib/battle/NewHorizonsDebuffStatuses.h"
#include "../../../lib/CCreatureHandler.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/effects/BattleForm.h"
#include "../../../server/queries/BattleQueries.h"
#include "../../../server/queries/QueriesProcessor.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../mock/mock_ServerCallback.h"
#include "../../mock/mock_vstd_RNG.h"

namespace
{
class NewHorizonsPolymorphLifecycleTest : public NewHorizonsElementalTerrainFixture
{
protected:
	CStack * target = nullptr;
	CStack * ally = nullptr;
	static SpellID polymorph() { return SpellID(SpellID::decode("new-horizons:polymorph")); }

	void prepare(bool largeOriginal = false)
	{
		startGame();
		ASSERT_TRUE(polymorph().hasValue());
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(polymorph());
		attackerSideHero->addSpellToSpellbook(SpellID::DISPEL);
		const SecondarySkill chaos(SecondarySkill::decode("new-horizons:chaosMagic"));
		attackerSideHero->setSecSkillLevel(chaos, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);
		startTerrainBattle(TerrainId::GRASS);
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		target = addStack(BattleSide::DEFENDER, creatureByName(largeOriginal ? "core:griffin" : "core:ogre"), BattleHex(8, 5), 10);
		ally = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 1000);
		ASSERT_NE(target, nullptr);
		ASSERT_NE(ally, nullptr);
		beginCombat();
	}

	bool cast(SpellID spell)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = spell;
		action.aimToUnit(target);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	void update(const std::function<void(battle::CUnitState &)> & mutate)
	{
		auto state = target->acquireState();
		mutate(*state);
		BattleUnitsChanged change;
		change.battleID = BattleID(0);
		change.changedStacks.emplace_back(target->unitId(), UnitChanges::EOperation::UPDATE);
		change.changedStacks.back().data = state->save();
		gameHandler->sendAndApply(change);
	}

	void installSmallForm()
	{
		update([&](battle::CUnitState & state) { state.beginBattleForm(creatureByName("core:ogre"), 2); });
		target->addNewBonus(std::make_shared<Bonus>(battle::polymorphMarker(polymorph(), PlayerColor(0))));
	}

	void defendUntilAttackerActivation()
	{
		ASSERT_EQ(battle()->battleActiveUnit(), target);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
			BattleAction::makeDefend(target)));
		ASSERT_EQ(battle()->battleActiveUnit(), ally);
	}

	void fillBoard()
	{
		for(int row = 0; row < GameConstants::BFIELD_HEIGHT; ++row)
			for(int column = 1; column < GameConstants::BFIELD_WIDTH - 1; ++column)
			{
				const BattleHex hex(column, row);
				if(!battle()->battleGetUnitByPos(hex, false))
					ASSERT_NE(addStack(BattleSide::ATTACKER, creatureByName("core:peasant"), hex, 1), nullptr);
			}
	}

	void releaseOriginalTail()
	{
		const auto tail = battle::Unit::occupiedHex(target->getPosition(), true, target->unitSide());
		const auto * blocker = battle()->battleGetUnitByPos(tail, false);
		ASSERT_NE(blocker, nullptr);
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		remove.changedStacks.emplace_back(blocker->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
	}
};
}

TEST_F(NewHorizonsPolymorphLifecycleTest, PaidRegisteredEnemyCastPreservesHPAndUsesNearestLegalNewFootprint)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NE(addStack(BattleSide::ATTACKER, creatureByName("core:peasant"), BattleHex(9, 5), 1), nullptr);
	update([](battle::CUnitState & state) { int64_t amount = 19; state.damage(amount); });
	const auto hp = target->getAvailableHealth();
	const auto id = target->unitId();
	const auto origin = target->getPosition();
	const auto access = battle()->getAccessibility(target);
	const auto mana = attackerSideHero->getManaAvailable();
	EXPECT_EQ(battle()->battleGetSpellCost(polymorph().toSpell(), attackerSideHero), 12);
	ASSERT_TRUE(cast(polymorph()));
	ASSERT_TRUE(target->hasBattleForm());
	EXPECT_EQ(target->getAvailableHealth(), hp);
	EXPECT_EQ(target->unitId(), id);
	EXPECT_EQ(target->unitSide(), BattleSide::DEFENDER);
	EXPECT_EQ(target->getCount(), (hp + target->getMaxHealth() - 1) / target->getMaxHealth());
	const auto nearest = access.nearestLegalPosition(origin, target->doubleWide(), target->unitSide());
	ASSERT_TRUE(nearest);
	EXPECT_EQ(target->getPosition(), *nearest);
	EXPECT_EQ(mana - attackerSideHero->getManaAvailable(), 12);
	EXPECT_EQ(target->getBattleFormRoundsRemaining(), 2);
	EXPECT_EQ(newHorizonsDebuffStatuses::snapshot(*target).count(), 1u);
}

TEST_F(NewHorizonsPolymorphLifecycleTest, ActualPaidFormExpiresAfterTwoBoundariesWithSurvivingHP)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto original = target->creatureId();
	ASSERT_TRUE(cast(polymorph()));
	update([](battle::CUnitState & state) { int64_t amount = 17; state.damage(amount); });
	const auto hp = target->getAvailableHealth();
	advanceRound();
	EXPECT_TRUE(target->hasBattleForm());
	EXPECT_EQ(target->getBattleFormRoundsRemaining(), 1);
	advanceRound();
	EXPECT_FALSE(target->hasBattleForm());
	EXPECT_EQ(target->creatureId(), original);
	EXPECT_EQ(target->getAvailableHealth(), hp);
	EXPECT_EQ(newHorizonsDebuffStatuses::snapshot(*target).count(), 0u);
}

TEST_F(NewHorizonsPolymorphLifecycleTest, NoLegalOriginalFootprintRetainsFormMarkerAndHPRetriesNextBoundary)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	ASSERT_NO_FATAL_FAILURE(installSmallForm());
	const auto hp = target->getAvailableHealth();
	ASSERT_NO_FATAL_FAILURE(fillBoard());
	advanceRound();
	advanceRound();
	EXPECT_TRUE(target->hasBattleForm());
	EXPECT_TRUE(target->isBattleFormRestorationPending());
	EXPECT_EQ(target->creatureId(), creatureByName("core:ogre"));
	EXPECT_EQ(target->getAvailableHealth(), hp);
	EXPECT_EQ(newHorizonsDebuffStatuses::snapshot(*target).count(), 1u);
	ASSERT_NO_FATAL_FAILURE(releaseOriginalTail());
	advanceRound();
	EXPECT_FALSE(target->hasBattleForm());
	EXPECT_EQ(target->creatureId(), creatureByName("core:griffin"));
	EXPECT_EQ(target->getAvailableHealth(), hp);
	EXPECT_EQ(newHorizonsDebuffStatuses::snapshot(*target).count(), 0u);
}

TEST_F(NewHorizonsPolymorphLifecycleTest, OrdinaryDispelRestoresOriginalWithoutChangingHP)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	ASSERT_NO_FATAL_FAILURE(installSmallForm());
	ASSERT_NO_FATAL_FAILURE(defendUntilAttackerActivation());
	const auto hp = target->getAvailableHealth();
	const auto mana = attackerSideHero->getManaAvailable();
	const auto cost = battle()->battleGetSpellCost(SpellID(SpellID::DISPEL).toSpell(), attackerSideHero);
	ASSERT_TRUE(cast(SpellID::DISPEL));
	EXPECT_EQ(mana - attackerSideHero->getManaAvailable(), cost);
	EXPECT_FALSE(target->hasBattleForm());
	EXPECT_EQ(target->creatureId(), creatureByName("core:griffin"));
	EXPECT_EQ(target->getAvailableHealth(), hp);
	EXPECT_EQ(newHorizonsDebuffStatuses::snapshot(*target).count(), 0u);
}

TEST_F(NewHorizonsPolymorphLifecycleTest, DispelWithoutSpaceRetainsMarkerFormAndHP)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	ASSERT_NO_FATAL_FAILURE(installSmallForm());
	ASSERT_NO_FATAL_FAILURE(defendUntilAttackerActivation());
	ASSERT_NO_FATAL_FAILURE(fillBoard());
	const auto hp = target->getAvailableHealth();
	const auto mana = attackerSideHero->getManaAvailable();
	const auto cost = battle()->battleGetSpellCost(SpellID(SpellID::DISPEL).toSpell(), attackerSideHero);
	ASSERT_TRUE(cast(SpellID::DISPEL));
	EXPECT_EQ(mana - attackerSideHero->getManaAvailable(), cost);
	EXPECT_TRUE(target->hasBattleForm());
	EXPECT_TRUE(target->isBattleFormRestorationPending());
	EXPECT_EQ(target->getAvailableHealth(), hp);
	EXPECT_EQ(newHorizonsDebuffStatuses::snapshot(*target).count(), 1u);
}

TEST_F(NewHorizonsPolymorphLifecycleTest, TimeStopPausesFormAndMarkerLifetimeTogether)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	ASSERT_NO_FATAL_FAILURE(installSmallForm());
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP,
		BonusSource::OTHER, 1, BonusSourceID()));
	advanceRound();
	advanceRound();
	EXPECT_EQ(target->getBattleFormRoundsRemaining(), 2);
	EXPECT_EQ(newHorizonsDebuffStatuses::snapshot(*target).count(), 1u);
	target->removeBonuses(Selector::type()(BonusType::TIME_STOP));
	advanceRound();
	EXPECT_EQ(target->getBattleFormRoundsRemaining(), 1);
	advanceRound();
	EXPECT_FALSE(target->hasBattleForm());
}

TEST_F(NewHorizonsPolymorphLifecycleTest, PhantomUsesOffensiveBodyHPNotIntegrityAndExpiresWithoutHealing)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	update([](battle::CUnitState & state) { state.summoned = true; state.initializePhantomProfile(200, 3); });
	const auto bodyHP = target->health.getCreatureHealthAvailable();
	ASSERT_GT(bodyHP, 200);
	ASSERT_TRUE(cast(polymorph()));
	EXPECT_EQ(target->getPhantomIntegrity(), 200);
	EXPECT_EQ(target->health.getCreatureHealthAvailable(), bodyHP);
	EXPECT_EQ(target->getCount(), (bodyHP + target->getMaxHealth() - 1) / target->getMaxHealth());
	update([](battle::CUnitState & state) { int64_t amount = 20; state.damage(amount); });
	EXPECT_EQ(target->getPhantomIntegrity(), 180);
	EXPECT_EQ(target->health.getCreatureHealthAvailable(), bodyHP);
	auto restored = target->acquireState();
	ASSERT_NO_THROW(restored->load(target->save()));
	EXPECT_EQ(restored->getPhantomIntegrity(), 180);
	EXPECT_EQ(restored->health.getCreatureHealthAvailable(), bodyHP);
	EXPECT_TRUE(restored->hasBattleFormState());
	advanceRound();
	advanceRound();
	EXPECT_FALSE(target->hasBattleForm());
	EXPECT_EQ(target->getPhantomIntegrity(), 180);
	EXPECT_EQ(target->health.getCreatureHealthAvailable(), bodyHP);
	ASSERT_NO_THROW(restored->load(target->save()));
	EXPECT_FALSE(restored->hasBattleForm());
	EXPECT_FALSE(restored->isBattleFormRestorationPending());
	EXPECT_EQ(restored->getPhantomIntegrity(), 180);
}

TEST_F(NewHorizonsPolymorphLifecycleTest, PhantomDeathClearsFormWithoutCreatingOriginalSpeciesBody)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	update([](battle::CUnitState & state) { state.summoned = true; state.initializePhantomProfile(200, 3); });
	ASSERT_TRUE(cast(polymorph()));
	update([](battle::CUnitState & state) { int64_t amount = 200; state.damage(amount); });
	EXPECT_FALSE(target->alive());
	EXPECT_FALSE(target->hasBattleForm());
	EXPECT_FALSE(target->isBattleFormRestorationPending());
	EXPECT_EQ(target->getAvailableHealth(), 0);
	auto restored = target->acquireState();
	ASSERT_NO_THROW(restored->load(target->save()));
	EXPECT_FALSE(restored->alive());
	EXPECT_FALSE(restored->hasBattleForm());
	EXPECT_FALSE(restored->isBattleFormRestorationPending());
}

TEST_F(NewHorizonsPolymorphLifecycleTest, EarlyBattleResultUsesOriginalSpeciesCasualties)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto original = target->creatureId();
	ASSERT_TRUE(cast(polymorph()));
	const auto query = std::make_shared<CBattleQuery>(gameHandler.get(), battle());
	gameHandler->queries->addQuery(query);
	gameHandler->battles->cheatBattleVictory(PlayerColor(0));
	ASSERT_TRUE(query->result);
	EXPECT_EQ(query->result->casualties[BattleSide::DEFENDER].at(original), 10);
}

TEST_F(NewHorizonsPolymorphLifecycleTest, ShapeshifterUsesTwoIIDDrawsAndChoosesLowerConvertedArmyValue)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const std::string skill = "new-horizons:chaosMagic";
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(skill)), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({skill, "new-horizons:chaosMagic.misfortuneWeaver"});
	attackerSideHero->applyPerkSelection({skill, "new-horizons:chaosMagic.shapeshifter"});
	ASSERT_TRUE(attackerSideHero->hasActivePerk(skill, "new-horizons:chaosMagic.shapeshifter"));
	spells::BattleCast castEvent(battle(), attackerSideHero, spells::Mode::HERO, polymorph().toSpell());
	const auto mechanics = polymorph().toSpell()->battleMechanics(&castEvent);
	spells::effects::BattleFormEffect effect;
	JsonNode config;
	config["type"].String() = "core:battleForm";
	effect.init(config);
	const auto candidates = effect.weightedFormsForTarget(mechanics.get(), target);
	ASSERT_GT(candidates.size(), 1u);
	std::vector<std::pair<uint64_t, size_t>> ranked;
	for(size_t index = 0; index < candidates.size(); ++index)
	{
		auto state = target->acquireState();
		state->beginBattleForm(candidates[index].form.creature, 2);
		ranked.emplace_back(static_cast<uint64_t>(state->getCount()) * candidates[index].form.creature.toCreature()->getAIValue(), index);
	}
	std::ranges::sort(ranked, [&](const auto & first, const auto & second)
	{
		return first.first < second.first || (first.first == second.first
			&& candidates[first.second].form.creature < candidates[second.second].form.creature);
	});
	for(size_t rank = 0; rank < ranked.size(); ++rank)
	{
		EXPECT_EQ(candidates[ranked[rank].second].weight, 2 * (candidates.size() - rank) - 1);
		EXPECT_EQ(candidates[ranked[rank].second].totalWeight, candidates.size() * candidates.size());
	}
	testing::StrictMock<vstd::RNGMock> random;
	testing::NiceMock<ServerCallbackMock> server;
	EXPECT_CALL(server, getRNG()).WillOnce(testing::Return(&random));
	EXPECT_CALL(random, nextInt64(0, candidates.size() - 1))
		.WillOnce(testing::Return(ranked.back().second))
		.WillOnce(testing::Return(ranked.front().second));
	ON_CALL(server, apply(testing::Matcher<BattleUnitsChanged &>(testing::_)))
		.WillByDefault([&](BattleUnitsChanged & packet) { gameHandler->sendAndApply(packet); });
	ON_CALL(server, apply(testing::Matcher<SetStackEffect &>(testing::_)))
		.WillByDefault([&](SetStackEffect & packet) { gameHandler->sendAndApply(packet); });
	const auto hp = target->getAvailableHealth();
	effect.apply(&server, mechanics.get(), {spells::Destination(target)});
	EXPECT_EQ(target->creatureId(), candidates[ranked.front().second].form.creature);
	EXPECT_EQ(target->getAvailableHealth(), hp);
}

TEST_F(NewHorizonsPolymorphLifecycleTest, RawPendingMetadataRejectsBeforeMutationAndLegacyAbsenceDefaultsFalse)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	ASSERT_NO_FATAL_FAILURE(installSmallForm());
	auto state = target->acquireState();
	const auto before = state->save();
	auto wrongType = before;
	wrongType["state"]["battleFormRestorationPending"] = JsonNode(std::string("true"));
	EXPECT_THROW(state->load(wrongType), std::runtime_error);
	EXPECT_EQ(state->save(), before);
	auto incoherent = before;
	incoherent["state"]["battleFormRestorationPending"].Bool() = true;
	// An ordinary two-round form cannot simultaneously be a held one-round restoration.
	EXPECT_THROW(state->load(incoherent), std::runtime_error);
	EXPECT_EQ(state->save(), before);
	state->deferBattleFormRestoration();
	ASSERT_TRUE(state->isBattleFormRestorationPending());
	auto legacy = state->save();
	legacy["state"].Struct().erase("battleFormRestorationPending");
	ASSERT_NO_THROW(state->load(legacy));
	EXPECT_FALSE(state->isBattleFormRestorationPending());
	EXPECT_TRUE(state->hasBattleForm());
	EXPECT_EQ(state->getAvailableHealth(), target->getAvailableHealth());
}

TEST(PolymorphProtocolTest, UnitStateAndCompositePacketsRejectOlderWritersBeforePrefixes)
{
	auto rejectOld = [](auto & packet)
	{
		CMemorySerializer writer;
		writer.oser.version = ESerializationVersion::NEW_HORIZONS_PURSUIT_MARCH;
		EXPECT_THROW(writer.oser & packet, std::runtime_error);
		EXPECT_TRUE(writer.extractBuffer().empty());
	};
	std::vector<JsonNode> meaningful;
	for(const auto * field : {"battleFormCreature", "battleFormOriginalCreature"})
	{
		JsonNode data;
		data["state"][field].String() = "core:ogre";
		meaningful.push_back(data);
	}
	JsonNode lifetime;
	lifetime["state"]["battleFormRoundsRemaining"].Integer() = 1;
	meaningful.push_back(lifetime);
	JsonNode pending;
	pending["state"]["battleFormRestorationPending"].Bool() = true;
	meaningful.push_back(pending);
	for(const auto & data : meaningful)
	{
		UnitChanges unit(7, UnitChanges::EOperation::UPDATE);
		unit.data = data;
		rejectOld(unit);
		BattleUnitsChanged units;
		units.battleID = BattleID(0);
		units.changedStacks.push_back(unit);
		rejectOld(units);
		BattleStackAttacked hit;
		hit.stackAttacked = 7;
		hit.newState = unit;
		rejectOld(hit);
		StacksInjured injury;
		injury.battleID = BattleID(0);
		injury.stacks.push_back(hit);
		rejectOld(injury);
		BattleAttack attack;
		attack.battleID = BattleID(0);
		attack.attackerChanges.battleID = BattleID(0);
		attack.bsa.push_back(hit);
		rejectOld(attack);
		attack.bsa.clear();
		attack.attackerChanges.changedStacks.push_back(unit);
		rejectOld(attack);
		CMemorySerializer current;
		current.oser & unit;
		CMemorySerializer reader(current.extractBuffer());
		UnitChanges restored;
		reader.iser & restored;
		EXPECT_EQ(restored.data, unit.data);
		EXPECT_EQ(restored.id, unit.id);
		EXPECT_EQ(restored.operation, unit.operation);
		EXPECT_EQ(restored.healthDelta, unit.healthDelta);
		for(const auto & [name, value] : data["state"].Struct())
		{
			EXPECT_EQ(restored.data["state"][name].getType(), value.getType());
			EXPECT_EQ(restored.data["state"][name], value);
		}
	}
	UnitChanges plain(7, UnitChanges::EOperation::UPDATE);
	plain.data["state"]["position"].Integer() = 88;
	CMemorySerializer older;
	older.oser.version = ESerializationVersion::NEW_HORIZONS_PURSUIT_MARCH;
	EXPECT_NO_THROW(older.oser & plain);
	EXPECT_FALSE(older.extractBuffer().empty());
}

TEST(PolymorphProtocolTest, MarkerAddUpdateRemoveRejectOlderWritersButPlainEffectsRemainCompatible)
{
	const SpellID spell(SpellID::decode("new-horizons:polymorph"));
	const Bonus marker = battle::polymorphMarker(spell, PlayerColor(0));
	for(int operation = 0; operation < 3; ++operation)
	{
		SetStackEffect packet;
		packet.battleID = BattleID(0);
		auto & rows = operation == 0 ? packet.toAdd : operation == 1 ? packet.toUpdate : packet.toRemove;
		rows.emplace_back(7, std::vector<Bonus>{marker});
		CMemorySerializer old;
		old.oser.version = ESerializationVersion::NEW_HORIZONS_PURSUIT_MARCH;
		EXPECT_THROW(old.oser & packet, std::runtime_error);
		EXPECT_TRUE(old.extractBuffer().empty());
		CMemorySerializer current;
		current.oser & packet;
		CMemorySerializer reader(current.extractBuffer());
		SetStackEffect restored;
		reader.iser & restored;
		const auto & decoded = operation == 0 ? restored.toAdd : operation == 1 ? restored.toUpdate : restored.toRemove;
		ASSERT_EQ(decoded.size(), 1u);
		ASSERT_EQ(decoded.front().second.size(), 1u);
		EXPECT_TRUE(battle::isPolymorphMarker(&decoded.front().second.front()));
	}
	SetStackEffect ordinary;
	ordinary.battleID = BattleID(0);
	ordinary.toAdd.emplace_back(7, std::vector<Bonus>{Bonus(BonusDuration::ONE_BATTLE,
		BonusType::MORALE, BonusSource::OTHER, 1, BonusSourceID())});
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_PURSUIT_MARCH;
	EXPECT_NO_THROW(old.oser & ordinary);
	EXPECT_FALSE(old.extractBuffer().empty());
}
