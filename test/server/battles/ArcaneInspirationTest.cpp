/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/battle/ArcaneInspiration.h"
#include "../../../lib/battle/ArcaneReprisal.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/MagicalCreatureAttackDamage.h"
#include "../../../lib/battle/StackEffectRemoval.h"
#include "../../../lib/bonuses/BonusList.h"
#include "../../../lib/bonuses/RecipientBonusOverlay.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/ArcaneInspirationCompletion.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/ObstacleCasterProxy.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../server/battles/BattleActivationEffects.h"
#include <gtest/gtest.h>

TEST(ArcaneInspiration, AllRanksAndGrandPercentagePoints)
{
	EXPECT_EQ(arcaneInspiration::corePercent(0, true), 0);
	for(int rank = 1; rank <= 3; ++rank)
	{
		EXPECT_EQ(arcaneInspiration::corePercent(rank, false), 5 * rank);
		EXPECT_EQ(arcaneInspiration::corePercent(rank, true), 5 * rank + 10);
	}
}
TEST(ArcaneInspiration, MarkersRoundtripAndCarryHasNoRiders)
{
	const auto active = arcaneInspiration::activeMarker({15, 127, true}, PlayerColor(0), 2);
	const auto retained = arcaneInspiration::retainedMarker(active);
	EXPECT_EQ(retained.val, 15);
	EXPECT_EQ(retained.subtype.getNum(), 0);
	EXPECT_EQ(retained.duration, BonusDuration::UNTIL_ATTACK);
	EXPECT_TRUE(recipientBonusOverlay::isSnapshotHeroSpecialEffect(&active));
	EXPECT_TRUE(recipientBonusOverlay::isSnapshotHeroSpecialEffect(&retained));
	CMemorySerializer wire;
	wire.oser.version = ESerializationVersion::CURRENT;
	wire.iser.version = ESerializationVersion::CURRENT;
	std::vector<Bonus> originals{active, retained, arcaneInspiration::spellshockMarker(PlayerColor(0), 2),
		arcaneInspiration::valorMarker(PlayerColor(0), 2)};
	BonusList morale;
	morale.push_back(std::make_shared<Bonus>(originals.back()));
	EXPECT_EQ(morale.totalValue(), 1);
	wire.oser & originals;
	std::vector<Bonus> restored;
	wire.iser & restored;
	ASSERT_EQ(restored.size(), originals.size());
	for(size_t i = 0; i < restored.size(); ++i)
	{
		EXPECT_TRUE(arcaneInspiration::isEffect(&restored[i]));
		EXPECT_EQ(restored[i].valType, BonusValueType::BASE_NUMBER);
		auto malformed = restored[i];
		malformed.valType = BonusValueType::ADDITIVE_VALUE;
		EXPECT_THROW(arcaneInspiration::isEffect(&malformed), std::runtime_error);
		EXPECT_EQ(restored[i].stacking, originals[i].stacking);
		EXPECT_EQ(restored[i].subtype, originals[i].subtype);
		EXPECT_EQ(restored[i].turnsRemain, originals[i].turnsRemain);
	}
}
TEST(ArcaneInspiration, RemovalDoesNotSelectAnotherOwnersOrUnrelatedHeroBonus)
{
	const auto active = arcaneInspiration::activeMarker({10, 0, false}, PlayerColor(0), 1);
	auto otherOwner = active;
	otherOwner.spellCasterOwner = PlayerColor(1);
	EXPECT_FALSE(battle::matchesStackEffectRemoval(active, otherOwner));
	auto unrelated = active;
	unrelated.stacking = "unrelated";
	EXPECT_FALSE(battle::matchesStackEffectRemoval(active, unrelated));
	EXPECT_TRUE(battle::matchesStackEffectRemoval(active, active));
}
TEST(ArcaneInspiration, ReprisalUsesPreCasualtyInflictedDamageWithoutHealthClamp)
{
	EXPECT_EQ(arcaneInspiration::reprisalRawDamage(999), 99);
	EXPECT_EQ(arcaneInspiration::reprisalRawDamage(9), 0);
	EXPECT_EQ(arcaneInspiration::reprisalRawDamage(-100), 0);
}

