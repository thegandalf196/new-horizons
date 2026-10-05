/*
 * NewHorizonsDivineMandateAITest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file in main folder
 */
#include "StdInc.h"

#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../lib/GameConstants.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/GameSettings.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/HeroActionAllowanceState.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/serializer/CMemorySerializer.h"

namespace
{
constexpr auto divineMandateSkill = "new-horizons:divineMandate";
constexpr auto chaplainsReservePerk = "new-horizons:divineMandate.chaplainSReserve";
using Ledger = HeroActionAllowanceState;
using ActionKind = Ledger::ActionKind;
using AllowanceKind = Ledger::AllowanceKind;
using GrantSource = Ledger::GrantSource;

class DivineMandateAIEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit DivineMandateAIEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class RecordingDivineMandateCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> heroActions;
	std::vector<BattleAction> creatureActions;

	RecordingDivineMandateCallback() : CBattleCallback(PlayerColor(0), nullptr) {}

	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override
	{
		heroActions.push_back(action);
	}

	void battleMakeUnitAction(const BattleID &, const BattleAction & action) override
	{
		creatureActions.push_back(action);
	}
};

void applyAcceptedSpell(Ledger & ledger, uint32_t grantId, int32_t round, bool isLight,
	uint8_t maximumPairs)
{
	const auto filter = [grantId, isLight](const Ledger::Grant & grant)
	{
		return grant.id == grantId && Ledger::grantAllowedForSpell(grant, isLight);
	};
	uint8_t metamagicUses = 0;
	uint8_t metamagicPending = 0;
	bool grandUsed = false;
	const auto result = HeroSpellAllowanceTransition::commitAcceptedCast(ledger, grantId, round,
		false, false, 0, false, metamagicUses, metamagicPending, grandUsed, 0, filter);
	ASSERT_TRUE(result);
	DivineMandateTransition::applyAcceptedAction(ledger, result->receipt, round, isLight, maximumPairs);
}

void applyAcceptedOrder(Ledger & ledger, uint32_t grantId, int32_t round, uint8_t maximumPairs)
{
	const auto receipt = ledger.consumeAllowance(grantId, ActionKind::ORDER, round);
	ASSERT_TRUE(receipt);
	DivineMandateTransition::applyAcceptedAction(ledger, *receipt, round, false, maximumPairs);
}
}

class NewHorizonsDivineMandateAITest : public HeroCommandFixture
{
protected:
	CStack * active = nullptr;
	CStack * enemy = nullptr;
	std::shared_ptr<DivineMandateAIEnvironment> environment;
	std::shared_ptr<RecordingDivineMandateCallback> callback;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		JsonNode perks(JsonPath::builtin("config/newHorizonsPerks"));
		for(const auto * rank : {"basic", "advanced", "expert"})
			perks["skills"][divineMandateSkill]["ranks"][rank]["effect"]["status"].String() = "active";
		bool foundChaplainReserve = false;
		for(auto & perk : perks["skills"][divineMandateSkill]["perks"].Vector())
		{
			if(perk["id"].String() == chaplainsReservePerk)
			{
				perk["effect"]["status"].String() = "active";
				foundChaplainReserve = true;
				break;
			}
		}
		if(!foundChaplainReserve)
			throw std::runtime_error("Missing Chaplain's Reserve registry entry");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perks));
	}

	void prepareBattle(MasteryLevel::Type rank, bool selectChaplainReserve = false)
	{
		startGame();
		ASSERT_EQ(attackerSideHero->getFactionID(), FactionID::CASTLE);
		const int decoded = SecondarySkill::decode(divineMandateSkill);
		ASSERT_GE(decoded, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(decoded), rank, ChangeValueMode::ABSOLUTE);
		ASSERT_NE(attackerSideHero->getSecSkillLevel(SecondarySkill(decoded)), 0);

		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(SpellID(SpellID::BLESS));
		attackerSideHero->addSpellToSpellbook(SpellID(SpellID::MAGIC_ARROW));
		setTestSpellPointTotal(attackerSideHero, 100);
		if(selectChaplainReserve)
		{
			attackerSideHero->applyPerkSelection({divineMandateSkill, chaplainsReservePerk});
			ASSERT_TRUE(attackerSideHero->hasActivePerk(divineMandateSkill, chaplainsReservePerk));

			// Leave exactly three Normal points of headroom so the paired reward is
			// observable even when the spell's cost is paid from Buffer first.
			attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 10, ChangeValueMode::ABSOLUTE);
			const auto normalCapacity = attackerSideHero->manaLimit();
			ASSERT_GE(normalCapacity, 3);
			setTestSpellPointTotal(attackerSideHero, normalCapacity + 20);
			attackerSideHero->setNormalSpellPoints(normalCapacity - 3);
			ASSERT_EQ(attackerSideHero->getNormalSpellPoints(), normalCapacity - 3);
			ASSERT_EQ(attackerSideHero->getBufferSpellPoints(), 20);
		}

		startBattle();
		BattleUnitsChanged changes;
		changes.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			changes.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(changes);
		active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 100);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 100);
		ASSERT_NE(active, nullptr);
		ASSERT_NE(enemy, nullptr);
		beginCombat();

		BattleSetActiveStack activate;
		activate.battleID = BattleID(0);
		activate.stack = active->unitId();
		activate.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activate);
		ASSERT_EQ(battle()->battleActiveUnit(), active);
		ASSERT_EQ(battle()->battleGetOwner(battle()->battleActiveUnit()), PlayerColor(0));

		callback = std::make_shared<RecordingDivineMandateCallback>();
		callback->onBattleStarted(battle());
		environment = std::make_shared<DivineMandateAIEnvironment>(gameState());
		const auto status = battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER);
		ASSERT_TRUE(status.active);
		EXPECT_EQ(status.maximumPairs, static_cast<uint8_t>(rank));
	}

	bool issueSpell(SpellID spell)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = spell;
		action.aimToUnit(active);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	bool runEvaluator()
	{
		BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
			BattleSide::ATTACKER, 1.0f, 2);
		return evaluator.attemptCastingSpell(active, true);
	}
};

