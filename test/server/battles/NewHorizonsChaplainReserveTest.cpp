/*
 * NewHorizonsChaplainReserveTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/CBattleInfoEssentials.h"
#include "../../../lib/battle/HeroCommand.h"
#include "../../../lib/battle/NewHorizonsDivineMandate.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

#include <algorithm>
#include <string>

namespace
{
constexpr auto divineMandateSkill = "new-horizons:divineMandate";
constexpr auto chaplainReservePerk = "new-horizons:divineMandate.chaplainSReserve";

bool activateChaplainReserve(JsonNode & rules)
{
	auto & perks = rules["skills"][std::string(divineMandateSkill)]["perks"].Vector();
	const auto found = std::find_if(perks.begin(), perks.end(), [](const JsonNode & perk)
	{
		return perk["id"].String() == chaplainReservePerk;
	});
	if(found == perks.end())
		return false;
	(*found)["effect"]["status"].String() = "active";
	return true;
}
}

class NewHorizonsChaplainReserveTest : public HeroCommandFixture
{
protected:
	CStack * friendly = nullptr;
	CStack * enemy = nullptr;

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
		if(!activateChaplainReserve(perkRules))
			throw std::runtime_error("Missing Chaplain's Reserve from the New Horizons perk registry");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perkRules));
	}

	SecondarySkill divineMandate() const
	{
		const int decoded = SecondarySkill::decode(divineMandateSkill);
		EXPECT_GE(decoded, 0);
		return SecondarySkill(decoded);
	}

	void selectChaplainReserve(CGHeroInstance * hero, MasteryLevel::Type rank)
	{
		hero->setSecSkillLevel(divineMandate(), rank, ChangeValueMode::ABSOLUTE);
		const auto rankLookup = [hero](const std::string & skillId)
		{
			return hero->getPerkSkillRank(skillId);
		};

		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer(rankLookup, seed);
			const auto selected = std::find_if(offer.begin(), offer.end(), [](const auto & candidate)
			{
				return candidate.selection.skillId == divineMandateSkill
					&& candidate.selection.perkId == chaplainReservePerk;
			});
			if(selected == offer.end())
				continue;

			const auto choice = static_cast<size_t>(std::distance(offer.begin(), selected));
			gameHandler->levelUpHero(hero, offer, choice, seed, false);
			ASSERT_TRUE(hero->hasActivePerk(divineMandateSkill, chaplainReservePerk));
			return;
		}

		FAIL() << chaplainReservePerk << " never appeared in a legal Divine Mandate perk offer";
	}

	void prepare(MasteryLevel::Type rank = MasteryLevel::BASIC, bool selectPerk = true,
		int32_t normal = -1, int32_t buffer = 0)
	{
		startGame();
		ASSERT_EQ(attackerSideHero->getFactionID(), FactionID::CASTLE);
		if(selectPerk)
			selectChaplainReserve(attackerSideHero, rank);
		else
			attackerSideHero->setSecSkillLevel(divineMandate(), rank, ChangeValueMode::ABSOLUTE);

		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(SpellID(SpellID::BLESS));
		attackerSideHero->addSpellToSpellbook(SpellID(SpellID::MAGIC_ARROW));
		const int32_t capacity = attackerSideHero->manaLimit();
		ASSERT_GT(capacity, 0);
		attackerSideHero->initializeSpellPoints(normal < 0 ? capacity : normal, buffer);

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

	bool issue(HeroCommand command)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			BattleAction::makeHeroCommand(BattleSide::ATTACKER, command));
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

	int32_t spellCost(SpellID spell) const
	{
		const auto * spellData = spell.toSpell();
		if(!spellData)
			return 0;
		return battle()->battleGetSpellCost(spellData, attackerSideHero);
	}
};

TEST_F(NewHorizonsChaplainReserveTest, FirstCompletedPairRecoversAfterSpellCostAndLaterPairDoesNotRepeat)
{
	prepare(MasteryLevel::ADVANCED);
	EXPECT_EQ(newHorizonsDivineMandate::chaplainReserveRecovery(attackerSideHero, 0, 1), 3);
	EXPECT_EQ(newHorizonsDivineMandate::chaplainReserveRecovery(attackerSideHero, 1, 2), 0);
	const int32_t capacity = attackerSideHero->manaLimit();
	const int32_t blessCost = spellCost(SpellID(SpellID::BLESS));
	ASSERT_GT(blessCost, 0);
	ASSERT_EQ(attackerSideHero->getNormalSpellPoints(), capacity);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), 0);
	EXPECT_EQ(battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER).completedPairs, 0);

	// An Order opens the real Light-Spell continuation, but earns no recovery yet.
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), capacity);
	EXPECT_EQ(battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER).completedPairs, 0);
	ASSERT_TRUE(cast(SpellID(SpellID::BLESS), friendly));
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), capacity - blessCost + 3);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), 0);
	EXPECT_EQ(battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER).completedPairs, 1);

	// On the next round the reverse ordering completes the Advanced second pair;
	// Chaplain's Reserve is still limited to the first completed pair in combat.
	advanceRound();
	const int32_t beforeSecondCast = attackerSideHero->getNormalSpellPoints();
	ASSERT_TRUE(cast(SpellID(SpellID::BLESS), friendly));
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), beforeSecondCast - blessCost);
	EXPECT_EQ(battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER).completedPairs, 1);
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), beforeSecondCast - blessCost);
	EXPECT_EQ(battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER).completedPairs, 2);
}

TEST_F(NewHorizonsChaplainReserveTest, RecoveryClampsToMissingNormalCapacityAndDoesNotRestoreBuffer)
{
	prepare(MasteryLevel::BASIC, true, 98, 100);
	const int32_t capacity = attackerSideHero->manaLimit();
	ASSERT_EQ(capacity, 100);
	const int32_t blessCost = spellCost(SpellID(SpellID::BLESS));
	ASSERT_GT(blessCost, 0);

	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	ASSERT_TRUE(cast(SpellID(SpellID::BLESS), friendly));
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), capacity);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), 100 - blessCost)
		<< "Chaplain's Reserve fills the two-point Normal deficit but does not add Buffer";
}

TEST_F(NewHorizonsChaplainReserveTest, FullNormalPoolCannotOverflowItsRecoveryIntoBuffer)
{
	prepare(MasteryLevel::BASIC, true, 100, 100);
	const int32_t blessCost = spellCost(SpellID(SpellID::BLESS));
	ASSERT_GT(blessCost, 0);

	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	ASSERT_TRUE(cast(SpellID(SpellID::BLESS), friendly));
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), attackerSideHero->manaLimit());
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), 100 - blessCost)
		<< "The spell spends Buffer first; the later capped Normal recovery must not refill it";
}

TEST_F(NewHorizonsChaplainReserveTest, UnselectedReserveDoesNotRecoverForAnOtherwiseCompletedPair)
{
	prepare(MasteryLevel::BASIC, false);
	ASSERT_FALSE(attackerSideHero->hasActivePerk(divineMandateSkill, chaplainReservePerk));
	EXPECT_EQ(newHorizonsDivineMandate::chaplainReserveRecovery(attackerSideHero, 0, 1), 0);
	const int32_t before = attackerSideHero->getNormalSpellPoints();
	const int32_t blessCost = spellCost(SpellID(SpellID::BLESS));

	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	ASSERT_TRUE(cast(SpellID(SpellID::BLESS), friendly));
	EXPECT_EQ(battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER).completedPairs, 1);
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), before - blessCost);
}

TEST_F(NewHorizonsChaplainReserveTest, RejectedNonLightFollowupExpiresWithoutRecovery)
{
	prepare();
	const int32_t before = attackerSideHero->getNormalSpellPoints();
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	ASSERT_TRUE(battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER).pendingFollowup);

	EXPECT_FALSE(cast(SpellID(SpellID::MAGIC_ARROW), enemy));
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), before);
	EXPECT_EQ(battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER).completedPairs, 0);
	ASSERT_TRUE(battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER).pendingFollowup);

	advanceRound();
	EXPECT_FALSE(battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER).pendingFollowup);
	EXPECT_EQ(battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER).completedPairs, 0);
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), before)
		<< "An expired, uncompleted Divine Mandate sequence does not activate the perk";
}