class ArcaneInspirationBattleTest : public HeroCommandFixture
{
protected:
	CStack * actor = nullptr;
	CStack * victim = nullptr;
	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}
	void prepare(int rank = 2)
	{
		startGame();
		const auto skill = SecondarySkill(SecondarySkill::decode(arcaneInspiration::SKILL));
		ASSERT_TRUE(skill.hasValue());
		attackerSideHero->setSecSkillLevel(skill, rank, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 1000, ChangeValueMode::ABSOLUTE);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		for(const auto spell : {SpellID::BLESS, SpellID::MAGIC_ARROW}) attackerSideHero->addSpellToSpellbook(spell);
		setTestSpellPointTotal(attackerSideHero, 1000);
		startBattle();
		actor = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 20);
		victim = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 1000);
		beginCombat();
		activate(actor);
	}
	void activate(const CStack * unit)
	{
		BattleSetActiveStack packet;
		packet.battleID = BattleID(0);
		packet.stack = unit->unitId();
		packet.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(packet);
	}
	void learnSpellshock()
	{
		attackerSideHero->applyPerkSelection({arcaneInspiration::SKILL, "new-horizons:warcasting.spellshock"});
		ASSERT_TRUE(attackerSideHero->hasActivePerk(arcaneInspiration::SKILL, "new-horizons:warcasting.spellshock"));
	}
	SpellCreatedObstacle delayedWall(bool heroOrigin)
	{
		const auto * sourceSpell = SpellID(SpellID::FIRE_WALL).toSpell();
		spells::BattleCast source(battle(), heroOrigin ? static_cast<const spells::Caster *>(attackerSideHero)
			: static_cast<const spells::Caster *>(actor), heroOrigin ? spells::Mode::HERO : spells::Mode::CREATURE_ACTIVE, sourceSpell);
		const auto sourceMechanics = sourceSpell->battleMechanics(&source);
		SpellCreatedObstacle obstacle;
		obstacle.ID = SpellID::FIRE_WALL;
		obstacle.trigger = SpellID(SpellID::decode("core:fireWallTrigger"));
		obstacle.casterSide = BattleSide::ATTACKER;
		obstacle.damageSnapshot = true;
		obstacle.minimalDamage = 100;
		obstacle.spellLevel = 3;
		obstacle.capturedMdrPenetration = sourceMechanics->getCapturedMdrPenetration();
		return obstacle;
	}
	void triggerWall(SpellCreatedObstacle & obstacle, CStack * target)
	{
		spells::ObstacleCasterProxy proxy(PlayerColor(0), attackerSideHero, obstacle);
		spells::BattleCast trigger(battle(), &proxy, spells::Mode::PASSIVE, obstacle.trigger.toSpell());
		trigger.cast(gameHandler->spellEnv.get(), battle::Target{spells::Destination(target)});
	}
	std::vector<std::byte> savedRandomizer() const
	{
		CMemorySerializer wire;
		wire.oser & *gameHandler->randomizer;
		return wire.extractBuffer();
	}
	void restoreWallBaseline(const JsonNode & state, const std::vector<std::byte> & randomizer)
	{
		BattleUnitsChanged restore;
		restore.battleID = BattleID(0);
		restore.changedStacks.emplace_back(victim->unitId(), UnitChanges::EOperation::UPDATE);
		restore.changedStacks.back().data = state;
		gameHandler->sendAndApply(restore);
		CMemorySerializer wire(randomizer);
		wire.iser & *gameHandler->randomizer;
	}
	void inspire(CStack * unit, arcaneInspiration::Profile policy)
	{
		SetStackEffect packet;
		packet.battleID = BattleID(0);
		const auto marker = arcaneInspiration::activeMarker(policy, battle()->battleGetOwner(unit), battle()->getRound());
		packet.toAdd.emplace_back(unit->unitId(), std::vector<Bonus>{marker});
		if(policy.riders & arcaneInspiration::LINGERING)
			packet.toAdd.emplace_back(unit->unitId(), std::vector<Bonus>{arcaneInspiration::retainedMarker(marker)});
		gameHandler->sendAndApply(packet);
	}
};
TEST_F(ArcaneInspirationBattleTest, FriendlySuccessfulCastInspiresButDoesNotGrantAnotherHeroAction)
{
	prepare(1);
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::BLESS, actor));
	EXPECT_EQ(arcaneInspiration::activeProfile(*actor, PlayerColor(0)).corePercent, 5);
	EXPECT_EQ(arcaneInspiration::activeProfile(*victim, PlayerColor(1)).corePercent, 0);
	EXPECT_FALSE(castOn(attackerSideHero, SpellID::MAGIC_ARROW, victim));
	EXPECT_FALSE(issue(HeroCommand::CHARGE));
	advanceRound();
	EXPECT_FALSE(actor->hasBonus(CSelector(arcaneInspiration::isActive)));
}
TEST_F(ArcaneInspirationBattleTest, HostileSuccessfulCastInspiresAndIllegalCastDoesNot)
{
	prepare(3);
	EXPECT_FALSE(castOn(attackerSideHero, SpellID::MAGIC_ARROW, actor));
	EXPECT_FALSE(actor->hasBonus(CSelector(arcaneInspiration::isActive)));
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::MAGIC_ARROW, victim));
	EXPECT_EQ(arcaneInspiration::activeProfile(*actor, PlayerColor(0)).corePercent, 15);
}
TEST_F(ArcaneInspirationBattleTest, CompletionRefreshDoesNotStackAndIncludesNewFriendlyBodies)
{
	prepare();
	arcaneInspiration::publishCompletedSpell(*gameHandler->spellEnv, *battle(), BattleSide::ATTACKER, {});
	const auto * newcomer = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex - 2), 2);
	EXPECT_FALSE(newcomer->hasBonus(CSelector(arcaneInspiration::isActive)));
	arcaneInspiration::publishCompletedSpell(*gameHandler->spellEnv, *battle(), BattleSide::ATTACKER, {});
	EXPECT_EQ(actor->getBonuses(CSelector(arcaneInspiration::isActive))->size(), 1u);
	EXPECT_EQ(newcomer->getBonuses(CSelector(arcaneInspiration::isActive))->size(), 1u);
}
TEST_F(ArcaneInspirationBattleTest, FocusIsAdditiveOnlyForRetaliationAndOtherRidersRequireActiveState)
{
	prepare();
	inspire(actor, {10, arcaneInspiration::FOCUS | arcaneInspiration::WEAPONS | arcaneInspiration::SUPREMACY, false});
	BattleAttackInfo attackInfo(actor, victim, 0, false);
	auto plan = arcaneInspiration::attackPlan(*battle(), attackInfo);
	EXPECT_EQ(plan.damageBonusPercent, 10);
	EXPECT_EQ(plan.defenseIgnorePercent, 15);
	EXPECT_EQ(plan.protectionIgnorePercent, 20);
	attackInfo.retaliation = true;
	EXPECT_EQ(arcaneInspiration::attackPlan(*battle(), attackInfo).damageBonusPercent, 25);
	advanceRound();
	EXPECT_EQ(arcaneInspiration::attackPlan(*battle(), attackInfo).damageBonusPercent, 0);
}
TEST_F(ArcaneInspirationBattleTest, LingeringNextStrikeSpendsChargeEvenWhileActiveAndDoesNotRearmAtExpiry)
{
	prepare();
	inspire(actor, {10, arcaneInspiration::LINGERING, false});
	blockRetaliation(victim);
	ASSERT_TRUE(attack(actor, victim->getPosition()));
	EXPECT_FALSE(actor->hasBonus(CSelector(arcaneInspiration::isRetained)));
	EXPECT_TRUE(actor->hasBonus(CSelector(arcaneInspiration::isActive)));
	advanceRound();
	EXPECT_FALSE(actor->hasBonus(CSelector(arcaneInspiration::isEffect)));
}
TEST_F(ArcaneInspirationBattleTest, UnspentCarrySurvivesExpiryWithCoreOnly)
{
	prepare();
	inspire(actor, {10, arcaneInspiration::LINGERING | arcaneInspiration::FOCUS | arcaneInspiration::WEAPONS, false});
	advanceRound();
	BattleAttackInfo attackInfo(actor, victim, 0, false);
	attackInfo.retaliation = true;
	const auto plan = arcaneInspiration::attackPlan(*battle(), attackInfo);
	EXPECT_EQ(plan.damageBonusPercent, 10);
	EXPECT_EQ(plan.defenseIgnorePercent, 0);
	EXPECT_EQ(plan.protectionIgnorePercent, 0);
	EXPECT_FALSE(actor->hasBonus(CSelector(arcaneInspiration::isActive)));
	EXPECT_TRUE(actor->hasBonus(CSelector(arcaneInspiration::isRetained)));
}
TEST_F(ArcaneInspirationBattleTest, SpellshockRequiresPositiveDirectEnemyHeroDamageAndRefreshes)
{
	prepare();
	// Supply a legal captured profile through the actual active-perk offer path.
	const auto rankLookup = [this](const std::string & skill) { return attackerSideHero->getPerkSkillRank(skill); };
	bool learned = false;
	for(uint64_t seed = 0; seed < 4096 && !learned; ++seed)
	{
		const auto offers = attackerSideHero->getPerkState().prepareOffer(rankLookup, seed, attackerSideHero->getPerkOfferWeightContext());
		for(size_t i = 0; i < offers.size(); ++i)
			if(offers[i].selection.perkId == "new-horizons:warcasting.spellshock")
			{
				gameHandler->levelUpHero(attackerSideHero, offers, i, seed, false);
				learned = true;
				break;
			}
	}
	ASSERT_TRUE(learned);
	BattleStackAttacked injury;
	injury.stackAttacked = victim->unitId();
	injury.damageAmount = 0;
	arcaneInspiration::publishCompletedSpell(*gameHandler->spellEnv, *battle(), BattleSide::ATTACKER, std::span(&injury, 1));
	EXPECT_FALSE(victim->hasBonus(CSelector(arcaneInspiration::isSpellshock)));
	injury.damageAmount = 20;
	for(int i = 0; i < 2; ++i)
		arcaneInspiration::publishCompletedSpell(*gameHandler->spellEnv, *battle(), BattleSide::ATTACKER, std::span(&injury, 1));
	EXPECT_EQ(victim->getBonuses(CSelector(arcaneInspiration::isSpellshock))->size(), 1u);
	BattleAttackInfo enemyAttack(victim, actor, 0, false);
	EXPECT_EQ(arcaneInspiration::attackPlan(*battle(), enemyAttack).outputPenaltyPercent, 10);
	advanceRound();
	EXPECT_FALSE(victim->hasBonus(CSelector(arcaneInspiration::isSpellshock)));
}
TEST_F(ArcaneInspirationBattleTest, ReprisalIsCapturedBeforeLethalCasualtiesAndDoesNotRequireRetaliation)
{
	prepare();
	inspire(actor, {10, arcaneInspiration::REPRISAL, false});
	blockRetaliation(actor);
	BattleAttackInfo enemyAttack(victim, actor, 0, false);
	const auto receipt = arcaneInspiration::captureReprisal(*battle(), enemyAttack, 100000);
	ASSERT_TRUE(receipt);
	EXPECT_EQ(receipt->rawDamage, 10000);
	EXPECT_EQ(receipt->source, actor->unitId());
	EXPECT_EQ(receipt->target, victim->unitId());
	advanceRound();
	EXPECT_FALSE(arcaneInspiration::captureReprisal(*battle(), enemyAttack, 100000));
}
TEST_F(ArcaneInspirationBattleTest, ValorUsesNativeMoraleAndUndeadImmunityAndExpiresAtRoundStart)
{
	prepare();
	const auto * undead = addStack(BattleSide::ATTACKER, creatureByName("core:skeleton"), BattleHex(leftHex - 3), 10);
	const int ordinaryBefore = battle()->battleGetMorale(actor);
	const int undeadBefore = battle()->battleGetMorale(undead);
	SetStackEffect packet;
	packet.battleID = BattleID(0);
	for(const auto * unit : {static_cast<const CStack *>(actor), undead})
		packet.toAdd.emplace_back(unit->unitId(), std::vector<Bonus>{arcaneInspiration::valorMarker(PlayerColor(0), battle()->getRound())});
	gameHandler->sendAndApply(packet);
	EXPECT_EQ(battle()->battleGetMorale(actor), ordinaryBefore + 1);
	EXPECT_EQ(battle()->battleGetMorale(undead), undeadBefore);
	advanceRound();
	EXPECT_EQ(battle()->battleGetMorale(actor), ordinaryBefore);
}
TEST_F(ArcaneInspirationBattleTest, BulwarkIsPhysicalProtectionAndDoesNotModifyCreatureDefense)
{
	prepare();
	BattleAttackInfo incoming(victim, actor, 0, false);
	const auto before = actor->getDefense(false);
	inspire(actor, {10, arcaneInspiration::BULWARK, false});
	EXPECT_EQ(arcaneInspiration::attackPlan(*battle(), incoming).physicalReductionPercent, 10);
	EXPECT_EQ(actor->getDefense(false), before);
	incoming.physicalDamage = false;
	EXPECT_EQ(arcaneInspiration::attackPlan(*battle(), incoming).physicalReductionPercent, 0);
}
TEST_F(ArcaneInspirationBattleTest, MagicalNormalAttacksReceiveCoreAndDefenseAndProtectionPenetration)
{
	prepare();
	inspire(actor, {10, arcaneInspiration::WEAPONS | arcaneInspiration::SUPREMACY, false});
	BattleAttackInfo magicalShot(actor, victim, 0, true);
	magicalShot.physicalDamage = false;
	const auto plan = arcaneInspiration::attackPlan(*battle(), magicalShot);
	EXPECT_EQ(plan.damageBonusPercent, 10);
	EXPECT_EQ(plan.defenseIgnorePercent, 15);
	EXPECT_EQ(plan.protectionIgnorePercent, 20);
}
TEST_F(ArcaneInspirationBattleTest, LethalIncomingNativeAttackStillPublishesOneNonrecursiveMagicalReprisal)
{
	prepare();
	inspire(actor, {10, arcaneInspiration::REPRISAL, false});
	inspire(victim, {10, arcaneInspiration::REPRISAL, false});
	blockRetaliation(actor);
	forceMaximumDamage(victim);
	activate(victim);
	const auto injuriesBefore = server.injuries.size();
	ASSERT_TRUE(attack(victim, actor->getPosition()));
	ASSERT_FALSE(actor->alive());
	ASSERT_EQ(server.injuries.size(), injuriesBefore + 1);
	const auto & injury = server.injuries.back();
	ASSERT_EQ(injury.stacks.size(), 1u);
	EXPECT_EQ(injury.stacks.front().attackerID, actor->unitId());
	EXPECT_EQ(injury.stacks.front().stackAttacked, victim->unitId());
	EXPECT_GT(injury.stacks.front().damageAmount, 0);
	EXPECT_TRUE(injury.stacks.front().flags & BattleStackAttacked::SECONDARY);
}
TEST_F(ArcaneInspirationBattleTest, NativeReceiptExcludesFullAndPartialShieldsButKeepsPreCasualtyLethalDamage)
{
	prepare();
	for(const auto & [incoming, shield, expected] : {
		std::tuple<int64_t, int64_t, int64_t>{150, 200, 0}, {150, 50, 100}, {100000, 50, 99950}})
	{
		auto state = actor->acquireState();
		state->arcaneShieldHitPoints = shield;
		int64_t damage = incoming;
		int64_t receipt = -1;
		state->damage(damage, false, battle::DamageProvenance::PHYSICAL_CREATURE, &receipt);
		EXPECT_EQ(receipt, expected);
		EXPECT_EQ(arcaneInspiration::reprisalRawDamage(receipt), expected / 10);
		if(incoming == 100000)
		{
			EXPECT_FALSE(state->alive());
			EXPECT_GT(receipt, actor->getAvailableHealth());
		}
	}
	auto layered = actor->acquireState();
	layered->arcaneShieldHitPoints = 50;
	layered->guardianSpiritHitPoints = 70;
	layered->guardianSpiritRoundsRemaining = 2;
	int64_t damage = 200;
	int64_t receipt = -1;
	layered->damage(damage, false, battle::DamageProvenance::PHYSICAL_CREATURE, &receipt);
	EXPECT_EQ(receipt, 80);
}
TEST_F(ArcaneInspirationBattleTest, InvincibleTargetsNeitherTriggerNorReceiveReprisal)
{
	prepare();
	inspire(actor, {10, arcaneInspiration::REPRISAL, false});
	SetStackEffect immunity;
	immunity.battleID = BattleID(0);
	immunity.toAdd.emplace_back(actor->unitId(), std::vector<Bonus>{
		Bonus(BonusDuration::ONE_BATTLE, BonusType::INVINCIBLE, BonusSource::OTHER, 1, BonusSourceID())});
	gameHandler->sendAndApply(immunity);
	BattleAttackInfo incoming(victim, actor, 0, false);
	EXPECT_FALSE(arcaneInspiration::captureReprisal(*battle(), incoming, 10000));
	EXPECT_EQ(arcaneInspiration::reprisalDamage(*battle(), *actor, 10000), 0);
}

