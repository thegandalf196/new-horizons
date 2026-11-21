/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "MetamagicDoubleCastFixture.h"
#include "../../../lib/CStack.h"
#include "../../../lib/battle/UnitShieldState.h"
#include "../../../lib/bonuses/BonusList.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/SpellCostBreakdown.h"
#include <algorithm>

using namespace metamagicDoubleCastTest;

namespace
{
class MetamagicDoubleCastPerkTest : public MetamagicDoubleCastTest
{
protected:
	battle::Units stacks(BattleSide side) const
	{
		return battle()->battleGetUnitsIf([side](const battle::Unit * unit)
		{
			return unit->unitSide() == side && unit->alive() && !unit->isGhost() && !unit->isTurret();
		});
	}

	const battle::Unit * enemy() const
	{
		const battle::Unit * result = nullptr;
		for(const auto * unit : battle()->battleGetAllStacks(false))
			if(unit->unitSide() == BattleSide::DEFENDER && unit->alive())
				result = unit;
		return result;
	}

	void doubleCast()
	{
		const auto completed = state().completedDoubleCasts;
		ASSERT_TRUE(cast(SpellID::BLESS));
		ASSERT_TRUE(offered());
		ASSERT_TRUE(cast(SpellID::CURSE));
		ASSERT_EQ(state().completedDoubleCasts, completed + 1);
	}

	static int turns(const battle::Unit * unit, SpellID spell)
	{
		int result = 0;
		const auto bonuses = unit->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell)));
		for(const auto & bonus : *bonuses)
			result = std::max<int>(result, bonus->turnsRemain);
		return result;
	}

	int32_t effectPower(SpellID spell) const
	{
		spells::BattleCast probe(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
		return spell.toSpell()->battleMechanics(&probe)->getEffectPower();
	}

	int32_t cost(SpellID spell) const
	{
		return battle()->battleGetSpellCost(spell.toSpell(), attackerSideHero);
	}

	/// Hostile, temporary, magical Slow and Weakness on every friendly stack.
	void afflictFriendlyStacks()
	{
		SetStackEffect packet;
		packet.battleID = battle()->getBattleID();
		for(const auto * unit : stacks(ATTACKER))
		{
			Bonus slow(BonusDuration::N_TURNS, BonusType::STACKS_SPEED, BonusSource::SPELL_EFFECT, -50,
				BonusSourceID(SpellID(SpellID::SLOW)), BonusSubtypeID(), BonusValueType::PERCENT_TO_ALL);
			Bonus weakness(BonusDuration::N_TURNS, BonusType::PRIMARY_SKILL, BonusSource::SPELL_EFFECT, -6,
				BonusSourceID(SpellID(SpellID::WEAKNESS)), BonusSubtypeID(PrimarySkill::ATTACK));
			for(auto * bonus : {&slow, &weakness})
			{
				bonus->turnsRemain = 3;
				bonus->spellCasterOwner = PlayerColor(1);
			}
			packet.toAdd.emplace_back(unit->unitId(), std::vector<Bonus>{slow, weakness});
		}
		gameHandler->sendAndApply(packet);
	}

	static int afflictions(const battle::Unit * unit)
	{
		return (turns(unit, SpellID::SLOW) > 0 ? 1 : 0) + (turns(unit, SpellID::WEAKNESS) > 0 ? 1 : 0);
	}
};
}

TEST(MetamagicArcaneWardStateTest, RefreshKeepsTheLargerPoolAndAbsorbsDamage)
{
	battle::ArcaneWardShieldState ward;
	EXPECT_TRUE(ward.refresh(50));
	int64_t damage = 30;
	EXPECT_EQ(ward.absorb(damage), 30);
	EXPECT_EQ(damage, 0);
	EXPECT_FALSE(ward.refresh(10)) << "A weaker refresh never lowers an active ward";
	EXPECT_EQ(ward.remaining, 20);
	EXPECT_TRUE(ward.refresh(40));
	EXPECT_EQ(ward.remaining, 40);
	EXPECT_EQ(ward.capacity, 40);
	damage = 70;
	EXPECT_EQ(ward.absorb(damage), 40);
	EXPECT_EQ(damage, 30);
	EXPECT_THROW(ward.refresh(-1), std::invalid_argument);
}

