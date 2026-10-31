/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include <stdexcept>
#include "../server/battles/BattleTestFixture.h"
#include "../../AI/BattleAI/CreatureBurnOwnedProjection.h"
#include "../../AI/BattleAI/HypotheticBattle.h"
#include "../../AI/BattleAI/HypotheticBattleTypes.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/battle/OwnedUnitBonusFrame.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/bonuses/OwnedBonusCaptureCodec.h"
#include "../../lib/entities/hero/NewHorizonsSpecialtySerialization.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../server/battles/BattleCreatureBurn.h"
#include "../../server/CGameHandler.h"
#include <vcmi/ServerCallback.h>
namespace
{
using Frame = battle::OwnedUnitBonusFrame;
using Resolver = battle::OwnedUnitCreatureResolver;
class BurnOwnedEnvironment final : public Environment
{
	std::shared_ptr<CGameState> value;
public:
	explicit BurnOwnedEnvironment(std::shared_ptr<CGameState> state) : value(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return value->getBattle(id); }
	const GameCb * game() const override { return value.get(); }
};
template<typename Random> auto burnOwnedWire(Random & random)
{
	CMemorySerializer wire; wire.oser & random; return wire.extractBuffer();
}
class OwnedBurnProjectionTest : public BattleTestFixture
{
protected:
	CStack * live = nullptr;
	std::shared_ptr<BurnOwnedEnvironment> environment;
	std::shared_ptr<HypotheticBattle> branch, sibling;
	std::shared_ptr<Bonus> rebirth, casts;
	void prepare(int revival = 0, bool countUpdater = false, int anyReduction = 0, bool echoCaster = false, bool invincible = false)
	{
		startGame(); startBattle();
		live = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 3);
		ASSERT_NE(live, nullptr); ASSERT_EQ(live->getMaxHealth(), 10); ASSERT_EQ(live->getAvailableHealth(), 30);
		const auto marker = newHorizonsCreatureBurn::marker(HeroTypeID(HeroTypeID::decode("core:ignissa")), live->unitId(), 40);
		live->addNewBonus(std::make_shared<Bonus>(marker));
		casts = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::CASTS, BonusSource::OTHER, 1, BonusSourceID());
		if(countUpdater) casts->updater = std::make_shared<TimesStackSizeUpdater>(0, 10000, 1, 1);
		live->addNewBonus(casts);
		live->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::SPELL_DAMAGE_REDUCTION,
			BonusSource::OTHER, 25, BonusSourceID(), BonusSubtypeID(SpellSchool::FIRE)));
		rebirth = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::REBIRTH, BonusSource::OTHER, revival, BonusSourceID());
		live->addNewBonus(rebirth);
		if(anyReduction > 0) live->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::SPELL_DAMAGE_REDUCTION, BonusSource::OTHER, anyReduction, BonusSourceID(), BonusSubtypeID(SpellSchool::ANY)));
		if(invincible) live->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::INVINCIBLE, BonusSource::OTHER, 1, BonusSourceID()));
		if(echoCaster)
		{
			ASSERT_NE(addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 3), nullptr);
		}
		environment = std::make_shared<BurnOwnedEnvironment>(gameState());
		auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		branch = std::make_shared<HypotheticBattle>(environment.get(), callback);
		sibling = std::make_shared<HypotheticBattle>(environment.get(), callback);
	}
	creatureBurnAI::PendingEcho configureEcho(int anyReduction = 50, bool formulaIdentity = true, bool invincible = false)
	{
		prepare(0, false, anyReduction, true, invincible);
		SetOverwhelmingFormulaState pending; pending.battleID = BattleID(0);
		pending.side = BattleSide::DEFENDER; pending.state.lastCandidateCastToken = 1;
		branch->getServerCallback()->apply(pending);
		pending.state.lastCandidateCastToken = 2;
		branch->getServerCallback()->apply(pending);
		creatureBurnAI::PendingEcho result;
		result.link.primaryUnitId = live->unitId(); result.link.casterSide = BattleSide::DEFENDER;
		result.link.echoBasisPoints = 2000; result.link.damageMultiplierPercent = 100;
		if(formulaIdentity)
		{
			result.link.mdrPenetration["overwhelmingFormulaSide"].Integer() = static_cast<int>(BattleSide::DEFENDER);
			result.link.mdrPenetration["overwhelmingFormulaToken"].String() = "1";
		}
		result.victimSide = BattleSide::ATTACKER; result.actualDamage = 100;
		return result;
	}

	creatureBurnAI::CapturedTick capture(const std::shared_ptr<const Frame> & owned, size_t handle = 2)
	{
		return creatureBurnAI::captureOwnedTick(branch, owned, handle, BattleUnitTurnReason::TURN_QUEUE,
			newHorizonsCreatureBurn::captureIdentityFacts());
	}
	std::shared_ptr<Frame> frame(bool supportedCodec = true, int anyReduction = 0, bool invincible = false)
	{
		// Controlled pure fixture recipes: these are declared RAW records, not a live capture factory.
		Frame::Inputs input;
		input.identity = {3, live->unitId(), live->unitSide(), live->unitOwner(), live->unitSlot(), live->creatureId(), {30, 30}};
		Resolver::Creature creature;
		creature.id = live->creatureId(); creature.key = "core:pikeman";
		creature.faction = FactionID(0); creature.level = 1;
		auto alternate = creature; alternate.id = CreatureID(CreatureID::decode("core:halberdier")); alternate.key = "core:halberdier";
		input.creatures = std::make_shared<Resolver>(std::vector<Resolver::Creature>{creature, alternate});
		OwnedRecipientBonusGraph::NodeInput root, species;
		root.facts.nodeType = BonusNodeType::STACK_BATTLE; root.facts.stackCount = 3; root.facts.stableStackLevel = 1;
		root.facts.acceptance.emplace(); root.facts.acceptance->metadata.slot = live->unitSlot(); root.parents = {1};
		root.local = {
			std::make_shared<Bonus>(*casts),
			std::make_shared<Bonus>(*rebirth),
			std::make_shared<Bonus>(newHorizonsCreatureBurn::marker(HeroTypeID(HeroTypeID::decode("core:ignissa")), live->unitId(), 40)),
			std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::SPELL_DAMAGE_REDUCTION,
				BonusSource::OTHER, 25, BonusSourceID(), BonusSubtypeID(SpellSchool::FIRE))};
		if(anyReduction > 0) root.local.push_back(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::SPELL_DAMAGE_REDUCTION, BonusSource::OTHER, anyReduction, BonusSourceID(), BonusSubtypeID(SpellSchool::ANY)));
		if(invincible) root.local.push_back(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::INVINCIBLE, BonusSource::OTHER, 1, BonusSourceID()));
		species.facts.nodeType = BonusNodeType::CREATURE;
		auto hp = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACK_HEALTH, BonusSource::CREATURE_ABILITY,
			10, BonusSourceID(live->creatureId()));
		species.local = {hp}; input.nodes = {root, species};
		Frame::FormSources form;
		form.full.facts = root.facts; form.full.records = {0, 1, 2, 3}; if(anyReduction > 0) form.full.records.push_back(4); if(invincible) form.full.records.push_back(root.local.size() - 1); form.full.parents = {1};
		form.full.facts.creatureContext.emplace();
		form.full.facts.creatureContext->creature = OwnedBonusEvaluationFacts::CreatureContext::Creature{creature.id, 1, EAlignment::GOOD};
		form.full.facts.creatureContext->faction = creature.faction;
		form.native = form.full; form.native.records.clear(); input.forms.emplace(creature.id, form);
		form.full.facts.creatureContext->creature->id = alternate.id;
		form.native.facts.creatureContext->creature->id = alternate.id;
		input.forms.emplace(alternate.id, form);
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		{
			input.environment.colors.at(side) = battle()->getSidePlayer(side);
			input.environment.inputs.at(side).emplace(); input.environment.inputs.at(side)->color = *input.environment.colors.at(side);
			input.environment.states.at(side).emplace();
		}
		input.environment.magicRules = battle()->getMagicRules(); input.environment.skeletonUpgrades.emplace();
		input.environment.legacyLuckLimits = std::pair{0, 0}; input.environment.legacyMoraleLimits = std::pair{0, 0};
		input.environmentUnits = {nullptr}; input.recipientPosition = 0; input.state = live->save();
		if(supportedCodec)
		{
			CMemorySerializer capability;
			capability.oser.version = ESerializationVersion::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES;
			input.captureCodec = OwnedBonusCaptureCodec::fromCapturedSpecialties(battle()->getHeroCommandRules(),
				newHorizonsHeroes::SpecialtySerializationContext::fromHandler(capability.oser));
		}
		return Frame::fromOwnedInputs(std::move(input));
	}
};
TEST_F(OwnedBurnProjectionTest, OwnedFixedPreinjuryCountUpdaterMatchesNativePreparedRebirthWithoutMutatingSourceOrSibling)
{
	prepare(100, true); const auto owned = frame(); const auto originalFrame = owned->save();
	const auto original = live->save(); const auto originalSibling = sibling->getForUpdate(live->unitId())->save();
	auto * nativeRandom = dynamic_cast<CRandomGenerator *>(&gameHandler->getRandomGenerator());
	auto * fixtureRandom = dynamic_cast<CRandomGenerator *>(getRNG()); ASSERT_NE(nativeRandom, nullptr); ASSERT_NE(fixtureRandom, nullptr);
	const auto nativeBefore = burnOwnedWire(*nativeRandom), fixtureBefore = burnOwnedWire(*fixtureRandom);
	const auto captured = capture(owned);
	const auto prepared = [&] {
		struct WithoutLibrary { GameLibrary * old = LIBRARY; WithoutLibrary() { LIBRARY = nullptr; } ~WithoutLibrary() { LIBRARY = old; } } absent;
		return creatureBurnAI::prepareOwnedCapturedTick(owned, captured, *branch->getServerCallback()->getRNG());
	}();
	ASSERT_TRUE(prepared.injury()); EXPECT_TRUE(prepared.injury()->killed()); EXPECT_TRUE(prepared.injury()->willRebirth());
	EXPECT_EQ(owned->save(), originalFrame);
	const auto echo = creatureBurnAI::applyOwnedTick(*branch, owned, prepared); EXPECT_FALSE(echo);
	const auto * result = branch->battleGetUnitByID(live->unitId()); ASSERT_NE(result, nullptr);
	EXPECT_TRUE(result->alive()); EXPECT_EQ(result->getAvailableHealth(), 30); EXPECT_EQ(result->getCount(), 3);
	EXPECT_TRUE(result->getUnstackedBonuses(CSelector(newHorizonsCreatureBurn::isBurn))->empty());
	EXPECT_EQ(live->save(), original); EXPECT_EQ(sibling->getForUpdate(live->unitId())->save(), originalSibling);
	EXPECT_EQ(burnOwnedWire(*nativeRandom), nativeBefore); EXPECT_EQ(burnOwnedWire(*fixtureRandom), fixtureBefore);
	// Canonical authority executes the same fixed pre-injury count updater, not the damaged scratch count0.
	ASSERT_TRUE(battleCreatureBurn::onActivation(*gameHandler, *battle(), *live, BattleUnitTurnReason::TURN_QUEUE));
	EXPECT_EQ(live->getAvailableHealth(), result->getAvailableHealth()); EXPECT_EQ(live->getCount(), result->getCount());
	const auto nativeRebirthState = live->save();
	const auto projectedRebirthState = result->acquireState()->save();
	ASSERT_TRUE(nativeRebirthState["state"].isStruct());
	ASSERT_TRUE(projectedRebirthState["state"].isStruct());
	const auto & nativeCasts = nativeRebirthState["state"]["casts"];
	const auto & projectedCasts = projectedRebirthState["state"]["casts"];
	ASSERT_TRUE(nativeCasts.isStruct());
	ASSERT_TRUE(projectedCasts.isStruct());
	ASSERT_EQ(nativeCasts["used"].getType(), JsonNode::JsonType::DATA_INTEGER);
	ASSERT_EQ(projectedCasts["used"].getType(), JsonNode::JsonType::DATA_INTEGER);
	EXPECT_EQ(nativeCasts["used"].Integer(), 1);
	EXPECT_EQ(nativeCasts, projectedCasts);
}
TEST_F(OwnedBurnProjectionTest, FractionalNativeRebirthUsesExistingMidpointRepresentativeWithoutClaimingNativeDrawParity)
{
	prepare(20); const auto owned = frame(); const auto original = live->save();
	auto * nativeRandom = dynamic_cast<CRandomGenerator *>(&gameHandler->getRandomGenerator());
	auto * fixtureRandom = dynamic_cast<CRandomGenerator *>(getRNG()); ASSERT_NE(nativeRandom, nullptr); ASSERT_NE(fixtureRandom, nullptr);
	const auto nativeBefore = burnOwnedWire(*nativeRandom), fixtureBefore = burnOwnedWire(*fixtureRandom);
	const auto captured = capture(owned);
	const auto prepared = [&] {
		struct WithoutLibrary { GameLibrary * old = LIBRARY; WithoutLibrary() { LIBRARY = nullptr; } ~WithoutLibrary() { LIBRARY = old; } } absent;
		return creatureBurnAI::prepareOwnedCapturedTick(owned, captured, *branch->getServerCallback()->getRNG());
	}();
	ASSERT_TRUE(prepared.injury()); EXPECT_TRUE(prepared.injury()->killed()); EXPECT_FALSE(prepared.injury()->willRebirth());
	// Existing RNGStub returns49 for each native0..99 residual trial;20 does not pass.
	EXPECT_EQ(prepared.injury()->damageAmount, 30); const auto echo = creatureBurnAI::applyOwnedTick(*branch, owned, prepared); EXPECT_FALSE(echo);
	EXPECT_FALSE(branch->getForUpdate(live->unitId())->alive());
	EXPECT_EQ(live->save(), original); EXPECT_TRUE(sibling->getForUpdate(live->unitId())->alive());
	EXPECT_EQ(burnOwnedWire(*nativeRandom), nativeBefore); EXPECT_EQ(burnOwnedWire(*fixtureRandom), fixtureBefore);
}
TEST_F(OwnedBurnProjectionTest, WrongOwnedRawHandleFailsBeforeBranchMarkerOrStateChanges)
{
	prepare(); const auto owned = frame(); const auto before = owned->save();
	const auto source = live->save(); const auto projected = branch->getForUpdate(live->unitId())->save();
	EXPECT_THROW(capture(owned, 0), std::invalid_argument);
	EXPECT_EQ(owned->save(), before); EXPECT_EQ(live->save(), source); EXPECT_EQ(branch->getForUpdate(live->unitId())->save(), projected);
	EXPECT_EQ(owned->unit().getUnstackedBonuses(CSelector(newHorizonsCreatureBurn::isBurn))->size(), 1u);
}
TEST_F(OwnedBurnProjectionTest, SameScalarsDoNotAdmitSiblingFrameOrBranchAndRejectBeforeRepresentativeDraw)
{
	prepare(20); const auto owned = frame(); const auto captured = capture(owned); const auto other = owned->fork();
	CRandomGenerator representative(1701); const auto randomBefore = burnOwnedWire(representative);
	EXPECT_THROW(creatureBurnAI::prepareOwnedCapturedTick(other, captured, representative), std::invalid_argument);
	EXPECT_EQ(burnOwnedWire(representative), randomBefore); EXPECT_EQ(other->save(), owned->save());
	const auto prepared = creatureBurnAI::prepareOwnedCapturedTick(owned, captured, *branch->getServerCallback()->getRNG());
	const auto before = sibling->getForUpdate(live->unitId())->save();
	EXPECT_THROW(creatureBurnAI::applyOwnedTick(*sibling, owned, prepared), std::invalid_argument);
	EXPECT_EQ(sibling->getForUpdate(live->unitId())->save(), before);
	EXPECT_EQ(sibling->getForUpdate(live->unitId())->getUnstackedBonuses(CSelector(newHorizonsCreatureBurn::isBurn))->size(), 1u);
}
TEST_F(OwnedBurnProjectionTest, ChangedHealthFormOrResistanceLedgerRejectsBeforeRepresentativeDraw)
{
	for(const auto change : {0, 1, 2})
	{
		// Independent frame for each scenario, sharing the one unchanged authoritative fixture.
		if(!live) prepare(20);
		const auto owned = frame(); const auto captured = capture(owned); const auto original = owned->save();
		if(change == 2)
		{
			owned->removeRecords({3}); EXPECT_EQ(owned->save(), original); // MDR is absent from scalar JSON.
		}
		else
		{
			auto scratch = owned->scratch();
			if(change == 0) { int64_t damage = 1; scratch.unit().damage(damage); }
			else scratch.unit().beginBattleForm(CreatureID(CreatureID::decode("core:halberdier")), 2);
			owned->load(scratch.unit().save());
		}
		CRandomGenerator representative(1702); const auto randomBefore = burnOwnedWire(representative);
		EXPECT_THROW(creatureBurnAI::prepareOwnedCapturedTick(owned, captured, representative), std::invalid_argument);
		EXPECT_EQ(burnOwnedWire(representative), randomBefore);
		EXPECT_EQ(live->getAvailableHealth(), 30);
	}
}

