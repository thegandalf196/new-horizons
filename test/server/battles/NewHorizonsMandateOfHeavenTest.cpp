/*
 * NewHorizonsMandateOfHeavenTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/CBattleInfoEssentials.h"
#include "../../../lib/battle/HeroActionAllowanceState.h"
#include "../../../lib/battle/HeroCommand.h"
#include "../../../lib/battle/NewHorizonsDivineMandate.h"
#include "../../../lib/entities/hero/NewHorizonsPerkState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/serializer/ESerializationVersion.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>

namespace
{
constexpr auto divineMandateSkill = "new-horizons:divineMandate";
constexpr auto chaplainReservePerk = "new-horizons:divineMandate.chaplainSReserve";
constexpr auto knightlySequencePerk = "new-horizons:divineMandate.knightlySequence";
constexpr auto mandateOfHeavenPerk = "new-horizons:divineMandate.mandateOfHeaven";

bool setPerkStatus(JsonNode & rules, const std::string & perkId, const std::string & status)
{
	auto & perks = rules["skills"][std::string(divineMandateSkill)]["perks"].Vector();
	const auto found = std::find_if(perks.begin(), perks.end(), [&perkId](const JsonNode & perk)
	{
		return perk["id"].String() == perkId;
	});
	if(found == perks.end())
		return false;
	(*found)["effect"]["status"].String() = status;
	return true;
}

constexpr auto previousVersion = ESerializationVersion::NEW_HORIZONS_KNIGHTLY_SEQUENCE;
}

class NewHorizonsMandateOfHeavenTest : public HeroCommandFixture
{
protected:
	CStack * friendly = nullptr;
	CStack * enemy = nullptr;
	int32_t expectedManaSpent = 0;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));

		JsonNode perkRules(JsonPath::builtin("config/newHorizonsPerks"));
		if(!setPerkStatus(perkRules, chaplainReservePerk, "active")
			|| !setPerkStatus(perkRules, knightlySequencePerk, "active")
			|| !setPerkStatus(perkRules, mandateOfHeavenPerk, "active"))
			throw std::runtime_error("Missing Divine Mandate perk from the New Horizons registry");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perkRules));
	}

	SecondarySkill divineMandate() const
	{
		const int decoded = SecondarySkill::decode(divineMandateSkill);
		EXPECT_GE(decoded, 0);
		return SecondarySkill(decoded);
	}

	void selectPerk(CGHeroInstance * hero, MasteryLevel::Type rank, const std::string & perkId)
	{
		hero->setSecSkillLevel(divineMandate(), rank, ChangeValueMode::ABSOLUTE);
		const auto rankLookup = [hero](const std::string & skillId)
		{
			return hero->getPerkSkillRank(skillId);
		};

		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer(rankLookup, seed);
			const auto selected = std::find_if(offer.begin(), offer.end(), [&perkId, rank](const auto & candidate)
			{
				return candidate.selection.skillId == divineMandateSkill
					&& candidate.selection.perkId == perkId
					&& candidate.requiredRank == rank;
			});
			if(selected == offer.end())
				continue;

			const auto choice = static_cast<size_t>(std::distance(offer.begin(), selected));
			gameHandler->levelUpHero(hero, offer, choice, seed, false);
			ASSERT_TRUE(hero->hasActivePerk(divineMandateSkill, perkId));
			return;
		}

		FAIL() << perkId << " never appeared in a legal " << rank << " Divine Mandate perk offer";
	}

	void prepare(bool selectMandateOfHeaven)
	{
		expectedManaSpent = 0;
		startGame();
		ASSERT_EQ(attackerSideHero->getFactionID(), FactionID::CASTLE);

		// Acquire each rank through its real offer, preserving the Basic-to-Expert prerequisites.
		selectPerk(attackerSideHero, MasteryLevel::BASIC, chaplainReservePerk);
		selectPerk(attackerSideHero, MasteryLevel::ADVANCED, knightlySequencePerk);
		attackerSideHero->setSecSkillLevel(divineMandate(), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		if(selectMandateOfHeaven)
			selectPerk(attackerSideHero, MasteryLevel::EXPERT, mandateOfHeavenPerk);

		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(SpellID(SpellID::BLESS));
		const int32_t capacity = attackerSideHero->manaLimit();
		ASSERT_GT(capacity, 20);
		attackerSideHero->initializeSpellPoints(capacity - 20, 0);

		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		if(!remove.changedStacks.empty())
			gameHandler->sendAndApply(remove);

		friendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 100);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 100);
		ASSERT_NE(friendly, nullptr);
		ASSERT_NE(enemy, nullptr);
		beginCombat();
		activate(friendly);
		ASSERT_EQ(battle()->battleActiveUnit(), friendly);
		ASSERT_EQ(battle()->battleGetOwner(friendly), PlayerColor(0));
	}

	void activate(const CStack * stack)
	{
		BattleSetActiveStack activation;
		activation.battleID = BattleID(0);
		activation.stack = stack->unitId();
		activation.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activation);
	}

	void advanceRound()
	{
		BattleNextRound next;
		next.battleID = BattleID(0);
		gameHandler->sendAndApply(next);
	}

	bool issueCharge()
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE));
	}

	bool castBless()
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = SpellID::BLESS;
		action.aimToUnit(friendly);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	int32_t blessCost() const
	{
		return battle()->battleGetSpellCost(SpellID(SpellID::BLESS).toSpell(), attackerSideHero);
	}

	void completeOrderFirstPair(uint8_t expectedCompletedPairs)
	{
		const int32_t before = attackerSideHero->getNormalSpellPoints();
		ASSERT_TRUE(issueCharge());
		const auto pending = battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER);
		ASSERT_TRUE(pending.pendingFollowup);
		EXPECT_EQ(pending.pendingFollowup->source, HeroActionAllowanceState::GrantSource::DIVINE_MANDATE);
		EXPECT_EQ(pending.pendingFollowup->allowance, HeroActionAllowanceState::AllowanceKind::SPELL);
		const int32_t cost = blessCost();
		ASSERT_GT(cost, 0);
		ASSERT_TRUE(castBless());
		expectedManaSpent += cost;
		const int32_t recovery = expectedCompletedPairs == 1
			? std::min(3, attackerSideHero->manaLimit() - (before - cost)) : 0;
		EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), before - cost + recovery);
		EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), 0);
		ASSERT_FALSE(server.casts.empty());
		EXPECT_EQ(server.casts.back().announcement.paidHeroManaCost, cost);
		EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).acceptedHeroManaSpent,
			expectedManaSpent);
		EXPECT_EQ(battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER).completedPairs,
			expectedCompletedPairs);
	}

	void completeSpellFirstPair(uint8_t expectedCompletedPairs)
	{
		const int32_t before = attackerSideHero->getNormalSpellPoints();
		const int32_t cost = blessCost();
		ASSERT_GT(cost, 0);
		ASSERT_TRUE(castBless());
		expectedManaSpent += cost;
		EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), before - cost);
		ASSERT_FALSE(server.casts.empty());
		EXPECT_EQ(server.casts.back().announcement.paidHeroManaCost, cost);
		EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).acceptedHeroManaSpent,
			expectedManaSpent);
		const auto pending = battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER);
		ASSERT_TRUE(pending.pendingFollowup);
		EXPECT_EQ(pending.pendingFollowup->source, HeroActionAllowanceState::GrantSource::DIVINE_MANDATE);
		EXPECT_EQ(pending.pendingFollowup->allowance, HeroActionAllowanceState::AllowanceKind::ORDER);
		ASSERT_TRUE(issueCharge());
		EXPECT_EQ(battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER).completedPairs,
			expectedCompletedPairs);
	}
};

TEST_F(NewHorizonsMandateOfHeavenTest, FourCompletedPairsAlternateActionsAndOnlyFirstPairRestoresMana)
{
	prepare(true);
	const auto initialStatus = battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER);
	ASSERT_TRUE(attackerSideHero->hasActivePerk(divineMandateSkill, chaplainReservePerk));
	ASSERT_TRUE(attackerSideHero->hasActivePerk(divineMandateSkill, knightlySequencePerk));
	ASSERT_TRUE(attackerSideHero->hasActivePerk(divineMandateSkill, mandateOfHeavenPerk));
	EXPECT_TRUE(initialStatus.active);
	EXPECT_EQ(initialStatus.maximumPairs, 4);
	EXPECT_EQ(initialStatus.completedPairs, 0);
	EXPECT_EQ(newHorizonsDivineMandate::chaplainReserveRecovery(attackerSideHero, 0, 1), 3);
	EXPECT_EQ(newHorizonsDivineMandate::chaplainReserveRecovery(attackerSideHero, 1, 2), 0);

	// A started but unused Order -> Spell opportunity expires at round end and earns no pair or recovery.
	const int32_t beforeUnused = attackerSideHero->getNormalSpellPoints();
	ASSERT_TRUE(issueCharge());
	ASSERT_TRUE(battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER).pendingFollowup);
	EXPECT_EQ(battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER).completedPairs, 0);
	advanceRound();
	const auto expired = battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER);
	EXPECT_FALSE(expired.pendingFollowup);
	EXPECT_EQ(expired.completedPairs, 0);
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), beforeUnused);

	completeOrderFirstPair(1);
	advanceRound();
	completeSpellFirstPair(2);
	advanceRound();
	completeOrderFirstPair(3);
	advanceRound();
	completeSpellFirstPair(4);

	EXPECT_EQ(battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER).maximumPairs, 4);
	EXPECT_EQ(battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER).completedPairs, 4);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), 0);
	const auto restored = CMemorySerializer::deepCopy(*battle(), gameState().get());
	ASSERT_NE(restored, nullptr);
	EXPECT_EQ(restored->getSide(BattleSide::ATTACKER).heroActionAllowances.divineMandateCompletedPairs, 4);

	CMemorySerializer oldLedgerWriter;
	oldLedgerWriter.oser.version = previousVersion;
	EXPECT_THROW(oldLedgerWriter.oser & battle()->getSide(BattleSide::ATTACKER).heroActionAllowances,
		std::runtime_error);
	EXPECT_TRUE(oldLedgerWriter.extractBuffer().empty());

	CMemorySerializer oldSideWriter;
	oldSideWriter.oser.version = previousVersion;
	EXPECT_THROW(oldSideWriter.oser & battle()->getSide(BattleSide::ATTACKER), std::runtime_error);
	EXPECT_TRUE(oldSideWriter.extractBuffer().empty());

	CMemorySerializer oldBattleWriter;
	oldBattleWriter.oser.version = previousVersion;
	EXPECT_THROW(oldBattleWriter.oser & *battle(), std::runtime_error);
	EXPECT_TRUE(oldBattleWriter.extractBuffer().empty());

	BattleStart startSnapshot;
	startSnapshot.battleID = BattleID(0);
	ASSERT_NO_THROW(startSnapshot.info = CMemorySerializer::deepCopy(*battle(), gameState().get()));
	CMemorySerializer oldStartWriter;
	oldStartWriter.oser.version = previousVersion;
	EXPECT_THROW(oldStartWriter.oser & startSnapshot, std::runtime_error);
	EXPECT_TRUE(oldStartWriter.extractBuffer().empty());

	// A fifth ordinary Charge can still be issued, but the cap no longer offers its Spell continuation.
	advanceRound();
	ASSERT_TRUE(issueCharge());
	const auto capped = battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER);
	EXPECT_EQ(capped.maximumPairs, 4);
	EXPECT_EQ(capped.completedPairs, 4);
	EXPECT_FALSE(capped.pendingFollowup);
	EXPECT_FALSE(castBless());
	ASSERT_EQ(battle()->battleActiveUnit(), friendly);
	EXPECT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeDefend(friendly)))
		<< "Completing or exhausting Hero-action pairs does not lock Creature Activation";
}

TEST_F(NewHorizonsMandateOfHeavenTest, ExpertWithoutSelectedPerkRetainsThreePairCap)
{
	prepare(false);
	const auto status = battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER);
	EXPECT_TRUE(status.active);
	EXPECT_EQ(status.maximumPairs, 3);
	EXPECT_FALSE(attackerSideHero->hasActivePerk(divineMandateSkill, mandateOfHeavenPerk));
}

TEST_F(NewHorizonsMandateOfHeavenTest, InactiveSavedPerkRuleRetainsThreePairCap)
{
	prepare(true);
	// Simulate loading the same selected identity against a saved profile where the mechanic is inactive.
	auto & savedPerks = const_cast<newHorizonsHeroes::PerkState &>(attackerSideHero->getPerkState());
	ASSERT_TRUE(savedPerks.hasSelection(divineMandateSkill, mandateOfHeavenPerk));
	ASSERT_TRUE(setPerkStatus(savedPerks.rules, mandateOfHeavenPerk, "planned"));
	EXPECT_TRUE(savedPerks.hasSelection(divineMandateSkill, mandateOfHeavenPerk));
	EXPECT_FALSE(attackerSideHero->hasActivePerk(divineMandateSkill, mandateOfHeavenPerk));
	const auto status = battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER);
	EXPECT_TRUE(status.active);
	EXPECT_EQ(status.maximumPairs, 3);
}