TEST_F(MetamagicDoubleCastPerkTest, ArcaneWardShieldsEveryFriendlyStackUntilTheNextRound)
{
	spellPower = 10;
	ASSERT_NO_FATAL_FAILURE(prepare(1));
	learn({"arcaneWard"});
	ASSERT_NO_FATAL_FAILURE(doubleCast());
	const auto friendly = stacks(ATTACKER);
	ASSERT_FALSE(friendly.empty());
	for(const auto * unit : friendly)
	{
		EXPECT_EQ(unit->getArcaneWardHitPoints(), 50);
		EXPECT_EQ(battle::shieldSnapshot(*unit).pools[2].capacity, 50);
	}
	for(const auto * unit : stacks(BattleSide::DEFENDER))
		EXPECT_EQ(unit->getArcaneWardHitPoints(), 0);

	const auto * target = friendly.front();
	auto unitState = target->acquireState();
	BattleStackAttacked hit;
	hit.stackAttacked = target->unitId();
	hit.damageAmount = 60;
	const battle::DamageContext context{battle::DamageClass::PHYSICAL, battle::DamageOrigin::CREATURE_STRIKE};
	CStack::prepareAttacked(hit, gameHandler->getRandomGenerator(), unitState, false, false,
		battle::DamageProvenance::PHYSICAL_CREATURE, nullptr, &context);
	EXPECT_EQ(unitState->getArcaneWardHitPoints(), 0);
	EXPECT_EQ(hit.damageAmount, std::min<int64_t>(10, target->getAvailableHealth()));

	advanceRound();
	for(const auto * unit : stacks(ATTACKER))
		EXPECT_EQ(unit->getArcaneWardHitPoints(), 0);
}

TEST_F(MetamagicDoubleCastPerkTest, ArcaneWardNeedsThePerk)
{
	spellPower = 10;
	ASSERT_NO_FATAL_FAILURE(prepare(1));
	ASSERT_NO_FATAL_FAILURE(doubleCast());
	for(const auto * unit : stacks(ATTACKER))
		EXPECT_EQ(unit->getArcaneWardHitPoints(), 0);
}

TEST_F(MetamagicDoubleCastPerkTest, ManaSiphonDrainsTheEnemyHero)
{
	defenderMana = 10;
	knowledge = 1000;
	ASSERT_NO_FATAL_FAILURE(prepare(1));
	learn({"manaSiphon"});
	ASSERT_TRUE(cast(SpellID::BLESS));
	const auto before = attackerSideHero->getManaAvailable();
	const auto curseCost = cost(SpellID::CURSE);
	ASSERT_TRUE(cast(SpellID::CURSE));
	EXPECT_EQ(defenderSideHero->getManaAvailable(), 6);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), before - curseCost + 4);
}

TEST_F(MetamagicDoubleCastPerkTest, ManaSiphonTakesOnlyWhatTheEnemyHas)
{
	defenderMana = 3;
	knowledge = 1000;
	ASSERT_NO_FATAL_FAILURE(prepare(1));
	learn({"manaSiphon"});
	ASSERT_TRUE(cast(SpellID::BLESS));
	const auto before = attackerSideHero->getManaAvailable();
	const auto curseCost = cost(SpellID::CURSE);
	ASSERT_TRUE(cast(SpellID::CURSE));
	EXPECT_EQ(defenderSideHero->getManaAvailable(), 0);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), before - curseCost + 3);
}

TEST_F(MetamagicDoubleCastPerkTest, ManaSiphonNeedsThePerk)
{
	defenderMana = 10;
	ASSERT_NO_FATAL_FAILURE(prepare(1));
	ASSERT_NO_FATAL_FAILURE(doubleCast());
	EXPECT_EQ(defenderSideHero->getManaAvailable(), 10);
}