TEST_F(OwnedBurnProjectionTest, CapturedPreparationSurvivesSourceBranchAndLibraryRetirement)
{
	prepare(100, true); const auto owned = frame(); const auto captured = capture(owned);
	const auto original = owned->save(); branch.reset();
	RNGStub representative;
	std::optional<BattleStackAttacked> result;
	{
		struct WithoutLibrary { GameLibrary * old = LIBRARY; WithoutLibrary() { LIBRARY = nullptr; } ~WithoutLibrary() { LIBRARY = old; } } absent;
		const auto prepared = creatureBurnAI::prepareOwnedCapturedTick(owned, captured, representative);
		result = prepared.injury();
		EXPECT_EQ(owned->save(), original);
	}
	ASSERT_TRUE(result); EXPECT_TRUE(result->killed()); EXPECT_TRUE(result->willRebirth());
	EXPECT_EQ(live->getAvailableHealth(), 30);
}
TEST_F(OwnedBurnProjectionTest, DefaultCloneRetainsCurrentCodecRefusalForRealBurnMarker)
{
	prepare();
	CMemorySerializer ordinary;
	EXPECT_EQ(ordinary.oser.version, ESerializationVersion::CURRENT);
	EXPECT_EQ(ordinary.iser.version, ESerializationVersion::CURRENT);
	if(ESerializationVersion::CURRENT < ESerializationVersion::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES)
	{
		EXPECT_THROW(frame(false), std::runtime_error);
	}
	EXPECT_NO_THROW(frame(true));
}

