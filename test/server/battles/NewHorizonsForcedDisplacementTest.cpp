/*
 * NewHorizonsForcedDisplacementTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/entities/hero/CHeroHandler.h"
#include "../../../lib/entities/hero/CHeroClass.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/IGameSettings.h"
#include "HeroCommandFixture.h"
#include "../../../lib/battle/BattleDisplacementCause.h"
#include "../../../lib/battle/CUnitState.h"
#include "../../../lib/bonuses/BonusSelector.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/gameState/GameStatePackVisitor.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#ifdef ENABLE_BATTLE_AI
#include "../../../AI/BattleAI/StackWithBonuses.h"
#endif

namespace
{
constexpr auto physical = BattleDisplacementCause::NON_MAGICAL;
constexpr auto magical = BattleDisplacementCause::MAGICAL;
constexpr auto armorer = "new-horizons:armorer";
constexpr auto unyielding = "new-horizons:armorer.unyielding";
constexpr auto bulwark = "new-horizons:bulwarkOfTheMire";
constexpr auto deep = "new-horizons:bulwarkOfTheMire.deepBulwark";

BattleStackMoved movePacket(const battle::Unit * unit, BattleHex endpoint, BattleDisplacementCause cause)
{
	BattleStackMoved move;
	move.battleID = BattleID(0);
	move.stack = unit->unitId();
	move.tilesToMove.insert(endpoint);
	move.displacementCause = cause;
	return move;
}

class NewHorizonsForcedDisplacementTest : public HeroCommandFixture
{
protected:
	CStack * target = nullptr;

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	HeroTypeID fortressHero() const
	{
		for(const auto & id : LIBRARY->heroh->getDefaultAllowed())
		{
			const auto * hero = dynamic_cast<const CHero *>(id.toHeroType());
			if(hero && hero->heroClass && hero->heroClass->faction == FactionID::FORTRESS)
				return id;
		}
		throw std::runtime_error("No installed Fortress hero");
	}

	void select(CGHeroInstance * hero, const std::string & skill, const std::string & perk)
	{
		bool registered = false;
		const JsonNode registry(JsonPath::builtin("config/newHorizonsPerks"));
		for(const auto & entry : registry["skills"][skill]["perks"].Vector())
			if(entry["id"].String() == perk)
			{
				EXPECT_EQ(entry["effect"]["status"].String(), "active");
				registered = true;
			}
		ASSERT_TRUE(registered);
		const auto rank = [hero](const std::string & id) { return hero->getPerkSkillRank(id); };
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = hero->getPerkState().prepareOffer(rank, seed);
			for(size_t index = 0; index < offers.size(); ++index)
				if(offers[index].selection.skillId == skill && offers[index].selection.perkId == perk)
				{
					gameHandler->levelUpHero(hero, offers, index, seed, false);
					ASSERT_TRUE(hero->hasActivePerk(skill, perk));
					return;
				}
		}
		FAIL() << "Required active perk was not legally offered";
	}

	void prepare(bool selectPerk = false, bool useDeep = false, bool ownAttacker = false,
		const std::string & creature = "core:pikeman")
	{
		ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		const CreatureID token(0);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, fortressHero(), PlayerColor(0)).heroGarrison({{token, 1}})
			.hero({7, 7, 0}, fortressHero(), PlayerColor(1)).heroGarrison({{token, 1}});
		startWithMap(std::move(builder));
		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		gameHandler->randomizer->setSeed(seed);
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		defenderSideHero = findHeroByOwner(PlayerColor(1));
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_NE(defenderSideHero, nullptr);
		auto * hero = ownAttacker ? attackerSideHero : defenderSideHero;
		const std::string skill = useDeep ? bulwark : armorer;
		hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(skill)), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		if(selectPerk)
		{
			select(hero, skill, useDeep ? "new-horizons:bulwarkOfTheMire.mireborn" : "new-horizons:armorer.shieldMaster");
			hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(skill)), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
			select(hero, skill, useDeep ? deep : unyielding);
		}
		startBattle(); // Fixture setup publishes BattleStart; round-dependent cases also begin combat.
		target = addStack(ownAttacker ? BattleSide::ATTACKER : BattleSide::DEFENDER,
			creatureByName(creature), BattleHex(10, 5), 100);
		ASSERT_NE(target, nullptr);
	}

	void defend()
	{
		battle()->activeStack = target->unitId();
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->battleGetOwner(target), BattleAction::makeDefend(target)));
		ASSERT_TRUE(target->defended());
	}

	bool displace(BattleHex endpoint, BattleDisplacementCause cause = physical)
	{
		return gameHandler->battles->tryForcedDisplacement(BattleID(0), target->unitId(), endpoint, cause);
	}

	std::vector<std::byte> randomState()
	{
		CMemorySerializer memory;
		gameHandler->randomizer->serialize(memory.oser);
		return memory.extractBuffer();
	}
};
}

TEST_F(NewHorizonsForcedDisplacementTest, UnprotectedAuthoritativeMoveHasNoRandomDraw)
{
	prepare();
	const auto health = target->getAvailableHealth();
	const auto random = randomState();
	ASSERT_TRUE(displace(BattleHex(11, 5)));
	EXPECT_EQ(target->getPosition(), BattleHex(11, 5));
	EXPECT_EQ(target->getAvailableHealth(), health);
	EXPECT_EQ(randomState(), random);
}

TEST_F(NewHorizonsForcedDisplacementTest, UnyieldingAcceptedDefendRejectsBeforeAnyMutation)
{
	prepare(true);
	defend();
	const JsonNode before = target->acquireState()->save();
	const auto random = randomState();
	EXPECT_TRUE(battle()->battleIsForcedDisplacementImmune(target, physical));
	EXPECT_FALSE(displace(BattleHex(11, 5)));
	EXPECT_EQ(target->acquireState()->save(), before);
	EXPECT_EQ(randomState(), random);
}

TEST_F(NewHorizonsForcedDisplacementTest, DeepBulwarkNeedsActualDefendingBenefit)
{
	prepare(true, true);
	EXPECT_FALSE(battle()->battleIsForcedDisplacementImmune(target, physical));
	defend();
	EXPECT_TRUE(battle()->battleIsForcedDisplacementImmune(target, physical));
	EXPECT_FALSE(displace(BattleHex(11, 5)));
	EXPECT_TRUE(target->defended());
}

TEST_F(NewHorizonsForcedDisplacementTest, RejectionPreservesEntangleWhileAcceptedMagicalMoveClearsOnlyItsMarker)
{
	prepare(true);
	defend();
	const SpellID entangle(SpellID::decode("new-horizons:entangle"));
	ASSERT_TRUE(entangle.hasValue());
	auto root = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::BIND_EFFECT, BonusSource::SPELL_EFFECT, 1, BonusSourceID(entangle));
	auto unrelated = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::BIND_EFFECT, BonusSource::OTHER, 1, BonusSourceID());
	target->addNewBonus(root);
	target->addNewBonus(unrelated);
	const JsonNode before = target->acquireState()->save();
	ASSERT_FALSE(displace(BattleHex(11, 5)));
	EXPECT_EQ(target->acquireState()->save(), before);
	EXPECT_TRUE(target->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(entangle))));
	ASSERT_TRUE(displace(BattleHex(11, 5), magical));
	EXPECT_FALSE(target->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(entangle))));
	EXPECT_TRUE(target->hasBonusOfType(BonusType::BIND_EFFECT)) << "Unrelated root survives";
}

TEST_F(NewHorizonsForcedDisplacementTest, UnselectedDefendDoesNotInventProtection)
{
	prepare();
	defend();
	EXPECT_FALSE(battle()->battleIsForcedDisplacementImmune(target, physical));
	EXPECT_TRUE(displace(BattleHex(11, 5)));
}

TEST_F(NewHorizonsForcedDisplacementTest, EffectiveHoldProtectsUntilItsActualAnchorBreaks)
{
	prepare(true, false, true);
	beginCombat();
	ASSERT_GE(battle()->battleGetRound(), 1);
	battle()->activeStack = target->unitId();
	ASSERT_TRUE(issue(HeroCommand::HOLD_THE_LINE));
	auto * hold = battle()->getSide(BattleSide::ATTACKER).findOrder(HeroCommand::HOLD_THE_LINE);
	ASSERT_NE(hold, nullptr);
	ASSERT_TRUE(battle()->battleIsHoldTheLineRecipient(*hold, target));
	EXPECT_FALSE(displace(BattleHex(11, 5)));
	hold->holdBrokenUnitIds.push_back(target->unitId());
	EXPECT_FALSE(battle()->battleIsForcedDisplacementImmune(target, physical));
	EXPECT_TRUE(displace(BattleHex(11, 5)));
}

TEST_F(NewHorizonsForcedDisplacementTest, CurrentControllerRatherThanOriginalArmyOwnsProtection)
{
	prepare(true);
	defend();
	auto hypnosis = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID());
	target->addNewBonus(hypnosis);
	ASSERT_EQ(battle()->battleGetOwnerHero(target), attackerSideHero);
	EXPECT_FALSE(battle()->battleIsForcedDisplacementImmune(target, physical));
	target->removeBonus(hypnosis);
	ASSERT_EQ(battle()->battleGetOwnerHero(target), defenderSideHero);
	EXPECT_TRUE(battle()->battleIsForcedDisplacementImmune(target, physical));
}

TEST_F(NewHorizonsForcedDisplacementTest, ExplicitMagicalCauseAndOrdinaryMovementIgnorePhysicalImmunity)
{
	prepare(true);
	defend();
	EXPECT_FALSE(battle()->battleIsForcedDisplacementImmune(target, magical));
	ASSERT_TRUE(displace(BattleHex(11, 5), magical));
	auto ordinary = movePacket(target, BattleHex(12, 5), BattleDisplacementCause::NONE);
	ordinary.teleporting = false;
	ASSERT_NO_THROW(gameHandler->sendAndApply(ordinary));
	EXPECT_EQ(target->getPosition(), BattleHex(12, 5));
}

TEST_F(NewHorizonsForcedDisplacementTest, FullDoubleWideFootprintAndNoopRejectBeforePublication)
{
	prepare(false, false, false, "core:phoenix");
	ASSERT_TRUE(target->doubleWide());
	// Defender Phoenix tail is anchor+1. The anchor itself remains free.
	ASSERT_NE(addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(13, 5), 1), nullptr);
	const JsonNode before = target->acquireState()->save();
	EXPECT_FALSE(displace(BattleHex(12, 5)));
	EXPECT_FALSE(displace(target->getPosition()));
	EXPECT_EQ(target->acquireState()->save(), before);
	auto invalid = movePacket(target, BattleHex(12, 5), magical);
	EXPECT_THROW(gameHandler->sendAndApply(invalid), std::runtime_error);
	EXPECT_EQ(target->acquireState()->save(), before);
}

TEST(NewHorizonsForcedDisplacementProtocolTest, CurrentCauseRoundTripsAndLegacyDefaultsReset)
{
	BattleStackMoved original;
	original.battleID = BattleID(0);
	original.stack = 4;
	original.tilesToMove.insert(BattleHex(80));
	original.displacementCause = physical;
	CMemorySerializer current;
	ASSERT_NO_THROW(original.serialize(current.oser));
	BattleStackMoved decoded;
	ASSERT_NO_THROW(decoded.serialize(current.iser));
	EXPECT_EQ(decoded.displacementCause, physical);
	EXPECT_EQ(decoded.tilesToMove, original.tilesToMove);

	original.displacementCause = BattleDisplacementCause::NONE;
	CMemorySerializer legacy;
	legacy.oser.version = legacy.iser.version = static_cast<ESerializationVersion>(
		static_cast<int>(ESerializationVersion::NEW_HORIZONS_FORCED_DISPLACEMENT) - 1);
	ASSERT_NO_THROW(original.serialize(legacy.oser));
	ASSERT_NO_THROW(decoded.serialize(legacy.iser));
	EXPECT_EQ(decoded.displacementCause, BattleDisplacementCause::NONE);
}

TEST(NewHorizonsForcedDisplacementProtocolTest, NondefaultAndMalformedCauseRejectBeforePrefix)
{
	BattleStackMoved packet;
	packet.battleID = BattleID(0);
	packet.stack = 4;
	packet.tilesToMove.insert(BattleHex(80));
	packet.displacementCause = physical;
	CMemorySerializer old;
	old.oser.version = static_cast<ESerializationVersion>(
		static_cast<int>(ESerializationVersion::NEW_HORIZONS_FORCED_DISPLACEMENT) - 1);
	EXPECT_THROW(packet.serialize(old.oser), std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
	for(const auto malformed : {static_cast<BattleDisplacementCause>(3), static_cast<BattleDisplacementCause>(255)})
	{
		packet.displacementCause = malformed;
		CMemorySerializer current;
		EXPECT_THROW(packet.serialize(current.oser), std::runtime_error);
		EXPECT_TRUE(current.extractBuffer().empty());
	}
}

TEST(NewHorizonsForcedDisplacementProtocolTest, CurrentRawUnknownCauseIsRejectedOnRead)
{
	BattleID id(0);
	uint32_t unit = 4;
	BattleHexArray tiles;
	tiles.insert(BattleHex(80));
	int distance = 0;
	bool teleporting = true;
	auto malformed = static_cast<BattleDisplacementCause>(3);
	CMemorySerializer memory;
	// Raw fields deliberately bypass writer admission to exercise decoder admission.
	memory.oser & id & unit & tiles & distance & teleporting & malformed;
	BattleStackMoved restored;
	EXPECT_THROW(restored.serialize(memory.iser), std::runtime_error);
}

#ifdef ENABLE_BATTLE_AI
TEST_F(NewHorizonsForcedDisplacementTest, DetachedSharedPacketAdmissionAndSiblingIsolation)
{
	prepare(true);
	defend();
	class ProjectionEnvironment final : public Environment
	{
		std::shared_ptr<CGameState> state;
	public:
		explicit ProjectionEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
		const Services * services() const override { return LIBRARY; }
		const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
		const GameCb * game() const override { return state.get(); }
	} environment(gameState());
	std::shared_ptr<CBattleInfoCallback> callback(gameState(), battle());
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	auto child = std::make_shared<HypotheticBattle>(&environment, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	auto rejected = movePacket(target, BattleHex(11, 5), physical);
	EXPECT_THROW(child->getServerCallback()->apply(rejected), std::runtime_error);
	EXPECT_EQ(child->battleGetUnitByID(target->unitId())->getPosition(), target->getPosition());
	auto allowed = movePacket(target, BattleHex(11, 5), magical);
	ASSERT_NO_THROW(child->getServerCallback()->apply(allowed));
	EXPECT_EQ(child->battleGetUnitByID(target->unitId())->getPosition(), BattleHex(11, 5));
	EXPECT_EQ(parent->battleGetUnitByID(target->unitId())->getPosition(), BattleHex(10, 5));
	EXPECT_EQ(sibling->battleGetUnitByID(target->unitId())->getPosition(), BattleHex(10, 5));
	EXPECT_EQ(target->getPosition(), BattleHex(10, 5));
}
#endif
