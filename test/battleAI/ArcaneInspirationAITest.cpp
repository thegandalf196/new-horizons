/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ProjectedTargetHitFixture.h"
#include "../../AI/BattleAI/PhysicalStrikeProjection.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../AI/BattleAI/InitialExportBattleSubject.h"
#include "../../lib/battle/ArcaneInspiration.h"
#include "../../lib/battle/ArcaneReprisal.h"
#include "../../lib/battle/NewHorizonsBulwark.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../AI/BattleAI/HeroSpellExpectedValueProjection.h"
#include "../../AI/BattleAI/PossibleSpellcast.h"
#include "../../AI/BattleAI/SpellTargetsEvaluator.h"
#include "../../lib/spells/ArcaneInspirationCompletion.h"
#include "../mock/mock_ServerCallback.h"

namespace
{
constexpr auto SIDE = BattleSide::ATTACKER;
class ArcaneInspirationAITest : public ProjectedTargetHitFixture
{
protected:
	uint32_t actor = 0, victim = 0;
	void prepareInspiration(const char * basic = nullptr)
	{
		startGame();
		if(basic)
		{
			const SecondarySkill skill(SecondarySkill::decode(arcaneInspiration::SKILL));
			ASSERT_GE(skill.getNum(), 0);
			attackerSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({arcaneInspiration::SKILL, std::string(arcaneInspiration::SKILL) + "." + basic});
		}
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(SpellID::MAGIC_ARROW);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 1000, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 100);
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		auto * a = addStack(SIDE, creatureByName("core:angel"), BattleHex(leftHex), 100);
		auto * b = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 100);
		ASSERT_NE(a, nullptr); ASSERT_NE(b, nullptr);
		actor = a->unitId(); victim = b->unitId();
		beginCombat(); activate(actor);
		environment = std::make_unique<projectedHitTest::Environment>(gameState());
		live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		resetChild();
	}
	void install(HypotheticBattle & state, uint32_t id, int core, uint32_t riders)
	{
		const auto owner = state.battleGetOwner(state.battleGetUnitByID(id));
		const auto active = arcaneInspiration::activeMarker({core, riders, false}, owner, state.getRound());
		std::vector<Bonus> effects{active};
		if(riders & arcaneInspiration::LINGERING) effects.push_back(arcaneInspiration::retainedMarker(active));
		SetStackEffect packet;
		packet.battleID = state.getBattleID();
		packet.toAdd.emplace_back(id, std::move(effects));
		state.getServerCallback()->apply(packet);
	}
	int64_t strike(HypotheticBattle & state, uint32_t from, uint32_t to, int64_t requested, bool retaliation = false)
	{
		auto a = state.getForUpdate(from), b = state.getForUpdate(to);
		BattleAttackInfo attack(a.get(), b.get(), 0, false);
		attack.retaliation = retaliation;
		PhysicalStrikeProjection projection(&state);
		projection.damage(state, attack, *b, requested, battle::DamageProvenance::PHYSICAL_CREATURE);
		projection.finish(from);
		return requested;
	}
	int64_t damage(HypotheticBattle & state, bool retaliation = false)
	{
		BattleAttackInfo attack(state.battleGetUnitByID(actor), state.battleGetUnitByID(victim), 0, false);
		attack.retaliation = retaliation;
		return state.calculateDmgRange(attack).damage.min;
	}
};