TEST_F(NewHorizonsDivineMandateAITest, OrderOpensLightOnlySpellAndAICompletesTheAcceptedPair)
{
	prepareBattle(MasteryLevel::BASIC, true);
	ASSERT_TRUE(battle()->battleCanUseHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE));
	const auto spellCost = battle()->battleGetSpellCost(SpellID(SpellID::BLESS).toSpell(), attackerSideHero);
	const auto normalBeforeSpell = attackerSideHero->getNormalSpellPoints();
	const auto bufferBeforeSpell = attackerSideHero->getBufferSpellPoints();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE)));

	const auto status = battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER);
	ASSERT_TRUE(status.pendingFollowup);
	EXPECT_EQ(status.pendingFollowup->allowance, AllowanceKind::SPELL);
	EXPECT_EQ(status.pendingFollowup->source, GrantSource::DIVINE_MANDATE);
	EXPECT_FALSE(battle()->battleGetSpellActionAllowance(BattleSide::ATTACKER,
		SpellID(SpellID::MAGIC_ARROW)));
	EXPECT_TRUE(battle()->battleGetSpellActionAllowance(BattleSide::ATTACKER,
		SpellID(SpellID::BLESS)));

	ASSERT_TRUE(runEvaluator());
	ASSERT_EQ(callback->heroActions.size(), 1u);
	const auto selected = callback->heroActions.front();
	ASSERT_EQ(selected.actionType, EActionType::HERO_SPELL);
	EXPECT_EQ(selected.spell, SpellID(SpellID::BLESS));
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), normalBeforeSpell);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), bufferBeforeSpell);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), selected));

	const auto completed = battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER);
	EXPECT_EQ(completed.completedPairs, 1);
	EXPECT_FALSE(completed.pendingFollowup);
	const auto normalAfterSpellCost = normalBeforeSpell - std::max(0, spellCost - bufferBeforeSpell);
	const auto bufferAfterSpellCost = std::max(0, bufferBeforeSpell - spellCost);
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(),
		std::min<int32_t>(attackerSideHero->manaLimit(), normalAfterSpellCost + 3));
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), bufferAfterSpellCost);
	EXPECT_FALSE(battle()->battleGetSpellActionAllowance(BattleSide::ATTACKER,
		SpellID(SpellID::BLESS)));
}

