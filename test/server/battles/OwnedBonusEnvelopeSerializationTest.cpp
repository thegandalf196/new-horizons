/*
 * OwnedBonusEnvelopeSerializationTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "BattleStartSnapshotFixture.h"
#include "FullGameSnapshotTypes.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/mapObjects/CRewardableObject.h"
#include "../../../lib/mapObjectConstructors/AObjectTypeHandler.h"
#include "../../../lib/entities/artifact/CArtifactInstance.h"
#include "../../../lib/mapObjects/army/CCommanderInstance.h"

namespace
{
constexpr auto previous = static_cast<ESerializationVersion>(1144);
std::shared_ptr<Bonus> marker()
{
	return std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::TARGET_MAGICAL_DAMAGE_REDUCTION_PENETRATION, BonusSource::OTHER, 20, BonusSourceID());
}
template<typename T> void expectOldWritable(T & value)
{
	CMemorySerializer wire;
	wire.oser.version = previous;
	ASSERT_NO_THROW(wire.oser & value);
	EXPECT_FALSE(wire.extractBuffer().empty());
}
template<typename T> void expectOldPrefixRejected(T & value)
{
	CMemorySerializer wire;
	wire.oser.version = previous;
	EXPECT_THROW(wire.oser & value, std::runtime_error);
	EXPECT_TRUE(wire.extractBuffer().empty());
}

class OwnedBonusEnvelopeTest : public BattleTestFixture
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		BattleTestFixture::mapLoaded(loaded);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		// Keep real first-six/core captures, but remove the unrelated1145 policy.
		rules["additionalSpecialties"].Struct().erase("ordinaryCreatureSpecialties");
		rules.setOverrideFlag(true);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
	}
};

TEST_F(OwnedBonusEnvelopeTest, ArmyStackSetAndArmedEnvelopeRejectBeforeTheirPrefixes)
{
	startGame();
	auto & army = static_cast<CArmedInstance &>(*attackerSideHero);
	auto & set = static_cast<CCreatureSet &>(army);
	auto * stack = army.getStackPtr(SlotID(0));
	ASSERT_NE(stack, nullptr);
	expectOldWritable(*stack);
	expectOldWritable(set);
	expectOldWritable(army);
	stack->addNewBonus(marker());
	CMemorySerializer current;
	ASSERT_NO_THROW(current.oser & set);
	current.iser.cb = gameState().get();
	CCreatureSet restored;
	ASSERT_NO_THROW(current.iser & restored);
	ASSERT_NE(restored.getStackPtr(SlotID(0)), nullptr);
	EXPECT_EQ(restored.getStackPtr(SlotID(0))->getExportedBonusList().back()->type, marker()->type);
	expectOldPrefixRejected(*stack);
	expectOldPrefixRejected(set);
	expectOldPrefixRejected(army);
}

TEST_F(OwnedBonusEnvelopeTest, DetachedBattleStackRejectsBeforeNodePayload)
{
	startGame();
	CStackBasicDescriptor base(CreatureID(0), 1);
	CStack source(&base, PlayerColor(0), 0, BattleSide::ATTACKER, SlotID(0));
	expectOldWritable(source);
	source.addNewBonus(marker());
	CMemorySerializer current;
	ASSERT_NO_THROW(current.oser & source);
	current.iser.cb = gameState().get();
	CStack restored;
	ASSERT_NO_THROW(current.iser & restored);
	EXPECT_EQ(restored.getExportedBonusList().back()->type, marker()->type);
	expectOldPrefixRejected(source);
}

TEST_F(OwnedBonusEnvelopeTest, RuntimeBattleNodeAndRetainedDeadStackProtectBattleAndStart)
{
	startGame();
	startBattle();
	// Descriptors preserve real captured rules without serializing attached live units.
	CMemorySerializer clean;
	clean.oser.version = previous;
	ASSERT_NO_THROW(battleStartFixture::writeDescriptors(*battle(), clean));
	for(const bool stackMarker : {false, true})
	{
		if(stackMarker)
		{
			ASSERT_FALSE(battle()->stacks.empty());
			auto & victim = battle()->stacks.back();
			int64_t damage = victim->health.available();
			victim->health.damage(damage);
			ASSERT_FALSE(victim->alive());
			victim->addNewBonus(marker());
			CMemorySerializer old;
			old.oser.version = previous;
			EXPECT_THROW(battleStartFixture::writeDescriptors(*battle(), old), std::runtime_error);
			EXPECT_TRUE(old.extractBuffer().empty());
		}
		auto source = battleStartFixture::snapshot(*battle(), gameState().get());
		ASSERT_NE(source, nullptr);
		if(!stackMarker) source->addNewBonus(marker());
		expectOldPrefixRejected(*source);
		BattleStart packet;
		packet.battleID = source->getBattleID();
		packet.info = std::move(source);
		CMemorySerializer current;
		ASSERT_NO_THROW(current.oser & packet);
		current.iser.cb = gameState().get();
		BattleStart restored;
		ASSERT_NO_THROW(current.iser & restored);
		ASSERT_TRUE(restored.info);
		EXPECT_THROW(restored.info->validateOwnedBonusSerialization(BonusSerializationContext{false, true}), std::runtime_error);
		expectOldPrefixRejected(packet);
	}
}

enum class WorldOwner { HERO, SLOT, COMMANDER, PLAYER, PLAYER_BATTLE, TEAM, GLOBAL, ARTIFACT, POOL_HERO, TOWN, TOWN_VIS };
class OwnedWorldEnvelopeTest : public OwnedBonusEnvelopeTest, public ::testing::WithParamInterface<WorldOwner>
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		OwnedBonusEnvelopeTest::mapLoaded(loaded);
		if(GetParam() == WorldOwner::COMMANDER)
		{
			JsonNode enabled;
			enabled.Bool() = true;
			loaded->overrideGameSetting(EGameSettings::MODULE_COMMANDERS, enabled);
		}
	}
};

TEST_P(OwnedWorldEnvelopeTest, RealWorldAndMapRejectBeforeOuterPayloadAndCurrentRoundtripSurvives)
{
	startGame(true);
	CBonusSystemNode * target = nullptr;
	const auto owner = GetParam();
	HeroTypeID pooledHeroType;
	switch(owner)
	{
	case WorldOwner::HERO: target = attackerSideHero; break;
	case WorldOwner::SLOT: target = attackerSideHero->getStackPtr(SlotID(0)); break;
	case WorldOwner::COMMANDER:
		target = attackerSideHero->getCommander(); break;
	case WorldOwner::PLAYER: target = &gameState()->players.at(PlayerColor(0)); break;
	case WorldOwner::PLAYER_BATTLE: break;
	case WorldOwner::TEAM: target = &gameState()->teams.begin()->second; break;
	case WorldOwner::GLOBAL: target = &gameState()->globalEffects; break;
	case WorldOwner::ARTIFACT:
		target = gameState()->createArtifact(ArtifactID(0), SpellID::NONE); break;
	case WorldOwner::POOL_HERO:
	{
		const auto heroes = map()->getHeroesInPool();
		ASSERT_FALSE(heroes.empty());
		pooledHeroType = heroes.front();
		target = map()->tryGetFromHeroPool(pooledHeroType);
		break;
	}
	case WorldOwner::TOWN: target = findFirst<CGTownInstance>(); break;
	case WorldOwner::TOWN_VIS:
		ASSERT_NE(findFirst<CGTownInstance>(), nullptr);
		target = &findFirst<CGTownInstance>()->townAndVis; break;
	}
	if(owner != WorldOwner::PLAYER_BATTLE) ASSERT_NE(target, nullptr);
	// These controls must succeed before injection: no1145 capture guard may mask the marker.
	expectOldWritable(*gameState());
	expectOldWritable(*map());
	if(owner == WorldOwner::PLAYER_BATTLE)
		gameState()->players.at(PlayerColor(0)).battleBonuses.push_back(*marker());
	else target->addNewBonus(marker());
	CGameState restored;
	restored.preInit(LIBRARY);
	ASSERT_NO_THROW(restored.loadFromMemory(gameState()->saveToMemory()));
	if(owner == WorldOwner::POOL_HERO)
	{
		const auto * pooled = restored.getMap().tryGetFromHeroPool(pooledHeroType);
		ASSERT_NE(pooled, nullptr);
		ASSERT_FALSE(pooled->getExportedBonusList().empty());
		EXPECT_EQ(pooled->getExportedBonusList().back()->type, marker()->type);
	}
	EXPECT_THROW(restored.validateOwnedBonusSerialization(BonusSerializationContext{false, true}), std::runtime_error);
	expectOldPrefixRejected(*gameState());
	// Player/team/global payloads live outside map, so only map-owned cases reject map.
	if(owner != WorldOwner::PLAYER && owner != WorldOwner::PLAYER_BATTLE
		&& owner != WorldOwner::TEAM && owner != WorldOwner::GLOBAL)
		expectOldPrefixRejected(*map());
}
INSTANTIATE_TEST_SUITE_P(OwnedPayloads, OwnedWorldEnvelopeTest, ::testing::Values(
	WorldOwner::HERO, WorldOwner::SLOT, WorldOwner::COMMANDER, WorldOwner::PLAYER,
	WorldOwner::PLAYER_BATTLE, WorldOwner::TEAM, WorldOwner::GLOBAL, WorldOwner::ARTIFACT,
	WorldOwner::POOL_HERO, WorldOwner::TOWN, WorldOwner::TOWN_VIS));

std::shared_ptr<Bonus> nestedCapability(int kind)
{
	auto child = marker();
	if(kind == 0) child->val = 0;
	if(kind == 2)
	{
		child->type = BonusType::STACKS_SPEED;
		auto filter = std::make_shared<UnitAdjacentLimiter>();
		filter->targetUnit = CreatureID(0);
		filter->filter.relationship = AdjacentUnitFilter::Relationship::SAME_ARMY;
		child->limiter = filter;
	}
	auto outer = marker();
	outer->type = BonusType::STACKS_SPEED;
	BonusParametersOnCombatEvent event;
	event.effects.push_back(BonusParametersOnCombatEvent::CombatEffectBonus{child, false});
	outer->parameters = std::make_shared<BonusParameters>(event);
	return outer;
}

TEST_F(OwnedBonusEnvelopeTest, NestedTypedParametersProtectRealMapAndWorld)
{
	startGame();
	for(int kind = 0; kind < 3; ++kind)
	{
		expectOldWritable(*gameState());
		expectOldWritable(*map());
		auto outer = nestedCapability(kind);
		attackerSideHero->addNewBonus(outer);
		expectOldPrefixRejected(*map());
		expectOldPrefixRejected(*gameState());
		CGameState restored;
		restored.preInit(LIBRARY);
		ASSERT_NO_THROW(restored.loadFromMemory(gameState()->saveToMemory()));
		EXPECT_THROW(restored.validateOwnedBonusSerialization(BonusSerializationContext{false, true}), std::runtime_error);
		attackerSideHero->removeBonus(outer);
	}
}

TEST_F(OwnedBonusEnvelopeTest, ActualBankRewardConfigurationProtectsMapAndWorld)
{
	startGame();
	auto bank = std::make_shared<CRewardableObject>(gameState().get());
	bank->ID = Obj::CREATURE_BANK;
	bank->subID = MapObjectSubID(0);
	bank->tempOwner = PlayerColor::UNFLAGGABLE;
	bank->pos = int3(16, 16, 0);
	const auto handler = bank->getObjectHandler();
	ASSERT_NE(handler, nullptr);
	ASSERT_FALSE(handler->getTemplates().empty());
	bank->appearance = handler->getTemplates().front();
	map()->generateUniqueInstanceName(bank.get());
	map()->addNewObject(bank);
	Rewardable::VisitInfo visit{};
	visit.visitType = Rewardable::EEventType::EVENT_FIRST_VISIT;
	bank->configuration.info.push_back(visit);
	for(int kind = 0; kind < 3; ++kind)
	{
		expectOldWritable(*map());
		expectOldWritable(*gameState());
		bank->configuration.info.back().reward.heroBonuses.push_back(nestedCapability(kind));
		expectOldPrefixRejected(*bank);
		expectOldPrefixRejected(*map());
		expectOldPrefixRejected(*gameState());
		CGameState restored;
		restored.preInit(LIBRARY);
		ASSERT_NO_THROW(restored.loadFromMemory(gameState()->saveToMemory()));
		const auto * decoded = dynamic_cast<const CRewardableObject *>(restored.getMap().objects.at(bank->id.getNum()).get());
		ASSERT_NE(decoded, nullptr);
		ASSERT_FALSE(decoded->configuration.info.empty());
		EXPECT_THROW(decoded->configuration.validateBonusSerialization(BonusSerializationContext{false, true}), std::runtime_error);
		bank->configuration.info.back().reward.heroBonuses.clear();
	}
}

TEST_F(OwnedBonusEnvelopeTest, ActualTownRewardableBuildingProtectsTownMapAndWorld)
{
	startGame(true);
	auto * town = findFirst<CGTownInstance>();
	ASSERT_NE(town, nullptr);
	ASSERT_FALSE(town->rewardableBuildings.empty());
	auto & building = *town->rewardableBuildings.begin()->second;
	Rewardable::VisitInfo visit{};
	visit.visitType = Rewardable::EEventType::EVENT_FIRST_VISIT;
	building.configuration.info.push_back(visit);
	for(int kind = 0; kind < 3; ++kind)
	{
		expectOldWritable(*town);
		expectOldWritable(*map());
		expectOldWritable(*gameState());
		building.configuration.info.back().reward.playerBonuses.push_back(nestedCapability(kind));
		expectOldPrefixRejected(building);
		expectOldPrefixRejected(*town);
		expectOldPrefixRejected(*map());
		expectOldPrefixRejected(*gameState());
		CGameState restored;
		restored.preInit(LIBRARY);
		ASSERT_NO_THROW(restored.loadFromMemory(gameState()->saveToMemory()));
		const auto * decoded = dynamic_cast<const CGTownInstance *>(restored.getMap().objects.at(town->id.getNum()).get());
		ASSERT_NE(decoded, nullptr);
		const auto & decodedBuilding = decoded->rewardableBuildings.at(building.getBuildingType());
		ASSERT_NE(decodedBuilding, nullptr);
		EXPECT_THROW(decodedBuilding->configuration.validateBonusSerialization(BonusSerializationContext{false, true}), std::runtime_error);
		building.configuration.info.back().reward.playerBonuses.clear();
	}
}
}