TEST_F(ArcaneInspirationAITest, SuccessfulSharedSpellProjectsCoreAndSpellshockWithoutAnotherHeroAction)
{
	ASSERT_NO_FATAL_FAILURE(prepareInspiration("spellshock"));
	const auto world = gameState()->saveToMemory();
	const auto random = projectedHitTest::serialized(*gameHandler->randomizer);
	const auto spell = SpellID(SpellID::MAGIC_ARROW);
	const auto prepared = child->prepareHeroSpellAllowance(SIDE, spell, false, false);
	ASSERT_TRUE(prepared); ASSERT_TRUE(child->beginProjectedHeroAction(SIDE, *prepared));
	spells::BattleCast cast(child.get(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
	auto mechanics = spell.toSpell()->battleMechanics(&cast);
	mechanics->castEval(child->getServerCallback(), {spells::Destination(child->battleGetUnitByID(victim))});
	ASSERT_TRUE(child->projectAcceptedHeroSpell(SIDE, spell, victim, false, false, false, false, *prepared));
	EXPECT_EQ(arcaneInspiration::activeProfile(*child->battleGetUnitByID(actor), PlayerColor(0)).corePercent, 5);
	EXPECT_TRUE(child->battleGetUnitByID(victim)->hasBonus(CSelector(arcaneInspiration::isSpellshock)));
	EXPECT_EQ(child->getArcaneInspirationEffects().size(), 2u);
	EXPECT_FALSE(child->prepareHeroSpellAllowance(SIDE, spell, false, false));
	EXPECT_FALSE(child->prepareHeroOrderAllowance(SIDE));
	EXPECT_EQ(child->getWisdomCombatState(SIDE).priorCasts(spell), 1u);
	EXPECT_EQ(gameState()->saveToMemory(), world);
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), random);
}

TEST_F(ArcaneInspirationAITest, SkippedEffectCompletionPublishesOneReversibleAuraAndNegationPublishesNone)
{
	ASSERT_NO_FATAL_FAILURE(prepareInspiration("arcaneValor"));
	const auto spell = SpellID(SpellID::MAGIC_ARROW);
	const auto prepared = child->prepareHeroSpellAllowance(SIDE, spell, false, false);
	ASSERT_TRUE(prepared); ASSERT_TRUE(child->beginProjectedHeroAction(SIDE, *prepared));
	ASSERT_TRUE(child->projectAcceptedHeroSpell(SIDE, spell, victim, false, false, false, false, *prepared));
	ASSERT_EQ(child->getArcaneInspirationEffects().size(), 1u);
	EXPECT_TRUE(child->battleGetUnitByID(actor)->hasBonus(CSelector(arcaneInspiration::isActive)));
	EXPECT_FALSE(child->battleGetUnitByID(victim)->hasBonus(CSelector(arcaneInspiration::isSpellshock)));
	const auto count = child->battleGetUnitByID(actor)->getBonuses(CSelector(arcaneInspiration::isActive))->size();
	EXPECT_FALSE(child->projectAcceptedHeroSpell(SIDE, spell, victim, false, false, false, false, *prepared));
	EXPECT_EQ(child->battleGetUnitByID(actor)->getBonuses(CSelector(arcaneInspiration::isActive))->size(), count);
	auto negated = std::make_shared<HypotheticBattle>(environment.get(), live);
	const auto denied = negated->prepareHeroSpellAllowance(SIDE, spell, false, false);
	ASSERT_TRUE(denied); ASSERT_TRUE(negated->beginProjectedHeroAction(SIDE, *denied));
	ASSERT_TRUE(negated->projectAcceptedHeroSpell(SIDE, spell, victim, false, false, true, true, *denied));
	EXPECT_TRUE(negated->getArcaneInspirationEffects().empty());
	EXPECT_FALSE(negated->battleGetUnitByID(actor)->hasBonus(CSelector(arcaneInspiration::isActive)));
	EXPECT_EQ(negated->getWisdomCombatState(SIDE).priorCasts(spell), 1u);
}

TEST_F(ArcaneInspirationAITest, CoreIsMultiplicativeAndRetaliationFocusAddsFifteenPoints)
{
	ASSERT_NO_FATAL_FAILURE(prepareInspiration());
	const auto base = damage(*child);
	install(*child, actor, 15, arcaneInspiration::FOCUS);
	EXPECT_EQ(damage(*child), base * 115 / 100);
	EXPECT_EQ(damage(*child, true), base * 130 / 100);
	child->nextRound();
	EXPECT_EQ(damage(*child), base);
	EXPECT_EQ(damage(*child, true), base);
}

