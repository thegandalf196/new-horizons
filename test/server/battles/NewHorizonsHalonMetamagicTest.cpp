/*
 * NewHorizonsHalonMetamagicTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/spells/CSpellHandler.h"
#include "HeroCommandFixture.h"
#include "BattleStartSnapshotFixture.h"
#include "../../mock/TinyH3MBuilder.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"

namespace
{
constexpr auto META = "new-horizons:metamagic";
class HalonEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit HalonEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
struct HalonPrefixProbe
{
	using Version = ESerializationVersion;
	bool saving = true;
	bool loadingGamestate = false;
	int fields = 0;
	bool hasFeature(Version feature) const { return feature != Version::NEW_HORIZONS_METAMAGIC_CAPACITY; }
	template <typename T> HalonPrefixProbe & operator&(T &)
	{
		++fields;
		throw std::runtime_error("Reached ordinary payload");
	}
};
template <typename T> void oldWriterRejectsBeforePrefix(T & value)
{
	HalonPrefixProbe probe;
	EXPECT_THROW(value.serialize(probe), std::runtime_error);
	EXPECT_EQ(probe.fields, 0);
}
}

class NewHorizonsHalonMetamagicTest : public HeroCommandFixture
{
protected:
	bool legacyMagic = false;
	CStack * attacker = nullptr;
	CStack * defender = nullptr;
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons profile";
	}
	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			legacyMagic ? JsonNode() : JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}
	void prepare(int rank, bool halon = true, bool withGrand = false)
	{
		const CreatureID pikeman(CreatureID::decode("core:pikeman"));
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("HalonMetamagicCapacity")
			.playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5,5,0}, HeroTypeID(HeroTypeID::decode(halon ? "core:halon" : "core:orrin")), PlayerColor(0))
			.heroExperience(0).heroGarrison({{pikeman,1}})
			.hero({7,7,0}, HeroTypeID(HeroTypeID::decode("core:solmyr")), PlayerColor(1))
			.heroExperience(0).heroGarrison({{pikeman,1}});
		startWithMap(std::move(builder));
		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		gameHandler->randomizer->setSeed(seed);
		attackerSideHero = findHeroAt({5,5,0});
		defenderSideHero = findHeroAt({7,7,0});
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_NE(defenderSideHero, nullptr);
		if(halon)
		{
			EXPECT_EQ(attackerSideHero->getPerkSkillRank(META), 1);
			EXPECT_EQ(attackerSideHero->getPerkSkillRank("new-horizons:spellcraft"), 1);
		}
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(META)), rank, ChangeValueMode::ABSOLUTE);
		if(withGrand)
			for(const auto perk : {"new-horizons:metamagic.arcaneAcquisition", "new-horizons:metamagic.echoedDuration",
				"new-horizons:metamagic.grandMetamagic"})
				select(perk);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 1000, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 10, ChangeValueMode::ABSOLUTE);
		if(!attackerSideHero->hasSpellbook())
			giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		for(const auto spell : {SpellID::HASTE, SpellID::SLOW, SpellID::BLESS})
			attackerSideHero->addSpellToSpellbook(SpellID(spell));
		setTestSpellPointTotal(attackerSideHero, 1000);
	}
	void select(const char * perk)
	{
		const auto rankLookup = [this](const std::string & skill) { return attackerSideHero->getPerkSkillRank(skill); };
		for(uint64_t seed = 0; seed < 10000; ++seed)
		{
			const auto offers = attackerSideHero->getPerkState().prepareOffer(rankLookup, seed);
			for(size_t index = 0; index < offers.size(); ++index)
				if(offers[index].selection.perkId == perk)
				{
					gameHandler->levelUpHero(attackerSideHero, offers, index, seed, false);
					ASSERT_TRUE(attackerSideHero->hasActivePerk(META, perk));
					return;
				}
		}
		FAIL() << "No legal public perk offer";
	}
	void combat()
	{
		startBattle();
		attacker = addStack(BattleSide::ATTACKER, CreatureID(CreatureID::decode("core:pikeman")), BattleHex(leftHex), 1000);
		defender = addStack(BattleSide::DEFENDER, CreatureID(CreatureID::decode("core:pikeman")), BattleHex(rightHex), 1000);
		beginCombat();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			if(unit != attacker && unit != defender)
				remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		activate();
	}
	void activate()
	{
		BattleSetActiveStack active;
		active.battleID = BattleID(0);
		active.stack = attacker->unitId();
		active.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(active);
	}
	bool cast(SpellID spell, const CStack * target, bool followup = false)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = spell;
		action.metamagicFollowup = followup;
		action.aimToUnit(target);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}
	void advanceUse(uint8_t used, bool grand = false)
	{
		if(used)
		{
			advanceRound();
			activate();
		}
		ASSERT_TRUE(cast(SpellID::HASTE, attacker));
		ASSERT_TRUE(cast(SpellID::SLOW, defender, true));
		EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicUsesConsumed, used + 1);
		if(grand && used == 2)
			ASSERT_TRUE(cast(SpellID::BLESS, attacker, true));
	}
};

TEST_F(NewHorizonsHalonMetamagicTest, ActualHalonMasteryRemainsLearnedWhileCapacitiesAreTwoThreeFour)
{
	ASSERT_NO_FATAL_FAILURE(prepare(1));
	for(int rank = 1; rank <= 3; ++rank)
	{
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(META)), rank, ChangeValueMode::ABSOLUTE);
		EXPECT_EQ(newHorizonsMagic::metamagicRank(attackerSideHero), rank);
		EXPECT_EQ(newHorizonsMagic::metamagicCapacity(attackerSideHero), rank + 1);
		EXPECT_EQ(attackerSideHero->valOfBonuses(BonusType::METAMAGIC_USES_PER_COMBAT), rank + 1);
	}
}

TEST_F(NewHorizonsHalonMetamagicTest, OrdinaryLearnedCapacitiesRemainOneTwoThree)
{
	ASSERT_NO_FATAL_FAILURE(prepare(1, false));
	for(int rank = 1; rank <= 3; ++rank)
	{
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(META)), rank, ChangeValueMode::ABSOLUTE);
		EXPECT_EQ(newHorizonsMagic::metamagicRank(attackerSideHero), rank);
		EXPECT_EQ(newHorizonsMagic::metamagicCapacity(attackerSideHero), rank);
	}
}

TEST_F(NewHorizonsHalonMetamagicTest, CurrentBonusOnlyAndSurplusBasicCannotGrantUnlearnedOrFourthUse)
{
	ASSERT_NO_FATAL_FAILURE(prepare(0));
	EXPECT_GT(attackerSideHero->valOfBonuses(BonusType::METAMAGIC_USES_PER_COMBAT), 0);
	EXPECT_EQ(newHorizonsMagic::metamagicRank(attackerSideHero), 0);
	EXPECT_EQ(newHorizonsMagic::metamagicCapacity(attackerSideHero), 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(META)), 1, ChangeValueMode::ABSOLUTE);
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::METAMAGIC_USES_PER_COMBAT, BonusSource::OTHER, 100, BonusSourceID()));
	EXPECT_EQ(newHorizonsMagic::metamagicRank(attackerSideHero), 1);
	EXPECT_EQ(newHorizonsMagic::metamagicCapacity(attackerSideHero), 2);
}

TEST_F(NewHorizonsHalonMetamagicTest, LegacyBonusOnlyProfileRetainsPriorEffectiveRankAndCapacity)
{
	legacyMagic = true;
	ASSERT_NO_FATAL_FAILURE(prepare(0));
	EXPECT_EQ(newHorizonsMagic::metamagicRank(attackerSideHero), 1);
	EXPECT_EQ(newHorizonsMagic::metamagicCapacity(attackerSideHero), 1);
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::METAMAGIC_USES_PER_COMBAT, BonusSource::OTHER, 100, BonusSourceID()));
	EXPECT_EQ(newHorizonsMagic::metamagicRank(attackerSideHero), 3);
	EXPECT_EQ(newHorizonsMagic::metamagicCapacity(attackerSideHero), 3);
}

TEST_F(NewHorizonsHalonMetamagicTest, ActualFourthAcceptedFollowupChargesOnceAndFifthBaseDoesNotReserve)
{
	ASSERT_NO_FATAL_FAILURE(prepare(3));
	ASSERT_NO_FATAL_FAILURE(combat());
	for(uint8_t used = 0; used < 4; ++used)
		ASSERT_NO_FATAL_FAILURE(advanceUse(used));
	auto & side = battle()->getSide(BattleSide::ATTACKER);
	EXPECT_EQ(side.metamagicUsesConsumed, 4);
	EXPECT_FALSE(side.metamagicGrandUsed);
	advanceRound();
	activate();
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	EXPECT_EQ(side.metamagicPendingCount, 0);
	EXPECT_FALSE(cast(SpellID::SLOW, defender, true));
	EXPECT_EQ(side.metamagicUsesConsumed, 4);
}

TEST_F(NewHorizonsHalonMetamagicTest, ExpertGrandStillActivatesThirdUseOnlyAndFourthHasNoExtraContinuation)
{
	ASSERT_NO_FATAL_FAILURE(prepare(3, true, true));
	ASSERT_NO_FATAL_FAILURE(combat());
	for(uint8_t used = 0; used < 4; ++used)
	{
		ASSERT_NO_FATAL_FAILURE(advanceUse(used, true));
		const auto & side = battle()->getSide(BattleSide::ATTACKER);
		EXPECT_EQ(side.metamagicGrandUsed, used >= 2);
		EXPECT_EQ(side.metamagicPendingCount, 0);
	}
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicUsesConsumed, 4);
}

TEST_F(NewHorizonsHalonMetamagicTest, BasicSpecialtyNeverDerivesGrandFromAdditionalCapacity)
{
	ASSERT_NO_FATAL_FAILURE(prepare(1));
	EXPECT_FALSE(HeroSpellAllowanceTransition::activatesGrand(true, 1, 1, 2,
		newHorizonsMagic::metamagicRank(attackerSideHero), true, false));
	ASSERT_NO_FATAL_FAILURE(combat());
	for(uint8_t used = 0; used < 2; ++used)
		ASSERT_NO_FATAL_FAILURE(advanceUse(used));
	EXPECT_FALSE(battle()->getSide(BattleSide::ATTACKER).metamagicGrandUsed);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicUsesConsumed, 2);
}

TEST_F(NewHorizonsHalonMetamagicTest, DetachedFourthAllowanceMatchesLiveAndKeepsParentSiblingUnchanged)
{
	ASSERT_NO_FATAL_FAILURE(prepare(3));
	ASSERT_NO_FATAL_FAILURE(combat());
	for(uint8_t used = 0; used < 3; ++used)
		ASSERT_NO_FATAL_FAILURE(advanceUse(used));
	advanceRound();
	activate();
	HalonEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	auto child = std::make_shared<HypotheticBattle>(&environment, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	ASSERT_TRUE(child->projectHeroSpellAllowance(BattleSide::ATTACKER, SpellID::HASTE, attacker->unitId(), false, false));
	ASSERT_TRUE(child->projectHeroSpellAllowance(BattleSide::ATTACKER, SpellID::SLOW, defender->unitId(), true, false));
	EXPECT_EQ(child->battleMetamagicUsesConsumed(BattleSide::ATTACKER), 4);
	EXPECT_EQ(parent->battleMetamagicUsesConsumed(BattleSide::ATTACKER), 3);
	EXPECT_EQ(sibling->battleMetamagicUsesConsumed(BattleSide::ATTACKER), 3);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicUsesConsumed, 3);
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	ASSERT_TRUE(cast(SpellID::SLOW, defender, true));
	EXPECT_EQ(child->battleMetamagicUsesConsumed(BattleSide::ATTACKER),
		battle()->getSide(BattleSide::ATTACKER).metamagicUsesConsumed);
}

TEST_F(NewHorizonsHalonMetamagicTest, CurrentFourthStateRoundTripsAndOlderSideBattleAndOuterStartRejectBeforePrefix)
{
	ASSERT_NO_FATAL_FAILURE(prepare(3));
	ASSERT_NO_FATAL_FAILURE(combat());
	for(uint8_t used = 0; used < 3; ++used)
		ASSERT_NO_FATAL_FAILURE(advanceUse(used));
	advanceRound();
	activate();
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	auto & side = battle()->getSide(BattleSide::ATTACKER);
	EXPECT_EQ(side.metamagicPendingCount, 1);
	oldWriterRejectsBeforePrefix(side);
	oldWriterRejectsBeforePrefix(*battle());
	BattleStart outgoing;
	outgoing.info = battleStartFixture::snapshot(*battle(), gameState().get());
	oldWriterRejectsBeforePrefix(outgoing);
	ASSERT_TRUE(cast(SpellID::SLOW, defender, true));
	oldWriterRejectsBeforePrefix(side);
	auto restored = battleStartFixture::snapshot(*battle(), gameState().get());
	EXPECT_EQ(restored->getSide(BattleSide::ATTACKER).metamagicUsesConsumed, 4);
	EXPECT_EQ(side.metamagicUsesConsumed, 4);
	side.metamagicUsesConsumed = 5;
	CMemorySerializer current;
	EXPECT_THROW(side.serialize(current.oser), std::runtime_error);
	EXPECT_TRUE(current.extractBuffer().empty());
	side.metamagicUsesConsumed = 4;
}

TEST_F(NewHorizonsHalonMetamagicTest, OrdinaryFourthCounterAndMalformedFifthRejectBeforeRuntimeMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare(3, false));
	ASSERT_NO_FATAL_FAILURE(combat());
	auto & side = battle()->getSide(BattleSide::ATTACKER);
	const auto allowances = side.heroActionAllowances;
	side.metamagicUsesConsumed = 4;
	CMemorySerializer wire;
	EXPECT_THROW(battle()->serialize(wire.oser), std::runtime_error);
	EXPECT_TRUE(wire.extractBuffer().empty());
	BattleSpellCast forged;
	forged.battleID = BattleID(0);
	forged.castByHero = true;
	forged.side = BattleSide::ATTACKER;
	forged.spellID = SpellID::HASTE;
	EXPECT_THROW(gameHandler->sendAndApply(forged), std::runtime_error);
	EXPECT_EQ(side.heroActionAllowances, allowances);
	EXPECT_EQ(side.metamagicUsesConsumed, 4);
	side.metamagicUsesConsumed = 0;
}