TEST_F(NewHorizonsDivineMandateAITest, LightSpellOpensOrderAndAICompletesTheAcceptedPair)
{
	prepareBattle(MasteryLevel::BASIC, true);
	const auto spellCost = battle()->battleGetSpellCost(SpellID(SpellID::BLESS).toSpell(), attackerSideHero);
	const auto normalBeforeSpell = attackerSideHero->getNormalSpellPoints();
	const auto bufferBeforeSpell = attackerSideHero->getBufferSpellPoints();
	ASSERT_TRUE(issueSpell(SpellID(SpellID::BLESS)));
	const auto normalAfterSpellCost = normalBeforeSpell - std::max(0, spellCost - bufferBeforeSpell);
	const auto bufferAfterSpellCost = std::max(0, bufferBeforeSpell - spellCost);
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), normalAfterSpellCost);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), bufferAfterSpellCost);

	const auto status = battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER);
	ASSERT_TRUE(status.pendingFollowup);
	EXPECT_EQ(status.pendingFollowup->allowance, AllowanceKind::ORDER);
	EXPECT_EQ(status.pendingFollowup->source, GrantSource::DIVINE_MANDATE);

	ASSERT_TRUE(runEvaluator());
	ASSERT_EQ(callback->heroActions.size(), 1u);
	const auto selected = callback->heroActions.front();
	ASSERT_EQ(selected.actionType, EActionType::HERO_COMMAND);
	EXPECT_TRUE(heroCommands::isActive(selected.command));
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), normalAfterSpellCost);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), bufferAfterSpellCost);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), selected));

	const auto completed = battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER);
	EXPECT_EQ(completed.completedPairs, 1);
	EXPECT_FALSE(completed.pendingFollowup);
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(),
		std::min<int32_t>(attackerSideHero->manaLimit(), normalAfterSpellCost + 3));
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), bufferAfterSpellCost);
}

TEST_F(NewHorizonsDivineMandateAITest, CreatureActionDoesNotCancelAnUnusedFollowup)
{
	prepareBattle(MasteryLevel::BASIC);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE)));
	ASSERT_TRUE(battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER).pendingFollowup);

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeDefend(active)));
	const auto afterCreatureAction = battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER);
	EXPECT_EQ(afterCreatureAction.completedPairs, 0);
	ASSERT_TRUE(afterCreatureAction.pendingFollowup);
	EXPECT_EQ(afterCreatureAction.pendingFollowup->allowance, AllowanceKind::SPELL);
}

TEST(NewHorizonsDivineMandateLedgerTest, CompletedPairCapsAreOneTwoAndThreeAndDoNotRecurse)
{
	for(uint8_t maximumPairs = 1; maximumPairs <= 3; ++maximumPairs)
	{
		Ledger ledger;
		int32_t round = 1;
		ledger.resetForRound(round);
		for(uint8_t completed = 0; completed < maximumPairs; ++completed)
		{
			if(completed > 0)
				ledger.resetForRound(++round);
			const bool lightFirst = completed % 2 == 0;
			const auto firstAction = lightFirst ? ActionKind::SPELL : ActionKind::ORDER;
			const auto first = ledger.eligibleAllowance(firstAction, round);
			ASSERT_TRUE(first);
			if(lightFirst)
				applyAcceptedSpell(ledger, first->grantId, round, true, maximumPairs);
			else
				applyAcceptedOrder(ledger, first->grantId, round, maximumPairs);
			const auto pending = std::find_if(ledger.grants.begin(), ledger.grants.end(), [](const auto & grant)
			{
				return grant.source == GrantSource::DIVINE_MANDATE;
			});
			ASSERT_NE(pending, ledger.grants.end());
			const auto followupAction = lightFirst ? ActionKind::ORDER : ActionKind::SPELL;
			const auto followup = ledger.eligibleAllowance(followupAction, round);
			ASSERT_TRUE(followup);
			EXPECT_EQ(followup->grantId, pending->id);
			if(lightFirst)
				applyAcceptedOrder(ledger, followup->grantId, round, maximumPairs);
			else
				applyAcceptedSpell(ledger, followup->grantId, round, true, maximumPairs);
			EXPECT_EQ(ledger.divineMandateCompletedPairs, completed + 1);
			EXPECT_FALSE(std::any_of(ledger.grants.begin(), ledger.grants.end(), [](const auto & grant)
			{
				return grant.source == GrantSource::DIVINE_MANDATE;
			})) << "A completed pair must not recursively create another follow-up";
		}

		ledger.resetForRound(++round);
		const auto regular = ledger.eligibleAllowance(ActionKind::ORDER, round);
		ASSERT_TRUE(regular);
		const auto receipt = ledger.consumeAllowance(regular->grantId, ActionKind::ORDER, round);
		ASSERT_TRUE(receipt);
		DivineMandateTransition::applyAcceptedAction(ledger, *receipt, round, false, maximumPairs);
		EXPECT_EQ(ledger.divineMandateCompletedPairs, maximumPairs);
		EXPECT_FALSE(std::any_of(ledger.grants.begin(), ledger.grants.end(), [](const auto & grant)
		{
			return grant.source == GrantSource::DIVINE_MANDATE;
		})) << "The current-rank pair cap is reached";
	}
}