TEST_F(OwnedBurnProjectionTest, ActualFormulaEchoClaimsBeforeInjuryAndPreventsLaterTokenReusingPenetration)
{
	const auto echo = configureEcho(); const auto owned = frame(true, 50); const auto original = owned->save();
	auto other = echo.link.mdrPenetration; other["overwhelmingFormulaToken"].String() = "2";
	const auto damage = [&](const JsonNode & identity) {
		return newHorizonsSoulChain::adjustedEchoDamage(*branch, BattleSide::DEFENDER,
			branch->battleGetUnitByID(live->unitId()), 100, 2000, &identity);
	};
	const auto expected = damage(echo.link.mdrPenetration), secondBefore = damage(other);
	auto * nativeRandom = dynamic_cast<CRandomGenerator *>(&gameHandler->getRandomGenerator());
	auto * fixtureRandom = dynamic_cast<CRandomGenerator *>(getRNG()); ASSERT_NE(nativeRandom, nullptr); ASSERT_NE(fixtureRandom, nullptr);
	const auto nativeBefore = burnOwnedWire(*nativeRandom), fixtureBefore = burnOwnedWire(*fixtureRandom);
	const auto prepared = creatureBurnAI::prepareOwnedEcho(branch, echo, owned, *branch->getServerCallback()->getRNG());
	ASSERT_TRUE(prepared); EXPECT_EQ(branch->getOverwhelmingFormulaState(BattleSide::DEFENDER).winningCastToken, 0u);
	EXPECT_GT(prepared->injury().damageAmount, 0);
	creatureBurnAI::applyOwnedEcho(*branch, owned, *prepared);
	const auto & state = branch->getOverwhelmingFormulaState(BattleSide::DEFENDER);
	EXPECT_EQ(state.winningCastToken, 1u); EXPECT_TRUE(state.canPenetrate(1)); EXPECT_FALSE(state.canPenetrate(2));
	EXPECT_EQ(30 - branch->battleGetUnitByID(live->unitId())->getAvailableHealth(), expected);
	EXPECT_LT(damage(other), secondBefore);
	EXPECT_EQ(live->getAvailableHealth(), 30); EXPECT_EQ(sibling->getForUpdate(live->unitId())->getAvailableHealth(), 30);
	EXPECT_EQ(sibling->getOverwhelmingFormulaState(BattleSide::DEFENDER).winningCastToken, 0u);
	EXPECT_EQ(battle()->getOverwhelmingFormulaState(BattleSide::DEFENDER).winningCastToken, 0u);
	EXPECT_EQ(owned->save(), original);
	EXPECT_EQ(burnOwnedWire(*nativeRandom), nativeBefore); EXPECT_EQ(burnOwnedWire(*fixtureRandom), fixtureBefore);
}
TEST_F(OwnedBurnProjectionTest, PositiveEchoWithoutApplicableShadowReductionDoesNotClaimFormula)
{
	const auto echo = configureEcho(0); const auto owned = frame();
	const auto prepared = creatureBurnAI::prepareOwnedEcho(branch, echo, owned, *branch->getServerCallback()->getRNG());
	ASSERT_TRUE(prepared); ASSERT_GT(prepared->injury().damageAmount, 0);
	creatureBurnAI::applyOwnedEcho(*branch, owned, *prepared);
	EXPECT_EQ(branch->getOverwhelmingFormulaState(BattleSide::DEFENDER).winningCastToken, 0u);
}
TEST_F(OwnedBurnProjectionTest, AbsentFormulaIdentityDoesNotInventTokenOrPenetration)
{
	const auto echo = configureEcho(50, false); const auto owned = frame(true, 50);
	const auto prepared = creatureBurnAI::prepareOwnedEcho(branch, echo, owned, *branch->getServerCallback()->getRNG());
	ASSERT_TRUE(prepared); ASSERT_GT(prepared->injury().damageAmount, 0);
	creatureBurnAI::applyOwnedEcho(*branch, owned, *prepared);
	EXPECT_EQ(branch->getOverwhelmingFormulaState(BattleSide::DEFENDER).winningCastToken, 0u);
}
TEST_F(OwnedBurnProjectionTest, AbsentZeroAndMalformedEchoCannotSpendTokenOrChangeRecipient)
{
	auto echo = configureEcho(); const auto owned = frame(true, 50); const auto state = branch->getOverwhelmingFormulaState(BattleSide::DEFENDER);
	const auto before = branch->getForUpdate(live->unitId())->save();
	EXPECT_FALSE(creatureBurnAI::prepareOwnedEcho(branch, echo, nullptr, *branch->getServerCallback()->getRNG()));
	echo.actualDamage = 0;
	EXPECT_FALSE(creatureBurnAI::prepareOwnedEcho(branch, echo, owned, *branch->getServerCallback()->getRNG()));
	echo.actualDamage = 100; echo.link.echoBasisPoints = 5501;
	CRandomGenerator representative(1703); const auto randomBefore = burnOwnedWire(representative);
	EXPECT_THROW(creatureBurnAI::prepareOwnedEcho(branch, echo, owned, representative), std::invalid_argument);
	EXPECT_EQ(burnOwnedWire(representative), randomBefore);
	EXPECT_EQ(branch->getOverwhelmingFormulaState(BattleSide::DEFENDER), state);
	EXPECT_EQ(branch->getForUpdate(live->unitId())->save(), before);
}
TEST_F(OwnedBurnProjectionTest, StaleEchoFrameForeignBranchAndChangedFormulaRejectBeforeClaimOrInjury)
{
	const auto echo = configureEcho();
	for(int change = 0; change < 4; ++change)
	{
		branch = std::make_shared<HypotheticBattle>(environment.get(), std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
		SetOverwhelmingFormulaState pending; pending.battleID = BattleID(0); pending.side = BattleSide::DEFENDER;
		pending.state.lastCandidateCastToken = 1; branch->getServerCallback()->apply(pending);
		pending.state.lastCandidateCastToken = 2; branch->getServerCallback()->apply(pending);
		const auto owned = frame(true, 50);
		const auto prepared = creatureBurnAI::prepareOwnedEcho(branch, echo, owned, *branch->getServerCallback()->getRNG());
		ASSERT_TRUE(prepared); const auto before = branch->getForUpdate(live->unitId())->save();
		if(change == 0)
		{
			auto scratch = owned->scratch(); int64_t damage = 1; scratch.unit().damage(damage); owned->load(scratch.unit().save());
			EXPECT_THROW(creatureBurnAI::applyOwnedEcho(*branch, owned, *prepared), std::invalid_argument);
		}
		else if(change == 1)
		{
			const auto siblingFrame = owned->fork();
			EXPECT_THROW(creatureBurnAI::applyOwnedEcho(*branch, siblingFrame, *prepared), std::invalid_argument);
		}
		else if(change == 2)
		{
			EXPECT_THROW(creatureBurnAI::applyOwnedEcho(*sibling, owned, *prepared), std::invalid_argument);
			EXPECT_EQ(sibling->getForUpdate(live->unitId())->getAvailableHealth(), 30);
			EXPECT_EQ(sibling->getOverwhelmingFormulaState(BattleSide::DEFENDER).winningCastToken, 0u);
		}
		else
		{
			pending.state.lastCandidateCastToken = 3; branch->getServerCallback()->apply(pending);
			EXPECT_THROW(creatureBurnAI::applyOwnedEcho(*branch, owned, *prepared), std::invalid_argument);
			EXPECT_EQ(branch->getOverwhelmingFormulaState(BattleSide::DEFENDER).lastCandidateCastToken, 3u);
		}
		EXPECT_EQ(branch->getOverwhelmingFormulaState(BattleSide::DEFENDER).winningCastToken, 0u);
		EXPECT_EQ(branch->getForUpdate(live->unitId())->save(), before); EXPECT_EQ(live->getAvailableHealth(), 30);
	}
}

TEST_F(OwnedBurnProjectionTest, InvinciblePrimaryPreventsEchoWithoutClaimingFormula)
{
	const auto echo = configureEcho(50, true, true); const auto owned = frame(true, 50, true);
	const auto before = branch->getForUpdate(live->unitId())->save();
	EXPECT_FALSE(creatureBurnAI::prepareOwnedEcho(branch, echo, owned, *branch->getServerCallback()->getRNG()));
	EXPECT_EQ(branch->getOverwhelmingFormulaState(BattleSide::DEFENDER).winningCastToken, 0u);
	EXPECT_EQ(branch->getForUpdate(live->unitId())->save(), before);
}

}
