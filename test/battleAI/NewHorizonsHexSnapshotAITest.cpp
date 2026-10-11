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
#include <vcmi/Environment.h>

namespace
{
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
	}

	void checkMainAttack(bool withHex, bool evaluateOnly)
	{
		ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
		startGame();
		const auto spell = SpellID(SpellID::decode("new-horizons:hexOfPain"));
		defenderSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 0, ChangeValueMode::ABSOLUTE);
		defenderSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		giveArtifact(defenderSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		defenderSideHero->addSpellToSpellbook(spell);
		setTestSpellPointTotal(defenderSideHero, 1000);
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		auto * actor = addStack(BattleSide::ATTACKER, creatureByName("core:peasant"), BattleHex(7, 5), 1000);
		auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(8, 5), 10000);
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
		auto source = child->getForUpdate(actor->unitId());
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
		ASSERT_LT(damage, targetHP);
		const auto enemyValue = AttackPossibility::calculateDamageReduce(source.get(), victim.get(), damage, cache, child);
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
			beforeHex->afterAttack(false, false, true);
			ownValue = AttackPossibility::calculateDamageReduce(nullptr, beforeHex.get(), hexDamage, cache, child);
			ASSERT_GT(ownValue, 0);
		}
		BattleExchangeVariant exchange;
		const auto score = exchange.trackAttack(source, victim, false, true, cache, child, evaluateOnly, false);
		EXPECT_FLOAT_EQ(score, enemyValue - ownValue);
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