TEST_F(ArcaneInspirationAITest, FirstRetaliationSpendsLingeringWhileActiveAndRoundExpiryDoesNotRearmIt)
{
	ASSERT_NO_FATAL_FAILURE(prepareInspiration());
	const auto base = damage(*child);
	install(*child, actor, 15, arcaneInspiration::FOCUS | arcaneInspiration::LINGERING);
	EXPECT_EQ(damage(*child, true), base * 130 / 100);
	strike(*child, actor, victim, 1, true);
	EXPECT_FALSE(child->battleGetUnitByID(actor)->hasBonus(CSelector(arcaneInspiration::isRetained)));
	EXPECT_TRUE(child->battleGetUnitByID(actor)->hasBonus(CSelector(arcaneInspiration::isActive)));
	EXPECT_EQ(damage(*child, true), base * 130 / 100);
	child->nextRound();
	EXPECT_FALSE(child->battleGetUnitByID(actor)->hasBonus(CSelector(arcaneInspiration::isRetained)));
	EXPECT_EQ(damage(*child, true), base);
}

TEST_F(ArcaneInspirationAITest, UnspentLingeringSurvivesExpiryWithCoreOnlyThenExpiresOnOneStrike)
{
	ASSERT_NO_FATAL_FAILURE(prepareInspiration());
	const auto base = damage(*child);
	install(*child, actor, 15, arcaneInspiration::FOCUS | arcaneInspiration::WEAPONS | arcaneInspiration::LINGERING);
	child->nextRound();
	EXPECT_FALSE(child->battleGetUnitByID(actor)->hasBonus(CSelector(arcaneInspiration::isActive)));
	EXPECT_TRUE(child->battleGetUnitByID(actor)->hasBonus(CSelector(arcaneInspiration::isRetained)));
	EXPECT_EQ(damage(*child, true), base * 115 / 100);
	strike(*child, actor, victim, 1, true);
	EXPECT_EQ(damage(*child), base);
	EXPECT_EQ(damage(*child, true), base);
}

TEST_F(ArcaneInspirationAITest, ParentAndSiblingKeepIndependentStatusAndChargeLifetimes)
{
	ASSERT_NO_FATAL_FAILURE(prepareInspiration());
	install(*child, actor, 15, arcaneInspiration::LINGERING);
	auto descendant = std::make_shared<HypotheticBattle>(environment.get(), child);
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), child);
	strike(*child, actor, victim, 1);
	EXPECT_TRUE(descendant->battleGetUnitByID(actor)->hasBonus(CSelector(arcaneInspiration::isRetained)));
	EXPECT_TRUE(sibling->battleGetUnitByID(actor)->hasBonus(CSelector(arcaneInspiration::isRetained)));
	descendant->nextRound();
	EXPECT_FALSE(descendant->battleGetUnitByID(actor)->hasBonus(CSelector(arcaneInspiration::isActive)));
	EXPECT_TRUE(sibling->battleGetUnitByID(actor)->hasBonus(CSelector(arcaneInspiration::isActive)));
	EXPECT_FALSE(battle()->battleGetUnitByID(actor)->hasBonus(CSelector(arcaneInspiration::isEffect)));
}

TEST_F(ArcaneInspirationAITest, ReprisalUsesPostShieldDamageBeforeLethalHealthClampAndNeverRecurses)
{
	ASSERT_NO_FATAL_FAILURE(prepareInspiration());
	install(*child, actor, 15, arcaneInspiration::REPRISAL);
	install(*child, victim, 15, arcaneInspiration::REPRISAL);
	auto a = child->getForUpdate(actor), b = child->getForUpdate(victim);
	int64_t trim = b->getAvailableHealth() - 1;
	b->damage(trim, false, battle::DamageProvenance::OTHER); // Fixture leaves one HP before the shield is installed.
	b->arcaneShieldHitPoints = 400;
	const auto before = a->getAvailableHealth();
	strike(*child, actor, victim, 1000);
	EXPECT_FALSE(b->alive());
	EXPECT_EQ(before - a->getAvailableHealth(), 60);
	EXPECT_EQ(a->getAvailableHealth(), before - 60); // SECONDARY injury never reflects back.
}