TEST(NewHorizonsDivineMandateLedgerTest, UnusedOpportunityExpiresWithoutSpendingAndCounterRoundTrips)
{
	Ledger ledger;
	ledger.resetForRound(1);
	const auto order = ledger.eligibleAllowance(ActionKind::ORDER, 1);
	ASSERT_TRUE(order);
	applyAcceptedOrder(ledger, order->grantId, 1, 3);
	ASSERT_TRUE(std::any_of(ledger.grants.begin(), ledger.grants.end(), [](const auto & grant)
	{
		return grant.source == GrantSource::DIVINE_MANDATE;
	}));
	EXPECT_EQ(ledger.divineMandateCompletedPairs, 0);

	ledger.resetForRound(2);
	EXPECT_EQ(ledger.divineMandateCompletedPairs, 0);
	EXPECT_FALSE(std::any_of(ledger.grants.begin(), ledger.grants.end(), [](const auto & grant)
	{
		return grant.source == GrantSource::DIVINE_MANDATE;
	}));

	const auto roundTwoOrder = ledger.eligibleAllowance(ActionKind::ORDER, 2);
	ASSERT_TRUE(roundTwoOrder);
	applyAcceptedOrder(ledger, roundTwoOrder->grantId, 2, 3);
	const auto followup = std::find_if(ledger.grants.begin(), ledger.grants.end(), [](const auto & grant)
	{
		return grant.source == GrantSource::DIVINE_MANDATE;
	});
	ASSERT_NE(followup, ledger.grants.end());
	applyAcceptedSpell(ledger, followup->id, 2, true, 3);
	ASSERT_EQ(ledger.divineMandateCompletedPairs, 1);
	ledger.resetForRound(3);

	CMemorySerializer writer;
	writer.oser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(writer.oser & ledger);
	CMemorySerializer reader(writer.extractBuffer());
	reader.iser.version = ESerializationVersion::CURRENT;
	Ledger restored;
	ASSERT_NO_THROW(reader.iser & restored);
	EXPECT_EQ(restored.divineMandateCompletedPairs, 1);
	EXPECT_EQ(restored.currentRound, 3);

	CMemorySerializer oldWriter;
	oldWriter.oser.version = ESerializationVersion::NEW_HORIZONS_ELEMENTAL_SPELL_DAMAGE;
	EXPECT_THROW(oldWriter.oser & ledger, std::runtime_error);
	EXPECT_TRUE(oldWriter.extractBuffer().empty());
}

TEST(NewHorizonsDivineMandateLedgerTest, NonLightSelectionSkipsMandateButKeepsOtherSpellGrantsUsable)
{
	Ledger ledger;
	ledger.resetForRound(1);
	const auto mandateSpell = ledger.grantAllowance(AllowanceKind::SPELL, GrantSource::DIVINE_MANDATE, 1);
	const auto ordinarySpell = ledger.grantAllowance(AllowanceKind::SPELL, GrantSource::PERK, 1);
	const auto nonLightFilter = [](const Ledger::Grant & grant)
	{
		return Ledger::grantAllowedForSpell(grant, false);
	};
	const auto nonLight = ledger.eligibleAllowance(ActionKind::SPELL, 1, nonLightFilter);
	ASSERT_TRUE(nonLight);
	EXPECT_EQ(nonLight->grantId, ordinarySpell);
	EXPECT_NE(nonLight->grantId, mandateSpell);

	uint8_t metamagicUses = 0;
	uint8_t metamagicPending = 0;
	bool grandUsed = false;
	const auto rejected = HeroSpellAllowanceTransition::commitAcceptedCast(ledger, mandateSpell, 1,
		false, false, 0, false, metamagicUses, metamagicPending, grandUsed, 0, nonLightFilter);
	EXPECT_FALSE(rejected);
	const auto acceptedOther = HeroSpellAllowanceTransition::commitAcceptedCast(ledger, nonLight->grantId, 1,
		false, false, 0, false, metamagicUses, metamagicPending, grandUsed, 0, nonLightFilter);
	ASSERT_TRUE(acceptedOther);
	EXPECT_EQ(acceptedOther->receipt.source, GrantSource::PERK);

	const auto lightFilter = [](const Ledger::Grant & grant)
	{
		return Ledger::grantAllowedForSpell(grant, true);
	};
	const auto light = ledger.eligibleAllowance(ActionKind::SPELL, 1, lightFilter);
	ASSERT_TRUE(light);
	EXPECT_EQ(light->grantId, mandateSpell);
	EXPECT_TRUE(ledger.consumeAllowance(light->grantId, ActionKind::SPELL, 1, lightFilter));
}
