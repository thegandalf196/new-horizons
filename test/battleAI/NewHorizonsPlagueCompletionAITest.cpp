/*
 * NewHorizonsPlagueCompletionAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"

#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../AI/BattleAI/SpellTargetsEvaluator.h"
#include "../../lib/GameSettings.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/NewHorizonsPlague.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/battle/NewHorizonsOffense.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../lib/spells/NewHorizonsSorcery.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/Problem.h"
#include <vcmi/ServerCallback.h>
#include <vcmi/Environment.h>
#include "../../AI/BattleAI/HypotheticBattle.h"

namespace
{
class PlagueCompletionEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit PlagueCompletionEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsPlagueCompletionAITest : public HeroCommandFixture
{
protected:
	CStack * actor = nullptr;
	CStack * victim = nullptr;
	std::shared_ptr<PlagueCompletionEnvironment> environment;

	SpellID plague() const { return SpellID(SpellID::decode(std::string(newHorizonsPlague::SPELL_ID))); }

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}

	void prepare(int count = 1000)
	{
		ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
		startGame();
		defenderSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:shadowMagic")),
			MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		defenderSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
		defenderSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		giveArtifact(defenderSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		defenderSideHero->addSpellToSpellbook(plague());
		setTestSpellPointTotal(defenderSideHero, 1000);
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		actor = addStack(BattleSide::ATTACKER, creatureByName("core:peasant"), BattleHex(7, 5), count);
		victim = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(8, 5), 10000);
		ASSERT_NE(actor, nullptr);
		ASSERT_NE(victim, nullptr);
		ASSERT_NE(addStack(BattleSide::ATTACKER, creatureByName("core:peasant"), BattleHex(3, 5), 1000), nullptr);
		blockRetaliation(actor);
		forceMaximumDamage(actor);
		// Dynamically added same-slot stacks must not rely on tied queue order.
		actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::STACKS_INITIATIVE_BASE, BonusSource::OTHER, 1000,
			BonusSourceID(), BonusSubtypeID(), BonusValueType::BASE_NUMBER));
		beginCombat();
		ASSERT_EQ(battle()->battleActiveUnit(), actor);
		ASSERT_TRUE(castOn(defenderSideHero, plague(), actor));
		ASSERT_NE(marker(actor), nullptr);
		environment = std::make_shared<PlagueCompletionEnvironment>(gameState());
	}

	std::shared_ptr<const Bonus> marker(const battle::Unit * unit) const
	{
		const auto bonuses = unit->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(plague())).And(Selector::type()(BonusType::COMBAT_EVENT_TRIGGER)));
		return bonuses->empty() ? std::shared_ptr<const Bonus>{} : bonuses->front();
	}

	std::shared_ptr<HypotheticBattle> projection()
	{
		return std::make_shared<HypotheticBattle>(environment.get(),
			std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	}

	void freezeActor()
	{
		SetStackEffect effect;
		effect.battleID = BattleID(0);
		effect.toAdd.emplace_back(actor->unitId(), std::vector<Bonus>{newHorizonsFrozen::makeFrozenMarker(
			BonusSourceID(creatureByName("core:iceElemental")), battle()->getRound())});
		gameHandler->sendAndApply(effect);
		ASSERT_TRUE(newHorizonsFrozen::isFrozen(*actor));
	}

	void expireCompletedBonuses(const std::shared_ptr<HypotheticBattle> & model)
	{
		model->getForUpdate(actor->unitId())->removeUnitBonus(CSelector([](const Bonus * bonus)
		{
			return Bonus::UntilActivationEnds(bonus) && !newHorizonsOffense::isNoQuarterMoralePenalty(bonus);
		}));
	}

	void applyInjury(const std::shared_ptr<HypotheticBattle> & model, SpellID spell, bool spellEffect, int64_t amount)
	{
		BattleStackAttacked hit;
		hit.stackAttacked = actor->unitId();
		hit.attackerID = victim->unitId();
		hit.spellID = spell;
		hit.flags = spellEffect ? BattleStackAttacked::SPELL_EFFECT : 0;
		hit.damageAmount = amount;
		CStack::prepareAttacked(hit, *model->getServerCallback()->getRNG(),
			model->battleGetUnitByID(actor->unitId())->acquireState(), false, false, battle::DamageProvenance::SPELL);
		StacksInjured injury;
		injury.battleID = BattleID(0);
		injury.stacks.push_back(hit);
		model->getServerCallback()->apply(injury);
	}

	void prepareStrategicRebirth()
	{
		ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
		startGame();
		attackerSideHero->clearSlots();
		defenderSideHero->clearSlots();
		attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:brissa")));
		const auto skill = "new-horizons:elementalRebirth";
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(skill)),
			MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({skill, "new-horizons:elementalRebirth.primalBurst"});
		ASSERT_TRUE(attackerSideHero->hasActivePerk(skill, "new-horizons:elementalRebirth.primalBurst"));
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creatureByName("core:peasant"), 100));
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(1), creatureByName("core:peasant"), 1000));
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), creatureByName("core:peasant"), 10000));
		defenderSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:shadowMagic")),
			MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		defenderSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 200, ChangeValueMode::ABSOLUTE);
		defenderSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		giveArtifact(defenderSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		defenderSideHero->addSpellToSpellbook(plague());
		setTestSpellPointTotal(defenderSideHero, 1000);
		startBattle();
		for(const auto & stack : battle()->stacks)
			if(stack->unitSlot() == SlotID(0))
			{
				if(stack->unitSide() == BattleSide::ATTACKER)
					actor = stack.get();
				else
					victim = stack.get();
			}
		ASSERT_NE(actor, nullptr);
		ASSERT_NE(victim, nullptr);
		ASSERT_GT(actor->getBattleStartMaximumAggregateHP(), 0);
		beginCombat();
		ASSERT_TRUE(castOn(defenderSideHero, plague(), actor));
		environment = std::make_shared<PlagueCompletionEnvironment>(gameState());
	}

	void completeProjectedAttack(const std::shared_ptr<HypotheticBattle> & model)
	{
		auto source = model->getForUpdate(actor->unitId());
		auto target = model->getForUpdate(victim->unitId());
		ASSERT_TRUE(source->alive());
		ASSERT_TRUE(target->alive());
		DamageCache cache;
		cache.buildDamageCache(model, BattleSide::ATTACKER);
		const auto attack = AttackPossibility::evaluate(BattleAttackInfo(source.get(), target.get(), 0, false),
			source->getPosition(), cache, model);
		ASSERT_NE(attack.attackerState, nullptr);
		BattleExchangeVariant exchange;
		exchange.trackAttack(attack, model, cache);
		ASSERT_LT(model->battleGetUnitByID(victim->unitId())->getAvailableHealth(), victim->getAvailableHealth());
		// This is the completed applied attack boundary, not nextTurn/startup.
		newHorizonsPlague::completeActivation(*model, *model->getServerCallback(), actor->unitId());
	}
};
}

TEST_F(NewHorizonsPlagueCompletionAITest, AppliedAttackThenTickMatchesActualAcceptedServerAttack)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto liveHealth = actor->getAvailableHealth();
	const auto liveMarker = marker(actor);
	ASSERT_EQ(defenderSideHero->getSpellBonus(plague().toSpell(), liveMarker->val, actor), liveMarker->val);
	ASSERT_TRUE(actor->getBonusesOfType(BonusType::SPELL_DAMAGE_REDUCTION)->empty());
	ASSERT_TRUE(actor->getBonusesOfType(BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS)->empty());
	ASSERT_EQ(battle()->battleGetHoldTheLineMagicalReductionBasisPoints(actor), 0);
	ASSERT_EQ(battle()->battleGetPerkMagicalReductionBasisPoints(actor), 0);
	actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::SPELL_DAMAGE_REDUCTION, BonusSource::CREATURE_ABILITY, 50,
		BonusSourceID(), BonusSubtypeID(SpellSchool::ANY)));
	const auto unmodifiedTick = newHorizonsPlague::adjustedTickDamage(*battle(),
		BattleSide::DEFENDER, actor, liveMarker->val);
	ASSERT_EQ(unmodifiedTick, liveMarker->val / 2);
	// Exercise the existing hero-only spell-damage producer after infection,
	// without changing the marker's captured raw damage or school coefficient.
	defenderSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::SPECIFIC_SPELL_DAMAGE, BonusSource::OTHER, 20, BonusSourceID(),
		BonusSubtypeID(plague())));
	// Independent numeric oracle: the sole 50% MDR source floors first,
	// then the controlled 20% hero-specific modifier scales that result.
	const auto expectedTick = (liveMarker->val / 2) * 120 / 100;
	ASSERT_EQ(newHorizonsPlague::adjustedTickDamage(*battle(),
		BattleSide::DEFENDER, actor, liveMarker->val), expectedTick);
	ASSERT_GT(expectedTick, unmodifiedTick);
	ASSERT_LT(expectedTick, liveHealth);
	const auto firstInjury = server.injuries.size();
	auto model = projection();
	ASSERT_EQ(model->battleGetFightingHero(BattleSide::DEFENDER), nullptr);
	ASSERT_EQ(model->getBattle()->getSideHero(BattleSide::DEFENDER), defenderSideHero);
	EXPECT_EQ(newHorizonsPlague::adjustedTickDamage(*model, BattleSide::DEFENDER,
		model->battleGetUnitByID(actor->unitId()), liveMarker->val), expectedTick);
	ASSERT_NO_FATAL_FAILURE(completeProjectedAttack(model));
	const auto * projected = model->battleGetUnitByID(actor->unitId());
	ASSERT_NE(marker(projected), nullptr);
	EXPECT_LT(projected->getAvailableHealth(), liveHealth);
	EXPECT_EQ(projected->getAvailableHealth(), liveHealth - expectedTick);
	ASSERT_EQ(model->getProjectedPlagueHits().size(), 1u);
	EXPECT_EQ(model->getProjectedPlagueHits().front().finalizedDamage, expectedTick);
	EXPECT_EQ(actor->getAvailableHealth(), liveHealth);
	EXPECT_EQ(marker(actor), liveMarker);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMeleeAttack(actor, victim->getPosition(), actor->getPosition())));
	ASSERT_NE(marker(actor), nullptr);
	int64_t actualTick = 0;
	for(size_t index = firstInjury; index < server.injuries.size(); ++index)
		for(const auto & hit : server.injuries[index].stacks)
			if(hit.stackAttacked == actor->unitId() && hit.spellID == plague()
				&& (hit.flags & BattleStackAttacked::SPELL_EFFECT))
				actualTick += hit.damageAmount;
	EXPECT_EQ(actualTick, expectedTick);
	EXPECT_EQ(actor->getAvailableHealth(), liveHealth - expectedTick);
	EXPECT_EQ(projected->getAvailableHealth(), actor->getAvailableHealth());
	EXPECT_EQ(marker(projected)->turnsRemain, marker(actor)->turnsRemain);
	EXPECT_EQ(marker(projected)->parameters->toCustom<JsonNode>(), marker(actor)->parameters->toCustom<JsonNode>());
	EXPECT_TRUE(newHorizonsPlague::hasPlague(model->battleGetUnitByID(victim->unitId())));
	EXPECT_TRUE(newHorizonsPlague::hasPlague(victim));
}

TEST_F(NewHorizonsPlagueCompletionAITest, SameRoundCompletionDeduplicatesWithoutChangingLiveOrSibling)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto parent = projection();
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	const auto health = actor->getAvailableHealth();
	ASSERT_NO_FATAL_FAILURE(completeProjectedAttack(child));
	const auto * unit = child->battleGetUnitByID(actor->unitId());
	const auto after = unit->getAvailableHealth();
	const auto parameters = marker(unit)->parameters->toCustom<JsonNode>();
	newHorizonsPlague::completeActivation(*child, *child->getServerCallback(), actor->unitId());
	EXPECT_EQ(unit->getAvailableHealth(), after);
	EXPECT_EQ(marker(unit)->turnsRemain, 2);
	EXPECT_EQ(marker(unit)->parameters->toCustom<JsonNode>(), parameters);
	EXPECT_EQ(parent->battleGetUnitByID(actor->unitId())->getAvailableHealth(), health);
	EXPECT_EQ(sibling->battleGetUnitByID(actor->unitId())->getAvailableHealth(), health);
	EXPECT_EQ(marker(sibling->battleGetUnitByID(actor->unitId()))->turnsRemain, 3);
	EXPECT_FALSE(newHorizonsPlague::hasPlague(parent->battleGetUnitByID(victim->unitId())));
	EXPECT_EQ(actor->getAvailableHealth(), health);
}

TEST_F(NewHorizonsPlagueCompletionAITest, ActualHostileLockPausesOnlyDurationWhileProjectedAttackTicksAndSpreads)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	defenderSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(newHorizonsSorcery::SORCERY_MAGIC_SKILL)),
		MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	const auto seal = SpellID(SpellID::decode(newHorizonsSorcery::SPELL_LOCK_SPELL));
	defenderSideHero->addSpellToSpellbook(seal);
	advanceRound();
	ASSERT_TRUE(castOn(defenderSideHero, seal, actor));
	ASSERT_NE(marker(actor), nullptr);
	ASSERT_TRUE(newHorizonsSorcery::durationPausedBySpellLock(*actor, *marker(actor)));
	const auto health = actor->getAvailableHealth();
	auto model = projection();
	ASSERT_NO_FATAL_FAILURE(completeProjectedAttack(model));
	const auto * unit = model->battleGetUnitByID(actor->unitId());
	ASSERT_NE(marker(unit), nullptr);
	EXPECT_LT(unit->getAvailableHealth(), health);
	EXPECT_EQ(marker(unit)->turnsRemain, 3);
	EXPECT_EQ(marker(unit)->parameters->toCustom<JsonNode>()["lastProcessedRound"].Integer(), model->battleGetRound());
	EXPECT_TRUE(newHorizonsPlague::hasPlague(model->battleGetUnitByID(victim->unitId())));
	EXPECT_EQ(actor->getAvailableHealth(), health);
	EXPECT_FALSE(newHorizonsPlague::hasPlague(victim));
}

TEST_F(NewHorizonsPlagueCompletionAITest, OneRoundHostileLockExpiresThenFinalPlagueTickRemovesOnlyChildInfection)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	defenderSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(newHorizonsSorcery::SORCERY_MAGIC_SKILL)),
		MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	defenderSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 0, ChangeValueMode::ABSOLUTE);
	const auto seal = SpellID(SpellID::decode(newHorizonsSorcery::SPELL_LOCK_SPELL));
	defenderSideHero->addSpellToSpellbook(seal);
	const auto liveActorState = actor->save();
	const auto liveVictimState = victim->save();
	// This branch evaluates the defender's paid cast, so it must retain the
	// caster's ordinary visible battle context, not the attacker-only callback.
	auto parent = std::make_shared<HypotheticBattle>(environment.get(),
		std::make_shared<CPlayerBattleCallback>(battle(), defenderSideHero->getOwner()));
	// Consume two actual detached attack completions in distinct rounds; do
	// not fabricate a one-tick infection by editing its bonus or parameters.
	ASSERT_NO_FATAL_FAILURE(completeProjectedAttack(parent));
	ASSERT_NE(marker(parent->battleGetUnitByID(actor->unitId())), nullptr);
	ASSERT_EQ(marker(parent->battleGetUnitByID(actor->unitId()))->turnsRemain, 2);
	parent->nextRound();
	ASSERT_NO_FATAL_FAILURE(completeProjectedAttack(parent));
	ASSERT_NE(marker(parent->battleGetUnitByID(actor->unitId())), nullptr);
	ASSERT_EQ(marker(parent->battleGetUnitByID(actor->unitId()))->turnsRemain, 1);
	parent->nextRound();
	auto parentActor = parent->getForUpdate(actor->unitId());
	auto parentVictim = parent->getForUpdate(victim->unitId());
	const auto parentActorState = parentActor->save();
	const auto parentVictimState = parentVictim->save();
	ASSERT_NE(marker(parentActor.get()), nullptr);
	const auto parentPlagueDuration = marker(parentActor.get())->turnsRemain;
	const auto parentPlagueParameters = marker(parentActor.get())->parameters->toCustom<JsonNode>();
	const auto parentHits = parent->getProjectedPlagueHits().size();
	const auto parentRound = parent->battleGetRound();
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto siblingActor = sibling->getForUpdate(actor->unitId());
	auto siblingVictim = sibling->getForUpdate(victim->unitId());
	const auto siblingActorState = siblingActor->save();
	const auto siblingVictimState = siblingVictim->save();
	ASSERT_NE(marker(siblingActor.get()), nullptr);
	const auto siblingPlagueDuration = marker(siblingActor.get())->turnsRemain;
	const auto siblingPlagueParameters = marker(siblingActor.get())->parameters->toCustom<JsonNode>();
	auto childActor = child->getForUpdate(actor->unitId());
	ASSERT_EQ(child->battleGetMySide(), BattleSide::DEFENDER);
	ASSERT_EQ(child->battleCanCastSpell(defenderSideHero, spells::Mode::HERO), ESpellCastProblem::OK);
	spells::BattleCast cast(child.get(), defenderSideHero, spells::Mode::HERO, seal.toSpell());
	const spells::Target target{spells::Destination(childActor.get())};
	spells::detail::ProblemImpl problem;
	const bool legal = seal.toSpell()->battleMechanics(&cast)->canBeCastAt(target, problem);
	std::vector<std::string> messages;
	problem.getAll(messages);
	ASSERT_TRUE(legal) << (messages.empty() ? "no diagnostic" : messages.front());
	cast.castEval(child->getServerCallback(), target);
	ASSERT_TRUE(newHorizonsSorcery::isSpellLocked(*childActor));
	const auto sealMarkers = childActor->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(seal)).And(Selector::type()(BonusType::NONE)));
	ASSERT_EQ(sealMarkers->size(), 1u);
	ASSERT_EQ(sealMarkers->front()->turnsRemain, 1);
	ASSERT_EQ(newHorizonsSorcery::spellLockPreservesBeneficial(*childActor), std::optional<bool>(false));
	ASSERT_NE(marker(childActor.get()), nullptr);
	ASSERT_TRUE(newHorizonsSorcery::durationPausedBySpellLock(*childActor, *marker(childActor.get())));
	const auto expectedTick = newHorizonsPlague::adjustedTickDamage(*child, BattleSide::DEFENDER,
		childActor.get(), marker(childActor.get())->val);
	ASSERT_GT(expectedTick, 0);
	const auto health = childActor->getAvailableHealth();
	ASSERT_GT(health, 2 * expectedTick);
	ASSERT_NO_FATAL_FAILURE(completeProjectedAttack(child));
	ASSERT_NE(marker(childActor.get()), nullptr);
	EXPECT_EQ(marker(childActor.get())->turnsRemain, 1);
	EXPECT_EQ(childActor->getAvailableHealth(), health - expectedTick);
	EXPECT_EQ(marker(childActor.get())->parameters->toCustom<JsonNode>()["lastProcessedRound"].Integer(), parentRound);
	EXPECT_EQ(child->getProjectedPlagueHits().size(), parentHits + 1);
	child->nextRound(); // The one-round seal expires, not the preserved last tick.
	ASSERT_FALSE(newHorizonsSorcery::isSpellLocked(*childActor));
	ASSERT_NE(marker(childActor.get()), nullptr);
	EXPECT_EQ(marker(childActor.get())->turnsRemain, 1);
	EXPECT_EQ(childActor->getAvailableHealth(), health - expectedTick);
	EXPECT_EQ(child->battleGetRound(), parentRound + 1);
	ASSERT_NO_FATAL_FAILURE(completeProjectedAttack(child));
	EXPECT_EQ(childActor->getAvailableHealth(), health - 2 * expectedTick);
	EXPECT_EQ(marker(childActor.get()), nullptr);
	EXPECT_EQ(child->getProjectedPlagueHits().size(), parentHits + 2);
	newHorizonsPlague::completeActivation(*child, *child->getServerCallback(), actor->unitId());
	EXPECT_EQ(childActor->getAvailableHealth(), health - 2 * expectedTick);
	EXPECT_EQ(child->getProjectedPlagueHits().size(), parentHits + 2);
	EXPECT_EQ(parentActor->save(), parentActorState);
	EXPECT_EQ(parentVictim->save(), parentVictimState);
	ASSERT_NE(marker(parentActor.get()), nullptr);
	EXPECT_EQ(marker(parentActor.get())->turnsRemain, parentPlagueDuration);
	EXPECT_EQ(marker(parentActor.get())->parameters->toCustom<JsonNode>(), parentPlagueParameters);
	EXPECT_EQ(parent->getProjectedPlagueHits().size(), parentHits);
	EXPECT_EQ(parent->battleGetRound(), parentRound);
	EXPECT_EQ(siblingActor->save(), siblingActorState);
	EXPECT_EQ(siblingVictim->save(), siblingVictimState);
	ASSERT_NE(marker(siblingActor.get()), nullptr);
	EXPECT_EQ(marker(siblingActor.get())->turnsRemain, siblingPlagueDuration);
	EXPECT_EQ(marker(siblingActor.get())->parameters->toCustom<JsonNode>(), siblingPlagueParameters);
	EXPECT_EQ(sibling->getProjectedPlagueHits().size(), parentHits);
	EXPECT_EQ(sibling->battleGetRound(), parentRound);
	EXPECT_FALSE(newHorizonsSorcery::isSpellLocked(*parentActor));
	EXPECT_FALSE(newHorizonsSorcery::isSpellLocked(*siblingActor));
	EXPECT_EQ(actor->save(), liveActorState);
	EXPECT_EQ(victim->save(), liveVictimState);
}

TEST_F(NewHorizonsPlagueCompletionAITest, LethalProjectedTickSpreadsThenRemovesSourceWithoutLiveMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare(20));
	const auto health = actor->getAvailableHealth();
	ASSERT_GT(newHorizonsPlague::adjustedTickDamage(*battle(), BattleSide::DEFENDER, actor, marker(actor)->val), health);
	auto model = projection();
	ASSERT_NO_FATAL_FAILURE(completeProjectedAttack(model));
	const auto * unit = model->battleGetUnitByID(actor->unitId());
	ASSERT_NE(unit, nullptr);
	EXPECT_FALSE(unit->alive());
	EXPECT_FALSE(newHorizonsPlague::hasPlague(unit));
	EXPECT_TRUE(newHorizonsPlague::hasPlague(model->battleGetUnitByID(victim->unitId())));
	EXPECT_EQ(actor->getAvailableHealth(), health);
	EXPECT_TRUE(actor->alive());
	EXPECT_TRUE(newHorizonsPlague::hasPlague(actor));
}

TEST_F(NewHorizonsPlagueCompletionAITest, ActualTimeStopRejectsCompletionTickWithoutConsumingMarker)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	defenderSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(newHorizonsSorcery::SORCERY_MAGIC_SKILL)),
		MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	const auto stop = SpellID(SpellID::decode(newHorizonsSorcery::TIME_STOP_SPELL));
	defenderSideHero->addSpellToSpellbook(stop);
	advanceRound();
	ASSERT_TRUE(castOn(defenderSideHero, stop, actor));
	ASSERT_TRUE(actor->isTimeStopped());
	auto model = projection();
	const auto health = actor->getAvailableHealth();
	const auto before = marker(actor)->parameters->toCustom<JsonNode>();
	newHorizonsPlague::completeActivation(*model, *model->getServerCallback(), actor->unitId());
	const auto * unit = model->battleGetUnitByID(actor->unitId());
	EXPECT_EQ(unit->getAvailableHealth(), health);
	EXPECT_EQ(marker(unit)->turnsRemain, 3);
	EXPECT_EQ(marker(unit)->parameters->toCustom<JsonNode>(), before);
	EXPECT_FALSE(newHorizonsPlague::hasPlague(model->battleGetUnitByID(victim->unitId())));
}

TEST_F(NewHorizonsPlagueCompletionAITest, FrozenForfeitKeepsReceiptThroughTickThenMatchesActualAutomaticThaw)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	// Wait does not complete Plague; the later automatic normal-slot forfeiture does.
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeWait(actor)));
	ASSERT_NO_FATAL_FAILURE(freezeActor());
	const auto health = actor->getAvailableHealth();
	const auto round = battle()->getRound();
	auto parent = projection();
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	const auto frozen = newHorizonsFrozen::removalPlan(*actor);
	child->nextTurnPreservingFrozenForfeit(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	ASSERT_TRUE(newHorizonsFrozen::isFrozen(*child->battleGetUnitByID(actor->unitId())));
	ASSERT_NO_FATAL_FAILURE(expireCompletedBonuses(child));
	newHorizonsPlague::completeActivation(*child, *child->getServerCallback(), actor->unitId());
	auto projected = child->getForUpdate(actor->unitId());
	ASSERT_TRUE(projected->alive());
	EXPECT_TRUE(newHorizonsFrozen::isFrozen(*projected)); // Magical tick is not physical Shatter.
	EXPECT_LT(projected->getAvailableHealth(), health);
	EXPECT_EQ(marker(projected.get())->turnsRemain, 2);
	projected->removeUnitBonus(frozen);
	EXPECT_FALSE(newHorizonsFrozen::isFrozen(*projected));
	const auto once = projected->getAvailableHealth();
	newHorizonsPlague::completeActivation(*child, *child->getServerCallback(), actor->unitId());
	EXPECT_EQ(projected->getAvailableHealth(), once);
	EXPECT_EQ(parent->battleGetUnitByID(actor->unitId())->getAvailableHealth(), health);
	EXPECT_EQ(sibling->battleGetUnitByID(actor->unitId())->getAvailableHealth(), health);
	EXPECT_TRUE(newHorizonsFrozen::isFrozen(*sibling->battleGetUnitByID(actor->unitId())));
	EXPECT_TRUE(newHorizonsFrozen::isFrozen(*actor));
	for(int actions = 0; actions < 16 && newHorizonsFrozen::isFrozen(*actor); ++actions)
	{
		const auto * active = battle()->battleActiveUnit();
		ASSERT_NE(active, nullptr);
		ASSERT_NE(active, actor);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(active->unitSide()), BattleAction::makeDefend(active)));
	}
	ASSERT_FALSE(newHorizonsFrozen::isFrozen(*actor));
	ASSERT_EQ(battle()->getRound(), round + 1);
	EXPECT_EQ(battle()->battleActiveUnit(), actor);
	ASSERT_NE(marker(actor), nullptr);
	EXPECT_EQ(marker(actor)->parameters->toCustom<JsonNode>()["lastProcessedRound"].Integer(), round);
	EXPECT_EQ(std::count_if(server.startedActions.begin(), server.startedActions.end(), [this](const auto & action)
	{
		return action.ba.stackNumber == actor->unitId() && action.ba.actionType == EActionType::NO_ACTION;
	}), 1);
	EXPECT_EQ(projected->getAvailableHealth(), actor->getAvailableHealth());
	EXPECT_EQ(marker(projected.get())->turnsRemain, marker(actor)->turnsRemain);
	EXPECT_EQ(projected->frozenLastAppliedRound(), actor->frozenLastAppliedRound());
}

TEST_F(NewHorizonsPlagueCompletionAITest, LethalFrozenTickCannotEnableNativeRebirthByThawingEarly)
{
	ASSERT_NO_FATAL_FAILURE(prepare(20));
	SetStackEffect ability;
	ability.battleID = BattleID(0);
	ability.toAdd.emplace_back(actor->unitId(), std::vector<Bonus>{
		Bonus(BonusDuration::ONE_BATTLE, BonusType::REBIRTH, BonusSource::OTHER, 100, BonusSourceID()),
		Bonus(BonusDuration::ONE_BATTLE, BonusType::CASTS, BonusSource::OTHER, 1, BonusSourceID())});
	gameHandler->sendAndApply(ability);
	ASSERT_TRUE(actor->canCast());
	ASSERT_NO_FATAL_FAILURE(freezeActor());
	const auto health = actor->getAvailableHealth();
	ASSERT_FALSE(actor->canCast());
	auto model = projection();
	model->nextTurnPreservingFrozenForfeit(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	ASSERT_NO_FATAL_FAILURE(expireCompletedBonuses(model));
	newHorizonsPlague::completeActivation(*model, *model->getServerCallback(), actor->unitId());
	const auto current = model->getForUpdate(actor->unitId());
	EXPECT_FALSE(current->alive());
	EXPECT_TRUE(newHorizonsFrozen::isFrozen(*current)); // Dead original must not receive manual thaw.
	EXPECT_FALSE(newHorizonsPlague::hasPlague(current.get()));
	EXPECT_EQ(actor->getAvailableHealth(), health);
	EXPECT_TRUE(actor->alive());
	// Ordinary two-argument nextTurn still thaws: this negative branch exposes
	// why thaw-before-tick incorrectly makes a native Rebirth eligible.
	auto earlyThaw = projection();
	earlyThaw->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	ASSERT_FALSE(newHorizonsFrozen::isFrozen(*earlyThaw->battleGetUnitByID(actor->unitId())));
	ASSERT_TRUE(earlyThaw->battleGetUnitByID(actor->unitId())->canCast());
	newHorizonsPlague::completeActivation(*earlyThaw, *earlyThaw->getServerCallback(), actor->unitId());
	EXPECT_TRUE(earlyThaw->battleGetUnitByID(actor->unitId())->alive());
}

TEST_F(NewHorizonsPlagueCompletionAITest, CompletedActivationExpiresTemporaryMitigationBeforePlagueDamage)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	Bonus protection(BonusDuration::STACK_ACTIVATION, BonusType::SPELL_DAMAGE_REDUCTION,
		BonusSource::OTHER, 90, BonusSourceID());
	protection.subtype = BonusSubtypeID(SpellSchool::ANY);
	SetStackEffect effects;
	effects.battleID = BattleID(0);
	effects.toAdd.emplace_back(actor->unitId(), std::vector<Bonus>{protection});
	gameHandler->sendAndApply(effects);
	auto model = projection();
	const auto raw = marker(actor)->val;
	const auto protectedDamage = newHorizonsPlague::adjustedTickDamage(*model, BattleSide::DEFENDER,
		model->battleGetUnitByID(actor->unitId()), raw);
	ASSERT_NO_FATAL_FAILURE(expireCompletedBonuses(model));
	const auto expected = newHorizonsPlague::adjustedTickDamage(*model, BattleSide::DEFENDER,
		model->battleGetUnitByID(actor->unitId()), raw);
	ASSERT_GT(expected, protectedDamage);
	const auto health = actor->getAvailableHealth();
	newHorizonsPlague::completeActivation(*model, *model->getServerCallback(), actor->unitId());
	EXPECT_EQ(health - model->battleGetUnitByID(actor->unitId())->getAvailableHealth(), expected);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMeleeAttack(actor, victim->getPosition(), actor->getPosition())));
	EXPECT_EQ(actor->getAvailableHealth(), model->battleGetUnitByID(actor->unitId())->getAvailableHealth());
}

TEST_F(NewHorizonsPlagueCompletionAITest, TimeStopCannotEnterDeferredFrozenForfeitPath)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	defenderSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(newHorizonsSorcery::SORCERY_MAGIC_SKILL)),
		MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	const auto stop = SpellID(SpellID::decode(newHorizonsSorcery::TIME_STOP_SPELL));
	defenderSideHero->addSpellToSpellbook(stop);
	advanceRound();
	ASSERT_TRUE(castOn(defenderSideHero, stop, actor));
	auto model = projection();
	const auto health = actor->getAvailableHealth();
	EXPECT_THROW(model->nextTurnPreservingFrozenForfeit(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE),
		std::invalid_argument);
	newHorizonsPlague::completeActivation(*model, *model->getServerCallback(), actor->unitId());
	EXPECT_EQ(model->battleGetUnitByID(actor->unitId())->getAvailableHealth(), health);
	EXPECT_EQ(marker(model->battleGetUnitByID(actor->unitId()))->turnsRemain, 3);
}

TEST_F(NewHorizonsPlagueCompletionAITest, VariantCompletionScoresOwnAndEnemyFinalizedHitsOnExistingScale)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	for(const bool isOurActor : {true, false})
	{
		auto model = projection();
		DamageCache cache;
		cache.buildDamageCache(model, BattleSide::ATTACKER);
		BattleExchangeVariant exchange;
		exchange.completePlagueActivation(actor->unitId(), model->battleGetOwner(actor), isOurActor, cache, model);
		ASSERT_EQ(model->getProjectedPlagueHits().size(), 1u);
		const auto & hit = model->getProjectedPlagueHits().front();
		const auto expected = AttackPossibility::calculateDamageReduce(nullptr, hit.preHitTarget.get(),
			static_cast<uint64_t>(hit.finalizedDamage), cache, model);
		ASSERT_GT(expected, 0);
		EXPECT_FLOAT_EQ(exchange.getScore().ourDamageReduce, isOurActor ? expected : 0);
		EXPECT_FLOAT_EQ(exchange.getScore().enemyDamageReduce, isOurActor ? 0 : expected);
		auto score = exchange.getScore();
		EXPECT_FLOAT_EQ(score.value(), isOurActor ? -expected : expected);
		exchange.completePlagueActivation(actor->unitId(), model->battleGetOwner(actor), isOurActor, cache, model);
		EXPECT_EQ(model->getProjectedPlagueHits().size(), 1u);
		EXPECT_FLOAT_EQ(exchange.getScore().ourDamageReduce, isOurActor ? expected : 0);
	}
}

TEST_F(NewHorizonsPlagueCompletionAITest, ChangedControllerUsesCapturedEffectiveOwnerNotOriginalSide)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto model = projection();
	const auto controller = model->getSidePlayer(BattleSide::DEFENDER);
	// Hypnotize changes battleGetOwner allegiance; Puppet Master deliberately
	// changes only action controller and is not this damage-cost perspective.
	Bonus control(BonusDuration::N_TURNS, BonusType::HYPNOTIZED,
		BonusSource::SPELL_EFFECT, 1, BonusSourceID(SpellID(SpellID::HYPNOTIZE)));
	control.turnsRemain = 2;
	model->getForUpdate(actor->unitId())->addUnitBonus({control});
	ASSERT_EQ(model->battleGetOwner(model->battleGetUnitByID(actor->unitId())), controller);
	DamageCache cache;
	BattleExchangeVariant exchange;
	exchange.completePlagueActivation(actor->unitId(), controller, true, cache, model);
	ASSERT_EQ(model->getProjectedPlagueHits().size(), 1u);
	const auto & hit = model->getProjectedPlagueHits().front();
	EXPECT_EQ(hit.targetController, controller);
	EXPECT_EQ(hit.preHitTarget->unitSide(), BattleSide::ATTACKER);
	EXPECT_GT(exchange.getScore().ourDamageReduce, 0);
	EXPECT_FLOAT_EQ(exchange.getScore().enemyDamageReduce, 0);
	model->getForUpdate(actor->unitId())->removeUnitBonus(std::vector<Bonus>{control});
	EXPECT_EQ(hit.targetController, controller);
	EXPECT_TRUE(hit.preHitTarget->isHypnotized());
	EXPECT_FALSE(actor->isHypnotized());
}

TEST_F(NewHorizonsPlagueCompletionAITest, SequentialReceiptsRemainImmutableAndBranchCursorsDoNotReplayParentHits)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto parent = projection();
	applyInjury(parent, plague(), true, 10);
	ASSERT_EQ(parent->getProjectedPlagueHits().size(), 1u);
	const auto first = parent->getProjectedPlagueHits().front();
	const auto firstHealth = first.preHitTarget->getAvailableHealth();
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	const auto cursor = child->getProjectedPlagueHits().size();
	DamageCache cache;
	BattleExchangeVariant exchange;
	exchange.completePlagueActivation(actor->unitId(), child->getSidePlayer(BattleSide::ATTACKER), true, cache, child);
	ASSERT_EQ(child->getProjectedPlagueHits().size(), cursor + 1);
	const auto & added = child->getProjectedPlagueHits()[cursor];
	const auto expected = AttackPossibility::calculateDamageReduce(nullptr, added.preHitTarget.get(),
		static_cast<uint64_t>(added.finalizedDamage), cache, child);
	EXPECT_FLOAT_EQ(exchange.getScore().ourDamageReduce, expected);
	EXPECT_EQ(parent->getProjectedPlagueHits().size(), 1u);
	EXPECT_EQ(sibling->getProjectedPlagueHits().size(), 1u);
	EXPECT_EQ(first.preHitTarget->getAvailableHealth(), firstHealth);
	EXPECT_EQ(first.finalizedDamage, 10);
	EXPECT_EQ(added.preHitTarget->getAvailableHealth(), firstHealth - 10);
	child->getForUpdate(actor->unitId())->addUnitBonus({Bonus(BonusDuration::ONE_BATTLE,
		BonusType::PRIMARY_SKILL, BonusSource::OTHER, 123, BonusSourceID(), BonusSubtypeID(PrimarySkill::ATTACK))});
	EXPECT_NE(child->getForUpdate(actor->unitId())->getAttack(false), added.preHitTarget->getAttack(false));
	EXPECT_EQ(actor->getAvailableHealth(), firstHealth);
}

TEST_F(NewHorizonsPlagueCompletionAITest, UnrelatedSpellAndUnflaggedInjuriesCannotCreatePlagueReceipts)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto model = projection();
	applyInjury(model, SpellID(SpellID::FIREBALL), true, 10);
	applyInjury(model, plague(), false, 10);
	EXPECT_TRUE(model->getProjectedPlagueHits().empty());
	applyInjury(model, plague(), true, 10);
	ASSERT_EQ(model->getProjectedPlagueHits().size(), 1u);
	EXPECT_EQ(model->getProjectedPlagueHits().front().finalizedDamage, 10);
	EXPECT_EQ(model->getProjectedPlagueHits().front().preHitTarget->getAvailableHealth(),
		actor->getAvailableHealth() - 20);
}

TEST_F(NewHorizonsPlagueCompletionAITest, NativeRebirthStillScoresFinalizedDamageWithoutPostHealthInference)
{
	ASSERT_NO_FATAL_FAILURE(prepare(20));
	auto model = projection();
	model->getForUpdate(actor->unitId())->addUnitBonus({
		Bonus(BonusDuration::ONE_BATTLE, BonusType::REBIRTH, BonusSource::OTHER, 100, BonusSourceID()),
		Bonus(BonusDuration::ONE_BATTLE, BonusType::CASTS, BonusSource::OTHER, 1, BonusSourceID())});
	ASSERT_TRUE(model->getForUpdate(actor->unitId())->canCast());
	const auto before = model->getForUpdate(actor->unitId())->getAvailableHealth();
	DamageCache cache;
	BattleExchangeVariant exchange;
	exchange.completePlagueActivation(actor->unitId(), model->getSidePlayer(BattleSide::ATTACKER), true, cache, model);
	ASSERT_EQ(model->getProjectedPlagueHits().size(), 1u);
	const auto & hit = model->getProjectedPlagueHits().front();
	ASSERT_GT(hit.finalizedDamage, 0);
	EXPECT_GE(hit.finalizedDamage, before);
	EXPECT_TRUE(model->battleGetUnitByID(actor->unitId())->alive());
	EXPECT_EQ(model->battleGetUnitByID(actor->unitId())->getAvailableHealth(), before);
	EXPECT_GT(exchange.getScore().ourDamageReduce, 0);
	const auto once = exchange.getScore().ourDamageReduce;
	exchange.completePlagueActivation(actor->unitId(), model->getSidePlayer(BattleSide::ATTACKER), true, cache, model);
	EXPECT_FLOAT_EQ(exchange.getScore().ourDamageReduce, once);
}

TEST_F(NewHorizonsPlagueCompletionAITest, ElementalRebirthAndPrimalBurstStaySeparateAndAreScoredOnce)
{
	ASSERT_NO_FATAL_FAILURE(prepareStrategicRebirth());
	auto model = projection();
	ASSERT_TRUE(model->captureElementalRebirthSource(*model->battleGetUnitByID(actor->unitId())).has_value());
	BattleHex adjacent = BattleHex::INVALID;
	for(const auto hex : model->battleGetUnitByID(actor->unitId())->getSurroundingHexes())
		if(hex.isAvailable() && !model->battleGetUnitByPos(hex, true))
		{
			adjacent = hex;
			break;
		}
	ASSERT_TRUE(adjacent.isAvailable());
	model->moveUnit(victim->unitId(), adjacent);
	DamageCache cache;
	BattleExchangeVariant exchange;
	exchange.completePlagueActivation(actor->unitId(), model->getSidePlayer(BattleSide::ATTACKER), true, cache, model);
	ASSERT_EQ(model->getProjectedPlagueHits().size(), 1u);
	ASSERT_EQ(model->getElementalRebirthSpawnUnitIds().size(), 1u);
	ASSERT_EQ(model->getProjectedPrimalBurstHits().size(), 1u);
	EXPECT_EQ(model->getProjectedPlagueHits().front().preHitTarget->unitId(), actor->unitId());
	EXPECT_EQ(model->getProjectedPrimalBurstHits().front().preHitTarget->unitId(), victim->unitId());
	EXPECT_GT(exchange.getScore().ourDamageReduce, 0);
	EXPECT_GT(exchange.getScore().enemyDamageReduce, 0);
	const auto once = exchange.getScore();
	exchange.completePlagueActivation(actor->unitId(), model->getSidePlayer(BattleSide::ATTACKER), true, cache, model);
	EXPECT_EQ(model->getElementalRebirthSpawnUnitIds().size(), 1u);
	EXPECT_EQ(model->getProjectedPrimalBurstHits().size(), 1u);
	EXPECT_FLOAT_EQ(exchange.getScore().ourDamageReduce, once.ourDamageReduce);
	EXPECT_FLOAT_EQ(exchange.getScore().enemyDamageReduce, once.enemyDamageReduce);
}

TEST_F(NewHorizonsPlagueCompletionAITest, InitialUninfectedCastHeuristicRemainsSeparateFromOngoingReceipts)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:shadowMagic")),
		MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	attackerSideHero->addSpellToSpellbook(plague());
	setTestSpellPointTotal(attackerSideHero, 1000);
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, plague().toSpell());
	auto mechanics = plague().toSpell()->battleMechanics(&cast);
	auto model = projection();
	const spells::Target target{spells::Destination(victim)};
	spells::detail::ProblemImpl problem;
	ASSERT_TRUE(mechanics->canBeCastAt(target, problem));
	EXPECT_GT(SpellTargetEvaluator::plagueDelayedDamageValue(mechanics.get(), target, model), 0);
	EXPECT_TRUE(model->getProjectedPlagueHits().empty());
	EXPECT_FALSE(newHorizonsPlague::hasPlague(victim));
}

TEST_F(NewHorizonsPlagueCompletionAITest, FrozenNoQuarterConsumesTwoNormalSlotsButTimeStopConsumesNone)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(freezeActor());
	auto model = projection();
	auto target = model->getForUpdate(actor->unitId());
	target->applyNoQuarter(2);
	DamageCache cache;
	BattleExchangeVariant exchange;
	for(int slot = 0; slot < 2; ++slot)
	{
		if(slot)
		{
			model->nextRound();
			target->addUnitBonus({newHorizonsFrozen::makeFrozenMarker(
				BonusSourceID(creatureByName("core:iceElemental")), model->getRound())});
		}
		const auto frozen = newHorizonsFrozen::removalPlan(*target);
		model->nextTurnPreservingFrozenForfeit(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
		ASSERT_NO_FATAL_FAILURE(expireCompletedBonuses(model));
		target->consumeNoQuarterActivation();
		exchange.completePlagueActivation(actor->unitId(), model->getSidePlayer(BattleSide::ATTACKER), true, cache, model);
		target = model->getForUpdate(actor->unitId());
		if(target->alive() && !target->isGhost() && !target->isTimeStopped() && newHorizonsFrozen::isFrozen(*target))
			target->removeUnitBonus(frozen);
		EXPECT_EQ(target->noQuarterMoraleActivationsRemaining, 1 - slot);
		EXPECT_EQ(target->hasBonus(CSelector(newHorizonsOffense::isNoQuarterMoralePenalty)), slot == 0);
	}
	defenderSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(newHorizonsSorcery::SORCERY_MAGIC_SKILL)),
		MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	const auto stop = SpellID(SpellID::decode(newHorizonsSorcery::TIME_STOP_SPELL));
	defenderSideHero->addSpellToSpellbook(stop);
	advanceRound();
	ASSERT_TRUE(castOn(defenderSideHero, stop, actor));
	auto stopped = projection();
	stopped->getForUpdate(actor->unitId())->applyNoQuarter(2);
	stopped->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	exchange.completePlagueActivation(actor->unitId(), stopped->getSidePlayer(BattleSide::ATTACKER), true, cache, stopped);
	EXPECT_EQ(stopped->getForUpdate(actor->unitId())->noQuarterMoraleActivationsRemaining, 2);
	EXPECT_TRUE(stopped->getProjectedPlagueHits().empty());
}