TEST_F(ArcaneInspirationAITest, FullyAbsorbedAttackReflectsNothingButSpendsTheAcceptedStrikeCharge)
{
	ASSERT_NO_FATAL_FAILURE(prepareInspiration());
	install(*child, actor, 15, arcaneInspiration::LINGERING);
	install(*child, victim, 15, arcaneInspiration::REPRISAL);
	auto a = child->getForUpdate(actor), b = child->getForUpdate(victim);
	b->arcaneShieldHitPoints = 1000;
	const auto beforeA = a->getAvailableHealth(), beforeB = b->getAvailableHealth();
	strike(*child, actor, victim, 1000);
	EXPECT_EQ(a->getAvailableHealth(), beforeA);
	EXPECT_EQ(b->getAvailableHealth(), beforeB);
	EXPECT_FALSE(a->hasBonus(CSelector(arcaneInspiration::isRetained)));
}

TEST_F(ArcaneInspirationAITest, ExistingDamageCacheDoesNotReviveActiveDamageAfterExpiryOrChargeConsumption)
{
	ASSERT_NO_FATAL_FAILURE(prepareInspiration());
	DamageCache cache;
	cache.buildDamageCache(child, SIDE);
	const auto baseline = cache.getDamage(child->battleGetUnitByID(actor), child->battleGetUnitByID(victim), child);
	install(*child, actor, 15, arcaneInspiration::LINGERING);
	EXPECT_GT(cache.getDamage(child->battleGetUnitByID(actor), child->battleGetUnitByID(victim), child), baseline);
	child->nextRound();
	EXPECT_GT(cache.getDamage(child->battleGetUnitByID(actor), child->battleGetUnitByID(victim), child), baseline);
	strike(*child, actor, victim, 0);
	EXPECT_EQ(cache.getDamage(child->battleGetUnitByID(actor), child->battleGetUnitByID(victim), child), baseline);
}
TEST_F(ArcaneInspirationAITest, OwnedExportCapturesActiveAndRetainedEffectsBeforeAncestorMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepareInspiration());
	const auto active = arcaneInspiration::activeMarker({15, arcaneInspiration::LINGERING, false}, PlayerColor(0), battle()->getRound());
	SetStackEffect native;
	native.battleID = BattleID(0);
	native.toAdd.emplace_back(actor, std::vector<Bonus>{active, arcaneInspiration::retainedMarker(active)});
	gameHandler->sendAndApply(native);
	auto authority = std::shared_ptr<CBattleInfoCallback>(gameState(), battle());
	auto exported = InitialExportBattleSubject::capture(authority, *battle());
	auto parent = std::make_shared<HypotheticBattle>(environment.get(), exported);
	auto descendant = std::make_shared<HypotheticBattle>(environment.get(), parent);
	parent->nextRound();
	strike(*parent, actor, victim, 1);
	EXPECT_FALSE(parent->battleGetUnitByID(actor)->hasBonus(CSelector(arcaneInspiration::isRetained)));
	EXPECT_TRUE(descendant->battleGetUnitByID(actor)->hasBonus(CSelector(arcaneInspiration::isActive)));
	EXPECT_TRUE(descendant->battleGetUnitByID(actor)->hasBonus(CSelector(arcaneInspiration::isRetained)));
	EXPECT_TRUE(battle()->battleGetUnitByID(actor)->hasBonus(CSelector(arcaneInspiration::isActive)));
	EXPECT_TRUE(battle()->battleGetUnitByID(actor)->hasBonus(CSelector(arcaneInspiration::isRetained)));
}

