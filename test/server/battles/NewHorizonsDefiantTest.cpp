/*
 * NewHorizonsDefiantTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"

#include "../../../AI/BattleAI/AttackPossibility.h"
#include "../../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/battle/ArmorerDefiantState.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/NewHorizonsArmorer.h"
#include "../../../lib/battle/NewHorizonsOffense.h"
#include "../../../lib/battle/NewHorizonsShroud.h"
#include "../../../lib/bonuses/BonusSelector.h"
#include "../../../lib/serializer/CMemorySerializer.h"

namespace
{
constexpr auto DEFIANT = "new-horizons:armorer.defiant";
using Cause = newHorizonsArmorer::DefiantDenialCause;

class DefiantEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit DefiantEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
}

class NewHorizonsDefiantTest : public HeroCommandFixture
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		auto rules = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
		auto & perks = rules["skills"][newHorizonsArmorer::SKILL_ID]["perks"].Vector();
		const auto entry = std::ranges::find_if(perks, [](const auto & perk)
		{
			return perk["id"].String() == DEFIANT;
		});
		if(entry == perks.end())
			throw std::runtime_error("Missing Defiant registry entry");
		RecordProperty("defiant_registry_status", (*entry)["effect"]["status"].String());
		// Only the pre-activation gate uses an override; the activated registry
		// follows exactly the same legitimate offer/accepted level-up path.
		if((*entry)["effect"]["status"].String() == "planned")
		{
			(*entry)["effect"]["status"].String() = "active";
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, rules);
		}
	}

	void accept(CGHeroInstance * hero, const char * skill, const char * perk)
	{
		const auto rank = [hero](const std::string & id) { return hero->getPerkSkillRank(id); };
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer(rank, seed);
			const auto choice = std::ranges::find_if(offer, [skill, perk](const auto & entry)
			{
				return entry.selection.skillId == skill && entry.selection.perkId == perk;
			});
			if(choice == offer.end())
				continue;
			gameHandler->levelUpHero(hero, offer, std::distance(offer.begin(), choice), seed, false);
			ASSERT_TRUE(hero->hasActivePerk(skill, perk));
			return;
		}
		FAIL() << "No legitimate offer for " << perk;
	}

	void acquireDefiant(CGHeroInstance * hero)
	{
		const SecondarySkill skill(SecondarySkill::decode(newHorizonsArmorer::SKILL_ID));
		hero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		ASSERT_NO_FATAL_FAILURE(accept(hero, newHorizonsArmorer::SKILL_ID, "new-horizons:armorer.ironDiscipline"));
		hero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		ASSERT_NO_FATAL_FAILURE(accept(hero, newHorizonsArmorer::SKILL_ID, DEFIANT));
	}

	void acquireNoQuarter()
	{
		const SecondarySkill skill(SecondarySkill::decode(newHorizonsOffense::SKILL));
		attackerSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		ASSERT_NO_FATAL_FAILURE(accept(attackerSideHero, newHorizonsOffense::SKILL, "new-horizons:offense.shockAssault"));
		attackerSideHero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		ASSERT_NO_FATAL_FAILURE(accept(attackerSideHero, newHorizonsOffense::SKILL, newHorizonsOffense::VENGEANCE));
		attackerSideHero->setSecSkillLevel(skill, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		ASSERT_NO_FATAL_FAILURE(accept(attackerSideHero, newHorizonsOffense::SKILL, newHorizonsOffense::NO_QUARTER));
	}

	void prepare(bool selected = true, bool noQuarter = false, bool shroud = false)
	{
		startGame();
		if(selected)
		{
			ASSERT_NO_FATAL_FAILURE(acquireDefiant(defenderSideHero));
		}
		if(noQuarter)
		{
			ASSERT_NO_FATAL_FAILURE(acquireNoQuarter());
		}
		if(shroud)
			attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(std::string(newHorizonsShroud::SKILL_ID))),
				MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		startBattle();
	}

	CStack * unit(BattleSide side, int hex, int count = 100)
	{
		auto * result = addStack(side, creatureByName("core:pikeman"), BattleHex(hex), count);
		forceMaximumDamage(result);
		return result;
	}

	bool strike(CStack * source, CStack * target)
	{
		BattleSetActiveStack activation;
		activation.battleID = BattleID(0);
		activation.stack = source->unitId();
		activation.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activation);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->battleGetOwner(source),
			BattleAction::makeMeleeAttack(source, target->getPosition(), source->getPosition()));
	}

	int counters(const CStack * target) const
	{
		return std::count_if(server.attacks.begin(), server.attacks.end(), [target](const auto & attack)
		{
			return attack.counter() && attack.stackAttacking == target->unitId();
		});
	}

	void health(CStack * target, int64_t desired)
	{
		auto state = target->acquireState();
		int64_t damage = state->getAvailableHealth() - desired;
		ASSERT_GE(damage, 0);
		state->damage(damage);
		BattleUnitsChanged packet;
		packet.battleID = BattleID(0);
		UnitChanges change(target->unitId(), UnitChanges::EOperation::UPDATE);
		change.data = state->save();
		change.healthDelta = -damage;
		packet.changedStacks.push_back(std::move(change));
		gameHandler->sendAndApply(packet);
	}

	bool noQuarterMorale(const battle::Unit * target) const
	{
		const auto bonuses = target->getAllBonuses(CSelector([](const Bonus * bonus)
		{
			return newHorizonsOffense::isNoQuarterMoralePenalty(bonus);
		}));
		return bonuses && !bonuses->empty();
	}
};

TEST_F(NewHorizonsDefiantTest, FirstInnateDenialIsSharedAcrossStacksAndRenewsNextRound)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * source = unit(BattleSide::ATTACKER, 92);
	auto * first = unit(BattleSide::DEFENDER, 93, 1000);
	auto * otherSource = unit(BattleSide::ATTACKER, 126);
	auto * second = unit(BattleSide::DEFENDER, 127, 1000);
	blockRetaliation(source);
	blockRetaliation(otherSource);
	beginCombat();
	const auto round = battle()->getRound();
	ASSERT_TRUE(battle()->battleCanUseDefiant(BattleAttackInfo(source, first, 0, false), Cause::INNATE_BLOCK));
	server.attacks.clear();
	ASSERT_TRUE(strike(source, first));
	EXPECT_EQ(counters(first), 1);
	EXPECT_EQ(battle()->getArmorerDefiantState(BattleSide::DEFENDER).lastConsumedRound, round);
	EXPECT_EQ(battle()->getArmorerDefiantState(BattleSide::ATTACKER).lastConsumedRound, -1);
	server.attacks.clear();
	ASSERT_TRUE(strike(otherSource, second));
	EXPECT_EQ(counters(second), 0);
	advanceRound();
	ASSERT_GT(battle()->getRound(), round);
	server.attacks.clear();
	ASSERT_TRUE(strike(otherSource, second));
	EXPECT_EQ(counters(second), 1);
	EXPECT_EQ(battle()->getArmorerDefiantState(BattleSide::DEFENDER).lastConsumedRound, battle()->getRound());
}

TEST_F(NewHorizonsDefiantTest, NoQuarterExemptionIncludesLinkedMoraleAndOnlyFirstTarget)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, true));
	auto * source = unit(BattleSide::ATTACKER, 92, 1);
	auto * first = unit(BattleSide::DEFENDER, 93, 1000);
	auto * otherSource = unit(BattleSide::ATTACKER, 126, 1);
	auto * second = unit(BattleSide::DEFENDER, 127, 1000);
	beginCombat();
	for(auto pair : {std::pair{source, first}, std::pair{otherSource, second}})
	{
		const auto damage = battle()->calculateDmgRange(BattleAttackInfo(pair.first, pair.second, 0, false)).damage.max;
		ASSERT_GT(damage, 0);
		health(pair.second, pair.second->getTotalHealth() / 4 + damage - 1);
	}
	server.attacks.clear();
	ASSERT_TRUE(strike(source, first));
	EXPECT_EQ(counters(first), 1);
	EXPECT_FALSE(noQuarterMorale(first));
	EXPECT_FALSE(first->hasBonusOfType(BonusType::NO_RETALIATION));
	EXPECT_EQ(first->noQuarterMoraleActivationsRemaining, 0);
	server.attacks.clear();
	ASSERT_TRUE(strike(otherSource, second));
	EXPECT_EQ(counters(second), 0);
	EXPECT_TRUE(noQuarterMorale(second));
	EXPECT_TRUE(second->hasBonusOfType(BonusType::NO_RETALIATION));
	EXPECT_EQ(second->noQuarterMoraleActivationsRemaining, 1);
}

TEST_F(NewHorizonsDefiantTest, ExpertShroudExemptsDenialButPreservesIndependentFlankingDamage)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, false, true));
	auto * target = unit(BattleSide::DEFENDER, 81, 1000);
	auto * rear = unit(BattleSide::ATTACKER, 82);
	auto * front = unit(BattleSide::ATTACKER, 80);
	beginCombat();
	const BattleAttackInfo rearAttack(rear, target, 0, false);
	ASSERT_TRUE(battle()->battleShroudDeniesRetaliation(rearAttack));
	ASSERT_TRUE(battle()->battleCanUseDefiant(rearAttack, Cause::EXPERT_SHROUD));
	const auto base = battle()->calculateDmgRange(BattleAttackInfo(front, target, 0, false)).damage.max;
	const auto enhanced = battle()->calculateDmgRange(rearAttack).damage.max;
	EXPECT_GT(enhanced, base); // Defiant exempts denial, not the independent rank damage premium.
	const auto before = target->getAvailableHealth();
	server.attacks.clear();
	ASSERT_TRUE(strike(rear, target));
	EXPECT_EQ(before - target->getAvailableHealth(), enhanced);
	EXPECT_EQ(counters(target), 1);
}

TEST_F(NewHorizonsDefiantTest, MagicalAndOrdinaryCapacityReachGuardsPreserveAllowance)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * source = unit(BattleSide::ATTACKER, 92);
	auto * target = unit(BattleSide::DEFENDER, 93, 1000);
	auto * distant = unit(BattleSide::DEFENDER, 130, 1000);
	source->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::BLOCKS_RETALIATION, BonusSource::SPELL_EFFECT, 1, BonusSourceID(SpellID(SpellID::HYPNOTIZE))));
	beginCombat();
	const BattleAttackInfo attack(source, target, 0, false);
	EXPECT_FALSE(battle()->battleCanUseDefiant(attack, Cause::INNATE_BLOCK));
	server.attacks.clear();
	ASSERT_TRUE(strike(source, target));
	EXPECT_EQ(counters(target), 0);
	EXPECT_EQ(battle()->getArmorerDefiantState(BattleSide::DEFENDER).lastConsumedRound, -1);
	blockRetaliation(source);
	auto magical = attack;
	magical.physicalDamage = false;
	EXPECT_FALSE(battle()->battleCanUseDefiant(magical, Cause::INNATE_BLOCK));
	EXPECT_FALSE(battle()->battleCanUseDefiant(BattleAttackInfo(source, distant, 0, false), Cause::INNATE_BLOCK));
	while(target->counterAttacks.canUse())
		target->counterAttacks.use();
	EXPECT_FALSE(battle()->battleCanUseDefiant(attack, Cause::INNATE_BLOCK));
	EXPECT_EQ(battle()->getArmorerDefiantState(BattleSide::DEFENDER).lastConsumedRound, -1);
}

TEST_F(NewHorizonsDefiantTest, UnselectedArmorerDoesNotOverrideInnateDenial)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	auto * source = unit(BattleSide::ATTACKER, 92);
	auto * target = unit(BattleSide::DEFENDER, 93, 1000);
	blockRetaliation(source);
	beginCombat();
	EXPECT_FALSE(battle()->battleCanUseDefiant(BattleAttackInfo(source, target, 0, false), Cause::INNATE_BLOCK));
	server.attacks.clear();
	ASSERT_TRUE(strike(source, target));
	EXPECT_EQ(counters(target), 0);
	EXPECT_EQ(battle()->getArmorerDefiantState(BattleSide::DEFENDER).lastConsumedRound, -1);
}

TEST_F(NewHorizonsDefiantTest, DetachedCandidateAndReplayConsumeOnlyTheirOwnSideState)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * source = unit(BattleSide::ATTACKER, 92);
	auto * target = unit(BattleSide::DEFENDER, 93, 1000);
	blockRetaliation(source);
	beginCombat();
	DefiantEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto model = std::make_shared<HypotheticBattle>(&environment, callback);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, model);
	const auto original = battle()->getArmorerDefiantState(BattleSide::DEFENDER);
	const auto healthBefore = target->getAvailableHealth();
	DamageCache cache;
	const auto prediction = AttackPossibility::evaluate(BattleAttackInfo(
		model->getForUpdate(source->unitId()).get(), model->getForUpdate(target->unitId()).get(), 0, false),
		source->getPosition(), cache, model);
	ASSERT_NE(prediction.effectPreview, nullptr);
	EXPECT_EQ(prediction.effectPreview->getArmorerDefiantState(BattleSide::DEFENDER).lastConsumedRound,
		battle()->getRound());
	EXPECT_TRUE(std::ranges::any_of(prediction.fortuneStrikes, [](const auto & strike)
	{
		return strike.retaliation;
	}));
	EXPECT_EQ(model->getArmorerDefiantState(BattleSide::DEFENDER), original);
	EXPECT_EQ(sibling->getArmorerDefiantState(BattleSide::DEFENDER), original);
	EXPECT_EQ(battle()->getArmorerDefiantState(BattleSide::DEFENDER), original);
	EXPECT_EQ(target->getAvailableHealth(), healthBefore);
	BattleExchangeVariant exchange;
	exchange.trackAttack(prediction, model, cache);
	EXPECT_EQ(model->getArmorerDefiantState(BattleSide::DEFENDER).lastConsumedRound, battle()->getRound());
	EXPECT_EQ(sibling->getArmorerDefiantState(BattleSide::DEFENDER), original);
	EXPECT_EQ(battle()->getArmorerDefiantState(BattleSide::DEFENDER), original);
	const auto projectedHealth = model->battleGetUnitByID(target->unitId())->getAvailableHealth();
	server.attacks.clear();
	ASSERT_TRUE(strike(source, target));
	EXPECT_EQ(counters(target), 1);
	EXPECT_EQ(target->getAvailableHealth(), projectedHealth);
	EXPECT_EQ(battle()->getArmorerDefiantState(BattleSide::DEFENDER), model->getArmorerDefiantState(BattleSide::DEFENDER));
	EXPECT_EQ(sibling->getArmorerDefiantState(BattleSide::DEFENDER), original);
}

TEST_F(NewHorizonsDefiantTest, CurrentStateAndConsumptionPacketRejectLossyOldSerialization)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * source = unit(BattleSide::ATTACKER, 92);
	auto * target = unit(BattleSide::DEFENDER, 93, 1000);
	blockRetaliation(source);
	beginCombat();
	// Untouched full-battle copy: existing Veteran attack history intentionally
	// forbids a binary whole-battle health snapshot after a physical strike.
	const auto armedCopy = CMemorySerializer::deepCopy(*battle(), gameState().get());
	EXPECT_EQ(armedCopy->getArmorerDefiantState(BattleSide::DEFENDER), ArmorerDefiantState{});
	SetArmorerDefiantState packet;
	packet.battleID = BattleID(0);
	packet.side = BattleSide::DEFENDER;
	packet.attackerId = source->unitId();
	packet.targetId = target->unitId();
	packet.cause = Cause::INNATE_BLOCK;
	ASSERT_TRUE(packet.state.consumeAt(battle()->getRound()));
	EXPECT_NO_THROW(packet.validateAgainst(*battle()));
	CMemorySerializer wire;
	wire.oser & packet;
	SetArmorerDefiantState restoredPacket;
	wire.iser & restoredPacket;
	EXPECT_EQ(restoredPacket.side, packet.side);
	EXPECT_EQ(restoredPacket.attackerId, packet.attackerId);
	EXPECT_EQ(restoredPacket.targetId, packet.targetId);
	EXPECT_EQ(restoredPacket.cause, packet.cause);
	EXPECT_EQ(restoredPacket.state, packet.state);
	ASSERT_TRUE(strike(source, target));
	EXPECT_EQ(battle()->getArmorerDefiantState(BattleSide::DEFENDER), packet.state);
	EXPECT_THROW(packet.validateAgainst(*battle()), std::runtime_error);
	CMemorySerializer sideWire;
	sideWire.oser & battle()->getSide(BattleSide::DEFENDER);
	SideInBattle restoredSide(gameState().get());
	sideWire.iser & restoredSide;
	EXPECT_EQ(restoredSide.armorerDefiant, packet.state);
	const auto old = static_cast<ESerializationVersion>(static_cast<int>(ESerializationVersion::NEW_HORIZONS_ARMORER_DEFIANT) - 1);
	CMemorySerializer oldState;
	oldState.oser.version = old;
	EXPECT_THROW(oldState.oser & packet.state, std::runtime_error);
	EXPECT_TRUE(oldState.extractBuffer().empty());
	CMemorySerializer oldPacket;
	oldPacket.oser.version = old;
	EXPECT_THROW(oldPacket.oser & packet, std::runtime_error);
	EXPECT_TRUE(oldPacket.extractBuffer().empty());
	CMemorySerializer oldSide;
	oldSide.oser.version = old;
	EXPECT_THROW(oldSide.oser & battle()->getSide(BattleSide::DEFENDER), std::runtime_error);
	CMemorySerializer legacy;
	legacy.oser.version = legacy.iser.version = old;
	ArmorerDefiantState unused;
	legacy.oser & unused;
	ArmorerDefiantState restoredState = packet.state;
	legacy.iser & restoredState;
	EXPECT_EQ(restoredState, unused);
	ArmorerDefiantState invalid;
	invalid.lastConsumedRound = -2;
	EXPECT_THROW(invalid.validate(), std::runtime_error);
}