TEST_F(MetamagicDoubleCastPerkTest, FormulaRecoveryRestoresManaAfterEachDoubleCast)
{
	knowledge = 1000;
	ASSERT_NO_FATAL_FAILURE(prepare(1));
	learn({"formulaRecovery"});
	ASSERT_TRUE(cast(SpellID::BLESS));
	const auto before = attackerSideHero->getManaAvailable();
	const auto curseCost = cost(SpellID::CURSE);
	ASSERT_TRUE(cast(SpellID::CURSE));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), before - curseCost + 3);
}

TEST_F(MetamagicDoubleCastPerkTest, FormulaRecoveryRespectsTheNormalManaCap)
{
	ASSERT_NO_FATAL_FAILURE(prepare(1));
	learn({"formulaRecovery"});
	ASSERT_TRUE(cast(SpellID::BLESS));
	// Default Knowledge: Normal Mana is full and the casts are paid from Buffer first.
	ASSERT_EQ(attackerSideHero->getNormalSpellPoints(), attackerSideHero->manaLimit());
	const auto before = attackerSideHero->getManaAvailable();
	const auto curseCost = cost(SpellID::CURSE);
	ASSERT_LT(curseCost, attackerSideHero->getBufferSpellPoints());
	ASSERT_TRUE(cast(SpellID::CURSE));
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), attackerSideHero->manaLimit());
	EXPECT_EQ(attackerSideHero->getManaAvailable(), before - curseCost);
}

TEST_F(MetamagicDoubleCastPerkTest, AfterglowExtendsBothSpellsOfADoubleCast)
{
	ASSERT_NO_FATAL_FAILURE(prepare(2));
	const auto * friendly = battle()->battleActiveUnit();
	ASSERT_NO_FATAL_FAILURE(doubleCast());
	const auto blessTurns = turns(friendly, SpellID::BLESS);
	const auto curseTurns = turns(enemy(), SpellID::CURSE);
	ASSERT_GT(blessTurns, 0);
	ASSERT_GT(curseTurns, 0);
	advanceRound();
	learn({"deepReserves", "afterglow"});
	ASSERT_EQ(battle()->battleActiveUnit(), friendly);
	ASSERT_NO_FATAL_FAILURE(doubleCast());
	EXPECT_EQ(turns(friendly, SpellID::BLESS), blessTurns + 1);
	EXPECT_EQ(turns(enemy(), SpellID::CURSE), curseTurns + 1);
}

TEST_F(MetamagicDoubleCastPerkTest, CrescendoAddsSpellPowerPerGrossSpentUseForTheCombat)
{
	spellPower = 10;
	ASSERT_NO_FATAL_FAILURE(prepare(3));
	learn({"deepReserves", "crescendo"});
	const auto power = effectPower(SpellID::BLESS);
	ASSERT_NO_FATAL_FAILURE(doubleCast());
	EXPECT_EQ(metamagicDoubleCast::crescendoSpellPower(*attackerSideHero, *battle()), 5);
	EXPECT_EQ(effectPower(SpellID::BLESS), power + 5);
	EXPECT_EQ(attackerSideHero->getPrimSkillLevel(PrimarySkill::SPELL_POWER), 10) << "The hero is never changed";
	advanceRound();
	ASSERT_NO_FATAL_FAILURE(doubleCast());
	EXPECT_EQ(effectPower(SpellID::BLESS), power + 10);
}

TEST_F(MetamagicDoubleCastPerkTest, CrescendoNeedsThePerk)
{
	spellPower = 10;
	ASSERT_NO_FATAL_FAILURE(prepare(3));
	const auto power = effectPower(SpellID::BLESS);
	ASSERT_NO_FATAL_FAILURE(doubleCast());
	EXPECT_EQ(effectPower(SpellID::BLESS), power);
	EXPECT_EQ(metamagicDoubleCast::crescendoSpellPower(*attackerSideHero, *battle()), 0);
}