TEST_F(ArcaneInspirationAITest, ReprisalChangesSignedCandidateUtilityAndProbeMatchesAppliedExchange)
{
	ASSERT_NO_FATAL_FAILURE(prepareInspiration());
	DamageCache cache;
	cache.buildDamageCache(child, SIDE);
	BattleAttackInfo ordinary(child->battleGetUnitByID(actor), child->battleGetUnitByID(victim), 0, false);
	const auto base = AttackPossibility::evaluate(ordinary, ordinary.attacker->getPosition(), cache, child);
	install(*child, victim, 15, arcaneInspiration::REPRISAL);
	const auto beforeA = child->battleGetUnitByID(actor)->getAvailableHealth();
	const auto beforeB = child->battleGetUnitByID(victim)->getAvailableHealth();
	const auto random = projectedHitTest::serialized(*gameHandler->randomizer);
	BattleAttackInfo responding(child->battleGetUnitByID(actor), child->battleGetUnitByID(victim), 0, false);
	const auto reflected = AttackPossibility::evaluate(responding, responding.attacker->getPosition(), cache, child);
	ASSERT_TRUE(reflected.effectPreview);
	EXPECT_GT(reflected.attackerDamageReduce, base.attackerDamageReduce);
	EXPECT_LT(reflected.attackValue(), base.attackValue());
	BattleExchangeVariant probe;
	const auto probeValue = probe.trackAttack(child->getForUpdate(actor), child->getForUpdate(victim), false, true, cache, child, true);
	EXPECT_EQ(child->battleGetUnitByID(actor)->getAvailableHealth(), beforeA);
	EXPECT_EQ(child->battleGetUnitByID(victim)->getAvailableHealth(), beforeB);
	auto applied = std::make_shared<HypotheticBattle>(environment.get(), child);
	DamageCache inner(&cache);
	BattleExchangeVariant execution;
	const auto appliedValue = execution.trackAttack(applied->getForUpdate(actor), applied->getForUpdate(victim), false, true, inner, applied);
	EXPECT_FLOAT_EQ(probeValue, appliedValue);
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), random);
	EXPECT_LT(applied->battleGetUnitByID(actor)->getAvailableHealth(), beforeA);
}

TEST_F(ArcaneInspirationAITest, FocusWeaponsSupremacyAndSpellshockUseTheSharedAttackPlan)
{
	ASSERT_NO_FATAL_FAILURE(prepareInspiration());
	const auto base = damage(*child);
	install(*child, actor, 15, arcaneInspiration::FOCUS | arcaneInspiration::WEAPONS | arcaneInspiration::SUPREMACY);
	SetStackEffect shock;
	shock.battleID = child->getBattleID();
	shock.toAdd.emplace_back(actor, std::vector<Bonus>{arcaneInspiration::spellshockMarker(PlayerColor(1), child->getRound())});
	child->getServerCallback()->apply(shock);
	BattleAttackInfo attack(child->battleGetUnitByID(actor), child->battleGetUnitByID(victim), 0, false);
	attack.retaliation = true;
	const auto plan = arcaneInspiration::attackPlan(*child, attack);
	EXPECT_EQ(plan.damageBonusPercent, 30);
	EXPECT_EQ(plan.outputPenaltyPercent, 10);
	EXPECT_EQ(plan.defenseIgnorePercent, 15);
	EXPECT_EQ(plan.protectionIgnorePercent, 20);
	EXPECT_GT(damage(*child, true), base);
	child->nextRound();
	EXPECT_EQ(damage(*child, true), base);
}

TEST_F(ArcaneInspirationAITest, CompletionWithoutAnAcceptedPrimaryStrikeCannotSpendRetainedCharge)
{
	ASSERT_NO_FATAL_FAILURE(prepareInspiration());
	install(*child, actor, 15, arcaneInspiration::LINGERING);
	PhysicalStrikeProjection incomplete(child.get());
	incomplete.finish(actor);
	EXPECT_TRUE(child->battleGetUnitByID(actor)->hasBonus(CSelector(arcaneInspiration::isRetained)));
}

}

