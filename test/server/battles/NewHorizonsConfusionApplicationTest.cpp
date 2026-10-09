/*
 * NewHorizonsConfusionApplicationTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../AI/BattleAI/BattleEvaluator.h"
#include "../../../AI/BattleAI/SpellTargetsEvaluator.h"
#include "../../../lib/callback/CBattleCallback.h"
#include "../../../lib/CRandomGenerator.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/NewHorizonsConfusionControl.h"
#include "../../../lib/battle/NewHorizonsConfusionResolution.h"
#include "../../../lib/battle/NewHorizonsBulwark.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/NewHorizonsMagic.h"

namespace
{
constexpr auto CONFUSION = "new-horizons:confusion";
constexpr auto CHAOS = "new-horizons:chaosMagic";
constexpr auto CONFOUNDER = "new-horizons:chaosMagic.confounder";
constexpr auto DISCIPLINE = "new-horizons:discipline";
constexpr auto RALLY = "new-horizons:discipline.rally";

class ConfusionApplicationEnvironment final : public ::Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit ConfusionApplicationEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class ConfusionAICallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> submitted;
	ConfusionAICallback() : CBattleCallback(PlayerColor(0), nullptr) {}
	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override { submitted.push_back(action); }
};

struct ConfusionRandomArchive
{
	bool saving = true;
	std::string state;
	void operator&(std::string & value) { state = value; }
};

std::string randomState(CRandomGenerator & generator)
{
	ConfusionRandomArchive archive;
	generator.serialize(archive);
	return archive.state;
}

class NewHorizonsConfusionApplicationTest : public HeroCommandFixture
{
protected:
	bool privateActive = true;
	bool useShippedRegistration = false;
	bool confounder = false;
	bool rally = false;
	CStack * friendly = nullptr;
	CStack * target = nullptr;
	CStack * adjacent = nullptr;

	SpellID confusion() const { return SpellID(SpellID::decode(CONFUSION)); }

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		JsonNode magic(JsonPath::builtin("config/newHorizonsMagic"));
		if(magic["spells"][CONFUSION].isNull())
			throw std::runtime_error("Registered Confusion profile is missing");
		if(!useShippedRegistration)
			magic["spells"][CONFUSION]["active"].Bool() = privateActive;
		newHorizonsMagic::validateRules(magic);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, std::move(magic));
		JsonNode perks(JsonPath::builtin("config/newHorizonsPerks"));
		if(confounder && !useShippedRegistration)
		{
			bool found = false;
			for(auto & row : perks["skills"][CHAOS]["perks"].Vector())
				if(row["id"].String() == CONFOUNDER)
				{
					row["effect"]["status"].String() = "active";
					found = true;
				}
			if(!found) throw std::runtime_error("Confounder registry entry missing");
		}
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perks));
		JsonNode badChance;
		for(int i = 0; i < 10; ++i) badChance.Vector().emplace_back(100);
		map->overrideGameSetting(EGameSettings::COMBAT_BAD_MORALE_CHANCE, badChance);
		map->overrideGameSetting(EGameSettings::COMBAT_MORALE_DICE_SIZE, JsonNode(100));
	}

	void acquirePerk(CGHeroInstance * hero, const std::string & skill, const std::string & perk)
	{
		hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(skill)), MasteryLevel::BASIC,
			ChangeValueMode::ABSOLUTE);
		const auto rank = [hero](const std::string & name) { return hero->getPerkSkillRank(name); };
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer(rank, seed);
			for(size_t i = 0; i < offer.size(); ++i)
				if(offer[i].selection.skillId == skill && offer[i].selection.perkId == perk)
				{
					gameHandler->levelUpHero(hero, offer, i, seed, false);
					ASSERT_TRUE(hero->hasActivePerk(skill, perk));
					return;
				}
		}
		FAIL() << "No legal offer for " << perk;
	}

	void prepare()
	{
		ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
		startGame();
		ASSERT_TRUE(confusion().hasValue());
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(confusion());
		attackerSideHero->addSpellToSpellbook(SpellID::DISPEL);
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(CHAOS)), MasteryLevel::BASIC,
			ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 100);
		if(confounder) acquirePerk(attackerSideHero, CHAOS, CONFOUNDER);
		if(rally) acquirePerk(defenderSideHero, DISCIPLINE, RALLY);
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		friendly = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(4, 5), 2);
		target = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 10);
		adjacent = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(13, 5), 10);
		beginCombat();
		ASSERT_EQ(battle()->battleActiveUnit(), friendly);
	}

	bool cast(SpellID spell, const CStack * unit)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = spell;
		action.aimToUnit(unit);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	bool pending(const CStack * unit) const { return unit->hasBonusOfType(BonusType::CONFUSION_PENDING); }
	void history(battle::ConfusionBehavior behavior) { target->confusionState.recordResolved(behavior); }
	void berserk(CStack * unit)
	{
		SetStackEffect pack;
		pack.battleID = BattleID(0);
		pack.toAdd.emplace_back(unit->unitId(), std::vector<Bonus>{Bonus(BonusDuration::UNTIL_OWN_ATTACK,
			BonusType::ATTACKS_NEAREST_CREATURE, BonusSource::SPELL_EFFECT, 0, BonusSourceID(SpellID(SpellID::BERSERK)))});
		gameHandler->sendAndApply(pack);
	}
	void negativeMorale()
	{
		target->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MORALE,
			BonusSource::OTHER, -20, BonusSourceID()));
		ASSERT_LT(target->moraleVal(), 0);
	}
	bool sawAction(EActionType type) const
	{
		return std::ranges::any_of(server.startedActions, [this, type](const StartAction & action)
		{ return action.ba.stackNumber == target->unitId() && action.ba.actionType == type; });
	}
	void advanceUntil(const std::function<bool()> & done)
	{
		for(int i = 0; i < 16 && !done(); ++i)
		{
			const auto * active = battle()->battleActiveUnit();
			ASSERT_NE(active, nullptr);
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->battleGetOwner(active),
				BattleAction::makeDefend(active)));
		}
		ASSERT_TRUE(done()) << "Bounded turn sequence did not reach its intended server path";
	}
};
}

TEST_F(NewHorizonsConfusionApplicationTest, ExplicitInactiveProfileRejectsBeforeManaAndHeroAction)
{
	privateActive = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto mana = attackerSideHero->getManaAvailable();
	EXPECT_FALSE(cast(confusion(), target));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 0);
	EXPECT_FALSE(pending(target));
}

TEST_F(NewHorizonsConfusionApplicationTest, ShippedRegistrationAllowsLegalConfounderAndPaidConfusion)
{
	const JsonNode magic(JsonPath::builtin("config/newHorizonsMagic"));
	ASSERT_TRUE(magic["spells"][CONFUSION]["active"].isBool());
	ASSERT_TRUE(magic["spells"][CONFUSION]["active"].Bool())
		<< "Production Confusion must be active without a private fixture override";
	const JsonNode perks(JsonPath::builtin("config/newHorizonsPerks"));
	const auto & pool = perks["skills"][CHAOS]["perks"].Vector();
	const auto row = std::ranges::find_if(pool, [](const JsonNode & value)
	{ return value["id"].String() == CONFOUNDER; });
	ASSERT_NE(row, pool.end());
	ASSERT_EQ((*row)["effect"]["status"].String(), "active")
		<< "Production Confounder must be active without a private fixture override";
	useShippedRegistration = true;
	confounder = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(attackerSideHero->hasActivePerk(CHAOS, CONFOUNDER));
	history(battle::ConfusionBehavior::DEFEND);
	const auto mana = attackerSideHero->getManaAvailable();
	const auto cost = battle()->battleGetSpellCost(confusion().toSpell(), attackerSideHero);
	ASSERT_TRUE(cast(confusion(), target));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - cost);
	EXPECT_TRUE(target->confusionState.pendingConfounder);
	ASSERT_NO_FATAL_FAILURE(advanceUntil([this]() { return !pending(target); }));
	EXPECT_FALSE(target->confusionState.pending);
	EXPECT_NE(target->confusionState.previousResolved, battle::ConfusionBehavior::DEFEND);
}

TEST_F(NewHorizonsConfusionApplicationTest, AcceptedRequestSpendsManaAndHeroActionOnlyOnSelectedTarget)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto cost = battle()->battleGetSpellCost(confusion().toSpell(), attackerSideHero);
	const auto mana = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(battle()->battleGetSpellActionAllowance(BattleSide::ATTACKER, confusion()).has_value());
	ASSERT_TRUE(cast(confusion(), target));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - cost);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 1);
	EXPECT_FALSE(battle()->battleGetSpellActionAllowance(BattleSide::ATTACKER, confusion()).has_value());
	EXPECT_TRUE(pending(target));
	EXPECT_TRUE(target->confusionState.pending);
	EXPECT_EQ(target->confusionState.pendingCaster, PlayerColor(0));
	EXPECT_FALSE(target->confusionState.pendingConfounder);
	EXPECT_FALSE(pending(friendly));
	EXPECT_FALSE(pending(adjacent));
}

TEST_F(NewHorizonsConfusionApplicationTest, AcceptedCastRemovesOnlyTargetBerserkAndPreservesUnrelatedBonus)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	berserk(target);
	berserk(adjacent);
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::STACKS_SPEED,
		BonusSource::OTHER, 2, BonusSourceID()));
	ASSERT_TRUE(cast(confusion(), target));
	EXPECT_FALSE(target->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE));
	EXPECT_TRUE(adjacent->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE));
	EXPECT_TRUE(target->hasBonus(Selector::type()(BonusType::STACKS_SPEED).And(Selector::sourceType()(BonusSource::OTHER))));
}

TEST_F(NewHorizonsConfusionApplicationTest, RecastPreservesLastResolvedBehaviorAndDoesNotDuplicatePendingMarker)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	history(battle::ConfusionBehavior::WANDER);
	ASSERT_TRUE(cast(confusion(), target));
	advanceRound();
	ASSERT_TRUE(cast(confusion(), target));
	EXPECT_EQ(target->confusionState.previousResolved, battle::ConfusionBehavior::WANDER);
	const auto markers = target->getBonuses(Selector::type()(BonusType::CONFUSION_PENDING));
	ASSERT_NE(markers, nullptr);
	EXPECT_EQ(markers->size(), 1);
}

TEST_F(NewHorizonsConfusionApplicationTest, LegallyAcquiredPrivateConfounderIsCapturedAsCasterProvenance)
{
	confounder = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	history(battle::ConfusionBehavior::DEFEND);
	ASSERT_TRUE(attackerSideHero->hasActivePerk(CHAOS, CONFOUNDER));
	ASSERT_TRUE(cast(confusion(), target));
	EXPECT_TRUE(target->confusionState.pendingConfounder);
	EXPECT_EQ(target->confusionState.previousResolved, battle::ConfusionBehavior::DEFEND);
	const auto markers = target->getBonuses(Selector::type()(BonusType::CONFUSION_PENDING));
	ASSERT_EQ(markers->size(), 1);
	EXPECT_EQ(markers->front()->val, 2);
}

TEST_F(NewHorizonsConfusionApplicationTest, FriendlyAndInsufficientManaRequestsPreserveAllowanceAndState)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto mana = attackerSideHero->getManaAvailable();
	const auto allowance = battle()->battleGetSpellActionAllowance(BattleSide::ATTACKER, confusion());
	EXPECT_FALSE(cast(confusion(), friendly));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(battle()->battleGetSpellActionAllowance(BattleSide::ATTACKER, confusion()), allowance);
	setTestSpellPointTotal(attackerSideHero, 0);
	EXPECT_FALSE(cast(confusion(), target));
	EXPECT_FALSE(pending(target));
	EXPECT_FALSE(target->confusionState.hasState());
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 0);
	EXPECT_EQ(battle()->battleGetSpellActionAllowance(BattleSide::ATTACKER, confusion()), allowance);
}

TEST_F(NewHorizonsConfusionApplicationTest, ActualBadMoraleForfeitureClearsPendingButNotHistory)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	history(battle::ConfusionBehavior::ATTACK);
	ASSERT_TRUE(cast(confusion(), target));
	negativeMorale();
	ASSERT_NO_FATAL_FAILURE(advanceUntil([this]() { return sawAction(EActionType::BAD_MORALE); }));
	EXPECT_FALSE(pending(target));
	EXPECT_FALSE(target->confusionState.pending);
	EXPECT_EQ(target->confusionState.previousResolved, battle::ConfusionBehavior::ATTACK);
}

TEST_F(NewHorizonsConfusionApplicationTest, RallyCancellationAllowsConfusionToResolveInsteadOfConsumingForMorale)
{
	rally = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	history(battle::ConfusionBehavior::WANDER);
	ASSERT_TRUE(cast(confusion(), target));
	negativeMorale();
	ASSERT_NO_FATAL_FAILURE(advanceUntil([this]() { return !pending(target); }));
	EXPECT_TRUE(battle()->getMoraleSuppressionState(BattleSide::DEFENDER).used);
	EXPECT_FALSE(sawAction(EActionType::BAD_MORALE));
	EXPECT_FALSE(target->confusionState.pending);
	EXPECT_NE(target->confusionState.previousResolved, battle::ConfusionBehavior::NONE);
}

TEST_F(NewHorizonsConfusionApplicationTest, NextActivationIsForcedAndConsumesPendingExactlyOnce)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(cast(confusion(), target));
	ASSERT_NO_FATAL_FAILURE(advanceUntil([this]() { return !pending(target); }));
	EXPECT_FALSE(target->confusionState.pending);
	const auto resolved = target->confusionState.previousResolved;
	EXPECT_NE(resolved, battle::ConfusionBehavior::NONE);
	EXPECT_TRUE(sawAction(resolved == battle::ConfusionBehavior::DEFEND ? EActionType::DEFEND : EActionType::WALK));
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [](const auto & line)
	{ return line.find("Confusion makes") != std::string::npos; }));
	EXPECT_EQ(battle()->battleGetOwner(target), PlayerColor(1));
}

TEST_F(NewHorizonsConfusionApplicationTest, StartupPoisonDeathConsumesPendingWithoutResolvedHistory)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	history(battle::ConfusionBehavior::ATTACK);
	ASSERT_TRUE(cast(confusion(), target));
	auto state = target->acquireState();
	ASSERT_TRUE(newHorizonsBulwark::applyPhysicalPoison(state.get(), 100, friendly->unitId()));
	BattleUnitsChanged poison;
	poison.battleID = BattleID(0);
	UnitChanges change(target->unitId(), UnitChanges::EOperation::UPDATE);
	change.data = state->save();
	poison.changedStacks.push_back(std::move(change));
	gameHandler->sendAndApply(poison);
	ASSERT_NO_FATAL_FAILURE(advanceUntil([this]() { return !pending(target); }));
	EXPECT_FALSE(target->alive());
	EXPECT_FALSE(target->confusionState.pending);
	EXPECT_EQ(target->confusionState.previousResolved, battle::ConfusionBehavior::ATTACK);
	EXPECT_FALSE(sawAction(EActionType::DEFEND));
	EXPECT_FALSE(sawAction(EActionType::WALK));
}

TEST_F(NewHorizonsConfusionApplicationTest, ManaDrainAndPhysicalPoisonRunOnceOnForcedActivation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(cast(confusion(), target));
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::MANA_DRAIN,
		BonusSource::OTHER, 7, BonusSourceID()));
	auto state = target->acquireState();
	ASSERT_TRUE(newHorizonsBulwark::applyPhysicalPoison(state.get(), 2, friendly->unitId()));
	BattleUnitsChanged poison;
	poison.battleID = BattleID(0);
	UnitChanges change(target->unitId(), UnitChanges::EOperation::UPDATE);
	change.data = state->save();
	poison.changedStacks.push_back(std::move(change));
	gameHandler->sendAndApply(poison);
	const auto mana = attackerSideHero->getManaAvailable();
	const auto hp = target->getAvailableHealth();
	const auto activationBefore = server.stackActivations.size();
	const auto roundBefore = battle()->getRound();
	ASSERT_NO_FATAL_FAILURE(advanceUntil([this]() { return !pending(target); }));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - 7);
	EXPECT_EQ(target->drainedMana, battle()->getRound() == roundBefore)
		<< "afterNewRound resets the per-round Mana Drain flag";
	EXPECT_EQ(target->getAvailableHealth(), hp - 2);
	EXPECT_EQ(target->physicalPoisonActivationsRemaining, 2);
	int activations = 0;
	for(size_t i = activationBefore; i < server.stackActivations.size(); ++i)
		if(server.stackActivations[i].stack == target->unitId()
			&& server.stackActivations[i].reason == BattleUnitTurnReason::AUTOMATIC_ACTION)
			++activations;
	EXPECT_EQ(activations, 1);
}

TEST_F(NewHorizonsConfusionApplicationTest, ExpiredBindingIsRemovedBeforeForcedMovementEnumeration)
{
	confounder = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	history(battle::ConfusionBehavior::DEFEND);
	Bonus binding(BonusDuration::ONE_BATTLE, BonusType::BIND_EFFECT, BonusSource::OTHER, 1, BonusSourceID());
	binding.parameters = std::make_shared<BonusParameters>(static_cast<int>(friendly->unitId()));
	target->addNewBonus(std::make_shared<Bonus>(binding));
	ASSERT_TRUE(target->hasBonusOfType(BonusType::BIND_EFFECT));
	ASSERT_TRUE(cast(confusion(), target));
	ASSERT_NO_FATAL_FAILURE(advanceUntil([this]() { return !pending(target); }));
	EXPECT_FALSE(target->hasBonusOfType(BonusType::BIND_EFFECT));
	EXPECT_NE(target->confusionState.previousResolved, battle::ConfusionBehavior::DEFEND);
	EXPECT_TRUE(sawAction(EActionType::WALK));
}

TEST_F(NewHorizonsConfusionApplicationTest, OrdinaryFearForfeitureConsumesPendingWithoutResolvedHistory)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	history(battle::ConfusionBehavior::WANDER);
	ASSERT_TRUE(cast(confusion(), target));
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::FEARFUL,
		BonusSource::OTHER, 100, BonusSourceID()));
	const auto roundBefore = battle()->getRound();
	ASSERT_NO_FATAL_FAILURE(advanceUntil([this]() { return !pending(target); }));
	EXPECT_EQ(target->fear, battle()->getRound() == roundBefore)
		<< "afterNewRound resets the per-round Fear flag";
	EXPECT_TRUE(sawAction(EActionType::NO_ACTION));
	EXPECT_FALSE(target->confusionState.pending);
	EXPECT_EQ(target->confusionState.previousResolved, battle::ConfusionBehavior::WANDER);
}

TEST_F(NewHorizonsConfusionApplicationTest, ImpossibleAttackAndWanderAllowConfounderSoleDefendRepeat)
{
	confounder = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	history(battle::ConfusionBehavior::DEFEND);
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::STACKS_SPEED,
		BonusSource::OTHER, -100, BonusSourceID()));
	ASSERT_TRUE(cast(confusion(), target));
	ASSERT_NO_FATAL_FAILURE(advanceUntil([this]() { return !pending(target); }));
	EXPECT_TRUE(sawAction(EActionType::DEFEND));
	EXPECT_EQ(target->confusionState.previousResolved, battle::ConfusionBehavior::DEFEND);
	EXPECT_FALSE(target->confusionState.pending);
}

TEST_F(NewHorizonsConfusionApplicationTest, ConfounderForcedAttackUsesEnemyOnlyOrdinaryAttack)
{
	confounder = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	history(battle::ConfusionBehavior::DEFEND);
	const auto * victim = addStack(BattleSide::ATTACKER, creatureByName("core:peasant"), BattleHex(11, 5), 100);
	for(const auto hex : BattleHexArray::getNeighbouringTiles(target->getPosition()))
		if(hex.isAvailable() && !battle()->battleGetUnitByPos(hex, true))
			addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), hex, 10);
	ASSERT_TRUE(newHorizonsConfusion::enumerateChoices(*battle(), target).wanderDestinations.empty());
	const auto allyHealth = adjacent->getAvailableHealth();
	const auto victimHealth = victim->getAvailableHealth();
	ASSERT_TRUE(cast(confusion(), target));
	ASSERT_NO_FATAL_FAILURE(advanceUntil([this]() { return !pending(target); }));
	EXPECT_TRUE(sawAction(EActionType::WALK_AND_ATTACK));
	EXPECT_EQ(target->confusionState.previousResolved, battle::ConfusionBehavior::ATTACK);
	EXPECT_EQ(adjacent->getAvailableHealth(), allyHealth);
	EXPECT_LT(victim->getAvailableHealth(), victimHealth);
}

TEST_F(NewHorizonsConfusionApplicationTest, DetachedExpectationAndActualAIChoosePaidLegalConfusion)
{
	// Exercise Confusion discovery/submission independently of Order ranking;
	// a higher-valued legal Order is not a failed spell implementation.
	useCommands = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	for(const auto spell : attackerSideHero->getSpellsInSpellbook())
		if(spell != confusion()) attackerSideHero->removeSpellFromSpellbook(spell);
	const auto * shooter = addStack(BattleSide::DEFENDER, creatureByName("core:archer"), BattleHex(10, 2), 100);
	auto environment = std::make_shared<ConfusionApplicationEnvironment>(gameState());
	auto callback = std::make_shared<ConfusionAICallback>();
	callback->onBattleStarted(battle());
	spells::BattleCast preview(battle(), attackerSideHero, spells::Mode::HERO, confusion().toSpell());
	const auto mechanics = confusion().toSpell()->battleMechanics(&preview);
	const auto hp = friendly->getAvailableHealth();
	const auto mana = attackerSideHero->getManaAvailable();
	auto * rng = dynamic_cast<CRandomGenerator *>(&gameHandler->getRandomGenerator());
	ASSERT_NE(rng, nullptr);
	const auto randomBefore = randomState(*rng);
	const auto expected = SpellTargetEvaluator::confusionExpectedActivationValue(mechanics.get(),
		spells::Target{spells::Destination(shooter)}, environment.get());
	ASSERT_TRUE(expected);
	EXPECT_GT(*expected, 0);
	EXPECT_FALSE(shooter->confusionState.pending);
	EXPECT_EQ(friendly->getAvailableHealth(), hp);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	BattleEvaluator evaluator(environment, callback, friendly, PlayerColor(0), BattleID(0), BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(friendly);
	ASSERT_TRUE(evaluator.canCastSpell());
	ASSERT_TRUE(evaluator.attemptCastingSpell(friendly));
	ASSERT_EQ(callback->submitted.size(), 1);
	EXPECT_EQ(randomState(*rng), randomBefore);
	const auto & action = callback->submitted.front();
	ASSERT_EQ(action.spell, confusion());
	const auto aim = action.getTarget(battle());
	ASSERT_EQ(aim.size(), 1);
	ASSERT_NE(aim.front().unitValue, nullptr);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - battle()->battleGetSpellCost(confusion().toSpell(), attackerSideHero));
	EXPECT_TRUE(aim.front().unitValue->hasBonusOfType(BonusType::CONFUSION_PENDING));
}

TEST_F(NewHorizonsConfusionApplicationTest, TimeStopNoActionDoesNotConsumePendingConfusionOrRollBadMorale)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	history(battle::ConfusionBehavior::DEFEND);
	ASSERT_TRUE(cast(confusion(), target));
	negativeMorale();
	Bonus stop(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	stop.parameters = std::make_shared<BonusParameters>(static_cast<int>(BattleSide::DEFENDER));
	battle()->addOrUpdateUnitBonus(target, stop, true);
	ASSERT_TRUE(target->isTimeStopped());
	ASSERT_NO_FATAL_FAILURE(advanceUntil([this]() { return sawAction(EActionType::NO_ACTION); }));
	EXPECT_FALSE(sawAction(EActionType::BAD_MORALE));
	EXPECT_TRUE(pending(target));
	EXPECT_TRUE(target->confusionState.pending);
	EXPECT_EQ(target->confusionState.previousResolved, battle::ConfusionBehavior::DEFEND);
}

TEST_F(NewHorizonsConfusionApplicationTest, ActualDispelRemovesPendingMarkerAndRetainsResolvedHistory)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	history(battle::ConfusionBehavior::ATTACK);
	ASSERT_TRUE(cast(confusion(), target));
	advanceRound();
	ASSERT_TRUE(cast(SpellID::DISPEL, target));
	EXPECT_FALSE(pending(target));
	EXPECT_FALSE(target->confusionState.pending);
	EXPECT_EQ(target->confusionState.previousResolved, battle::ConfusionBehavior::ATTACK);
}

TEST_F(NewHorizonsConfusionApplicationTest, DetachedActualEffectProducerRemovesBerserkAndKeepsLiveBattleUnchanged)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	berserk(target);
	history(battle::ConfusionBehavior::WANDER);
	ConfusionApplicationEnvironment env(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto model = std::make_shared<HypotheticBattle>(&env, callback);
	auto projected = model->getForUpdate(target->unitId());
	spells::BattleCast cast(model.get(), attackerSideHero, spells::Mode::HERO, confusion().toSpell());
	const auto mechanics = confusion().toSpell()->battleMechanics(&cast);
	spells::Target aim{spells::Destination(projected.get())};
	ASSERT_TRUE(mechanics->canBeCastAt(aim));
	mechanics->castEval(model->getServerCallback(), aim);
	EXPECT_TRUE(projected->confusionState.pending);
	EXPECT_EQ(projected->confusionState.previousResolved, battle::ConfusionBehavior::WANDER);
	EXPECT_TRUE(projected->hasBonusOfType(BonusType::CONFUSION_PENDING));
	EXPECT_FALSE(projected->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE));
	EXPECT_FALSE(target->confusionState.pending);
	EXPECT_TRUE(target->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE));
}

TEST_F(NewHorizonsConfusionApplicationTest, InvalidAndOldMarkerPacketRejectBeforeAnyPayload)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const Bonus valid = newHorizonsConfusionControl::pendingMarker(confusion(), PlayerColor(0), false);
	SetStackEffect packet;
	packet.battleID = BattleID(0);
	packet.toAdd.emplace_back(target->unitId(), std::vector<Bonus>{valid});
	CMemorySerializer current;
	ASSERT_NO_THROW(current.oser & packet);
	SetStackEffect restored;
	ASSERT_NO_THROW(current.iser & restored);
	ASSERT_EQ(restored.toAdd.size(), 1);
	ASSERT_EQ(restored.toAdd.front().second.size(), 1);
	EXPECT_TRUE(newHorizonsConfusionControl::isPendingMarker(&restored.toAdd.front().second.front()));
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_CONFUSION_STATE;
	EXPECT_THROW(old.oser & packet, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
	Bonus oldMarker = valid;
	CMemorySerializer oldBonus;
	oldBonus.oser.version = ESerializationVersion::NEW_HORIZONS_CONFUSION_STATE;
	EXPECT_THROW(oldBonus.oser & oldMarker, std::runtime_error);
	EXPECT_TRUE(oldBonus.extractBuffer().empty());
	for(const int value : {0, 3})
	{
		packet.toAdd.front().second.front().val = value;
		CMemorySerializer invalid;
		EXPECT_THROW(invalid.oser & packet, std::runtime_error);
		EXPECT_TRUE(invalid.extractBuffer().empty());
	}
}
