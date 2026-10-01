/*
 * NewHorizonsManaConservationTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/battle/SideInBattle.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../server/battles/BattleProcessor.h"
#include "../../../server/queries/BattleQueries.h"
#include "../../../server/queries/QueriesProcessor.h"

namespace
{
constexpr auto wisdomSkill = "new-horizons:wisdom";
constexpr auto mysticismPerk = "new-horizons:wisdom.mysticism";
constexpr auto manaConservationPerk = "new-horizons:wisdom.manaConservation";
constexpr auto counterspellKey = "new-horizons:counterspell";

SpellID counterspell()
{
	return SpellID(SpellID::decode(counterspellKey));
}

class NewHorizonsManaConservationTest : public HeroCommandFixture
{
protected:
	bool plannedManaConservation = false;
	bool enableHistoricalCounterspell = false;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		JsonNode magicRules(JsonPath::builtin("config/newHorizonsMagic"));
		if(enableHistoricalCounterspell)
			magicRules["spells"][counterspellKey].Struct().erase("active");
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magicRules);

		JsonNode perkRules(JsonPath::builtin("config/newHorizonsPerks"));
		if(plannedManaConservation)
			for(auto & perk : perkRules["skills"][wisdomSkill]["perks"].Vector())
				if(perk["id"].String() == manaConservationPerk)
					perk["effect"]["status"].String() = "planned";
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perkRules);
	}

	void prepare(int wisdomRank = 2, bool selectConservation = true, bool includeCounterspell = false)
	{
		enableHistoricalCounterspell = includeCounterspell;
		startGame();
		const int wisdom = SecondarySkill::decode(wisdomSkill);
		ASSERT_GE(wisdom, 0);
		if(wisdomRank > 0)
		{
			attackerSideHero->setSecSkillLevel(SecondarySkill(wisdom), wisdomRank, ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({wisdomSkill, mysticismPerk});
		}
		if(selectConservation && wisdomRank >= 2 && !plannedManaConservation)
			attackerSideHero->applyPerkSelection({wisdomSkill, manaConservationPerk});

		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 1000, ChangeValueMode::ABSOLUTE);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		giveArtifact(defenderSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(SpellID::HASTE);
		defenderSideHero->addSpellToSpellbook(SpellID::HASTE);
		if(includeCounterspell)
			attackerSideHero->addSpellToSpellbook(counterspell());
		setTestSpellPointTotal(attackerSideHero, 1000);
		setTestSpellPointTotal(defenderSideHero, 1000);

		startBattle();
		friendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 100);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 100);
		ASSERT_NE(friendly, nullptr);
		ASSERT_NE(enemy, nullptr);
		beginCombat();
	}

	bool cast(SpellID spell, BattleSide side, const CStack * target)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = side;
		action.spell = spell;
		if(target)
			action.aimToUnit(target);
		else
			action.aimToHex(BattleHex::INVALID);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(side), action);
	}

	void activate(const CStack * stack)
	{
		BattleSetActiveStack action;
		action.battleID = BattleID(0);
		action.stack = stack->unitId();
		action.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(action);
	}

	std::shared_ptr<CBattleQuery> installBattleQuery()
	{
		auto query = std::make_shared<CBattleQuery>(gameHandler.get(), battle());
		gameHandler->queries->addQuery(query);
		return query;
	}

	void acceptBattleResult()
	{
		for(const auto player : {PlayerColor(0), PlayerColor(1)})
		{
			auto query = gameHandler->queries->topQuery(player);
			if(query && query->getType() == QueryType::BattleDialog)
				ASSERT_TRUE(gameHandler->queryReply(query->queryID, 0, player));
		}
	}

	void finishWithAttackerVictory()
	{
		const auto battleQuery = installBattleQuery();
		gameHandler->battles->cheatBattleVictory(PlayerColor(0));
		ASSERT_TRUE(battleQuery->result);
		acceptBattleResult();
	}

	CStack * friendly = nullptr;
	CStack * enemy = nullptr;
};
}

TEST_F(NewHorizonsManaConservationTest, AcceptedHeroCostsAreCountedButRejectedAndCreatureCastsAreNot)
{
	prepare();
	ASSERT_TRUE(attackerSideHero->hasActivePerk(wisdomSkill, manaConservationPerk));
	const auto manaBefore = attackerSideHero->getManaAvailable();

	EXPECT_FALSE(cast(SpellID::HASTE, BattleSide::ATTACKER, enemy));
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).acceptedHeroManaSpent, 0);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);

	ASSERT_TRUE(cast(SpellID::HASTE, BattleSide::ATTACKER, friendly));
	ASSERT_FALSE(server.casts.empty());
	const auto & accepted = server.casts.back().announcement;
	ASSERT_TRUE(accepted.castByHero);
	EXPECT_EQ(accepted.paidHeroManaCost, attackerSideHero->getSpellCost(SpellID(SpellID::HASTE).toSpell()));
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).acceptedHeroManaSpent, accepted.paidHeroManaCost);

	const auto recordedSpend = battle()->getSide(BattleSide::ATTACKER).acceptedHeroManaSpent;
	gameHandler->restoreSpellPoints(attackerSideHero->id, 5);
	gameHandler->grantBufferSpellPoints(attackerSideHero->id, 7);
	gameHandler->spendSpellPoints(attackerSideHero->id, 3);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).acceptedHeroManaSpent, recordedSpend)
		<< "separate Normal recovery, Buffer grants and drains do not alter gross accepted-spell costs";

	auto * creatureCaster = addStack(BattleSide::ATTACKER, creatureByName("core:imp"), BattleHex(4, 4), 1);
	ASSERT_NE(creatureCaster, nullptr);
	creatureCaster->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELLCASTER, BonusSource::OTHER, 3, BonusSourceID(),
		BonusSubtypeID(SpellID(SpellID::MAGIC_ARROW))));
	creatureCaster->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::CASTS, BonusSource::OTHER, 1, BonusSourceID()));
	creatureCaster->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::CREATURE_SPELL_POWER, BonusSource::OTHER, 20, BonusSourceID()));
	const auto * arrow = SpellID(SpellID::MAGIC_ARROW).toSpell();
	spells::BattleCast creatureCast(battle(), creatureCaster, spells::Mode::CREATURE_ACTIVE, arrow);
	const auto mechanics = arrow->battleMechanics(&creatureCast);
	ASSERT_NE(mechanics, nullptr);
	spells::Target target{spells::Destination(enemy)};
	ASSERT_TRUE(mechanics->canBeCastAt(target));
	const auto castsBeforeCreature = server.casts.size();
	creatureCast.cast(gameHandler->spellcastEnvironment(), target);

	ASSERT_GT(server.casts.size(), castsBeforeCreature);
	EXPECT_FALSE(server.casts.back().announcement.castByHero);
	EXPECT_EQ(server.casts.back().announcement.paidHeroManaCost, 0);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).acceptedHeroManaSpent, recordedSpend);
}

TEST_F(NewHorizonsManaConservationTest, AcceptedCounterspellWardPaymentIsAddedToTheWardingSideLedger)
{
	prepare(2, true, true);
	ASSERT_TRUE(cast(counterspell(), BattleSide::ATTACKER, nullptr));
	const auto counterspellCost = server.casts.back().announcement.paidHeroManaCost;
	EXPECT_GT(counterspellCost, 0);
	activate(enemy);
	ASSERT_TRUE(cast(SpellID::HASTE, BattleSide::DEFENDER, enemy));

	const auto & announcement = server.casts.back().announcement;
	EXPECT_TRUE(announcement.counterspellNegated);
	EXPECT_EQ(announcement.counterspellSide, BattleSide::ATTACKER);
	EXPECT_EQ(announcement.paidCounterspellManaCost,
		newHorizonsMagic::counterspellCost(defenderSideHero->getListedSpellCost(SpellID(SpellID::HASTE).toSpell()),
			false));
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).acceptedHeroManaSpent,
		counterspellCost + announcement.paidCounterspellManaCost);
	EXPECT_EQ(battle()->getSide(BattleSide::DEFENDER).acceptedHeroManaSpent,
		announcement.paidHeroManaCost);
}

TEST_F(NewHorizonsManaConservationTest, MalformedWardPaymentDoesNotMutateEitherSideLedger)
{
	prepare();
	BattleSpellCast forged;
	forged.battleID = BattleID(0);
	forged.side = BattleSide::ATTACKER;
	forged.spellID = SpellID::HASTE;
	forged.castByHero = true;
	forged.activeCast = true;
	forged.counterspellNegated = true;
	forged.counterspellSide = BattleSide::ATTACKER;
	forged.paidHeroManaCost = 7;
	forged.paidCounterspellManaCost = 4;

	EXPECT_THROW(gameHandler->sendAndApply(forged), std::runtime_error);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).acceptedHeroManaSpent, 0);
	EXPECT_EQ(battle()->getSide(BattleSide::DEFENDER).acceptedHeroManaSpent, 0);
}

TEST_F(NewHorizonsManaConservationTest, ResultRestoresAtMostTwentyNormalPointsAndPreservesBuffer)
{
	prepare();
	gameHandler->setManaPoints(attackerSideHero->id, 0);
	gameHandler->grantBufferSpellPoints(attackerSideHero->id, 13);
	auto & attackerSide = battle()->getSide(BattleSide::ATTACKER);
	attackerSide.acceptedHeroManaSpent = 500;
	finishWithAttackerVictory();

	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 20);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), 13);
}

TEST_F(NewHorizonsManaConservationTest, ResultRecoveryFloorsTwentyPercentOfGrossManaSpent)
{
	prepare();
	gameHandler->setManaPoints(attackerSideHero->id, 0);
	gameHandler->grantBufferSpellPoints(attackerSideHero->id, 13);
	battle()->getSide(BattleSide::ATTACKER).acceptedHeroManaSpent = 19;
	finishWithAttackerVictory();

	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 3);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), 13);
}

TEST_F(NewHorizonsManaConservationTest, ResultRestorationIsLimitedByNormalCapacity)
{
	prepare();
	const auto capacity = attackerSideHero->manaLimit();
	gameHandler->setManaPoints(attackerSideHero->id, capacity - 10);
	gameHandler->grantBufferSpellPoints(attackerSideHero->id, 13);
	battle()->getSide(BattleSide::ATTACKER).acceptedHeroManaSpent = 500;
	finishWithAttackerVictory();

	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), capacity);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), 13);
}

TEST_F(NewHorizonsManaConservationTest, MissingPerkDoesNotRestoreMana)
{
	prepare(2, false);
	EXPECT_FALSE(attackerSideHero->hasActivePerk(wisdomSkill, manaConservationPerk));
	gameHandler->setManaPoints(attackerSideHero->id, 0);
	gameHandler->grantBufferSpellPoints(attackerSideHero->id, 13);
	battle()->getSide(BattleSide::ATTACKER).acceptedHeroManaSpent = 500;
	finishWithAttackerVictory();
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 0);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), 13);
}

TEST_F(NewHorizonsManaConservationTest, PlannedPerkCannotBeSelectedOrRestoreMana)
{
	plannedManaConservation = true;
	prepare(2, false);
	EXPECT_THROW(attackerSideHero->applyPerkSelection({wisdomSkill, manaConservationPerk}), std::runtime_error);
	EXPECT_FALSE(attackerSideHero->hasActivePerk(wisdomSkill, manaConservationPerk));
	gameHandler->setManaPoints(attackerSideHero->id, 0);
	gameHandler->grantBufferSpellPoints(attackerSideHero->id, 13);
	battle()->getSide(BattleSide::ATTACKER).acceptedHeroManaSpent = 500;
	finishWithAttackerVictory();
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 0);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), 13);
}

TEST_F(NewHorizonsManaConservationTest, AdvancedPerkIsInactiveBelowAdvancedWisdom)
{
	prepare(1, false);
	EXPECT_THROW(attackerSideHero->applyPerkSelection({wisdomSkill, manaConservationPerk}), std::runtime_error);
	EXPECT_FALSE(attackerSideHero->hasActivePerk(wisdomSkill, manaConservationPerk));
	gameHandler->setManaPoints(attackerSideHero->id, 0);
	gameHandler->grantBufferSpellPoints(attackerSideHero->id, 13);
	battle()->getSide(BattleSide::ATTACKER).acceptedHeroManaSpent = 500;
	finishWithAttackerVictory();
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 0);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), 13);
}

TEST(NewHorizonsManaConservationStateTest, SideAndCastAnnouncementRoundTripAndRejectLossyDownsave)
{
	SideInBattle source(nullptr);
	source.acceptedHeroManaSpent = 321;
	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(current.oser & source);
	SideInBattle decoded(nullptr);
	ASSERT_NO_THROW(current.iser & decoded);
	EXPECT_EQ(decoded.acceptedHeroManaSpent, 321);

	CMemorySerializer oldSide;
	oldSide.oser.version = ESerializationVersion::NEW_HORIZONS_SPELL_POINTS;
	EXPECT_THROW(oldSide.oser & source, std::runtime_error);

	BattleSpellCast cast;
	cast.battleID = BattleID(0);
	cast.side = BattleSide::ATTACKER;
	cast.spellID = SpellID::HASTE;
	cast.castByHero = true;
	cast.activeCast = true;
	cast.paidHeroManaCost = 7;
	CMemorySerializer packet;
	packet.oser.version = ESerializationVersion::CURRENT;
	packet.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(packet.oser & cast);
	BattleSpellCast decodedCast;
	ASSERT_NO_THROW(packet.iser & decodedCast);
	EXPECT_EQ(decodedCast.paidHeroManaCost, 7);

	CMemorySerializer oldPacket;
	oldPacket.oser.version = ESerializationVersion::NEW_HORIZONS_SPELL_POINTS;
	EXPECT_THROW(oldPacket.oser & cast, std::runtime_error);
}
