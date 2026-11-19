/*
 * NewHorizonsWarcastingTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../../PerkTestSetup.h"

#include "HeroCommandFixture.h"
#include "../../../lib/spells/CSpell.h"

#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/HeroActionAllowanceState.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/battle/CObstacleInstance.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/Limiters.h"
#include "../../../lib/bonuses/Propagators.h"
#include "../../../lib/bonuses/Updaters.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../AI/BattleAI/HypotheticBattle.h"

namespace
{
constexpr auto warcastingSkill = "new-horizons:warcasting";
constexpr auto metamagicSkill = "new-horizons:metamagic";
constexpr auto sorcerySkill = "new-horizons:sorceryMagic";
constexpr auto countermagePerk = "new-horizons:sorceryMagic.countermage";
constexpr auto sorceryBasicPerk = "new-horizons:sorceryMagic.overcharger";

std::shared_ptr<Bonus> ordinaryLuckMarker(BattleSide side)
{
	auto marker = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::LUCK,
		BonusSource::OTHER, 1, BonusSourceID());
	marker->parameters = std::make_shared<BonusParameters>(static_cast<int32_t>(side));
	return marker;
}

class WarcastingEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit WarcastingEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsWarcastingTest : public HeroCommandFixture
{
protected:
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		auto magicRules = JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magicRules);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void acceptPerkThroughOffer(CGHeroInstance * hero, const std::string & skillId,
		const std::string & perkId, int expectedRank)
	{
		const auto rankLookup = [hero](const std::string & offeredSkillId)
		{
			return hero->getPerkSkillRank(offeredSkillId);
		};
		for(uint64_t seed = 0; seed < perkTestSetup::seedLimit(hero->getPerkState(), 4096); ++seed)
		{
			const auto offers = perkTestSetup::forSkill(hero->getPerkState(), skillId, rankLookup, seed, hero->getPerkOfferWeightContext());
			const auto candidate = std::find_if(offers.begin(), offers.end(), [&](const auto & offer)
			{
				return offer.selection.skillId == skillId && offer.selection.perkId == perkId;
			});
			if(candidate == offers.end())
				continue;

			ASSERT_EQ(candidate->requiredRank, expectedRank);
			const auto choice = static_cast<size_t>(std::distance(offers.begin(), candidate));
			gameHandler->levelUpHero(hero, offers, choice, seed, false);
			ASSERT_TRUE(hero->hasActivePerk(skillId, perkId));
			return;
		}
		FAIL() << "Perk was not present in a legal offer: " << perkId;
	}

	void acquireBasicSorceryPrerequisite(CGHeroInstance * hero)
	{
		const int decodedSorcery = SecondarySkill::decode(sorcerySkill);
		ASSERT_GE(decodedSorcery, 0);
		const auto skill = SecondarySkill(decodedSorcery);

		hero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		if(!hero->hasActivePerk(sorcerySkill, sorceryBasicPerk))
			acceptPerkThroughOffer(hero, sorcerySkill, sorceryBasicPerk, static_cast<int>(MasteryLevel::BASIC));
	}

	void acquireCountermageThroughAdvancedOffer(CGHeroInstance * hero)
	{
		acquireBasicSorceryPrerequisite(hero);
		const int decodedSorcery = SecondarySkill::decode(sorcerySkill);
		ASSERT_GE(decodedSorcery, 0);
		const auto skill = SecondarySkill(decodedSorcery);

		gameHandler->levelUpHero(hero, skill, false);
		acceptPerkThroughOffer(hero, sorcerySkill, countermagePerk, static_cast<int>(MasteryLevel::ADVANCED));
	}

	void prepareWarcasting(int rank = 1, bool withMetamagic = false, bool defenderCountermage = false,
		bool defenderSorceryBasic = false)
	{
		startGame();
		const auto skill = SecondarySkill(SecondarySkill::decode(warcastingSkill));
		ASSERT_TRUE(skill.hasValue());
		attackerSideHero->setSecSkillLevel(skill, rank, ChangeValueMode::ABSOLUTE);
		if(withMetamagic)
		{
			// A passive Metamagic skill must not produce an additional action.
			attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:neela")));
			const int decodedMetamagic = SecondarySkill::decode(metamagicSkill);
			ASSERT_GE(decodedMetamagic, 0);
			attackerSideHero->setSecSkillLevel(SecondarySkill(decodedMetamagic), 1, ChangeValueMode::ABSOLUTE);
		}
		if(defenderCountermage)
			acquireCountermageThroughAdvancedOffer(defenderSideHero);
		else if(defenderSorceryBasic)
			acquireBasicSorceryPrerequisite(defenderSideHero);
		attackerSideHero->setPrimarySkill(PrimarySkill::ATTACK, 100, ChangeValueMode::ABSOLUTE);
		// Recovery restores Normal, not Buffer: give these casting fixtures real
		// capacity instead of implicitly overcharging a zero-Knowledge hero.
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 1000, ChangeValueMode::ABSOLUTE);
		defenderSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 1000, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);
		setTestSpellPointTotal(defenderSideHero, 1000);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		for(const auto spell : {SpellID::BLESS, SpellID::SLOW, SpellID::MAGIC_ARROW})
			attackerSideHero->addSpellToSpellbook(spell);

		startBattle();
		attacker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 10);
		const int initialDefenderCount = 10;
		defender = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex),
			initialDefenderCount);
		beginCombat();

		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			if(unit != attacker && unit != defender)
				remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		activate(attacker);
	}

	void prepareWithDefenderBasicSorcery()
	{
		prepareWarcasting(1, true, false, true);
	}

	void activate(const CStack * stack)
	{
		BattleSetActiveStack activate;
		activate.battleID = BattleID(0);
		activate.stack = stack->unitId();
		activate.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activate);
	}

	uint32_t grantOrderAllowanceForFixture(BattleSide side)
	{
		auto & allowances = battle()->getSide(side).heroActionAllowances;
		return allowances.grantAllowance(HeroActionAllowanceState::AllowanceKind::ORDER,
			HeroActionAllowanceState::GrantSource::PERK, battle()->battleGetRound());
	}

	uint32_t grantSpellAllowanceForFixture(BattleSide side)
	{
		auto & allowances = battle()->getSide(side).heroActionAllowances;
		return allowances.grantAllowance(HeroActionAllowanceState::AllowanceKind::SPELL,
			HeroActionAllowanceState::GrantSource::PERK, battle()->battleGetRound());
	}

	bool cast(SpellID spell, const CStack * target)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = spell;
		action.aimToUnit(target);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	void advanceRound()
	{
		BattleNextRound next;
		next.battleID = BattleID(0);
		gameHandler->sendAndApply(next);
	}

	std::vector<std::byte> wisdomBytes(const WisdomCombatState & state) const
	{
		CMemorySerializer bytes;
		auto value = state;
		bytes.oser & value;
		return bytes.extractBuffer();
	}

	std::vector<std::byte> rng() const
	{
		CMemorySerializer bytes;
		bytes.oser & *gameHandler->randomizer;
		return bytes.extractBuffer();
	}

	CStack * attacker = nullptr;
	CStack * defender = nullptr;
};
}

TEST_F(NewHorizonsWarcastingTest, FreshBattleStartRoundTripsBeforeAnyHeroActionIsGranted)
{
	startGame();
	startBattle();
	ASSERT_EQ(battle()->getRound(), 0);
	EXPECT_EQ(battle()->battleHeroActionAllowanceCounts(BattleSide::ATTACKER).heroActions, 0u);
	EXPECT_NO_THROW(EXPECT_NE(battle()->battleCanCastSpell(attackerSideHero, spells::Mode::HERO),
		ESpellCastProblem::OK));
	BattleStart packet;
	packet.battleID = BattleID(0);
	ASSERT_NO_THROW(packet.info = CMemorySerializer::deepCopy(*battle(), gameState().get()));
	std::unique_ptr<BattleStart> restored;
	ASSERT_NO_THROW(restored = CMemorySerializer::deepCopy(packet, gameState().get()));
	ASSERT_TRUE(restored && restored->info);
	EXPECT_EQ(restored->info->getRound(), 0);
	EXPECT_EQ(restored->info->battleHeroActionAllowanceCounts(BattleSide::ATTACKER).heroActions, 0u);
	EXPECT_EQ(restored->info->getHeroActionAllowances(BattleSide::ATTACKER).currentRound, -1);

	auto & preplayAllowances = battle()->getSide(BattleSide::ATTACKER).heroActionAllowances;
	preplayAllowances.resetForRound(0);
	preplayAllowances.grantAllowance(HeroActionAllowanceState::AllowanceKind::ORDER,
		HeroActionAllowanceState::GrantSource::PERK, 0);
	EXPECT_THROW(CMemorySerializer::deepCopy(*battle(), gameState().get()), std::runtime_error);
}

TEST_F(NewHorizonsWarcastingTest, TypedOrderAfterHeroSpellPreservesAllowancesAndRoundTrips)
{
	prepareWarcasting(1, true);
	const auto side = BattleSide::ATTACKER;
	const auto round = battle()->battleGetRound();
	const auto orderGrantId = grantOrderAllowanceForFixture(side);
	const auto beforeSpellCounts = battle()->getHeroActionAllowances(side).remainingCounts(round);
	EXPECT_EQ(beforeSpellCounts.heroActions, 1u);
	EXPECT_EQ(beforeSpellCounts.orderActions, 1u);
	EXPECT_EQ(beforeSpellCounts.spellActions, 0u);

	ASSERT_TRUE(cast(SpellID::BLESS, attacker));
	const auto & afterSpell = battle()->getSide(side);
	ASSERT_EQ(afterSpell.castSpellsCount, 1u);
	ASSERT_EQ(afterSpell.usedSpellsHistory, (std::vector<SpellID>{SpellID::BLESS}));
	const auto spellMemory = wisdomBytes(battle()->getWisdomCombatState(side));
	const auto afterSpellCounts = afterSpell.heroActionAllowances.remainingCounts(round);
	EXPECT_EQ(afterSpellCounts.heroActions, 0u);
	EXPECT_EQ(afterSpellCounts.orderActions, 1u);
	EXPECT_EQ(afterSpellCounts.spellActions, 0u);

	const auto eligibleOrder = afterSpell.heroActionAllowances.eligibleAllowance(
		HeroActionAllowanceState::ActionKind::ORDER, round);
	ASSERT_TRUE(eligibleOrder);
	EXPECT_EQ(eligibleOrder->grantId, orderGrantId);
	EXPECT_EQ(eligibleOrder->allowance, HeroActionAllowanceState::AllowanceKind::ORDER);
	ASSERT_TRUE(issue(HeroCommand::CHARGE));

	const auto & afterOrder = battle()->getSide(side);
	EXPECT_EQ(battle()->getHeroCommandUsed(side), true);
	EXPECT_EQ(battle()->getActiveOrder(side), HeroCommand::CHARGE);
	EXPECT_EQ(afterOrder.castSpellsCount, 1u);
	EXPECT_EQ(afterOrder.usedSpellsHistory, (std::vector<SpellID>{SpellID::BLESS}));
	EXPECT_EQ(wisdomBytes(battle()->getWisdomCombatState(side)), spellMemory);
	const auto afterOrderCounts = afterOrder.heroActionAllowances.remainingCounts(round);
	EXPECT_EQ(afterOrderCounts.heroActions, 0u);
	EXPECT_EQ(afterOrderCounts.orderActions, 0u);
	EXPECT_EQ(afterOrderCounts.spellActions, 0u);
	EXPECT_TRUE(afterOrder.heroActionAllowances.grants.empty());

	auto restored = CMemorySerializer::deepCopy(*battle(), gameState().get());
	ASSERT_NE(restored, nullptr);
	const auto & restoredSide = restored->getSide(side);
	EXPECT_EQ(restoredSide.castSpellsCount, 1u);
	EXPECT_EQ(restoredSide.usedSpellsHistory, afterOrder.usedSpellsHistory);
	EXPECT_EQ(wisdomBytes(restored->getWisdomCombatState(side)), wisdomBytes(battle()->getWisdomCombatState(side)));
	EXPECT_TRUE(restored->getHeroCommandUsed(side));
	EXPECT_EQ(restored->getActiveOrder(side), HeroCommand::CHARGE);
	EXPECT_EQ(restored->getHeroOrderState(side), battle()->getHeroOrderState(side));
	EXPECT_EQ(restored->getHeroActionAllowances(side), afterOrder.heroActionAllowances);
	const auto restoredCounts = restored->getHeroActionAllowances(side).remainingCounts(round);
	EXPECT_EQ(restoredCounts.orderActions, 0u);
	EXPECT_EQ(restoredCounts.spellActions, 0u);
}

TEST_F(NewHorizonsWarcastingTest, HypotheticalBattleSpendsGenericTypedSpellAndHeroGrantsWithoutChangingLiveState)
{
	prepareWarcasting(1, true);
	const auto side = BattleSide::ATTACKER;
	// This explicit PERK grant tests the generic engine ledger, not a Metamagic producer.
	grantSpellAllowanceForFixture(side);
	const auto authoritative = battle()->getHeroActionAllowances(side);
	const auto wisdom = wisdomBytes(battle()->getWisdomCombatState(side));
	const auto random = rng();
	WarcastingEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle projection(&environment, callback);
	ASSERT_EQ(projection.battleHeroActionAllowanceCounts(side).heroActions, 1u);
	ASSERT_EQ(projection.battleHeroActionAllowanceCounts(side).spellActions, 1u);
	const auto typed = projection.prepareHeroSpellAllowance(side, SpellID::BLESS);
	ASSERT_TRUE(typed);
	EXPECT_EQ(typed->action.receipt.allowance, HeroActionAllowanceState::AllowanceKind::SPELL);
	EXPECT_EQ(typed->action.receipt.source, HeroActionAllowanceState::GrantSource::PERK);
	ASSERT_TRUE(projection.projectHeroSpellAllowance(side, SpellID::BLESS, attacker->unitId()));
	EXPECT_EQ(projection.battleHeroActionAllowanceCounts(side).heroActions, 1u);
	EXPECT_EQ(projection.battleHeroActionAllowanceCounts(side).spellActions, 0u);
	const auto ordinary = projection.prepareHeroSpellAllowance(side, SpellID::SLOW);
	ASSERT_TRUE(ordinary);
	EXPECT_EQ(ordinary->action.receipt.allowance, HeroActionAllowanceState::AllowanceKind::HERO);
	ASSERT_TRUE(projection.projectHeroSpellAllowance(side, SpellID::SLOW, defender->unitId()));
	EXPECT_EQ(projection.battleHeroActionAllowanceCounts(side).heroActions, 0u);
	EXPECT_FALSE(projection.projectHeroOrderAllowance(side));
	EXPECT_EQ(projection.getWisdomCombatState(side).priorCasts(SpellID::BLESS), 1u);
	EXPECT_EQ(projection.getWisdomCombatState(side).priorCasts(SpellID::SLOW), 1u);
	EXPECT_EQ(battle()->getHeroActionAllowances(side), authoritative);
	EXPECT_EQ(wisdomBytes(battle()->getWisdomCombatState(side)), wisdom);
	EXPECT_EQ(rng(), random);
	projection.nextRound();
	EXPECT_EQ(projection.battleHeroActionAllowanceCounts(side).heroActions, 1u);
	EXPECT_EQ(projection.battleHeroActionAllowanceCounts(side).spellActions, 0u);
	ASSERT_TRUE(projection.projectHeroOrderAllowance(side));
	EXPECT_EQ(projection.battleHeroActionAllowanceCounts(side).heroActions, 0u);
	EXPECT_EQ(battle()->getHeroActionAllowances(side), authoritative);
	EXPECT_EQ(wisdomBytes(battle()->getWisdomCombatState(side)), wisdom);
	EXPECT_EQ(rng(), random);
}





TEST_F(NewHorizonsWarcastingTest, PreparedAllowanceCommitCannotBeReplayed)
{
	prepareWarcasting(1, true);
	WarcastingEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle projection(&environment, callback);
	const auto prepared = projection.prepareHeroSpellAllowance(BattleSide::ATTACKER, SpellID::BLESS);
	ASSERT_TRUE(prepared);
	ASSERT_TRUE(projection.beginProjectedHeroAction(BattleSide::ATTACKER, *prepared));
	ASSERT_TRUE(projection.projectAcceptedHeroSpell(BattleSide::ATTACKER, SpellID::BLESS,
		attacker->unitId(), *prepared));
	const auto afterCommit = projection.getHeroActionAllowances(BattleSide::ATTACKER);
	EXPECT_FALSE(projection.projectAcceptedHeroSpell(BattleSide::ATTACKER, SpellID::BLESS,
		attacker->unitId(), *prepared));
	EXPECT_EQ(projection.getHeroActionAllowances(BattleSide::ATTACKER), afterCommit);
}

TEST_F(NewHorizonsWarcastingTest, StalePreparedActionCannotClearEffectsBeforeCommitRejection)
{
	prepareWarcasting(1, true);
	const auto side = BattleSide::ATTACKER;
	const auto round = battle()->battleGetRound();
	battle()->getSide(side).heroActionAllowances.grantAllowance(
		HeroActionAllowanceState::AllowanceKind::ORDER,
		HeroActionAllowanceState::GrantSource::PERK, round);
	auto marker = ordinaryLuckMarker(side);
	battle()->addOrUpdateUnitBonus(attacker, *marker, true);

	WarcastingEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle projection(&environment, callback);
	const auto staleSpell = projection.prepareHeroSpellAllowance(side, SpellID::BLESS);
	ASSERT_TRUE(staleSpell);

	// A typed Order consumes a different grant without crossing the Hero-action
	// boundary. It makes the prepared spell token stale while preserving effects.
	ASSERT_TRUE(projection.projectHeroOrderAllowance(side));
	auto projectedAttacker = projection.battleGetUnitByID(attacker->unitId());
	ASSERT_NE(projectedAttacker, nullptr);
	EXPECT_TRUE(projectedAttacker->hasBonusOfType(BonusType::LUCK));

	EXPECT_FALSE(projection.beginProjectedHeroAction(side, *staleSpell));
	EXPECT_FALSE(projection.projectAcceptedHeroSpell(side, SpellID::BLESS,
		attacker->unitId(), *staleSpell));
	EXPECT_TRUE(projectedAttacker->hasBonusOfType(BonusType::LUCK));
}

TEST_F(NewHorizonsWarcastingTest, BegunOrderTokenCannotAuthorizeSameEpochSpellCommit)
{
	prepareWarcasting(1, true);
	const auto side = BattleSide::ATTACKER;
	const auto round = battle()->battleGetRound();
	battle()->getSide(side).heroActionAllowances.grantAllowance(
		HeroActionAllowanceState::AllowanceKind::ORDER,
		HeroActionAllowanceState::GrantSource::PERK, round);
	auto marker = ordinaryLuckMarker(side);
	battle()->addOrUpdateUnitBonus(attacker, *marker, true);

	WarcastingEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle projection(&environment, callback);
	const auto spell = projection.prepareHeroSpellAllowance(side, SpellID::BLESS);
	const auto order = projection.prepareHeroOrderAllowance(side);
	ASSERT_TRUE(spell);
	ASSERT_TRUE(order);
	EXPECT_EQ(spell->action.receipt.allowance, HeroActionAllowanceState::AllowanceKind::HERO);
	EXPECT_EQ(order->action.receipt.allowance, HeroActionAllowanceState::AllowanceKind::ORDER);

	ASSERT_TRUE(projection.beginProjectedHeroAction(side, *order));
	EXPECT_FALSE(projection.projectAcceptedHeroSpell(side, SpellID::BLESS,
		attacker->unitId(), *spell));
	auto projectedAttacker = projection.battleGetUnitByID(attacker->unitId());
	ASSERT_NE(projectedAttacker, nullptr);
	EXPECT_TRUE(projectedAttacker->hasBonusOfType(BonusType::LUCK));

	ASSERT_TRUE(projection.projectAcceptedHeroOrder(side, *order));
	EXPECT_TRUE(projectedAttacker->hasBonusOfType(BonusType::LUCK));
}









TEST_F(NewHorizonsWarcastingTest, OrdinarySpellThenOrderSharesOneActionAndRejectsAtomically)
{
	prepareWarcasting();
	ASSERT_TRUE(cast(SpellID::BLESS, attacker));
	const auto allowances = battle()->getHeroActionAllowances(BattleSide::ATTACKER);
	const auto mana = attackerSideHero->getManaAvailable();
	const auto actions = server.startedActions.size();
	const auto history = battle()->getSide(BattleSide::ATTACKER).usedSpellsHistory;
	EXPECT_FALSE(issue(HeroCommand::CHARGE));
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER), allowances);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(server.startedActions.size(), actions);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).usedSpellsHistory, history);
	EXPECT_FALSE(battle()->getHeroOrderState(BattleSide::ATTACKER));
}

TEST_F(NewHorizonsWarcastingTest, OrdinaryOrderThenSpellSharesOneActionAndRejectsAtomically)
{
	prepareWarcasting();
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	const auto allowances = battle()->getHeroActionAllowances(BattleSide::ATTACKER);
	const auto mana = attackerSideHero->getManaAvailable();
	const auto actions = server.startedActions.size();
	const auto order = battle()->getHeroOrderState(BattleSide::ATTACKER);
	EXPECT_FALSE(cast(SpellID::BLESS, attacker));
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER), allowances);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(server.startedActions.size(), actions);
	EXPECT_EQ(battle()->getHeroOrderState(BattleSide::ATTACKER), order);
	EXPECT_TRUE(server.castsOf(SpellID::BLESS).empty());
}



TEST_F(NewHorizonsWarcastingTest, GenericTypedSpellAndHeroActionsRemainSeparate)
{
	prepareWarcasting(1, true);
	const auto side = BattleSide::ATTACKER;
	const auto round = battle()->getRound();
	// Generic test grant, explicitly not produced by current passive Metamagic.
	grantSpellAllowanceForFixture(side);
	ASSERT_TRUE(cast(SpellID::BLESS, attacker));
	EXPECT_EQ(battle()->getHeroActionAllowances(side).remainingCounts(round).heroActions, 1u);
	EXPECT_EQ(battle()->getHeroActionAllowances(side).remainingCounts(round).spellActions, 0u);
	ASSERT_TRUE(cast(SpellID::SLOW, defender));
	EXPECT_EQ(battle()->getHeroActionAllowances(side).remainingCounts(round).heroActions, 0u);
	EXPECT_EQ(battle()->getHeroActionAllowances(side).remainingCounts(round).spellActions, 0u);
	EXPECT_EQ(battle()->getSide(side).castSpellsCount, 2u);
	EXPECT_EQ(battle()->getSide(side).usedSpellsHistory,
		(std::vector<SpellID>{SpellID::BLESS, SpellID::SLOW}));
	EXPECT_EQ(battle()->getWisdomCombatState(side).priorCasts(SpellID::BLESS), 1u);
	EXPECT_EQ(battle()->getWisdomCombatState(side).priorCasts(SpellID::SLOW), 1u);
}
