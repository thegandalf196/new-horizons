/*
 * NewHorizonsWarcastingTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"
#include "../../../lib/spells/CSpell.h"

#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/HeroActionAllowanceState.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/NewHorizonsMetamagicMarks.h"
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
constexpr auto counterspellKey = "new-horizons:counterspell";
constexpr auto sorcerySkill = "new-horizons:sorceryMagic";
constexpr auto countermagePerk = "new-horizons:sorceryMagic.countermage";
constexpr auto sorceryBasicPerk = "new-horizons:sorceryMagic.overcharger";

std::shared_ptr<Bonus> testTimeStopMarker(BattleSide side)
{
	auto marker = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP,
		BonusSource::OTHER, 1, BonusSourceID());
	marker->parameters = std::make_shared<BonusParameters>(static_cast<int32_t>(side));
	return marker;
}

SpellID counterspellId()
{
	return SpellID(SpellID::decode(counterspellKey));
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
	bool historicalCounterspell = false;
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
		if(historicalCounterspell)
			magicRules["spells"][counterspellKey].Struct().erase("active");
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
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = hero->getPerkState().prepareOffer(rankLookup, seed, hero->getPerkOfferWeightContext());
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
			// Current points belong to Tower; this is no legacy extra-action producer.
			attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:neela")));
			ASSERT_TRUE(newHorizonsMetamagic::usesPoints(attackerSideHero->getPerkState().rules));
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
		for(const auto spell : {SpellID::HASTE, SpellID::SLOW, SpellID::MAGIC_ARROW})
			attackerSideHero->addSpellToSpellbook(spell);

		startBattle();
		attacker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 10);
		const int initialDefenderCount = 10;
		defender = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex),
			initialDefenderCount);
		beginCombat();
		if(withMetamagic)
		{
			EXPECT_EQ(battle()->getMetamagicPointState(BattleSide::ATTACKER).points, 1);
			EXPECT_EQ(newHorizonsMagic::metamagicRank(attackerSideHero), 0);
			EXPECT_EQ(newHorizonsMagic::metamagicCapacity(attackerSideHero), 0);
		}

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

	bool cast(SpellID spell, const CStack * target, bool followup = false, bool grand = false)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = spell;
		action.metamagicFollowup = followup;
		action.metamagicGrand = grand;
		action.aimToUnit(target);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	bool declineMetamagic()
	{
		BattleAction retired = BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::NONE);
		retired.metamagicDecline = true;
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), retired);
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
	EXPECT_NO_THROW(EXPECT_FALSE(battle()->battleCanUseMetamagicFollowup(BattleSide::ATTACKER)));
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

TEST_F(NewHorizonsWarcastingTest, TypedOrderAfterHeroSpellPreservesCurrentPointsAndRoundTrips)
{
	prepareWarcasting(1, true);
	const auto side = BattleSide::ATTACKER;
	const auto round = battle()->battleGetRound();
	const auto orderGrantId = grantOrderAllowanceForFixture(side);
	const auto beforeSpellCounts = battle()->getHeroActionAllowances(side).remainingCounts(round);
	EXPECT_EQ(beforeSpellCounts.heroActions, 1u);
	EXPECT_EQ(beforeSpellCounts.orderActions, 1u);
	EXPECT_EQ(beforeSpellCounts.spellActions, 0u);

	const auto points = battle()->getMetamagicPointState(side);
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	const auto & afterSpell = battle()->getSide(side);
	ASSERT_EQ(afterSpell.castSpellsCount, 1u);
	ASSERT_EQ(afterSpell.metamagicPendingCount, 0u);
	ASSERT_EQ(afterSpell.metamagicSequenceSpells, (std::vector<SpellID>{}));
	EXPECT_EQ(afterSpell.metamagicPointState, points);
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
	EXPECT_EQ(afterOrder.usedSpellsHistory, (std::vector<SpellID>{SpellID::HASTE}));
	EXPECT_EQ(afterOrder.metamagicPendingCount, 0u);
	EXPECT_EQ(afterOrder.metamagicSequenceSpells, (std::vector<SpellID>{}));
	const auto afterOrderCounts = afterOrder.heroActionAllowances.remainingCounts(round);
	EXPECT_EQ(afterOrderCounts.heroActions, 0u);
	EXPECT_EQ(afterOrderCounts.orderActions, 0u);
	EXPECT_EQ(afterOrderCounts.spellActions, 0u);
	EXPECT_TRUE(afterOrder.heroActionAllowances.grants.empty());
	EXPECT_EQ(afterOrder.metamagicPointState, points);

	auto restored = CMemorySerializer::deepCopy(*battle(), gameState().get());
	ASSERT_NE(restored, nullptr);
	const auto & restoredSide = restored->getSide(side);
	EXPECT_EQ(restoredSide.castSpellsCount, 1u);
	EXPECT_EQ(restoredSide.usedSpellsHistory, afterOrder.usedSpellsHistory);
	EXPECT_EQ(restoredSide.metamagicPendingCount, 0u);
	EXPECT_EQ(restoredSide.metamagicSequenceSpells, afterOrder.metamagicSequenceSpells);
	EXPECT_EQ(restoredSide.metamagicPointState, points);
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
	const auto points = battle()->getMetamagicPointState(side);
	const auto wisdom = wisdomBytes(battle()->getWisdomCombatState(side));
	const auto random = rng();
	WarcastingEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle projection(&environment, callback);
	ASSERT_EQ(projection.battleHeroActionAllowanceCounts(side).heroActions, 1u);
	ASSERT_EQ(projection.battleHeroActionAllowanceCounts(side).spellActions, 1u);
	const auto typed = projection.prepareHeroSpellAllowance(side, SpellID::HASTE, false, false);
	ASSERT_TRUE(typed);
	EXPECT_EQ(typed->action.receipt.allowance, HeroActionAllowanceState::AllowanceKind::SPELL);
	EXPECT_EQ(typed->action.receipt.source, HeroActionAllowanceState::GrantSource::PERK);
	ASSERT_TRUE(projection.projectHeroSpellAllowance(side, SpellID::HASTE, attacker->unitId(), false, false));
	EXPECT_EQ(projection.battleHeroActionAllowanceCounts(side).heroActions, 1u);
	EXPECT_EQ(projection.battleHeroActionAllowanceCounts(side).spellActions, 0u);
	EXPECT_FALSE(projection.prepareHeroSpellAllowance(side, SpellID::SLOW, true, false));
	const auto ordinary = projection.prepareHeroSpellAllowance(side, SpellID::SLOW, false, false);
	ASSERT_TRUE(ordinary);
	EXPECT_EQ(ordinary->action.receipt.allowance, HeroActionAllowanceState::AllowanceKind::HERO);
	ASSERT_TRUE(projection.projectHeroSpellAllowance(side, SpellID::SLOW, defender->unitId(), false, false));
	EXPECT_EQ(projection.battleHeroActionAllowanceCounts(side).heroActions, 0u);
	EXPECT_FALSE(projection.projectHeroOrderAllowance(side));
	EXPECT_EQ(projection.getMetamagicPendingCount(side), 0);
	EXPECT_EQ(projection.getMetamagicUsesConsumed(side), 0);
	EXPECT_EQ(projection.getMetamagicPointState(side), points);
	EXPECT_EQ(projection.getWisdomCombatState(side).priorCasts(SpellID::HASTE), 1u);
	EXPECT_EQ(projection.getWisdomCombatState(side).priorCasts(SpellID::SLOW), 1u);
	EXPECT_EQ(battle()->getHeroActionAllowances(side), authoritative);
	EXPECT_EQ(battle()->getMetamagicPointState(side), points);
	EXPECT_EQ(wisdomBytes(battle()->getWisdomCombatState(side)), wisdom);
	EXPECT_EQ(rng(), random);
	projection.nextRound();
	EXPECT_EQ(projection.battleHeroActionAllowanceCounts(side).heroActions, 1u);
	EXPECT_EQ(projection.battleHeroActionAllowanceCounts(side).spellActions, 0u);
	ASSERT_TRUE(projection.projectHeroOrderAllowance(side));
	EXPECT_EQ(projection.battleHeroActionAllowanceCounts(side).heroActions, 0u);
	EXPECT_EQ(battle()->getHeroActionAllowances(side), authoritative);
	EXPECT_EQ(battle()->getMetamagicPointState(side), points);
	EXPECT_EQ(wisdomBytes(battle()->getWisdomCombatState(side)), wisdom);
	EXPECT_EQ(rng(), random);
}

TEST_F(NewHorizonsWarcastingTest, HypotheticalHeroReceiptExpiresOnlyItsTimeStopAndWard)
{
	prepareWarcasting(1, true);
	auto attackerMarker = testTimeStopMarker(BattleSide::ATTACKER);
	auto defenderMarker = testTimeStopMarker(BattleSide::DEFENDER);
	battle()->addOrUpdateUnitBonus(attacker, *attackerMarker, true);
	battle()->addOrUpdateUnitBonus(defender, *defenderMarker, true);
	battle()->notePendingTimeStopHeroAction(BattleSide::ATTACKER);
	battle()->notePendingTimeStopHeroAction(BattleSide::DEFENDER);
	battle()->getSide(BattleSide::ATTACKER).counterspellArmed = true;
	battle()->getSide(BattleSide::ATTACKER).metamagicCountersequenceArmed = true;
	battle()->getSide(BattleSide::DEFENDER).counterspellArmed = true;

	WarcastingEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle projection(&environment, callback);
	const auto prepared = projection.prepareHeroSpellAllowance(BattleSide::ATTACKER, SpellID::HASTE, false, false);
	ASSERT_TRUE(prepared);
	EXPECT_EQ(prepared->action.receipt.allowance, HeroActionAllowanceState::AllowanceKind::HERO);
	ASSERT_TRUE(projection.beginProjectedHeroAction(BattleSide::ATTACKER, *prepared));

	EXPECT_FALSE(projection.getCounterspellArmed(BattleSide::ATTACKER));
	EXPECT_FALSE(projection.getMetamagicCountersequenceArmed(BattleSide::ATTACKER));
	EXPECT_TRUE(projection.getCounterspellArmed(BattleSide::DEFENDER));
	auto projectedAttacker = projection.battleGetUnitByID(attacker->unitId());
	auto projectedDefender = projection.battleGetUnitByID(defender->unitId());
	ASSERT_NE(projectedAttacker, nullptr);
	ASSERT_NE(projectedDefender, nullptr);
	EXPECT_FALSE(projectedAttacker->isTimeStopped());
	EXPECT_TRUE(projectedDefender->isTimeStopped());
	EXPECT_EQ(projection.getProjectedPendingTimeStopHeroActionSides(), 2u);
	EXPECT_TRUE(attacker->isTimeStopped());
	EXPECT_TRUE(defender->isTimeStopped());
	EXPECT_EQ(battle()->getPendingTimeStopHeroActionSides(), 3u);
	EXPECT_TRUE(battle()->getSide(BattleSide::ATTACKER).counterspellArmed);
}

TEST_F(NewHorizonsWarcastingTest, TypedSpellAndOrderReceiptsPreserveOwnTimeStopAndWard)
{
	prepareWarcasting(1, true);
	const auto round = battle()->battleGetRound();
	auto & allowances = battle()->getSide(BattleSide::ATTACKER).heroActionAllowances;
	allowances.grantAllowance(HeroActionAllowanceState::AllowanceKind::SPELL,
		HeroActionAllowanceState::GrantSource::PERK, round);
	allowances.grantAllowance(HeroActionAllowanceState::AllowanceKind::ORDER,
		HeroActionAllowanceState::GrantSource::PERK, round);
	auto marker = testTimeStopMarker(BattleSide::ATTACKER);
	battle()->addOrUpdateUnitBonus(attacker, *marker, true);
	battle()->notePendingTimeStopHeroAction(BattleSide::ATTACKER);
	battle()->getSide(BattleSide::ATTACKER).counterspellArmed = true;
	battle()->getSide(BattleSide::ATTACKER).metamagicCountersequenceArmed = true;

	WarcastingEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle projection(&environment, callback);
	auto spell = projection.prepareHeroSpellAllowance(BattleSide::ATTACKER, SpellID::HASTE, false, false);
	ASSERT_TRUE(spell);
	EXPECT_EQ(spell->action.receipt.allowance, HeroActionAllowanceState::AllowanceKind::SPELL);
	EXPECT_EQ(spell->action.receipt.source, HeroActionAllowanceState::GrantSource::PERK);
	ASSERT_TRUE(projection.beginProjectedHeroAction(BattleSide::ATTACKER, *spell));
	ASSERT_TRUE(projection.projectAcceptedHeroSpell(BattleSide::ATTACKER, SpellID::HASTE,
		attacker->unitId(), false, false, false, false, *spell));
	EXPECT_TRUE(projection.getCounterspellArmed(BattleSide::ATTACKER));
	EXPECT_TRUE(projection.getMetamagicCountersequenceArmed(BattleSide::ATTACKER));
	EXPECT_TRUE(projection.battleGetUnitByID(attacker->unitId())->isTimeStopped());
	EXPECT_EQ(projection.getProjectedPendingTimeStopHeroActionSides(), 1u);

	auto order = projection.prepareHeroOrderAllowance(BattleSide::ATTACKER);
	ASSERT_TRUE(order);
	EXPECT_EQ(order->action.receipt.allowance, HeroActionAllowanceState::AllowanceKind::ORDER);
	ASSERT_TRUE(projection.beginProjectedHeroAction(BattleSide::ATTACKER, *order));
	ASSERT_TRUE(projection.projectAcceptedHeroOrder(BattleSide::ATTACKER, *order));
	EXPECT_TRUE(projection.getCounterspellArmed(BattleSide::ATTACKER));
	EXPECT_TRUE(projection.getMetamagicCountersequenceArmed(BattleSide::ATTACKER));
	EXPECT_TRUE(projection.battleGetUnitByID(attacker->unitId())->isTimeStopped());
	EXPECT_EQ(projection.getProjectedPendingTimeStopHeroActionSides(), 1u);
}

TEST_F(NewHorizonsWarcastingTest, PreparedAllowanceCommitCannotBeReplayed)
{
	prepareWarcasting(1, true);
	WarcastingEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle projection(&environment, callback);
	const auto prepared = projection.prepareHeroSpellAllowance(BattleSide::ATTACKER, SpellID::HASTE, false, false);
	ASSERT_TRUE(prepared);
	ASSERT_TRUE(projection.beginProjectedHeroAction(BattleSide::ATTACKER, *prepared));
	ASSERT_TRUE(projection.projectAcceptedHeroSpell(BattleSide::ATTACKER, SpellID::HASTE,
		attacker->unitId(), false, false, false, false, *prepared));
	const auto afterCommit = projection.getHeroActionAllowances(BattleSide::ATTACKER);
	EXPECT_FALSE(projection.projectAcceptedHeroSpell(BattleSide::ATTACKER, SpellID::HASTE,
		attacker->unitId(), false, false, false, false, *prepared));
	EXPECT_EQ(projection.getHeroActionAllowances(BattleSide::ATTACKER), afterCommit);
	EXPECT_EQ(projection.getMetamagicUsesConsumed(BattleSide::ATTACKER), 0);
}

TEST_F(NewHorizonsWarcastingTest, StalePreparedActionCannotClearEffectsBeforeCommitRejection)
{
	prepareWarcasting(1, true);
	const auto side = BattleSide::ATTACKER;
	const auto round = battle()->battleGetRound();
	battle()->getSide(side).heroActionAllowances.grantAllowance(
		HeroActionAllowanceState::AllowanceKind::ORDER,
		HeroActionAllowanceState::GrantSource::PERK, round);
	auto marker = testTimeStopMarker(side);
	battle()->addOrUpdateUnitBonus(attacker, *marker, true);
	battle()->notePendingTimeStopHeroAction(side);
	battle()->getSide(side).counterspellArmed = true;
	battle()->getSide(side).metamagicCountersequenceArmed = true;

	WarcastingEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle projection(&environment, callback);
	const auto staleSpell = projection.prepareHeroSpellAllowance(side, SpellID::HASTE, false, false);
	ASSERT_TRUE(staleSpell);

	// A typed Order consumes a different grant without crossing the Hero-action
	// boundary. It makes the prepared spell token stale while preserving effects.
	ASSERT_TRUE(projection.projectHeroOrderAllowance(side));
	EXPECT_TRUE(projection.getCounterspellArmed(side));
	EXPECT_TRUE(projection.getMetamagicCountersequenceArmed(side));
	EXPECT_EQ(projection.getProjectedPendingTimeStopHeroActionSides(), 1u);
	auto projectedAttacker = projection.battleGetUnitByID(attacker->unitId());
	ASSERT_NE(projectedAttacker, nullptr);
	EXPECT_TRUE(projectedAttacker->isTimeStopped());

	EXPECT_FALSE(projection.beginProjectedHeroAction(side, *staleSpell));
	EXPECT_FALSE(projection.projectAcceptedHeroSpell(side, SpellID::HASTE,
		attacker->unitId(), false, false, false, false, *staleSpell));
	EXPECT_TRUE(projection.getCounterspellArmed(side));
	EXPECT_TRUE(projection.getMetamagicCountersequenceArmed(side));
	EXPECT_EQ(projection.getProjectedPendingTimeStopHeroActionSides(), 1u);
	EXPECT_TRUE(projectedAttacker->isTimeStopped());
}

TEST_F(NewHorizonsWarcastingTest, BegunOrderTokenCannotAuthorizeSameEpochSpellCommit)
{
	prepareWarcasting(1, true);
	const auto side = BattleSide::ATTACKER;
	const auto round = battle()->battleGetRound();
	battle()->getSide(side).heroActionAllowances.grantAllowance(
		HeroActionAllowanceState::AllowanceKind::ORDER,
		HeroActionAllowanceState::GrantSource::PERK, round);
	auto marker = testTimeStopMarker(side);
	battle()->addOrUpdateUnitBonus(attacker, *marker, true);
	battle()->notePendingTimeStopHeroAction(side);
	battle()->getSide(side).counterspellArmed = true;
	battle()->getSide(side).metamagicCountersequenceArmed = true;

	WarcastingEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle projection(&environment, callback);
	const auto spell = projection.prepareHeroSpellAllowance(side, SpellID::HASTE, false, false);
	const auto order = projection.prepareHeroOrderAllowance(side);
	ASSERT_TRUE(spell);
	ASSERT_TRUE(order);
	EXPECT_EQ(spell->action.receipt.allowance, HeroActionAllowanceState::AllowanceKind::HERO);
	EXPECT_EQ(order->action.receipt.allowance, HeroActionAllowanceState::AllowanceKind::ORDER);

	ASSERT_TRUE(projection.beginProjectedHeroAction(side, *order));
	EXPECT_FALSE(projection.projectAcceptedHeroSpell(side, SpellID::HASTE,
		attacker->unitId(), false, false, false, false, *spell));
	EXPECT_TRUE(projection.getCounterspellArmed(side));
	EXPECT_TRUE(projection.getMetamagicCountersequenceArmed(side));
	EXPECT_EQ(projection.getProjectedPendingTimeStopHeroActionSides(), 1u);
	auto projectedAttacker = projection.battleGetUnitByID(attacker->unitId());
	ASSERT_NE(projectedAttacker, nullptr);
	EXPECT_TRUE(projectedAttacker->isTimeStopped());

	ASSERT_TRUE(projection.projectAcceptedHeroOrder(side, *order));
	EXPECT_TRUE(projection.getCounterspellArmed(side));
	EXPECT_TRUE(projection.getMetamagicCountersequenceArmed(side));
	EXPECT_TRUE(projectedAttacker->isTimeStopped());
}

TEST_F(NewHorizonsWarcastingTest, HypotheticalCounterspellUsesRealManaThresholdAndPreservesCurrentPoints)
{
	prepareWarcasting(1, true);
	const int listedCost = attackerSideHero->getListedSpellCost(SpellID(SpellID::HASTE).toSpell());
	const int wardCost = newHorizonsMagic::counterspellCost(listedCost, false, false);
	setTestSpellPointTotal(defenderSideHero, wardCost - 1);
	battle()->getSide(BattleSide::DEFENDER).counterspellArmed = true;

	const auto points = battle()->getMetamagicPointState(BattleSide::ATTACKER);
	const auto liveLedger = battle()->getHeroActionAllowances(BattleSide::ATTACKER);
	const auto liveWisdom = wisdomBytes(battle()->getWisdomCombatState(BattleSide::ATTACKER));
	const auto random = rng();
	WarcastingEnvironment environment(gameState());
	// Counterspell resolution needs the opposing hero's saved perk and mana.
	// Use an all-knowing battle view here; the player-scoped attacker view
	// intentionally hides the defender hero.
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor::SPECTATOR);
	ASSERT_NE(callback->battleGetFightingHero(BattleSide::DEFENDER), nullptr);
	HypotheticBattle lowManaProjection(&environment, callback);
	auto lowManaPrepared = lowManaProjection.prepareHeroSpellAllowance(BattleSide::ATTACKER, SpellID::HASTE, false, false);
	ASSERT_TRUE(lowManaPrepared);
	ASSERT_TRUE(lowManaProjection.beginProjectedHeroAction(BattleSide::ATTACKER, *lowManaPrepared));
	auto outcome = lowManaProjection.resolveProjectedCounterspell(BattleSide::ATTACKER,
		SpellID(SpellID::HASTE).toSpell());
	EXPECT_TRUE(outcome.wardActive);
	EXPECT_EQ(outcome.wardSide, BattleSide::DEFENDER);
	ASSERT_TRUE(outcome.resolutionKnown);
	ASSERT_TRUE(outcome.manaCost.has_value());
	ASSERT_TRUE(outcome.negated.has_value());
	EXPECT_EQ(*outcome.manaCost, wardCost);
	EXPECT_FALSE(*outcome.negated);
	ASSERT_TRUE(lowManaProjection.projectAcceptedHeroSpell(BattleSide::ATTACKER, SpellID::HASTE,
		attacker->unitId(), false, false, outcome.wardActive, *outcome.negated, *lowManaPrepared));
	EXPECT_EQ(lowManaProjection.getMetamagicPendingCount(BattleSide::ATTACKER), 0);
	EXPECT_EQ(lowManaProjection.getMetamagicPointState(BattleSide::ATTACKER), points);
	EXPECT_EQ(lowManaProjection.getWisdomCombatState(BattleSide::ATTACKER).priorCasts(SpellID::HASTE), 1u);
	EXPECT_EQ(lowManaProjection.battleHeroActionAllowanceCounts(BattleSide::ATTACKER).heroActions, 0u);
	EXPECT_FALSE(lowManaProjection.getCounterspellArmed(BattleSide::DEFENDER));
	EXPECT_EQ(defenderSideHero->getManaAvailable(), wardCost - 1);

	setTestSpellPointTotal(defenderSideHero, wardCost);
	HypotheticBattle negatedProjection(&environment, callback);
	const auto prepared = negatedProjection.prepareHeroSpellAllowance(BattleSide::ATTACKER, SpellID::HASTE, false, false);
	ASSERT_TRUE(prepared);
	ASSERT_TRUE(negatedProjection.beginProjectedHeroAction(BattleSide::ATTACKER, *prepared));
	outcome = negatedProjection.resolveProjectedCounterspell(BattleSide::ATTACKER,
		SpellID(SpellID::HASTE).toSpell());
	ASSERT_TRUE(outcome.resolutionKnown);
	ASSERT_TRUE(outcome.manaCost.has_value());
	ASSERT_TRUE(outcome.negated.has_value());
	EXPECT_TRUE(*outcome.negated);
	EXPECT_EQ(*outcome.manaCost, wardCost);
	ASSERT_TRUE(negatedProjection.projectAcceptedHeroSpell(BattleSide::ATTACKER, SpellID::HASTE,
		attacker->unitId(), false, false, outcome.wardActive, *outcome.negated, *prepared));
	EXPECT_EQ(negatedProjection.getMetamagicPendingCount(BattleSide::ATTACKER), 0);
	EXPECT_EQ(negatedProjection.getMetamagicPointState(BattleSide::ATTACKER), points);
	EXPECT_EQ(negatedProjection.getWisdomCombatState(BattleSide::ATTACKER).priorCasts(SpellID::HASTE), 1u);
	EXPECT_EQ(negatedProjection.battleHeroActionAllowanceCounts(BattleSide::ATTACKER).heroActions, 0u);
	EXPECT_FALSE(negatedProjection.getCounterspellArmed(BattleSide::DEFENDER));
	EXPECT_EQ(defenderSideHero->getManaAvailable(), wardCost);
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER), liveLedger);
	EXPECT_EQ(wisdomBytes(battle()->getWisdomCombatState(BattleSide::ATTACKER)), liveWisdom);
	EXPECT_EQ(battle()->getMetamagicPointState(BattleSide::ATTACKER), points);
	EXPECT_EQ(rng(), random);
}

TEST_F(NewHorizonsWarcastingTest, HypotheticalCounterspellUsesCountermageCostWithoutSpendingMana)
{
	prepareWarcasting(1, true, true);
	const int listedCost = attackerSideHero->getListedSpellCost(SpellID(SpellID::HASTE).toSpell());
	const int wardCost = newHorizonsMagic::counterspellCost(listedCost, true, false);
	setTestSpellPointTotal(defenderSideHero, wardCost - 1);
	battle()->getSide(BattleSide::DEFENDER).counterspellArmed = true;

	WarcastingEnvironment environment(gameState());
	// Counterspell resolution needs the opposing hero's saved perk and mana.
	// Use an all-knowing battle view here; the player-scoped attacker view
	// intentionally hides the defender hero.
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor::SPECTATOR);
	ASSERT_NE(callback->battleGetFightingHero(BattleSide::DEFENDER), nullptr);
	HypotheticBattle projection(&environment, callback);
	const auto outcome = projection.resolveProjectedCounterspell(BattleSide::ATTACKER,
		SpellID(SpellID::HASTE).toSpell());
	EXPECT_TRUE(outcome.wardActive);
	ASSERT_TRUE(outcome.resolutionKnown);
	ASSERT_TRUE(outcome.manaCost.has_value());
	ASSERT_TRUE(outcome.negated.has_value());
	EXPECT_EQ(*outcome.manaCost, wardCost);
	EXPECT_FALSE(*outcome.negated);
	EXPECT_EQ(defenderSideHero->getManaAvailable(), wardCost - 1);
}

TEST_F(NewHorizonsWarcastingTest, PlayerViewKeepsHiddenArmedCounterspellUnresolved)
{
	prepareWithDefenderBasicSorcery();
	battle()->getSide(BattleSide::DEFENDER).counterspellArmed = true;
	setTestSpellPointTotal(defenderSideHero, 0);
	ASSERT_FALSE(defenderSideHero->hasActivePerk(std::string(sorcerySkill), std::string(countermagePerk)));

	WarcastingEnvironment environment(gameState());
	auto projectFromAttackerView = [&]()
	{
		auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		EXPECT_EQ(callback->battleGetFightingHero(BattleSide::DEFENDER), nullptr);
		EXPECT_TRUE(callback->battleWasCounterspellArmed(BattleSide::DEFENDER));
		HypotheticBattle projection(&environment, callback);
		const auto allowancesBefore = projection.getHeroActionAllowances(BattleSide::ATTACKER);
		const auto outcome = projection.resolveProjectedCounterspell(BattleSide::ATTACKER,
			SpellID(SpellID::HASTE).toSpell());
		EXPECT_TRUE(outcome.wardActive);
		EXPECT_EQ(outcome.wardSide, BattleSide::DEFENDER);
		EXPECT_FALSE(outcome.resolutionKnown);
		EXPECT_FALSE(outcome.manaCost.has_value());
		EXPECT_FALSE(outcome.negated.has_value());

		// The convenience wrapper must not turn an unknown ward into a resolved
		// cast or spend the projected allowance while doing so.
		EXPECT_FALSE(projection.projectHeroSpellAllowance(BattleSide::ATTACKER, SpellID::HASTE,
			attacker->unitId(), false, false));
		EXPECT_EQ(projection.getHeroActionAllowances(BattleSide::ATTACKER), allowancesBefore);
		EXPECT_TRUE(projection.getCounterspellArmed(BattleSide::DEFENDER));
		return outcome;
	};

	const auto withoutCountermage = projectFromAttackerView();
	acquireCountermageThroughAdvancedOffer(defenderSideHero);
	setTestSpellPointTotal(defenderSideHero, 1000);
	const auto withCountermage = projectFromAttackerView();

	EXPECT_EQ(withCountermage.wardActive, withoutCountermage.wardActive);
	EXPECT_EQ(withCountermage.wardSide, withoutCountermage.wardSide);
	EXPECT_EQ(withCountermage.resolutionKnown, withoutCountermage.resolutionKnown);
	EXPECT_EQ(withCountermage.manaCost, withoutCountermage.manaCost);
	EXPECT_EQ(withCountermage.negated, withoutCountermage.negated);
}

TEST_F(NewHorizonsWarcastingTest, TypedCounterspellReplacesOldCountersequenceProvenance)
{
	historicalCounterspell = true;
	prepareWarcasting();
	const auto round = battle()->battleGetRound();
	battle()->getSide(BattleSide::ATTACKER).heroActionAllowances.grantAllowance(
		HeroActionAllowanceState::AllowanceKind::SPELL,
		HeroActionAllowanceState::GrantSource::PERK, round);
	battle()->getSide(BattleSide::ATTACKER).counterspellArmed = true;
	battle()->getSide(BattleSide::ATTACKER).metamagicCountersequenceArmed = true;

	WarcastingEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle projection(&environment, callback);
	const auto prepared = projection.prepareHeroSpellAllowance(BattleSide::ATTACKER, counterspellId(), false, false);
	ASSERT_TRUE(prepared);
	EXPECT_EQ(prepared->action.receipt.allowance, HeroActionAllowanceState::AllowanceKind::SPELL);
	ASSERT_TRUE(projection.beginProjectedHeroAction(BattleSide::ATTACKER, *prepared));
	const auto id = counterspellId();
	ASSERT_TRUE(id.hasValue());
	ASSERT_TRUE(projection.projectAcceptedHeroSpell(BattleSide::ATTACKER, id,
		std::numeric_limits<uint32_t>::max(), false, false, false, false, *prepared));
	EXPECT_TRUE(projection.getCounterspellArmed(BattleSide::ATTACKER));
	EXPECT_FALSE(projection.getMetamagicCountersequenceArmed(BattleSide::ATTACKER));
}

TEST_F(NewHorizonsWarcastingTest, OrdinarySpellThenOrderSharesOneActionAndRejectsAtomically)
{
	prepareWarcasting();
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
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
	EXPECT_FALSE(cast(SpellID::HASTE, attacker));
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER), allowances);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(server.startedActions.size(), actions);
	EXPECT_EQ(battle()->getHeroOrderState(BattleSide::ATTACKER), order);
	EXPECT_TRUE(server.castsOf(SpellID::HASTE).empty());
}

TEST_F(NewHorizonsWarcastingTest, RetiredMetamagicDeclineRejectsWithoutChangingAllowances)
{
	prepareWarcasting(1, true);
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	const auto allowances = battle()->getHeroActionAllowances(BattleSide::ATTACKER);
	const auto actions = server.startedActions.size();
	EXPECT_FALSE(declineMetamagic());
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER), allowances);
	EXPECT_EQ(server.startedActions.size(), actions);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).castSpellsCount, 1u);
}

TEST_F(NewHorizonsWarcastingTest, GenericTypedSpellAndHeroActionsRemainSeparateWhileCurrentPointsRejectLegacyFollowup)
{
	prepareWarcasting(1, true);
	const auto side = BattleSide::ATTACKER;
	const auto round = battle()->getRound();
	// Generic test grant, explicitly not produced by current passive Metamagic.
	grantSpellAllowanceForFixture(side);
	const auto points = battle()->getMetamagicPointState(side);
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	EXPECT_EQ(battle()->getHeroActionAllowances(side).remainingCounts(round).heroActions, 1u);
	EXPECT_EQ(battle()->getHeroActionAllowances(side).remainingCounts(round).spellActions, 0u);
	const auto ledger = battle()->getHeroActionAllowances(side);
	const auto wisdom = wisdomBytes(battle()->getWisdomCombatState(side));
	const auto history = battle()->getSide(side).usedSpellsHistory;
	const auto mana = attackerSideHero->getManaAvailable();
	const auto actions = server.startedActions.size();
	const auto random = rng();
	EXPECT_FALSE(cast(SpellID::SLOW, defender, true));
	EXPECT_EQ(battle()->getHeroActionAllowances(side), ledger);
	EXPECT_EQ(wisdomBytes(battle()->getWisdomCombatState(side)), wisdom);
	EXPECT_EQ(battle()->getSide(side).usedSpellsHistory, history);
	EXPECT_EQ(battle()->getMetamagicPointState(side), points);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(server.startedActions.size(), actions);
	EXPECT_EQ(rng(), random);
	ASSERT_TRUE(cast(SpellID::SLOW, defender));
	EXPECT_EQ(battle()->getHeroActionAllowances(side).remainingCounts(round).heroActions, 0u);
	EXPECT_EQ(battle()->getHeroActionAllowances(side).remainingCounts(round).spellActions, 0u);
	EXPECT_EQ(battle()->getMetamagicPendingCount(side), 0);
	EXPECT_EQ(battle()->getMetamagicPointState(side), points);
	EXPECT_EQ(battle()->getSide(side).castSpellsCount, 2u);
	EXPECT_EQ(battle()->getSide(side).usedSpellsHistory,
		(std::vector<SpellID>{SpellID::HASTE, SpellID::SLOW}));
	EXPECT_EQ(battle()->getWisdomCombatState(side).priorCasts(SpellID::HASTE), 1u);
	EXPECT_EQ(battle()->getWisdomCombatState(side).priorCasts(SpellID::SLOW), 1u);
}

TEST_F(NewHorizonsWarcastingTest, CounterspelledOrdinaryCastSpendsManaAndSharedActionWithoutApplyingSpell)
{
	prepareWarcasting();
	const auto hasteEffect = Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::HASTE)));
	ASSERT_FALSE(attacker->hasBonus(hasteEffect));
	battle()->getSide(BattleSide::DEFENDER).counterspellArmed = true;
	const auto mana = attackerSideHero->getManaAvailable();
	const auto cost = battle()->battleGetSpellCost(SpellID(SpellID::HASTE).toSpell(), attackerSideHero);
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - cost);
	EXPECT_FALSE(attacker->hasBonus(hasteEffect));
	const auto casts = server.castsOf(SpellID::HASTE);
	ASSERT_EQ(casts.size(), 1u);
	EXPECT_TRUE(casts.front().announcement.counterspellNegated);
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER).remainingCounts(battle()->getRound()).heroActions, 0u);
	EXPECT_FALSE(issue(HeroCommand::CHARGE));
}
