/*
 * NewHorizonsMandateOfHeavenTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "../../PerkTestSetup.h"
#include "../../PerkStateFixture.h"

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

// Pair-policy characterization, before the passive Divine Mandate redesign.
// Complete authored row retained from 2488cb6bdc17, not current Consecrated Arms.
void captureHistoricalPairRules(JsonNode & rules)
{
	const std::string text = R"DIVINE_PAIR({
	"id": "new-horizons:divineMandate",
	"name": "Divine Mandate",
	"domain": "Faction",
	"availability": "Castle heroes only",
	"description": "Faction-locked Skill. Castle unites sacred intervention and military command. Divine Mandate creates a precise exception to the normal Spell-or-Order choice without introducing a second action currency.",
	"ranks": {
		"basic": {
			"description": "Once per completed pair per combat, when the hero uses the Hero Action to cast a Light Spell, gain one Order-only follow-up usable until round end; or after using the Hero Action to issue an Order, gain one Light-Spell-only follow-up usable until round end.",
			"effect": {
				"status": "active",
				"description": "Once per completed pair per combat, when the hero uses the Hero Action to cast a Light Spell, gain one Order-only follow-up usable until round end; or after using the Hero Action to issue an Order, gain one Light-Spell-only follow-up usable until round end."
			}
		},
		"advanced": {
			"description": "Divine Mandate may be used twice per combat.",
			"effect": {
				"status": "active",
				"description": "Divine Mandate may be used twice per combat."
			}
		},
		"expert": {
			"description": "Divine Mandate may be used three times per combat.",
			"effect": {
				"status": "active",
				"description": "Divine Mandate may be used three times per combat."
			}
		}
	},
	"perks": [
		{
			"id": "new-horizons:divineMandate.sacredCommand",
			"name": "Sacred Command",
			"requires": "basic",
			"description": "An Order issued through Divine Mandate gains +10 percentage points of efficiency on its attribute-derived components.",
			"effect": {
				"status": "active",
				"description": "An Order issued through Divine Mandate gains +10 percentage points of efficiency on its attribute-derived components."
			}
		},
		{
			"id": "new-horizons:divineMandate.consecratedCasting",
			"name": "Consecrated Casting",
			"requires": "basic",
			"description": "A Light Spell cast through Divine Mandate gains +10% to its Spell Power-derived numerical component.",
			"effect": {
				"status": "active",
				"description": "A Light Spell cast through Divine Mandate gains +10% to its Spell Power-derived numerical component."
			}
		},
		{
			"id": "new-horizons:divineMandate.sharedPurpose",
			"name": "Shared Purpose",
			"requires": "basic",
			"description": "If the paired Light Spell and Order both affect the same friendly stack, that stack gains +1 Morale until its next activation.",
			"effect": {
				"status": "active",
				"description": "If the paired Light Spell and Order both affect the same friendly stack, that stack gains +1 Morale until its next activation."
			}
		},
		{
			"id": "new-horizons:divineMandate.chaplainSReserve",
			"name": "Chaplain's Reserve",
			"requires": "basic",
			"description": "After the first Divine Mandate sequence each combat resolves, recover 3 Mana.",
			"effect": {
				"status": "active",
				"description": "After the first Divine Mandate sequence each combat resolves, recover 3 Mana."
			}
		},
		{
			"id": "new-horizons:divineMandate.divineDiscipline",
			"name": "Divine Discipline",
			"requires": "advanced",
			"description": "An Order issued through Divine Mandate that would normally end at round end remains separately on its original affected friendly recipients through their first genuine completed Creature Activation after the issuing round. Wait and same-activation continuations do not consume this carry.",
			"effect": {
				"status": "active",
				"description": "An Order issued through Divine Mandate that would normally end at round end remains separately on its original affected friendly recipients through their first genuine completed Creature Activation after the issuing round. Wait and same-activation continuations do not consume this carry."
			}
		},
		{
			"id": "new-horizons:divineMandate.purifyingMandate",
			"name": "Purifying Mandate",
			"requires": "advanced",
			"description": "When a Light Spell cast through Divine Mandate removes a negative magical effect, it also removes one physical affliction from that target if present.",
			"effect": {
				"status": "active",
				"description": "When a Light Spell cast through Divine Mandate removes a negative magical effect, it also removes one physical affliction from that target if present."
			}
		},
		{
			"id": "new-horizons:divineMandate.knightlySequence",
			"name": "Knightly Sequence",
			"requires": "advanced",
			"description": "If the Order is performed first, the paired Light Spell costs 2 less Mana; if the Light Spell is performed first, the paired Order gains +5 additional percentage points of efficiency.",
			"effect": {
				"status": "active",
				"description": "If the Order is performed first, the paired Light Spell costs 2 less Mana; if the Light Spell is performed first, the paired Order gains +5 additional percentage points of efficiency."
			}
		},
		{
			"id": "new-horizons:divineMandate.royalStandard",
			"name": "Royal Standard",
			"requires": "advanced",
			"description": "Friendly stacks affected by an Order issued through Divine Mandate treat negative Morale as 0 for that Order's duration.",
			"effect": {
				"status": "active",
				"description": "Friendly stacks affected by an Order issued through Divine Mandate treat negative Morale as 0 for that Order's duration."
			}
		},
		{
			"id": "new-horizons:divineMandate.mandateOfHeaven",
			"name": "Mandate of Heaven",
			"requires": "expert",
			"description": "The first Divine Mandate sequence each combat does not count against the Skill's normal per-combat usage limit.",
			"effect": {
				"status": "active",
				"description": "The first Divine Mandate sequence each combat does not count against the Skill's normal per-combat usage limit."
			}
		},
		{
			"id": "new-horizons:divineMandate.crownAndAltar",
			"name": "Crown and Altar",
			"requires": "expert",
			"description": "The second action of a Divine Mandate pair automatically gains +20% to its rating-derived numerical components on each friendly stack also affected by the first action. This applies in either Spell-then-Order or Order-then-Spell order. It requires no extra button or target-preselection step and does not retroactively change the first action.",
			"effect": {
				"status": "active",
				"description": "The second action of a Divine Mandate pair automatically gains +20% to its rating-derived numerical components on each friendly stack also affected by the first action. This applies in either Spell-then-Order or Order-then-Spell order. It requires no extra button or target-preselection step and does not retroactively change the first action."
			}
		}
	]
})DIVINE_PAIR";
	rules["skills"][std::string(divineMandateSkill)] = JsonNode(text);
	rules.Struct().erase("divineMandatePolicy");
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
		useRevisedOrders = true; // Shipped Warfare issuance; no retired finite Order pool.
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));

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

		for(uint64_t seed = 0; seed < perkTestSetup::seedLimit(hero->getPerkState(), 4096); ++seed)
		{
			const auto offer = perkTestSetup::forSkill(hero->getPerkState(), divineMandateSkill, rankLookup, seed, hero->getPerkOfferWeightContext());
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
		// Author this hero's captured legacy profile after ordinary current-rule
		// initialization; unrelated starting heroes keep their current roster.
		auto & perks = const_cast<newHorizonsHeroes::PerkState &>(attackerSideHero->getPerkState());
		perkStateFixture::editRules(perks, captureHistoricalPairRules);

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

TEST_F(NewHorizonsMandateOfHeavenTest, InactiveSelectedStrictPerkRuleIsRejectedWithoutChangingTheCapturedCap)
{
	prepare(true);
	// Replacement must re-admit the immutable capture and its selected tiers.
	// A strict profile cannot make an acquired prerequisite/perk inactive in place.
	auto & savedPerks = const_cast<newHorizonsHeroes::PerkState &>(attackerSideHero->getPerkState());
	ASSERT_TRUE(savedPerks.hasSelection(divineMandateSkill, mandateOfHeavenPerk));
	const auto owner = savedPerks.capturedOwner();
	EXPECT_THROW(perkStateFixture::editRules(savedPerks, [](JsonNode & rules)
	{
		if(!setPerkStatus(rules, mandateOfHeavenPerk, "planned"))
			throw std::runtime_error("Missing selected historical Mandate identity");
	}), std::runtime_error);
	EXPECT_EQ(savedPerks.capturedOwner(), owner);
	EXPECT_TRUE(savedPerks.hasSelection(divineMandateSkill, mandateOfHeavenPerk));
	EXPECT_TRUE(attackerSideHero->hasActivePerk(divineMandateSkill, mandateOfHeavenPerk));
	const auto status = battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER);
	EXPECT_TRUE(status.active);
	EXPECT_EQ(status.maximumPairs, 4);
}

TEST_F(NewHorizonsMandateOfHeavenTest, DeserializedInactiveSelectedHistoricalPerkRetainsThreePairCap)
{
	prepare(true);
	auto & livePerks = const_cast<newHorizonsHeroes::PerkState &>(attackerSideHero->getPerkState());
	auto payload = livePerks.toJson();
	auto & rules = payload["rules"];
	// Pre-strict captured profiles retain existing inactive identities. Neither
	// current rules nor deserialization may reinterpret them as active perks.
	rules.Struct().erase("perkProgressionPolicyVersion");
	rules.Struct().erase("fourOfferProgressionVersion");
	ASSERT_TRUE(setPerkStatus(rules, mandateOfHeavenPerk, "planned"));
	auto historical = newHorizonsHeroes::PerkState::fromJson(payload);
	ASSERT_TRUE(historical.hasSelection(divineMandateSkill, mandateOfHeavenPerk));
	CMemorySerializer saved;
	saved.oser & historical;
	newHorizonsHeroes::PerkState restored;
	saved.iser & restored;
	EXPECT_EQ(restored.getRules(), historical.getRules());
	EXPECT_EQ(restored.getSelected(), historical.getSelected());
	livePerks = std::move(restored);
	EXPECT_TRUE(livePerks.hasSelection(divineMandateSkill, mandateOfHeavenPerk));
	EXPECT_FALSE(attackerSideHero->hasActivePerk(divineMandateSkill, mandateOfHeavenPerk));
	const auto status = battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER);
	EXPECT_TRUE(status.active);
	EXPECT_EQ(status.maximumPairs, 3);
	const auto snapshot = CMemorySerializer::deepCopy(*battle(), gameState().get());
	ASSERT_NE(snapshot, nullptr);
	EXPECT_EQ(snapshot->battleGetDivineMandateStatus(BattleSide::ATTACKER).maximumPairs, 3);
}
