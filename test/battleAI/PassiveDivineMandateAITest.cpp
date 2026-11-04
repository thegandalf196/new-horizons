/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/PassiveDivineMandateProjection.h"
#include "../../AI/BattleAI/FixedActionValue.h"
#include "../../AI/BattleAI/LightSpellProtectionValue.h"
#include "HeroActionSelectionState.h"
#include "../../lib/battle/PassiveDivineMandateSpellPool.h"
#include "../../lib/battle/ArcaneInspiration.h"
#include "../../AI/BattleAI/HypotheticBattle.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
namespace
{
class MandateEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit MandateEnvironment(std::shared_ptr<CGameState> source) : state(std::move(source)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
class PassiveDivineMandateAITest : public HeroCommandFixture
{
protected:
	std::shared_ptr<MandateEnvironment> environment;
	std::shared_ptr<HypotheticBattle> postOrder;
	HeroOrderState order;
	uint32_t recipient = 0;
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons content";
		startGame();
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		const auto known = attackerSideHero->getSpellsInSpellbook();
		for(const auto spell : known) attackerSideHero->removeSpellFromSpellbook(spell);
		attackerSideHero->addSpellToSpellbook(SpellID::CURE);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:lightMagic")),
			MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:divineMandate")),
			MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		auto & perks = const_cast<newHorizonsHeroes::PerkState &>(attackerSideHero->getPerkState());
		perks.rules["divineMandatePolicy"]["version"].Integer() = 2;
		perks.selected.clear();
		setTestSpellPointTotal(attackerSideHero, 0);
		startBattle();
		beginCombat();
		const auto * target = battle()->battleActiveUnit();
		ASSERT_EQ(target->unitSide(), BattleSide::ATTACKER);
		recipient = target->unitId();
		auto wounded = target->acquireState();
		int64_t damage = 1;
		wounded->damage(damage);
		BattleUnitsChanged update;
		update.battleID = BattleID(0);
		auto & changed = update.changedStacks.emplace_back(recipient, UnitChanges::EOperation::UPDATE);
		changed.data = wounded->save();
		gameHandler->sendAndApply(update);
		environment = std::make_shared<MandateEnvironment>(gameState());
		auto visible = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		postOrder = std::make_shared<HypotheticBattle>(environment.get(), visible);
		const auto allowance = postOrder->prepareHeroOrderAllowance(BattleSide::ATTACKER);
		ASSERT_TRUE(allowance);
		const auto effects = postOrder->beginProjectedHeroOrder(BattleSide::ATTACKER, HeroCommand::CHARGE, {}, *allowance);
		ASSERT_TRUE(effects);
		order = effects->order;
		ASSERT_TRUE(order.passiveDivineMandate);
		ASSERT_TRUE(postOrder->projectAcceptedHeroOrder(BattleSide::ATTACKER, *effects, *allowance));
	}
	std::shared_ptr<HypotheticBattle> captureLiveBattle()
	{
		auto visible = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		return std::make_shared<HypotheticBattle>(environment.get(), visible);
	}
	void woundLiveRecipient()
	{
		auto wounded = battle()->getStack(recipient)->acquireState();
		int64_t damage = 1;
		wounded->damage(damage);
		BattleUnitsChanged update;
		update.battleID = BattleID(0);
		auto & changed = update.changedStacks.emplace_back(recipient, UnitChanges::EOperation::UPDATE);
		changed.data = wounded->save();
		gameHandler->sendAndApply(update);
	}
	void seedNativeAutomaticCast()
	{
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:divineMandate")),
			MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		ASSERT_TRUE(issue(HeroCommand::CHARGE));
		ASSERT_EQ(server.casts.size(), 1);
		ASSERT_EQ(server.casts.back().announcement.spellID, SpellID(SpellID::CURE));
		ASSERT_EQ(server.casts.back().announcement.automaticHeroCast.kind,
			spells::AutomaticHeroCastKind::DIVINE_MANDATE);
		ASSERT_EQ(battle()->getCastSpells(BattleSide::ATTACKER), 1);
		ASSERT_EQ(battle()->getPassiveDivineMandateUses(BattleSide::ATTACKER), 1);
		woundLiveRecipient();
		setTestSpellPointTotal(attackerSideHero, 100);
		postOrder = captureLiveBattle();
	}
	void completePaidCure(HypotheticBattle & state)
	{
		const auto side = BattleSide::ATTACKER;
		const auto allowance = state.prepareHeroSpellAllowance(side, SpellID::CURE, false, false);
		ASSERT_TRUE(allowance);
		const spells::Target aim{spells::Destination(state.battleGetUnitByID(recipient))};
		spells::BattleCast cast(&state, attackerSideHero, spells::Mode::HERO, SpellID(SpellID::CURE).toSpell());
		const auto mechanics = cast.mechanicsForTarget(aim);
		ASSERT_TRUE(mechanics->canBeCastAt(aim));
		ASSERT_TRUE(state.beginProjectedHeroAction(side, *allowance));
		mechanics->castEval(state.getServerCallback(), aim);
		ASSERT_TRUE(state.projectAcceptedHeroSpell(side, SpellID::CURE, recipient,
			false, false, false, false, *allowance));
	}
	void prepareSingleProtectionThreat(bool gale)
	{
		const auto known = attackerSideHero->getSpellsInSpellbook();
		for(const auto spell : known) attackerSideHero->removeSpellFromSpellbook(spell);
		const auto spell = SpellID(SpellID::decode(gale ? "new-horizons:heavenlyGale" : "new-horizons:guardianSpirit"));
		attackerSideHero->addSpellToSpellbook(spell);
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:lightMagic")),
			MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		ASSERT_TRUE(attackerSideHero->canCastThisSpell(spell.toSpell()));
		BattleUnitsChanged removed;
		removed.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			removed.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(removed);
		auto * ally = addStack(BattleSide::ATTACKER, creatureByName("core:ogre"), BattleHex(3, 5), 100);
		auto * enemy = addStack(BattleSide::DEFENDER, creatureByName(gale ? "core:archer" : "core:ogre"),
			gale ? BattleHex(13, 5) : BattleHex(4, 5), 20);
		ASSERT_NE(ally, nullptr);
		ASSERT_NE(enemy, nullptr);
		recipient = ally->unitId();
		for(auto * unit : {ally, enemy})
		{
			for(const auto type : {BonusType::NO_MORALE, BonusType::NO_LUCK})
				unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, type,
					BonusSource::OTHER, 1, BonusSourceID()));
			(void)unit->counterAttacks.total();
		}
		if(gale)
		{
			ASSERT_TRUE(enemy->shots.isLimited());
			ASSERT_GT(enemy->shots.available(), 0);
			enemy->shots.use(enemy->shots.available() - 1);
			ASSERT_EQ(enemy->shots.available(), 1);
			ASSERT_TRUE(battle()->battleCanShoot(enemy, ally->getPosition()));
		}
		else ASSERT_TRUE(battle()->isMeleeAttackPossible(enemy, ally));
		BattleSetActiveStack activation;
		activation.battleID = BattleID(0);
		activation.stack = recipient;
		activation.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activation);
		postOrder = captureLiveBattle();
		const auto allowance = postOrder->prepareHeroOrderAllowance(BattleSide::ATTACKER);
		ASSERT_TRUE(allowance);
		const auto effects = postOrder->beginProjectedHeroOrder(BattleSide::ATTACKER, HeroCommand::CHARGE, {}, *allowance);
		ASSERT_TRUE(effects);
		order = effects->order;
		ASSERT_TRUE(order.passiveDivineMandate);
		ASSERT_TRUE(postOrder->projectAcceptedHeroOrder(BattleSide::ATTACKER, *effects, *allowance));
	}
	void checkProtectionMarginalIsCountedOnce(bool gale)
	{
		ASSERT_NO_FATAL_FAILURE(prepareSingleProtectionThreat(gale));
		const auto side = BattleSide::ATTACKER;
		const auto next = newHorizonsPassiveDivineMandate::afterAcceptedCast(*order.passiveDivineMandate,
			{postOrder->getPassiveDivineMandateUses(side)});
		ASSERT_TRUE(next);
		spells::AutomaticHeroCastReceipt receipt{spells::AutomaticHeroCastKind::DIVINE_MANDATE, next->completedCasts};
		receipt.issuingOrder = order.command;
		receipt.issuingRound = order.issuedRound;
		const auto pool = newHorizonsPassiveDivineMandate::buildSpellPool(*postOrder, *attackerSideHero, receipt, {});
		ASSERT_EQ(pool.size(), 1u);
		ASSERT_EQ(pool.front().targets.size(), 1u);
		const auto & target = pool.front().targets.front();
		ASSERT_EQ(target.aimUnitId, recipient);
		ASSERT_EQ(target.recipientUnitIds, std::vector<uint32_t>{recipient});
		DamageCache damage;
		damage.buildDamageCache(postOrder, side);
		fixedActionValue::Reference reference(postOrder, PlayerColor(0), damage, 1, 1);
		ASSERT_EQ(reference.actionCount(), gale ? 1u : 2u);
		const auto liveBefore = gameState()->saveToMemory();
		const auto parentBefore = heroSelectionTest::snapshot(*postOrder, *gameHandler->randomizer,
			dynamic_cast<CRandomGenerator &>(*getRNG()));
		const auto rng = randomState();
		const auto parentMana = postOrder->getProjectedManaAvailable(side);
		const auto parentCasts = postOrder->getCastSpells(side);
		const auto nativeCasts = battle()->getCastSpells(side);
		auto outcome = std::make_shared<HypotheticBattle>(environment.get(), postOrder);
		const auto * definition = pool.front().spell.toSpell();
		const auto ward = outcome->resolveProjectedCounterspell(side, definition);
		ASSERT_TRUE(ward.resolutionKnown);
		ASSERT_TRUE(ward.negated);
		ASSERT_FALSE(*ward.negated);
		ASSERT_TRUE(outcome->projectAutomaticCounterspell(ward));
		const auto action = newHorizonsPassiveDivineMandate::automaticSpellAction(*outcome,
			side, pool.front().spell, target.aimUnitId);
		spells::BattleCast cast(outcome.get(), attackerSideHero, spells::Mode::HERO, definition);
		cast.setAutomaticHeroCast(receipt);
		const auto aim = action.getTarget(outcome.get());
		const auto mechanics = cast.mechanicsForTarget(aim);
		ASSERT_TRUE(mechanics->canBeCastAt(aim));
		mechanics->castEval(outcome->getServerCallback(), aim);
		const auto common = reference.marginal(outcome, target.recipientUnitIds);
		ASSERT_TRUE(common);
		ASSERT_GT(*common, 0);
		DamageCache legacyCache(&damage);
		const auto legacy = gale ? heavenlyGaleMitigationValue(side, *postOrder, outcome,
			pool.front().family, legacyCache) : guardianSpiritMitigationValue(
			postOrder->battleGetUnitByID(recipient), outcome->battleGetUnitByID(recipient), legacyCache, outcome);
		ASSERT_GT(legacy, 0); // Baseline witness: this same physical protection was priced by both paths.
		passiveDivineMandateAI::Workload work;
		const auto actual = passiveDivineMandateAI::projectedValue(environment.get(), postOrder,
			side, order, damage, 1, 1, &work);
		ASSERT_TRUE(actual);
		EXPECT_FLOAT_EQ(*actual, *common);
		EXPECT_EQ(work.spells, 1u);
		EXPECT_EQ(work.outcomes, 1u);
		EXPECT_EQ(work.evaluatedOutcomes, 1u);
		RecordProperty("CommonNativeMarginal", std::to_string(*common));
		RecordProperty("LegacyOverlappingProtection", std::to_string(legacy));
		RecordProperty("ActualAutomaticMarginal", std::to_string(*actual));
		EXPECT_EQ(gameState()->saveToMemory(), liveBefore);
		EXPECT_EQ(heroSelectionTest::snapshot(*postOrder, *gameHandler->randomizer,
			dynamic_cast<CRandomGenerator &>(*getRNG())), parentBefore);
		EXPECT_EQ(postOrder->getPassiveDivineMandateUses(side), 0);
		EXPECT_EQ(battle()->getPassiveDivineMandateUses(side), 0);
		EXPECT_EQ(attackerSideHero->getManaAvailable(), 0);
		EXPECT_EQ(postOrder->getProjectedManaAvailable(side), parentMana);
		EXPECT_EQ(postOrder->getCastSpells(side), parentCasts);
		EXPECT_EQ(battle()->getCastSpells(side), nativeCasts);
		EXPECT_EQ(randomState(), rng);
	}
	std::vector<std::byte> randomState()
	{
		CMemorySerializer wire;
		wire.oser & *gameHandler->randomizer;
		wire.oser & dynamic_cast<CRandomGenerator &>(*getRNG());
		wire.oser & CRandomGenerator::getDefault();
		return wire.extractBuffer();
	}
};
}
TEST_F(PassiveDivineMandateAITest, UsefulFreeHealingHasPositiveMarginalValueWithoutSpendingParentBudgetOrRng)
{
	DamageCache damage;
	damage.buildDamageCache(postOrder, BattleSide::ATTACKER);
	const auto rng = randomState();
	const auto health = battle()->getStack(recipient)->getAvailableHealth();
	passiveDivineMandateAI::Workload work;
	const auto value = passiveDivineMandateAI::projectedValue(environment.get(), postOrder,
		BattleSide::ATTACKER, order, damage, 1, 1, &work);
	ASSERT_TRUE(value);
	EXPECT_GT(*value, 0);
	EXPECT_EQ(work.spells, 1);
	EXPECT_EQ(work.evaluatedOutcomes, 1);
	EXPECT_EQ(postOrder->getPassiveDivineMandateUses(BattleSide::ATTACKER), 0);
	EXPECT_EQ(battle()->getPassiveDivineMandateUses(BattleSide::ATTACKER), 0);
	EXPECT_EQ(battle()->getStack(recipient)->getAvailableHealth(), health);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), 0);
	EXPECT_EQ(randomState(), rng);
}
TEST_F(PassiveDivineMandateAITest, ExhaustedBudgetSkipsAllOutcomeWork)
{
	postOrder->setPassiveDivineMandateUses(BattleSide::ATTACKER, 1);
	DamageCache damage;
	passiveDivineMandateAI::Workload work;
	const auto value = passiveDivineMandateAI::projectedValue(environment.get(), postOrder,
		BattleSide::ATTACKER, order, damage, 1, 1, &work);
	ASSERT_TRUE(value);
	EXPECT_FLOAT_EQ(*value, 0);
	EXPECT_EQ(work.spells, 0);
	EXPECT_EQ(work.evaluatedOutcomes, 0);
	EXPECT_EQ(postOrder->getPassiveDivineMandateUses(BattleSide::ATTACKER), 1);
}
TEST_F(PassiveDivineMandateAITest, PaidCastAfterChildRoundResetLeavesNativeAndParentCountersIsolated)
{
	ASSERT_NO_FATAL_FAILURE(seedNativeAutomaticCast());
	const auto rng = randomState();
	const auto round = postOrder->getRound();
	const auto health = battle()->getStack(recipient)->getAvailableHealth();
	const auto mana = attackerSideHero->getManaAvailable();
	auto childState = std::make_shared<HypotheticBattle>(environment.get(), postOrder);
	auto & child = *childState;
	EXPECT_EQ(child.getCastSpells(BattleSide::ATTACKER), 1);
	child.nextRound();
	EXPECT_EQ(child.getRound(), round + 1);
	EXPECT_EQ(child.getCastSpells(BattleSide::ATTACKER), 0);
	EXPECT_EQ(child.getPassiveDivineMandateUses(BattleSide::ATTACKER), 1);
	ASSERT_NO_FATAL_FAILURE(completePaidCure(child));
	EXPECT_EQ(child.getCastSpells(BattleSide::ATTACKER), 1);
	EXPECT_EQ(child.getPassiveDivineMandateUses(BattleSide::ATTACKER), 1);
	EXPECT_GT(child.battleGetUnitByID(recipient)->getAvailableHealth(), health);
	HypotheticBattle grandchild(environment.get(), childState);
	EXPECT_EQ(grandchild.getCastSpells(BattleSide::ATTACKER), 1);
	grandchild.nextRound();
	EXPECT_EQ(grandchild.getCastSpells(BattleSide::ATTACKER), 0);
	EXPECT_EQ(grandchild.getPassiveDivineMandateUses(BattleSide::ATTACKER), 1);
	EXPECT_EQ(child.getCastSpells(BattleSide::ATTACKER), 1);
	EXPECT_EQ(child.getPassiveDivineMandateUses(BattleSide::ATTACKER), 1);
	EXPECT_EQ(postOrder->getRound(), round);
	EXPECT_EQ(battle()->getRound(), round);
	EXPECT_EQ(postOrder->getCastSpells(BattleSide::ATTACKER), 1);
	EXPECT_EQ(battle()->getCastSpells(BattleSide::ATTACKER), 1);
	EXPECT_EQ(postOrder->getPassiveDivineMandateUses(BattleSide::ATTACKER), 1);
	EXPECT_EQ(battle()->getPassiveDivineMandateUses(BattleSide::ATTACKER), 1);
	EXPECT_EQ(battle()->getStack(recipient)->getAvailableHealth(), health);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(randomState(), rng);
}
TEST_F(PassiveDivineMandateAITest, AutomaticCastAfterChildRoundResetKeepsPerCombatUsageAndForkIsolation)
{
	ASSERT_NO_FATAL_FAILURE(seedNativeAutomaticCast());
	const auto rng = randomState();
	const auto round = postOrder->getRound();
	const auto health = battle()->getStack(recipient)->getAvailableHealth();
	auto childState = std::make_shared<HypotheticBattle>(environment.get(), postOrder);
	auto & child = *childState;
	child.nextRound();
	EXPECT_EQ(child.getCastSpells(BattleSide::ATTACKER), 0);
	EXPECT_EQ(child.getPassiveDivineMandateUses(BattleSide::ATTACKER), 1);
	const auto side = BattleSide::ATTACKER;
	const auto allowance = child.prepareHeroOrderAllowance(side);
	ASSERT_TRUE(allowance);
	const auto effects = child.beginProjectedHeroOrder(side, HeroCommand::CHARGE, {}, *allowance);
	ASSERT_TRUE(effects);
	ASSERT_TRUE(child.projectAcceptedHeroOrder(side, *effects, *allowance));
	ASSERT_TRUE(effects->order.passiveDivineMandate);
	const auto next = newHorizonsPassiveDivineMandate::afterAcceptedCast(
		*effects->order.passiveDivineMandate, {child.getPassiveDivineMandateUses(side)});
	ASSERT_TRUE(next);
	spells::AutomaticHeroCastReceipt receipt{spells::AutomaticHeroCastKind::DIVINE_MANDATE, next->completedCasts};
	receipt.issuingOrder = effects->order.command;
	receipt.issuingRound = effects->order.issuedRound;
	const auto * spell = SpellID(SpellID::CURE).toSpell();
	ASSERT_TRUE(spells::automaticHeroCastAllowed(child, attackerSideHero, spell, receipt));
	const auto ward = child.resolveProjectedCounterspell(side, spell);
	ASSERT_TRUE(ward.resolutionKnown);
	ASSERT_TRUE(ward.negated);
	ASSERT_FALSE(*ward.negated);
	ASSERT_TRUE(child.projectAutomaticCounterspell(ward));
	const auto mana = child.getProjectedManaAvailable(side);
	const spells::Target aim{spells::Destination(child.battleGetUnitByID(recipient))};
	spells::BattleCast cast(&child, attackerSideHero, spells::Mode::HERO, spell);
	cast.setAutomaticHeroCast(receipt);
	const auto mechanics = cast.mechanicsForTarget(aim);
	ASSERT_TRUE(mechanics->canBeCastAt(aim));
	mechanics->castEval(child.getServerCallback(), aim);
	EXPECT_EQ(child.getCastSpells(side), 1);
	EXPECT_EQ(child.getPassiveDivineMandateUses(side), 2);
	EXPECT_EQ(child.getProjectedManaAvailable(side), mana);
	EXPECT_GT(child.battleGetUnitByID(recipient)->getAvailableHealth(), health);
	HypotheticBattle grandchild(environment.get(), childState);
	EXPECT_EQ(grandchild.getCastSpells(side), 1);
	EXPECT_EQ(grandchild.getPassiveDivineMandateUses(side), 2);
	grandchild.nextRound();
	EXPECT_EQ(grandchild.getCastSpells(side), 0);
	EXPECT_EQ(grandchild.getPassiveDivineMandateUses(side), 2);
	EXPECT_EQ(child.getCastSpells(side), 1);
	EXPECT_EQ(child.getPassiveDivineMandateUses(side), 2);
	EXPECT_EQ(postOrder->getRound(), round);
	EXPECT_EQ(battle()->getRound(), round);
	EXPECT_EQ(postOrder->getCastSpells(side), 1);
	EXPECT_EQ(battle()->getCastSpells(side), 1);
	EXPECT_EQ(postOrder->getPassiveDivineMandateUses(side), 1);
	EXPECT_EQ(battle()->getPassiveDivineMandateUses(side), 1);
	EXPECT_EQ(battle()->getStack(recipient)->getAvailableHealth(), health);
	EXPECT_EQ(randomState(), rng);
}
TEST_F(PassiveDivineMandateAITest, UnresolvedRoundTransitionDoesNotResetCompletedCastOrMandateUsage)
{
	ASSERT_NO_FATAL_FAILURE(seedNativeAutomaticCast());
	HypotheticBattle child(environment.get(), postOrder);
	PreCombatOrderState pending;
	pending.phase = PreCombatOrderState::Phase::AVAILABLE;
	pending.beginOrderRequired(child.getRound(), recipient);
	child.setPreCombatOrderState(BattleSide::ATTACKER, pending);
	const auto round = child.getRound();
	const auto ledger = child.getHeroActionAllowances(BattleSide::ATTACKER);
	const auto rng = randomState();
	EXPECT_THROW(child.nextRound(), std::runtime_error);
	EXPECT_EQ(child.getRound(), round);
	EXPECT_EQ(child.getPreCombatOrderState(BattleSide::ATTACKER), pending);
	EXPECT_EQ(child.getHeroActionAllowances(BattleSide::ATTACKER), ledger);
	EXPECT_EQ(child.getCastSpells(BattleSide::ATTACKER), 1);
	EXPECT_EQ(child.getPassiveDivineMandateUses(BattleSide::ATTACKER), 1);
	EXPECT_EQ(postOrder->getCastSpells(BattleSide::ATTACKER), 1);
	EXPECT_EQ(battle()->getCastSpells(BattleSide::ATTACKER), 1);
	EXPECT_EQ(postOrder->getPassiveDivineMandateUses(BattleSide::ATTACKER), 1);
	EXPECT_EQ(battle()->getPassiveDivineMandateUses(BattleSide::ATTACKER), 1);
	EXPECT_EQ(randomState(), rng);
}
TEST_F(PassiveDivineMandateAITest, AbsentPassivePolicyRetainsHistoricalCastCounterFallback)
{
	auto & perks = const_cast<newHorizonsHeroes::PerkState &>(attackerSideHero->getPerkState());
	perks.rules.Struct().erase("divineMandatePolicy");
	ASSERT_FALSE(newHorizonsPassiveDivineMandate::enabled(perks.rules));
	setTestSpellPointTotal(attackerSideHero, 100);
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = SpellID::CURE;
	action.aimToUnit(battle()->getStack(recipient));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	ASSERT_EQ(battle()->getCastSpells(BattleSide::ATTACKER), 1);
	woundLiveRecipient();
	postOrder = captureLiveBattle();
	const auto rng = randomState();
	const auto round = postOrder->getRound();
	HypotheticBattle child(environment.get(), postOrder);
	EXPECT_EQ(child.getCastSpells(BattleSide::ATTACKER), 1);
	child.nextRound();
	EXPECT_EQ(child.getRound(), round + 1);
	EXPECT_EQ(child.getCastSpells(BattleSide::ATTACKER), 1);
	ASSERT_NO_FATAL_FAILURE(completePaidCure(child));
	EXPECT_EQ(child.getCastSpells(BattleSide::ATTACKER), 1);
	EXPECT_EQ(child.getPassiveDivineMandateUses(BattleSide::ATTACKER), 0);
	EXPECT_EQ(postOrder->getRound(), round);
	EXPECT_EQ(battle()->getRound(), round);
	EXPECT_EQ(postOrder->getCastSpells(BattleSide::ATTACKER), 1);
	EXPECT_EQ(battle()->getCastSpells(BattleSide::ATTACKER), 1);
	EXPECT_EQ(postOrder->getPassiveDivineMandateUses(BattleSide::ATTACKER), 0);
	EXPECT_EQ(battle()->getPassiveDivineMandateUses(BattleSide::ATTACKER), 0);
	EXPECT_EQ(randomState(), rng);
}
TEST_F(PassiveDivineMandateAITest, OnePhysicalShotGaleMarginalIsCountedOnce)
{
	ASSERT_NO_FATAL_FAILURE(checkProtectionMarginalIsCountedOnce(true));
}
TEST_F(PassiveDivineMandateAITest, AdjacentPhysicalGuardianMarginalIsCountedOnce)
{
	ASSERT_NO_FATAL_FAILURE(checkProtectionMarginalIsCountedOnce(false));
}