TEST_F(ArcaneInspirationAITest, LastHeroPoisonTickAppliesOnlySpellshockWithIsolatedHistoryBudgetAndRng)
{
 ASSERT_NO_FATAL_FAILURE(prepareInspiration("spellshock"));
 auto target = child->getForUpdate(victim);
 ASSERT_TRUE(newHorizonsBulwark::applyPhysicalPoison(target.get(), 10, -1));
 target->physicalPoisonSpellSide = SIDE;
 target->physicalPoisonActivationsRemaining = 1;
 auto sibling = std::make_shared<HypotheticBattle>(environment.get(), child);
 const auto history = projectedHitTest::serialized(child->getWisdomCombatState(SIDE));
 const auto allowance = child->getHeroActionAllowances(SIDE);
 const auto points = child->getMetamagicPointState(SIDE);
 const auto world = gameState()->saveToMemory();
 const auto random = projectedHitTest::serialized(*gameHandler->randomizer);
 const auto hp = target->getAvailableHealth();
 child->nextTurn(victim, BattleUnitTurnReason::TURN_QUEUE);
 EXPECT_LT(target->getAvailableHealth(), hp);
 EXPECT_EQ(target->physicalPoisonSpellOrigin(), BattleSide::NONE);
 EXPECT_TRUE(target->hasBonus(CSelector(arcaneInspiration::isSpellshock)));
 EXPECT_FALSE(child->battleGetUnitByID(actor)->hasBonus(CSelector(arcaneInspiration::isActive)));
 EXPECT_EQ(projectedHitTest::serialized(child->getWisdomCombatState(SIDE)), history);
 EXPECT_EQ(child->getHeroActionAllowances(SIDE), allowance);
 EXPECT_EQ(child->getMetamagicPointState(SIDE), points);
 EXPECT_FALSE(sibling->battleGetUnitByID(victim)->hasBonus(CSelector(arcaneInspiration::isSpellshock)));
 EXPECT_EQ(sibling->battleGetUnitByID(victim)->getAvailableHealth(), hp);
 EXPECT_EQ(gameState()->saveToMemory(), world);
 EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), random);
}

TEST_F(ArcaneInspirationAITest, CreaturePoisonDoesNotBorrowItsOwnersHeroSpellshock)
{
 ASSERT_NO_FATAL_FAILURE(prepareInspiration("spellshock"));
 auto target = child->getForUpdate(victim);
 ASSERT_TRUE(newHorizonsBulwark::applyPhysicalPoison(target.get(), 10, actor));
 ASSERT_EQ(target->physicalPoisonSpellOrigin(), BattleSide::NONE);
 const auto hp = target->getAvailableHealth();
 child->nextTurn(victim, BattleUnitTurnReason::TURN_QUEUE);
 EXPECT_LT(target->getAvailableHealth(), hp);
 EXPECT_FALSE(target->hasBonus(CSelector(arcaneInspiration::isSpellshock)));
 EXPECT_FALSE(child->battleGetUnitByID(actor)->hasBonus(CSelector(arcaneInspiration::isActive)));
}

TEST_F(ArcaneInspirationAITest, FullyShieldedHeroPoisonHasNoActualDamageAndNoSpellshock)
{
 ASSERT_NO_FATAL_FAILURE(prepareInspiration("spellshock"));
 auto target = child->getForUpdate(victim);
 ASSERT_TRUE(newHorizonsBulwark::applyPhysicalPoison(target.get(), 10, -1));
 target->physicalPoisonSpellSide = SIDE;
 target->guardianSpiritHitPoints = 1000;
 const auto hp = target->getAvailableHealth();
 child->nextTurn(victim, BattleUnitTurnReason::TURN_QUEUE);
 EXPECT_EQ(target->getAvailableHealth(), hp);
 EXPECT_FALSE(target->hasBonus(CSelector(arcaneInspiration::isSpellshock)));
 EXPECT_FALSE(child->battleGetUnitByID(actor)->hasBonus(CSelector(arcaneInspiration::isActive)));
}