TEST_F(ArcaneInspirationBattleTest, NativeDelayedHeroWallShocksWithoutCoreHistoryActionOrRngChanges)
{
	prepare(1);
	learnSpellshock();
	auto obstacle = delayedWall(true);
	ASSERT_EQ(obstacle.capturedMdrPenetration["sourceHeroCasterId"].Integer(), attackerSideHero->id.getNum());
	CMemorySerializer wire;
	wire.oser & obstacle;
	SpellCreatedObstacle restored;
	wire.iser & restored;
	EXPECT_EQ(restored.capturedMdrPenetration, obstacle.capturedMdrPenetration);
	obstacle = restored;
	const auto history = battle()->getWisdomCombatState(BattleSide::ATTACKER);
	const auto points = battle()->getMetamagicPointState(BattleSide::ATTACKER);
	const auto actions = battle()->getHeroActionAllowances(BattleSide::ATTACKER);
	const auto health = victim->getAvailableHealth();
	const auto stateBefore = victim->acquireState()->save();
	const auto randomizerBefore = savedRandomizer();
	// Keep the exact damage snapshot, caster delegate and layout; only the
	// unowned baseline lacks the original-side receipt required for Spellshock.
	auto baseline = obstacle;
	baseline.casterSide = BattleSide::NONE;
	triggerWall(baseline, victim);
	const auto baselineHealth = victim->getAvailableHealth();
	const auto baselineRandomizer = savedRandomizer();
	EXPECT_LT(baselineHealth, health);
	EXPECT_FALSE(victim->hasBonus(CSelector(arcaneInspiration::isSpellshock)));
	EXPECT_NE(baselineRandomizer, randomizerBefore); // Canonical resistance draws remain native.
	restoreWallBaseline(stateBefore, randomizerBefore);
	ASSERT_EQ(victim->acquireState()->save(), stateBefore);
	ASSERT_EQ(savedRandomizer(), randomizerBefore);
	const auto casts = server.casts.size();
	triggerWall(obstacle, victim);
	EXPECT_EQ(victim->getAvailableHealth(), baselineHealth);
	EXPECT_TRUE(victim->hasBonus(CSelector(arcaneInspiration::isSpellshock)));
	EXPECT_FALSE(actor->hasBonus(CSelector(arcaneInspiration::isActive)));
	EXPECT_EQ(battle()->getWisdomCombatState(BattleSide::ATTACKER).completedCasts, history.completedCasts);
	EXPECT_EQ(battle()->getWisdomCombatState(BattleSide::ATTACKER).lastCastRound, history.lastCastRound);
	EXPECT_EQ(battle()->getMetamagicPointState(BattleSide::ATTACKER), points);
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER), actions);
	ASSERT_EQ(server.casts.size(), casts + 1);
	EXPECT_FALSE(server.casts.back().announcement.castByHero);
	EXPECT_FALSE(server.casts.back().announcement.activeCast);
	EXPECT_EQ(savedRandomizer(), baselineRandomizer);
	// The delayed publisher itself refreshes only Spellshock: no damage or RNG.
	spells::ObstacleCasterProxy proxy(PlayerColor(0), attackerSideHero, obstacle);
	spells::BattleCast completion(battle(), &proxy, spells::Mode::PASSIVE, obstacle.trigger.toSpell());
	const auto mechanics = obstacle.trigger.toSpell()->battleMechanics(&completion);
	BattleStackAttacked receipt;
	receipt.stackAttacked = victim->unitId();
	receipt.damageAmount = health - baselineHealth;
	arcaneInspiration::publishDelayedSpellDamage(*gameHandler->spellEnv, *battle(), *mechanics, std::span(&receipt, 1));
	EXPECT_EQ(savedRandomizer(), baselineRandomizer);
	EXPECT_EQ(victim->getAvailableHealth(), baselineHealth);
	advanceRound();
	EXPECT_FALSE(victim->hasBonus(CSelector(arcaneInspiration::isSpellshock)));
}
TEST_F(ArcaneInspirationBattleTest, NativeCreatureAndUnownedWallNeverInferHeroOriginFromSamePlayerProxy)
{
	prepare(1);
	learnSpellshock();
	auto creatureWall = delayedWall(false);
	EXPECT_TRUE(creatureWall.capturedMdrPenetration["sourceHeroCasterId"].isNull());
	triggerWall(creatureWall, victim);
	EXPECT_FALSE(victim->hasBonus(CSelector(arcaneInspiration::isSpellshock)));
	auto unownedWall = delayedWall(true);
	unownedWall.casterSide = BattleSide::NONE;
	triggerWall(unownedWall, victim);
	EXPECT_FALSE(victim->hasBonus(CSelector(arcaneInspiration::isSpellshock)));
	auto wrongHero = delayedWall(true);
	wrongHero.capturedMdrPenetration["sourceHeroCasterId"].Integer() = defenderSideHero->id.getNum();
	triggerWall(wrongHero, victim);
	EXPECT_FALSE(victim->hasBonus(CSelector(arcaneInspiration::isSpellshock)));
	EXPECT_FALSE(actor->hasBonus(CSelector(arcaneInspiration::isActive)));
}
TEST_F(ArcaneInspirationBattleTest, HeroDamageTriggerExcludesFriendlySelfZeroAndInvincibleRecipients)
{
	prepare(1);
	learnSpellshock();
	BattleStackAttacked hit;
	hit.damageAmount = 20;
	hit.stackAttacked = actor->unitId();
	arcaneInspiration::publishSpellDamage(*gameHandler->spellEnv, *battle(), BattleSide::ATTACKER, std::span(&hit, 1));
	EXPECT_FALSE(actor->hasBonus(CSelector(arcaneInspiration::isSpellshock)));
	hit.stackAttacked = victim->unitId();
	hit.damageAmount = 0;
	arcaneInspiration::publishSpellDamage(*gameHandler->spellEnv, *battle(), BattleSide::ATTACKER, std::span(&hit, 1));
	EXPECT_FALSE(victim->hasBonus(CSelector(arcaneInspiration::isSpellshock)));
	victim->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::INVINCIBLE,
		BonusSource::OTHER, 1, BonusSourceID()));
	hit.damageAmount = 20;
	arcaneInspiration::publishSpellDamage(*gameHandler->spellEnv, *battle(), BattleSide::ATTACKER, std::span(&hit, 1));
	EXPECT_FALSE(victim->hasBonus(CSelector(arcaneInspiration::isSpellshock)));
}
TEST_F(ArcaneInspirationBattleTest, NativeSpellPoisonTicksShockButCreaturePoisonTicksDoNotRefreshCore)
{
	prepare(1);
	learnSpellshock();
	for(const auto origin : {BattleSide::ATTACKER, BattleSide::NONE})
	{
		auto state = victim->acquireState();
		state->physicalPoisonBaseDamage = 20;
		state->physicalPoisonActivationsRemaining = 1;
		state->physicalPoisonSpellSide = origin;
		UnitChanges change(victim->unitId(), UnitChanges::EOperation::UPDATE);
		change.data = state->save();
		BattleUnitsChanged update;
		update.battleID = BattleID(0);
		update.changedStacks.push_back(std::move(change));
		gameHandler->sendAndApply(update);
		const auto history = battle()->getWisdomCombatState(BattleSide::ATTACKER).completedCasts;
		const auto actions = battle()->getHeroActionAllowances(BattleSide::ATTACKER);
		battleActivationEffects::apply(gameHandler.get(), *battle(), victim, BattleUnitTurnReason::TURN_QUEUE);
		EXPECT_EQ(victim->hasBonus(CSelector(arcaneInspiration::isSpellshock)), origin == BattleSide::ATTACKER);
		EXPECT_FALSE(actor->hasBonus(CSelector(arcaneInspiration::isActive)));
		EXPECT_EQ(battle()->getWisdomCombatState(BattleSide::ATTACKER).completedCasts, history);
		EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER), actions);
		advanceRound();
	}
}
TEST_F(ArcaneInspirationBattleTest, NativeShieldedDelayedHeroWallDoesNotShockWithoutHpDamage)
{
	prepare(1);
	learnSpellshock();
	auto obstacle = delayedWall(true);
	auto state = victim->acquireState();
	state->arcaneShieldHitPoints = 10000;
	UnitChanges change(victim->unitId(), UnitChanges::EOperation::UPDATE);
	change.data = state->save();
	BattleUnitsChanged update;
	update.battleID = BattleID(0);
	update.changedStacks.push_back(std::move(change));
	gameHandler->sendAndApply(update);
	const auto health = victim->getAvailableHealth();
	triggerWall(obstacle, victim);
	EXPECT_EQ(victim->getAvailableHealth(), health);
	EXPECT_FALSE(victim->hasBonus(CSelector(arcaneInspiration::isSpellshock)));
	EXPECT_FALSE(actor->hasBonus(CSelector(arcaneInspiration::isActive)));
}
TEST_F(ArcaneInspirationBattleTest, NativeFriendlyDelayedHeroWallCanDamageButNeverSpellshockItsOwnArmy)
{
	prepare(1);
	learnSpellshock();
	auto obstacle = delayedWall(true);
	const auto health = actor->getAvailableHealth();
	triggerWall(obstacle, actor);
	EXPECT_LT(actor->getAvailableHealth(), health);
	EXPECT_FALSE(actor->hasBonus(CSelector(arcaneInspiration::isSpellshock)));
	EXPECT_FALSE(actor->hasBonus(CSelector(arcaneInspiration::isActive)));
}