TEST_F(PassiveDivineMandateAITest, AutomaticFreeHeroCastInspiresWithoutAnotherManualAllowanceOrManaPayment)
{
	ASSERT_NO_FATAL_FAILURE(seedNativeAutomaticCast());
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(arcaneInspiration::SKILL)),
		MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	const auto random = randomState();
	auto child = std::make_shared<HypotheticBattle>(environment.get(), postOrder);
	child->nextRound();
	const auto side = BattleSide::ATTACKER;
	const auto allowance = child->prepareHeroOrderAllowance(side);
	ASSERT_TRUE(allowance);
	const auto effects = child->beginProjectedHeroOrder(side, HeroCommand::CHARGE, {}, *allowance);
	ASSERT_TRUE(effects); ASSERT_TRUE(child->projectAcceptedHeroOrder(side, *effects, *allowance));
	ASSERT_TRUE(effects->order.passiveDivineMandate);
	const auto next = newHorizonsPassiveDivineMandate::afterAcceptedCast(*effects->order.passiveDivineMandate,
		{child->getPassiveDivineMandateUses(side)});
	ASSERT_TRUE(next);
	spells::AutomaticHeroCastReceipt receipt{spells::AutomaticHeroCastKind::DIVINE_MANDATE, next->completedCasts};
	receipt.issuingOrder = effects->order.command;
	receipt.issuingRound = effects->order.issuedRound;
	const auto * spell = SpellID(SpellID::CURE).toSpell();
	ASSERT_TRUE(spells::automaticHeroCastAllowed(*child, attackerSideHero, spell, receipt));
	const auto mana = child->getProjectedManaAvailable(side);
	spells::BattleCast cast(child.get(), attackerSideHero, spells::Mode::HERO, spell);
	cast.setAutomaticHeroCast(receipt);
	const spells::Target target{spells::Destination(child->battleGetUnitByID(recipient))};
	const auto mechanics = cast.mechanicsForTarget(target);
	ASSERT_TRUE(mechanics->canBeCastAt(target));
	mechanics->castEval(child->getServerCallback(), target);
	EXPECT_EQ(child->getProjectedManaAvailable(side), mana);
	EXPECT_EQ(arcaneInspiration::activeProfile(*child->battleGetUnitByID(recipient), PlayerColor(0)).corePercent, 5);
	EXPECT_FALSE(child->prepareHeroSpellAllowance(side, spell->getId(), false, false));
	EXPECT_FALSE(child->prepareHeroOrderAllowance(side));
	EXPECT_EQ(randomState(), random);
	EXPECT_FALSE(battle()->battleGetUnitByID(recipient)->hasBonus(CSelector(arcaneInspiration::isActive)));
}