TEST_F(ArcaneInspirationAITest, CanonicalCompletionReceiptsReverseInOrderWithoutReversingCandidateEffectOrDamage)
{
 ASSERT_NO_FATAL_FAILURE(prepareInspiration("spellshock"));
 install(*child, actor, 15, arcaneInspiration::LINGERING);
 Bonus candidateBuff(BonusDuration::ONE_BATTLE, BonusType::PRIMARY_SKILL, BonusSource::SPELL_EFFECT,
  17, BonusSourceID(SpellID(SpellID::BLESS)), BonusSubtypeID(PrimarySkill::ATTACK));
 SetStackEffect candidate;
 candidate.battleID = child->getBattleID(); candidate.toAdd.emplace_back(actor, std::vector<Bonus>{candidateBuff});
 child->getServerCallback()->apply(candidate);
 EXPECT_TRUE(child->getArcaneInspirationEffects().empty()); // Ordinary candidate packets are not receipts.
 auto target = child->getForUpdate(victim); int64_t injuryDamage = 1;
 target->damage(injuryDamage);
 BattleStackAttacked injury; injury.stackAttacked = victim; injury.damageAmount = injuryDamage;
 arcaneInspiration::publishCompletedSpell(*child->getServerCallback(), *child, SIDE, std::span(&injury, 1));
 ASSERT_EQ(child->getArcaneInspirationEffects().size(), 2u);
 // A second completion refreshes the prior status; reversal must unwind both packets first.
 arcaneInspiration::publishCompletedSpell(*child->getServerCallback(), *child, SIDE, std::span(&injury, 1));
 ASSERT_EQ(child->getArcaneInspirationEffects().size(), 4u);
 const auto hp = target->getAvailableHealth();
 auto without = std::make_shared<HypotheticBattle>(environment.get(), child);
 without->reverseArcaneInspirationEffects(child->getArcaneInspirationEffects());
 EXPECT_EQ(without->battleGetUnitByID(victim)->getAvailableHealth(), hp);
 EXPECT_FALSE(without->battleGetUnitByID(victim)->hasBonus(CSelector(arcaneInspiration::isSpellshock)));
 EXPECT_EQ(arcaneInspiration::activeProfile(*without->battleGetUnitByID(actor), PlayerColor(0)).corePercent, 15);
 EXPECT_TRUE(without->battleGetUnitByID(actor)->hasBonus(CSelector(arcaneInspiration::isRetained)));
 EXPECT_TRUE(without->battleGetUnitByID(actor)->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::BLESS)))));
 EXPECT_EQ(arcaneInspiration::activeProfile(*child->battleGetUnitByID(actor), PlayerColor(0)).corePercent, 5);
}

TEST_F(ArcaneInspirationAITest, FailedCanonicalApplyCannotPublishACompletionReceipt)
{
 ASSERT_NO_FATAL_FAILURE(prepareInspiration("spellshock"));
 class ReceiptServer : public ServerCallbackMock
 {
 public:
  size_t observed = 0;
  void recordArcaneInspirationEffects(const SetStackEffect &) override { ++observed; }
 };
 testing::NiceMock<ReceiptServer> server;
 EXPECT_CALL(server, apply(testing::A<SetStackEffect &>())).WillOnce(testing::Throw(std::runtime_error("rejected effect")));
 EXPECT_THROW(arcaneInspiration::publishCompletedSpell(server, *child, SIDE, {}), std::runtime_error);
 EXPECT_EQ(server.observed, 0u);
 EXPECT_TRUE(child->getArcaneInspirationEffects().empty());
}