TEST_F(MetamagicDoubleCastPerkTest, CleansingWeaveRemovesOneNegativeEffectFromEveryFriendlyStack)
{
	ASSERT_NO_FATAL_FAILURE(prepare(2));
	learn({"deepReserves", "cleansingWeave"});
	afflictFriendlyStacks();
	for(const auto * unit : stacks(ATTACKER))
		ASSERT_EQ(afflictions(unit), 2);
	ASSERT_NO_FATAL_FAILURE(doubleCast());
	for(const auto * unit : stacks(ATTACKER))
		EXPECT_EQ(afflictions(unit), 1);
}

TEST_F(MetamagicDoubleCastPerkTest, CleansingWeaveNeedsThePerk)
{
	ASSERT_NO_FATAL_FAILURE(prepare(2));
	afflictFriendlyStacks();
	ASSERT_NO_FATAL_FAILURE(doubleCast());
	for(const auto * unit : stacks(ATTACKER))
		EXPECT_EQ(afflictions(unit), 2);
}

TEST_F(MetamagicDoubleCastPerkTest, SpellstormReducesCostsAfterTheThirdDoubleCast)
{
	knowledge = 1000;
	ASSERT_NO_FATAL_FAILURE(prepare(3));
	learn({"deepReserves", "afterglow", "spellstorm"});
	const auto base = cost(SpellID::SLOW);
	for(int completed = 1; completed <= 3; ++completed)
	{
		EXPECT_EQ(cost(SpellID::SLOW), base) << completed;
		ASSERT_NO_FATAL_FAILURE(doubleCast());
		advanceRound();
	}
	EXPECT_EQ(cost(SpellID::SLOW), std::max(1, (base * 75 + 99) / 100));
	const auto breakdown = battle()->battleGetSpellCostBreakdown(SpellID(SpellID::SLOW).toSpell(), attackerSideHero);
	EXPECT_TRUE(std::any_of(breakdown.stages.begin(), breakdown.stages.end(), [](const SpellCostStage & stage)
	{
		return stage.kind == SpellCostStage::Kind::METAMAGIC_SPELLSTORM;
	}));
}

TEST_F(MetamagicDoubleCastPerkTest, SpellstormNeedsThePerk)
{
	knowledge = 1000;
	ASSERT_NO_FATAL_FAILURE(prepare(3));
	const auto base = cost(SpellID::SLOW);
	for(int completed = 1; completed <= 3; ++completed)
	{
		ASSERT_NO_FATAL_FAILURE(doubleCast());
		advanceRound();
	}
	EXPECT_EQ(cost(SpellID::SLOW), base);
}

TEST_F(MetamagicDoubleCastPerkTest, PolicySixWorldsKeepTheirHistoricalBehaviour)
{
	pinOverkill = true;
	spellPower = 10;
	knowledge = 1000;
	ASSERT_NO_FATAL_FAILURE(prepare(3));
	ASSERT_EQ(nativeRules::metamagicRules(attackerSideHero)->doubleCastPolicy(), nullptr);
	EXPECT_EQ(nativeRules::doubleCastPerks(attackerSideHero), 0);
	const auto power = effectPower(SpellID::BLESS);
	const auto slowCost = cost(SpellID::SLOW);
	const auto mana = attackerSideHero->getManaAvailable();
	const auto blessCost = cost(SpellID::BLESS);
	ASSERT_TRUE(cast(SpellID::BLESS));
	EXPECT_FALSE(offered());
	EXPECT_FALSE(cast(SpellID::CURSE));
	EXPECT_TRUE(state().defaultState());
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - blessCost);
	for(const auto * unit : stacks(ATTACKER))
		EXPECT_EQ(unit->getArcaneWardHitPoints(), 0);
	EXPECT_EQ(effectPower(SpellID::BLESS), power);
	EXPECT_EQ(cost(SpellID::SLOW), slowCost);
	EXPECT_EQ(metamagicDoubleCast::crescendoSpellPower(*attackerSideHero, *battle()), 0);
	EXPECT_EQ(metamagicDoubleCast::spellstormReductionPercent(*battle(), *attackerSideHero), 0);
}
