/*
 * NewHorizonsHexSnapshotAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"

#include "../server/battles/HeroCommandFixture.h"
#include "../hero/NewHorizonsHeroRulesFixture.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../AI/BattleAI/NewHorizonsHexOfPain.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/battle/NewHorizonsOffense.h"
#include <vcmi/Environment.h>

namespace
{
// A local virtual hook measures this actor's snapshots without a global counter
// or any change to the production forecast/state ownership contract.
class SnapshotCountingStack final : public StackWithBonuses
{
public:
	mutable unsigned snapshotCalls = 0;
	SnapshotCountingStack(const HypotheticBattle * owner, const battle::CUnitState * source)
		: StackWithBonuses(owner, source) {}
	std::shared_ptr<battle::CUnitState> acquireState() const override
	{
		++snapshotCalls;
		return StackWithBonuses::acquireState();
	}
};

class HexSnapshotEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit HexSnapshotEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsHexSnapshotAITest : public HeroCommandFixture
{
protected:
	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, testHeroRules());
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void checkMainAttack(bool withHex, bool evaluateOnly, bool shooting = false, bool withCleave = false)
	{
		ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
		startGame();
		const auto spell = SpellID(SpellID::decode("new-horizons:hexOfPain"));
		defenderSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 0, ChangeValueMode::ABSOLUTE);
		defenderSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		giveArtifact(defenderSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		defenderSideHero->addSpellToSpellbook(spell);
		setTestSpellPointTotal(defenderSideHero, 1000);
		if(withCleave)
		{
			const SecondarySkill offense(SecondarySkill::decode(newHorizonsOffense::SKILL));
			attackerSideHero->setSecSkillLevel(offense, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({newHorizonsOffense::SKILL, "new-horizons:offense.shockAssault"});
			attackerSideHero->setSecSkillLevel(offense, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({newHorizonsOffense::SKILL, newHorizonsOffense::CLEAVE});
			ASSERT_TRUE(attackerSideHero->hasActivePerk(newHorizonsOffense::SKILL, newHorizonsOffense::CLEAVE));
		}
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		auto * actor = addStack(BattleSide::ATTACKER, creatureByName(shooting ? "core:archer" : "core:peasant"), BattleHex(7, 5), 1000);
		auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(shooting ? 12 : 8, 5), withCleave ? 1 : 10000);
		CStack * cleaveTarget = withCleave
			? addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(9, 5), 10000) : nullptr;
		ASSERT_NE(actor, nullptr);
		ASSERT_NE(target, nullptr);
		blockRetaliation(actor);
		forceMaximumDamage(actor);
		// Like the maintained Hex spell fixture, enter a real spell-enabled round.
		// setupBattle alone has not reset the Hero Action allowance for that round.
		battle()->nextRound();
		ASSERT_EQ(battle()->getHeroActionAllowances(BattleSide::DEFENDER).currentRound, battle()->getRound());
		if(withHex)
		{
			ASSERT_TRUE(castOn(defenderSideHero, spell, actor)); // Mechanics-validated real script setup.
		}
		ASSERT_EQ(newHorizonsHexOfPainAI::hasEffect(actor), withHex);

		HexSnapshotEnvironment environment(gameState());
		auto parent = std::make_shared<HypotheticBattle>(&environment,
			std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
		auto child = std::make_shared<HypotheticBattle>(&environment, parent);
		auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
		auto ownedSource = child->getForUpdate(actor->unitId());
		auto countedSource = std::make_shared<SnapshotCountingStack>(child.get(), ownedSource.get());
		std::shared_ptr<StackWithBonuses> source = evaluateOnly ? countedSource : ownedSource;
		auto victim = child->getForUpdate(target->unitId());
		auto siblingSource = sibling->getForUpdate(actor->unitId());
		auto siblingTarget = sibling->getForUpdate(target->unitId());
		const auto actorHP = source->getAvailableHealth();
		const auto targetHP = victim->getAvailableHealth();
		const auto casts = source->castSpellThisTurn;
		const auto attacks = source->getTotalAttacks(false);
		const auto sourceState = source->save();
		const auto targetState = victim->save();
		DamageCache cache;
		cache.buildDamageCache(child, BattleSide::ATTACKER);
		const auto damage = cache.getDamage(source.get(), victim.get(), child);
		ASSERT_GT(damage, 0);
		if(withCleave)
			ASSERT_GE(damage, targetHP);
		else
			ASSERT_LT(damage, targetHP);
		const auto enemyValue = AttackPossibility::calculateDamageReduce(source.get(), victim.get(), damage, cache, child);
		float cleaveValue = 0;
		JsonNode cleaveState;
		if(withCleave)
		{
			ASSERT_TRUE(child->battleCanTriggerCleave(source.get()));
			const auto * selected = child->battleSelectCleaveTarget(source.get(), victim.get());
			ASSERT_NE(selected, nullptr);
			ASSERT_EQ(selected->unitId(), cleaveTarget->unitId());
			auto afterAttack = source->acquireState();
			afterAttack->afterAttack(false, false, true);
			BattleAttackInfo cleaveAttack(afterAttack.get(), cleaveTarget, 0, false);
			cleaveAttack.cleaveDamagePercent = newHorizonsOffense::CLEAVE_DAMAGE_PERCENT;
			const auto cleaveDamage = child->battleExpectedLuckDamage(cleaveAttack);
			cleaveValue = AttackPossibility::calculateDamageReduce(afterAttack.get(), cleaveTarget, cleaveDamage, cache, child);
			ASSERT_GT(cleaveValue, 0);
			cleaveState = cleaveTarget->save();
		}
		int64_t hexDamage = 0;
		float ownValue = 0;
		if(withHex)
		{
			const auto markers = source->getBonuses(Selector::type()(BonusType::COMBAT_EVENT_TRIGGER)
				.And(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell))));
			ASSERT_EQ(markers->size(), 1u);
			ASSERT_EQ(markers->front()->val, 15);
			ASSERT_FALSE(source->hasBonusOfType(BonusType::SPELL_DAMAGE_REDUCTION));
			hexDamage = 15 + damage / 10;
			ASSERT_EQ(defenderSideHero->getSpellBonus(spell.toSpell(), hexDamage, actor), hexDamage);
			ASSERT_LT(hexDamage, actorHP);
			// Independent pre-trigger scoring state: the ordinary strike has finished,
			// but reactive Hex damage has not reduced the attacker's health yet.
			auto beforeHex = source->acquireState();
			beforeHex->afterAttack(shooting, false, true);
			ownValue = AttackPossibility::calculateDamageReduce(nullptr, beforeHex.get(), hexDamage, cache, child);
			ASSERT_GT(ownValue, 0);
		}
		BattleExchangeVariant exchange;
		countedSource->snapshotCalls = 0;
		const auto score = exchange.trackAttack(source, victim, shooting, true, cache, child, evaluateOnly, false);
		EXPECT_FLOAT_EQ(score, enemyValue + cleaveValue - ownValue);
		EXPECT_EQ(countedSource->snapshotCalls, evaluateOnly && (withHex || withCleave) ? 1u : 0u);
		EXPECT_FLOAT_EQ(exchange.getScore().ourDamageReduce, ownValue);
		EXPECT_FLOAT_EQ(exchange.getScore().enemyDamageReduce, evaluateOnly ? 0 : enemyValue);
		EXPECT_EQ(source->getAvailableHealth(), actorHP - (evaluateOnly ? 0 : hexDamage));
		EXPECT_EQ(victim->getAvailableHealth(), targetHP - (evaluateOnly ? 0 : damage));
		EXPECT_EQ(source->castSpellThisTurn, casts);
		if(evaluateOnly)
		{
			EXPECT_EQ(source->getTotalAttacks(false), attacks);
			EXPECT_EQ(source->save(), sourceState);
			EXPECT_EQ(victim->save(), targetState);
		}
		EXPECT_EQ(newHorizonsHexOfPainAI::hasEffect(source.get()), withHex);
		EXPECT_EQ(actor->getAvailableHealth(), actorHP);
		EXPECT_EQ(target->getAvailableHealth(), targetHP);
		EXPECT_EQ(parent->battleGetUnitByID(actor->unitId())->getAvailableHealth(), actorHP);
		EXPECT_EQ(parent->battleGetUnitByID(target->unitId())->getAvailableHealth(), targetHP);
		EXPECT_EQ(siblingSource->getAvailableHealth(), actorHP);
		EXPECT_EQ(siblingTarget->getAvailableHealth(), targetHP);
		if(withCleave)
		{
			EXPECT_EQ(cleaveTarget->save(), cleaveState);
		}
	}
};
}

TEST_F(NewHorizonsHexSnapshotAITest, NoHexProbePreservesScoreAndActionState)
{
	checkMainAttack(false, true);
}

TEST_F(NewHorizonsHexSnapshotAITest, NoHexAppliedAttackPreservesDamageAndScoring)
{
	checkMainAttack(false, false);
}

TEST_F(NewHorizonsHexSnapshotAITest, ActiveHexProbeUsesPreTriggerSnapshotWithoutMutatingBranches)
{
	checkMainAttack(true, true);
}

TEST_F(NewHorizonsHexSnapshotAITest, ActiveHexAppliedAttackRetainsExactDamageAndPreTriggerScoring)
{
	checkMainAttack(true, false);
}

TEST_F(NewHorizonsHexSnapshotAITest, NoHexRangedProbePreservesExactScoreWithoutSnapshot)
{
	checkMainAttack(false, true, true);
}

TEST_F(NewHorizonsHexSnapshotAITest, ActiveHexRangedProbeKeepsPostShotSnapshotAndExactScore)
{
	checkMainAttack(true, true, true);
}

TEST_F(NewHorizonsHexSnapshotAITest, MeleeCleaveProbeKeepsPostAttackSnapshotAndExactSecondaryScore)
{
	checkMainAttack(false, true, false, true);
}