TEST_F(ArcaneInspirationBattleTest, AuthoritativeAttackPlanFeedsRealLuaDamageAndBeforeDefenseStages)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	for(auto * hero : {attackerSideHero, defenderSideHero})
	{
		hero->setPrimarySkill(PrimarySkill::ATTACK, 0, ChangeValueMode::ABSOLUTE);
		hero->setPrimarySkill(PrimarySkill::DEFENSE, 0, ChangeValueMode::ABSOLUTE);
	}
	battle()->terrainType = TerrainId(ETerrainId::SAND);
	battle()->nodeHasChanged();
	const auto makeAngel = [this](BattleSide side, BattleHex hex, int count)
	{
		return addStack(side, creatureByName("core:angel"), hex, count);
	};
	const auto expectDamage = [](const DamageEstimation & result, int64_t damage, int64_t beforeDefense)
	{
		EXPECT_EQ(result.damage.min, damage);
		EXPECT_EQ(result.damage.max, damage);
		EXPECT_EQ(result.damageBeforeDefense.min, beforeDefense);
		EXPECT_EQ(result.damageBeforeDefense.max, beforeDefense);
	};
	auto * coreActor = makeAngel(BattleSide::ATTACKER, BattleHex{6, 3}, 100);
	auto * coreTarget = makeAngel(BattleSide::DEFENDER, BattleHex{7, 3}, 1000);
	ASSERT_EQ(coreActor->getAttack(false), 20);
	ASSERT_EQ(coreTarget->getDefense(false), 20);
	BattleAttackInfo coreAttack(coreActor, coreTarget, 0, false);
	expectDamage(battle()->calculateDmgRange(coreAttack), 5000, 5000);
	inspire(coreActor, {15, 0, false});
	SetStackEffect shock;
	shock.battleID = BattleID(0);
	shock.toAdd.emplace_back(coreActor->unitId(), std::vector<Bonus>{
		arcaneInspiration::spellshockMarker(PlayerColor(1), battle()->getRound())});
	gameHandler->sendAndApply(shock);
	const auto core = arcaneInspiration::attackPlan(*battle(), coreAttack);
	ASSERT_EQ(core.damageBonusPercent, 15);
	ASSERT_EQ(core.outputPenaltyPercent, 10);
	// 5000 * 1.15 * 0.90; the before-defense basis must use the same product.
	expectDamage(battle()->calculateDmgRange(coreAttack), 5175, 5175);

	auto * weaponActor = makeAngel(BattleSide::ATTACKER, BattleHex{8, 4}, 100);
	auto * weaponTarget = makeAngel(BattleSide::DEFENDER, BattleHex{9, 4}, 1000);
	BattleAttackInfo weaponAttack(weaponActor, weaponTarget, 0, false);
	inspire(weaponActor, {5, arcaneInspiration::WEAPONS, false});
	ASSERT_EQ(arcaneInspiration::attackPlan(*battle(), weaponAttack).defenseIgnorePercent, 15);
	// Ignore floor(20 * 15%) = 3 target Defense, then apply the 5% core.
	expectDamage(battle()->calculateDmgRange(weaponAttack), 6037, 6037);
	weaponActor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::IN_FRENZY,
		BonusSource::OTHER, 100, BonusSourceID()));
	// Frenzy still trades all 20 own Defense: (40 - 17) * 5% + 1, times core1.05.
	expectDamage(battle()->calculateDmgRange(weaponAttack), 11287, 11287);

	auto * bulwarkActor = makeAngel(BattleSide::ATTACKER, BattleHex{6, 7}, 100);
	auto * bulwarkTarget = makeAngel(BattleSide::DEFENDER, BattleHex{7, 7}, 1000);
	BattleAttackInfo physical(bulwarkActor, bulwarkTarget, 0, false);
	inspire(bulwarkTarget, {5, arcaneInspiration::BULWARK, false});
	ASSERT_EQ(arcaneInspiration::attackPlan(*battle(), physical).physicalReductionPercent, 10);
	expectDamage(battle()->calculateDmgRange(physical), 4500, 5000);
	auto magical = physical;
	magical.physicalDamage = false;
	ASSERT_EQ(arcaneInspiration::attackPlan(*battle(), magical).physicalReductionPercent, 0);
	expectDamage(battle()->calculateDmgRange(magical), 5000, 5000);
}