TEST_F(ArcaneInspirationAITest, TerminalShadowGiftMaterializesSelectedSacrificeOnceBeforeCompletionMarginal)
{
	ASSERT_NO_FATAL_FAILURE(prepareInspiration("spellshock"));
	const SpellID spell(SpellID::decode("new-horizons:shadowGift"));
	attackerSideHero->addSpellToSpellbook(spell);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:shadowMagic")), 3, ChangeValueMode::ABSOLUTE);
	resetChild();
	const auto world = gameState()->saveToMemory();
	const auto random = projectedHitTest::serialized(*gameHandler->randomizer);
	const auto prepared = child->prepareHeroSpellAllowance(SIDE, spell, false, false);
	ASSERT_TRUE(prepared); ASSERT_TRUE(child->beginProjectedHeroAction(SIDE, *prepared));
	PossibleSpellcast candidate;
	candidate.spell = spell.toSpell(); candidate.dest = {spells::Destination(child->battleGetUnitByID(actor))};
	candidate.spellShadowGiftSacrificePercent = 10;
	uint32_t targetId = actor;
	const auto hp = child->battleGetUnitByID(actor)->getAvailableHealth();
	HypotheticBattle::ProjectedCounterspellOutcome ward;
	ASSERT_TRUE(heroSpellProjection::acceptTerminalEffects(*child, SIDE, candidate, targetId, ward, false, *prepared, {}));
	const auto afterHp = child->battleGetUnitByID(actor)->getAvailableHealth();
	EXPECT_LT(afterHp, hp);
	EXPECT_EQ(child->getWisdomCombatState(SIDE).priorCasts(spell), 1u);
	EXPECT_FALSE(child->prepareHeroSpellAllowance(SIDE, spell, false, false));
	ASSERT_FALSE(child->getArcaneInspirationEffects().empty());
	auto without = std::make_shared<HypotheticBattle>(environment.get(), child);
	without->reverseArcaneInspirationEffects(child->getArcaneInspirationEffects());
	EXPECT_EQ(without->battleGetUnitByID(actor)->getAvailableHealth(), afterHp);
	EXPECT_TRUE(without->battleGetUnitByID(actor)->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell))));
	EXPECT_GT(damage(*child), damage(*without));
	EXPECT_FALSE(heroSpellProjection::acceptTerminalEffects(*child, SIDE, candidate, targetId, ward, false, *prepared, {}));
	EXPECT_EQ(child->battleGetUnitByID(actor)->getAvailableHealth(), afterHp);
	EXPECT_EQ(child->getWisdomCombatState(SIDE).priorCasts(spell), 1u);
	EXPECT_EQ(gameState()->saveToMemory(), world);
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), random);
}

TEST_F(ArcaneInspirationAITest, ExistingFateOutcomeLoopPassesActualFriendlyCasualtiesAndUnscaledCoreBaseline)
{
 ASSERT_NO_FATAL_FAILURE(prepareInspiration("spellshock"));
 const SpellID fate(SpellID::decode("new-horizons:handOfFate"));
 attackerSideHero->addSpellToSpellbook(fate);
 attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:chaosMagic")), 3, ChangeValueMode::ABSOLUTE);
 attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
 const auto world = gameState()->saveToMemory();
 const auto random = projectedHitTest::serialized(*gameHandler->randomizer);
 spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, fate.toSpell());
 auto mechanics = fate.toSpell()->battleMechanics(&cast);
 size_t observations = 0, continuations = 0; bool sawFriendlyLoss = false; float coreOnly = 0;
 const heroSpellProjection::OutcomeValue value = [&](const auto & board, auto injuries, bool completed, bool localOnly)
 {
  EXPECT_FALSE(completed);
  if(injuries.size() == 2)
   sawFriendlyLoss |= board->battleGetUnitByID(actor)->getCount() < battle()->battleGetUnitByID(actor)->getCount();
  arcaneInspiration::publishCompletedSpell(*board->getServerCallback(), *board, SIDE, injuries);
  auto without = std::make_shared<HypotheticBattle>(environment.get(), board);
  without->reverseArcaneInspirationEffects(board->getArcaneInspirationEffects());
  const float marginal = static_cast<float>(damage(*board) - damage(*without));
  ++observations;
  if(localOnly) return 0.0f;
  ++continuations; coreOnly = marginal;
  return marginal;
 };
 const auto expected = SpellTargetEvaluator::handOfFateExpectedDamageValue(mechanics.get(),
  {spells::Destination(battle()->battleGetUnitByID(victim))}, PlayerColor(0), live, environment.get(), value);
 ASSERT_TRUE(expected);
 EXPECT_EQ(continuations, 1u); // No candidate-wide forecast per collateral outcome.
 EXPECT_EQ(observations, 3u); // Existing two-unit pool: core-only, primary-only, one collateral outcome.
 EXPECT_TRUE(sawFriendlyLoss);
 EXPECT_GT(coreOnly, 0);
 EXPECT_GT(expected->inspirationValue, 0);
 EXPECT_LT(expected->inspirationValue, coreOnly);
 EXPECT_EQ(gameState()->saveToMemory(), world);
 EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), random);
}
