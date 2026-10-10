/*
 * NewHorizonsReactiveWeaveTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/spells/CSpellHandler.h"
#include "HeroCommandFixture.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/NewHorizonsWarcasting.h"
#include "../../../lib/gameState/GameStatePackVisitor.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"

namespace
{
using State = AlternatingHeroActionState;
using Action = State::Action;
constexpr auto skillKey = "new-horizons:warcasting";
constexpr auto reactiveKey = "new-horizons:warcasting.reactiveWeave";
constexpr auto martialKey = "new-horizons:warcasting.martialChanneling";

class ReactiveEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit ReactiveEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

/// Decode the actual current binary stream, then inject a semantic corruption
/// at the side/rules boundary before BattleInfo's post-decode admission runs.
/// Other fields still use the real BinaryDeserializer and saved object IDs.
class MalformedReactiveReader
{
	BinaryDeserializer & reader;
	int corruption;
public:
	using Version = ESerializationVersion;
	static constexpr bool saving = false;
	MalformedReactiveReader(BinaryDeserializer & reader, int corruption)
		: reader(reader), corruption(corruption) {}
	bool hasFeature(Version feature) const { return reader.hasFeature(feature); }
	template<typename T> MalformedReactiveReader & operator&(T & value)
	{
		reader & value;
		return *this;
	}
	MalformedReactiveReader & operator&(BattleSideArray<SideInBattle> & sides)
	{
		reader & sides;
		auto & side = sides.at(BattleSide::DEFENDER);
		if(corruption == 1)
			side.heroID = ObjectInstanceID::NONE;
		else if(corruption == 2)
			++side.warcastingState.reactiveEmpowermentPercent;
		else if(corruption == 3)
			side.warcastingState.reactiveExpiryRound = INT32_MAX;
		return *this;
	}
	MalformedReactiveReader & operator&(JsonNode & rules)
	{
		reader & rules;
		if(corruption == 4 && rules.isStruct() && rules.Struct().contains("warcasting")
			&& rules["warcasting"].isBool())
			rules["warcasting"] = JsonNode(false);
		return *this;
	}
};

class NewHorizonsReactiveWeaveTest : public HeroCommandFixture
{
protected:
	CStack * casterUnit = nullptr;
	CStack * recipient = nullptr;

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		auto rules = JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
		rules["warcasting"] = JsonNode(true);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, rules);
	}

	void select(CGHeroInstance * hero, const std::string & perk)
	{
		const auto lookup = [hero](const std::string & skill) { return hero->getPerkSkillRank(skill); };
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = hero->getPerkState().prepareOffer(lookup, seed);
			const auto found = std::find_if(offers.begin(), offers.end(), [&](const auto & offer)
			{
				return offer.selection.skillId == skillKey && offer.selection.perkId == perk;
			});
			if(found != offers.end())
			{
				gameHandler->levelUpHero(hero, offers, std::distance(offers.begin(), found), seed, false);
				ASSERT_TRUE(hero->hasActivePerk(skillKey, perk));
				return;
			}
		}
		FAIL() << "No public perk offer for " << perk;
	}

	void prepare(bool selected = true, bool synthesis = false)
	{
		startGame();
		const auto & perks = defenderSideHero->getPerkState().rules["skills"][skillKey]["perks"].Vector();
		const auto entry = std::find_if(perks.begin(), perks.end(), [](const auto & perk)
		{
			return perk["id"].String() == reactiveKey;
		});
		ASSERT_NE(entry, perks.end());
		ASSERT_EQ((*entry)["effect"]["status"].String(), "active");
		const auto skill = SecondarySkill(SecondarySkill::decode(skillKey));
		defenderSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		select(defenderSideHero, martialKey);
		gameHandler->levelUpHero(defenderSideHero, skill, false);
		if(selected)
			select(defenderSideHero, reactiveKey);
		if(synthesis)
		{
			gameHandler->levelUpHero(defenderSideHero, skill, false);
			select(defenderSideHero, "new-horizons:warcasting.masterSynthesis");
		}
		for(const auto school : {"new-horizons:chaosMagic", "new-horizons:natureMagic"})
			attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(school)),
				MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		for(const auto spell : {SpellID::MAGIC_ARROW, SpellID::HASTE})
			attackerSideHero->addSpellToSpellbook(spell);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 1000, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);
		startBattle();
		casterUnit = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 100);
		recipient = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 100);
		beginCombat();
		BattleSetActiveStack active;
		active.battleID = BattleID(0);
		active.stack = casterUnit->unitId();
		active.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(active);
	}

	void rejectMalformedCurrentRead(int corruption,
		const std::string & expectedError = "Reactive Weave saved readiness")
	{
		prepare();
		battle()->armReactiveWeave(BattleSide::DEFENDER, battle()->getRound(), 15);
		CMemorySerializer stream;
		stream.oser.version = ESerializationVersion::CURRENT;
		stream.iser.version = ESerializationVersion::CURRENT;
		stream.iser.cb = gameState().get();
		battle()->serialize(stream.oser);
		BattleInfo decoded(gameState().get());
		MalformedReactiveReader reader(stream.iser, corruption);
		try
		{
			decoded.serialize(reader);
			FAIL() << "Malformed current Reactive Weave battle was accepted";
		}
		catch(const std::runtime_error & error)
		{
			EXPECT_NE(std::string(error.what()).find(expectedError), std::string::npos);
		}
		EXPECT_EQ(battle()->getWarcastingState(BattleSide::DEFENDER).reactiveEmpowermentPercent, 15);
	}

	SetReactiveWeaveState receipt() const
	{
		SetReactiveWeaveState result;
		result.battleID = BattleID(0);
		result.side = BattleSide::DEFENDER;
		result.casterSide = BattleSide::ATTACKER;
		result.round = battle()->getRound();
		result.empowerment = newHorizonsWarcasting::reactiveEmpowerment(defenderSideHero);
		result.recipients = {recipient->unitId()};
		return result;
	}
};
}

TEST(NewHorizonsReactiveWeaveState, StrongerCandidateNeverAddsAndConsumesBoth)
{
	State state;
	state.recordAcceptedAction(Action::SPELL, 2, 30);
	const auto history = state.recentActions;
	state.armReactive(2, 15);
	EXPECT_EQ(state.recentActions, history);
	EXPECT_EQ(state.bonusFor(Action::ORDER, 2), 30);
	EXPECT_EQ(state.recordAcceptedAction(Action::ORDER, 2, 20), 30);
	EXPECT_EQ(state.reactiveEmpowermentPercent, 0);
	EXPECT_EQ(state.bonusFor(Action::ORDER, 2), 0);
	EXPECT_EQ(state.bonusFor(Action::SPELL, 2), 20);
	EXPECT_TRUE(state.hasConsumedBonus);
}

TEST(NewHorizonsReactiveWeaveState, IndependentExpiryDoesNotExtendOrdinary)
{
	State state;
	state.recordAcceptedAction(Action::SPELL, 1, 30);
	state.armReactive(2, 15);
	EXPECT_EQ(state.expiryRound, 2);
	EXPECT_EQ(state.readinessExpiryFor(Action::ORDER, 2), 2);
	state = state.clearedIfExpired(3);
	EXPECT_EQ(state.nextEligibleAction, Action::NONE);
	EXPECT_EQ(state.bonusFor(Action::ORDER, 3), 15);
	EXPECT_EQ(state.readinessExpiryFor(Action::ORDER, 3), 3);
	EXPECT_EQ(state.clearedIfExpired(4).reactiveEmpowermentPercent, 0);
}

TEST(NewHorizonsReactiveWeaveState, OwnSpellDoesNotConsumeIndependentOrderReaction)
{
	State state;
	state.armReactive(1, 15);
	EXPECT_EQ(state.recordAcceptedAction(Action::SPELL, 1, 10), 0);
	EXPECT_EQ(state.bonusFor(Action::ORDER, 1), 15);
	EXPECT_FALSE(state.hasConsumedBonus);
	EXPECT_EQ(state.recordAcceptedAction(Action::ORDER, 1, 0), 15);
	EXPECT_EQ(state.reactiveEmpowermentPercent, 0);
}

TEST(NewHorizonsReactiveWeaveState, MalformedAndOverflowRejectedWithoutMutation)
{
	State state;
	const auto before = state;
	EXPECT_THROW(state.armReactive(INT32_MAX, 15), std::invalid_argument);
	EXPECT_THROW(state.armReactive(1, 0), std::invalid_argument);
	EXPECT_EQ(state, before);
	state.reactiveExpiryRound = 2;
	EXPECT_THROW(state.validateShape(), std::runtime_error);
}

TEST_F(NewHorizonsReactiveWeaveTest, ActualEnemyDamageArmsWithoutOwnActionOrAllowance)
{
	prepare();
	const auto history = battle()->getWarcastingState(BattleSide::DEFENDER).recentActions;
	const auto allowances = battle()->getHeroActionAllowances(BattleSide::DEFENDER);
	const auto health = recipient->getAvailableHealth();
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::MAGIC_ARROW, recipient));
	EXPECT_LT(recipient->getAvailableHealth(), health);
	const auto & reaction = battle()->getWarcastingState(BattleSide::DEFENDER);
	EXPECT_EQ(reaction.reactiveEmpowermentPercent, 15);
	EXPECT_EQ(reaction.reactiveExpiryRound, battle()->getRound() + 1);
	EXPECT_EQ(reaction.recentActions, history);
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::DEFENDER), allowances);
	EXPECT_FALSE(reaction.hasConsumedBonus);
}

TEST_F(NewHorizonsReactiveWeaveTest, AcceptedFriendlySpellDoesNotAffectEnemyArmy)
{
	prepare();
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::HASTE, casterUnit));
	EXPECT_EQ(battle()->getWarcastingState(BattleSide::DEFENDER).reactiveEmpowermentPercent, 0);
}

TEST_F(NewHorizonsReactiveWeaveTest, UnselectedAndRejectedDoNotArm)
{
	prepare(false);
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::MAGIC_ARROW, recipient));
	EXPECT_EQ(battle()->getWarcastingState(BattleSide::DEFENDER).reactiveEmpowermentPercent, 0);
	const auto before = battle()->getWarcastingState(BattleSide::DEFENDER);
	EXPECT_FALSE(castOn(attackerSideHero, SpellID::MAGIC_ARROW, recipient));
	EXPECT_EQ(battle()->getWarcastingState(BattleSide::DEFENDER), before);
}

TEST_F(NewHorizonsReactiveWeaveTest, AcceptedOrderConsumesBothAndUsesStrongerOrdinary)
{
	prepare();
	auto & state = battle()->getSide(BattleSide::DEFENDER).warcastingState;
	state.recordAcceptedAction(Action::SPELL, battle()->getRound(), 30);
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::MAGIC_ARROW, recipient));
	ASSERT_EQ(newHorizonsWarcasting::orderBonus(defenderSideHero, state, battle()->getRound()), 30);
	BattleSetActiveStack active;
	active.battleID = BattleID(0);
	active.stack = recipient->unitId();
	active.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(active);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
		BattleAction::makeHeroCommand(BattleSide::DEFENDER, HeroCommand::CHARGE)));
	const auto order = battle()->getHeroOrderState(BattleSide::DEFENDER);
	ASSERT_TRUE(order);
	EXPECT_EQ(order->warcastingBonusPercent, 30);
	EXPECT_EQ(state.reactiveEmpowermentPercent, 0);
	EXPECT_TRUE(state.hasConsumedBonus);
}

TEST_F(NewHorizonsReactiveWeaveTest, ActualDetachedEffectReceiptIsBranchLocalAndMatchesLive)
{
	prepare();
	ReactiveEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), battle()->sideToPlayer(BattleSide::ATTACKER));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	HypotheticBattle branch(&environment, parent);
	HypotheticBattle sibling(&environment, parent);
	const auto * spell = SpellID(SpellID::MAGIC_ARROW).toSpell();
	spells::BattleCast projected(&branch, attackerSideHero, spells::Mode::HERO, spell);
	const spells::Target target{spells::Destination(branch.battleGetUnitByID(recipient->unitId()))};
	ASSERT_TRUE(projected.mechanicsForTarget(target)->canBeCastAt(target));
	projected.castEval(branch.getServerCallback(), target);
	EXPECT_EQ(branch.getWarcastingState(BattleSide::DEFENDER).reactiveEmpowermentPercent, 15);
	EXPECT_EQ(parent->getWarcastingState(BattleSide::DEFENDER).reactiveEmpowermentPercent, 0);
	EXPECT_EQ(sibling.getWarcastingState(BattleSide::DEFENDER).reactiveEmpowermentPercent, 0);
	EXPECT_EQ(battle()->getWarcastingState(BattleSide::DEFENDER).reactiveEmpowermentPercent, 0);
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::MAGIC_ARROW, recipient));
	EXPECT_EQ(branch.getWarcastingState(BattleSide::DEFENDER),
		battle()->getWarcastingState(BattleSide::DEFENDER));
}

TEST_F(NewHorizonsReactiveWeaveTest, ForgedCapabilityAndRoundReceiptsAreAtomic)
{
	prepare();
	battle()->getSide(BattleSide::ATTACKER).heroSpellCastCompleted = true;
	const auto before = battle()->getWarcastingState(BattleSide::DEFENDER);
	BattleStatePackVisitor visitor(*battle());
	auto forged = receipt();
	++forged.empowerment;
	EXPECT_THROW(forged.visitTyped(visitor), std::runtime_error);
	EXPECT_EQ(battle()->getWarcastingState(BattleSide::DEFENDER), before);
	forged = receipt();
	++forged.round;
	EXPECT_THROW(forged.visitTyped(visitor), std::runtime_error);
	EXPECT_EQ(battle()->getWarcastingState(BattleSide::DEFENDER), before);
}

TEST(NewHorizonsReactiveWeaveProtocol, CurrentRoundTripAndLegacyDefault)
{
	State state;
	state.armReactive(2, 15);
	CMemorySerializer current;
	current.oser & state;
	State restored;
	current.iser & restored;
	EXPECT_EQ(restored, state);
	CMemorySerializer legacy;
	legacy.oser.version = ESerializationVersion::NEW_HORIZONS_WARCASTING;
	legacy.iser.version = legacy.oser.version;
	State ordinary;
	legacy.oser & ordinary;
	restored = state;
	legacy.iser & restored;
	EXPECT_EQ(restored.reactiveEmpowermentPercent, 0);
	EXPECT_EQ(restored.reactiveExpiryRound, 0);
}

TEST_F(NewHorizonsReactiveWeaveTest, OlderWritersRejectBeforeStateSideBattleAndPacketPrefixes)
{
	prepare();
	CMemorySerializer ordinary;
	ordinary.oser.version = static_cast<ESerializationVersion>(
		static_cast<int>(ESerializationVersion::NEW_HORIZONS_REACTIVE_WEAVE) - 1);
	ASSERT_NO_THROW(battle()->serialize(ordinary.oser));
	EXPECT_FALSE(ordinary.extractBuffer().empty());
	battle()->armReactiveWeave(BattleSide::DEFENDER, battle()->getRound(), 15);
	auto check = [](auto & value)
	{
		CMemorySerializer old;
		old.oser.version = static_cast<ESerializationVersion>(
			static_cast<int>(ESerializationVersion::NEW_HORIZONS_REACTIVE_WEAVE) - 1);
		EXPECT_THROW(value.serialize(old.oser), std::runtime_error);
		EXPECT_TRUE(old.extractBuffer().empty());
	};
	auto state = battle()->getWarcastingState(BattleSide::DEFENDER);
	check(state);
	check(battle()->getSide(BattleSide::DEFENDER));
	check(*battle());
	BattleStart start;
	start.battleID = BattleID(0);
	start.info = CMemorySerializer::deepCopy(*battle(), gameState().get());
	check(start);
	auto update = receipt();
	check(update);
}

TEST_F(NewHorizonsReactiveWeaveTest, FirstMasterSynthesisConsumesChosenReactionExactlyOnce)
{
	prepare(true, true);
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::MAGIC_ARROW, recipient));
	auto & state = battle()->getSide(BattleSide::DEFENDER).warcastingState;
	EXPECT_EQ(state.reactiveEmpowermentPercent, 20);
	EXPECT_EQ(newHorizonsWarcasting::orderBonus(defenderSideHero, state, battle()->getRound()), 50);
	BattleSetActiveStack active;
	active.battleID = BattleID(0);
	active.stack = recipient->unitId();
	active.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(active);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
		BattleAction::makeHeroCommand(BattleSide::DEFENDER, HeroCommand::CHARGE)));
	const auto order = battle()->getHeroOrderState(BattleSide::DEFENDER);
	ASSERT_TRUE(order);
	EXPECT_EQ(order->warcastingBonusPercent, 50);
	EXPECT_TRUE(state.hasConsumedBonus);
	EXPECT_EQ(state.reactiveEmpowermentPercent, 0);
	state.armReactive(battle()->getRound(), 20);
	EXPECT_EQ(newHorizonsWarcasting::orderBonus(defenderSideHero, state, battle()->getRound()), 20);
}

TEST_F(NewHorizonsReactiveWeaveTest, CurrentBattleReaderRejectsMissingCapturedHeroCapability)
{
	rejectMalformedCurrentRead(1);
}

TEST_F(NewHorizonsReactiveWeaveTest, CurrentBattleReaderRejectsWrongEmpowerment)
{
	rejectMalformedCurrentRead(2);
}

TEST_F(NewHorizonsReactiveWeaveTest, CurrentBattleReaderRejectsFutureExpiry)
{
	rejectMalformedCurrentRead(3);
}

TEST_F(NewHorizonsReactiveWeaveTest, CurrentBattleReaderDoesNotMaskStoredReactionInInactiveProfile)
{
	rejectMalformedCurrentRead(4, "Saved Warcasting state requires the opt-in magic rules");
}
