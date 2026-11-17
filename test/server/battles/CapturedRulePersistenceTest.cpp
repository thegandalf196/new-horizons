/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "PreparedPilotFixture.h"
#include "../../../lib/entities/hero/NewHorizonsPerkOperation.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/gameState/ReplayLog.h"
#include "../../../lib/networkPacks/PacksForClient.h"

class CapturedRulePersistenceTest : public PreparedPilotFixture
{
protected:
	void prepareCapturedWorld()
	{
		customValues = true;
		prepareWorld();
	}
	static int capturedDiscipline(const CGHeroInstance & hero)
	{
		return static_cast<int>(hero.getPerkState().getRules()["skills"]["new-horizons:discipline"]
			["ranks"]["basic"]["mechanics"]["operations"][0]["value"].Integer());
	}
};

TEST_F(CapturedRulePersistenceTest, WorldSaveLoadSharesCapturesAndRetainsSavedValuesOverInstalledDefaults)
{
	ASSERT_NO_FATAL_FAILURE(prepareCapturedWorld());
	const auto owner = attackerSideHero->getPerkState().capturedOwner();
	ASSERT_TRUE(owner);
	EXPECT_EQ(defenderSideHero->getPerkState().capturedOwner(), owner);
	const auto identities = gameState()->getMap().getHeroesInPool();
	ASSERT_FALSE(identities.empty());
	for(const auto identity : identities)
	{
		SCOPED_TRACE(identity.getNum());
		const auto * hero = gameState()->getMap().tryGetFromHeroPool(identity);
		ASSERT_NE(hero, nullptr);
		EXPECT_EQ(hero->getPerkState().capturedOwner(), owner);
	}
	const auto bytes = gameState()->saveToMemory();
	const auto count = gameState()->getCapturedRulePoolForAdmission()->documentCount();
	CGameState loaded;
	loaded.preInit(LIBRARY);
	ASSERT_NO_THROW(loaded.loadFromMemory(bytes));
	const auto * first = loaded.getHero(attackerSideHero->id);
	const auto * second = loaded.getHero(defenderSideHero->id);
	ASSERT_NE(first, nullptr);
	ASSERT_NE(second, nullptr);
	EXPECT_EQ(first->getPerkState().capturedOwner(), second->getPerkState().capturedOwner());
	EXPECT_EQ(first->getPreparedPilotPerks(), second->getPreparedPilotPerks());
	EXPECT_NE(first->getPerkState().capturedOwner(), owner);
	EXPECT_EQ(capturedDiscipline(*first), 9);
	EXPECT_EQ(first->getPerkState().getRules(), attackerSideHero->getPerkState().getRules());
	EXPECT_EQ(first->getPerkState().getSelected(), attackerSideHero->getPerkState().getSelected());
	EXPECT_EQ(loaded.getCapturedRulePoolForAdmission()->documentCount(), count);
	const auto & installed = LIBRARY->settingsHandler->getValue(EGameSettings::HEROES_NEW_HORIZONS_PERKS);
	EXPECT_NE(installed["skills"]["new-horizons:discipline"]["ranks"]["basic"]
		["mechanics"]["operations"][0]["value"].Integer(), 9);
	CGameState replay;
	replay.preInit(LIBRARY);
	ASSERT_NO_THROW(replay.loadReplaySnapshot(ReplayPayload{bytes, ESerializationVersion::CURRENT}));
	ASSERT_NE(replay.getHero(first->id), nullptr);
	EXPECT_EQ(capturedDiscipline(*replay.getHero(first->id)), 9);
}

TEST_F(CapturedRulePersistenceTest, StandaloneHeroAndReplayPacketHaveSelfContainedRuleTables)
{
	ASSERT_NO_FATAL_FAILURE(prepareCapturedWorld());
	const auto originalRules = attackerSideHero->getPerkState().getRules();
	auto clone = CMemorySerializer::deepCopy(*attackerSideHero, gameState().get());
	ASSERT_TRUE(clone);
	EXPECT_EQ(clone->getPerkState().getRules(), originalRules);
	EXPECT_EQ(clone->getPerkState().getSelected(), attackerSideHero->getPerkState().getSelected());
	EXPECT_NE(clone->getPerkState().capturedOwner(), attackerSideHero->getPerkState().capturedOwner());
	NewObject packet;
	packet.newObject = gameState()->getMap().objects.at(attackerSideHero->id.getNum());
	packet.initiator = PlayerColor(0);
	const auto payload = ReplayPackSerializer::write(packet);
	auto decoded = ReplayPackSerializer::read(payload, gameState().get());
	const auto * restored = dynamic_cast<const NewObject *>(decoded.get());
	ASSERT_NE(restored, nullptr);
	const auto * hero = dynamic_cast<const CGHeroInstance *>(restored->newObject.get());
	ASSERT_NE(hero, nullptr);
	EXPECT_EQ(hero->getPerkState().getRules(), originalRules);
	EXPECT_EQ(capturedDiscipline(*hero), 9);
	EXPECT_NE(hero->getPerkState().capturedOwner(), attackerSideHero->getPerkState().capturedOwner());
	EXPECT_EQ(attackerSideHero->getPerkState().getRules(), originalRules);
}

TEST_F(CapturedRulePersistenceTest, OneIndependentPerkReplacementAddsOneDocumentAndPreservesOtherCaptures)
{
	ASSERT_NO_FATAL_FAILURE(prepareCapturedWorld());
	gameState()->saveToMemory(); // Finish initial composition admission before measuring identities.
	const auto count = gameState()->getCapturedRulePoolForAdmission()->documentCount();
	const JsonNode growth = defenderSideHero->getPrimaryGrowthRules();
	const JsonNode capabilities = defenderSideHero->getCapabilityRules();
	const JsonNode mastery = defenderSideHero->getMasteryState().rules;
	auto alternate = defenderSideHero->getPerkState().getRules();
	alternate["skills"]["new-horizons:discipline"]["ranks"]["basic"]
		["mechanics"]["operations"].Vector()[0]["value"].Integer() = 10;
	auto & state = const_cast<newHorizonsHeroes::PerkState &>(defenderSideHero->getPerkState());
	state.replaceRules(std::move(alternate));
	defenderSideHero->recreateSecondarySkillsBonuses();
	const auto bytes = gameState()->saveToMemory();
	EXPECT_EQ(gameState()->getCapturedRulePoolForAdmission()->documentCount(), count + 1);
	CGameState loaded;
	loaded.preInit(LIBRARY);
	ASSERT_NO_THROW(loaded.loadFromMemory(bytes));
	const auto * first = loaded.getHero(attackerSideHero->id);
	const auto * second = loaded.getHero(defenderSideHero->id);
	ASSERT_NE(first, nullptr);
	ASSERT_NE(second, nullptr);
	EXPECT_EQ(capturedDiscipline(*first), 9);
	EXPECT_EQ(capturedDiscipline(*second), 10);
	EXPECT_NE(first->getPerkState().capturedOwner(), second->getPerkState().capturedOwner());
	EXPECT_EQ(second->getPrimaryGrowthRules(), growth);
	EXPECT_EQ(second->getCapabilityRules(), capabilities);
	EXPECT_EQ(second->getMasteryState().rules, mastery);
	EXPECT_EQ(loaded.getHeroPerkRules(), first->getPerkState().getRules());
}

TEST_F(CapturedRulePersistenceTest, PooledPerkBytesAndAdmissionViewsScaleWithOneCapture)
{
	ASSERT_NO_FATAL_FAILURE(prepareCapturedWorld());
	const auto & state = attackerSideHero->getPerkState();
	const auto captured = state.capturedOwner();
	ASSERT_TRUE(captured);
	newHorizonsHeroes::PerkSelectionOperation operation(state);
	EXPECT_EQ(&operation.definitions(), &captured->admitted());
	nativeRules::CapturedRulePool pool;
	pool.adopt(nativeRules::CapturedRuleDocument::wrapPerks(captured));
	CMemorySerializer pooled, legacy, cold;
	pooled.oser & pool;
	cold.oser & state.getRules();
	legacy.oser.version = ESerializationVersion::NEW_HORIZONS_SORCERY_RETIREMENT;
	constexpr size_t owners = 32;
	{
		nativeRules::CapturedRuleWireScope scope(pooled.oser, pool);
		for(size_t index = 0; index < owners; ++index)
		{
			pooled.oser & state;
			legacy.oser & state;
		}
	}
	const auto pooledBytes = pooled.extractBuffer().size();
	const auto legacyBytes = legacy.extractBuffer().size();
	const auto coldBytes = cold.extractBuffer().size();
	EXPECT_EQ(pool.documentCount(), 1);
	EXPECT_LT(pooledBytes, legacyBytes);
	EXPECT_LE(pooledBytes, coldBytes + owners * 16 + 64);
	EXPECT_EQ(state.capturedOwner(), captured);
	RecordProperty("pooled32Bytes", std::to_string(pooledBytes));
	RecordProperty("inline1162_32Bytes", std::to_string(legacyBytes));
	RecordProperty("oneCapturedDocumentBytes", std::to_string(coldBytes));
}

TEST_F(CapturedRulePersistenceTest, DisabledRollingReplayRetainsPolicyAcrossActualNewDaysAndSnapshotLoad)
{
	ASSERT_NO_FATAL_FAILURE(prepareCapturedWorld());
	gameState()->replayLog = ReplayLog();
	gameState()->replayLog.configure(false, 1, false);
	SetHeroExperience gain;
	gain.id = attackerSideHero->id;
	gain.val = 7;
	gameState()->apply(gain);
	for(uint32_t day : {1u, 2u})
	{
		NewTurn next;
		next.day = day;
		gameState()->apply(next);
	}
	EXPECT_TRUE(gameState()->replayLog.empty());
	EXPECT_FALSE(gameState()->replayLog.isCaptureEnabled());
	const auto bytes = gameState()->saveToMemory();
	EXPECT_FALSE(gameState()->replayLog.isCaptureEnabled());
	CGameState loaded;
	loaded.preInit(LIBRARY);
	ASSERT_NO_THROW(loaded.loadFromMemory(bytes));
	EXPECT_FALSE(loaded.replayLog.isCaptureEnabled());
	loaded.replayLog.reconfigureOnLoad(3);
	EXPECT_FALSE(loaded.replayLog.isRecordingPacks());
	ASSERT_NE(loaded.getHero(attackerSideHero->id), nullptr);
	EXPECT_EQ(loaded.getHero(attackerSideHero->id)->exp, attackerSideHero->exp);
	EXPECT_EQ(loaded.day, gameState()->day);
	EXPECT_TRUE(loaded.replayLog.empty());
}

TEST_F(CapturedRulePersistenceTest, EnabledReplayHistoryHasMeasuredStorageCostWithoutChangingActionResults)
{
	ASSERT_NO_FATAL_FAILURE(prepareCapturedWorld());
	const auto baseline = gameState()->saveToMemory();
	CGameState enabled, disabled;
	enabled.preInit(LIBRARY);
	disabled.preInit(LIBRARY);
	enabled.loadFromMemory(baseline);
	disabled.loadFromMemory(baseline);
	enabled.replayLog.configure(false, 1, true);
	disabled.replayLog.configure(false, 1, false);
	for(auto * world : {&enabled, &disabled})
	{
		SetHeroExperience gain;
		gain.id = attackerSideHero->id;
		gain.val = 7;
		world->apply(gain);
		NewTurn next;
		next.day = 1;
		world->apply(next);
	}
	EXPECT_FALSE(enabled.replayLog.empty());
	EXPECT_TRUE(disabled.replayLog.empty());
	ASSERT_NE(enabled.getHero(attackerSideHero->id), nullptr);
	ASSERT_NE(disabled.getHero(attackerSideHero->id), nullptr);
	EXPECT_EQ(enabled.getHero(attackerSideHero->id)->exp, disabled.getHero(attackerSideHero->id)->exp);
	EXPECT_EQ(enabled.day, disabled.day);
	CMemorySerializer enabledWire, disabledWire;
	enabledWire.oser & enabled;
	disabledWire.oser & disabled;
	const auto enabledBytes = enabledWire.extractBuffer().size();
	const auto disabledBytes = disabledWire.extractBuffer().size();
	EXPECT_GT(enabledBytes, disabledBytes);
	RecordProperty("enabledWorldWithHistoryBytes", std::to_string(enabledBytes));
	RecordProperty("disabledWorldBytes", std::to_string(disabledBytes));
}

TEST_F(CapturedRulePersistenceTest, CompiledHeroAndWorldCapabilitiesMatchValidatedRawBoundaries)
{
	ASSERT_NO_FATAL_FAILURE(prepareCapturedWorld());
	const auto * hero = attackerSideHero->compiledCapabilities();
	const auto * world = gameState()->getCompiledHeroCapabilityRules();
	ASSERT_NE(hero, nullptr);
	ASSERT_NE(world, nullptr);
	const auto & captured = attackerSideHero->getCapabilityRules();
	const auto & global = gameState()->getHeroCapabilityRules();
	for(int rank = 0; rank <= 3; ++rank)
	{
		EXPECT_EQ(hero->rating(rank), newHorizonsHeroes::capabilitySiegeRating(captured, rank));
		EXPECT_EQ(hero->directControlChance(rank), newHorizonsHeroes::capabilityDirectControlChance(captured, rank));
		EXPECT_EQ(hero->ballistaDamageMultiplier(rank), newHorizonsHeroes::capabilityBallistaMultiplier(captured, rank));
		for(const auto * output : {"ballistaDamage", "catapultStructuralDamage", "firstAidHealing", "defensiveTowerDamage"})
			EXPECT_EQ(attackerSideHero->getCapturedSiegeOutput(rank * 100, output),
				newHorizonsHeroes::capabilitySiegeOutput(captured, rank * 100, output));
	}
	for(const auto key : {"core:charmOfMana", "core:talismanOfMana", "core:mysticOrbOfMana"})
		for(int mana : {-1, 0, 1, 100, std::numeric_limits<int>::max()})
		{
			const ArtifactID artifact(ArtifactID::decode(key));
			EXPECT_EQ(hero->artifactManaRegeneration(artifact, mana),
				newHorizonsHeroes::capabilityArtifactManaRegeneration(captured, artifact, mana));
		}
	const auto aura = hero->glyphsOfFearAura();
	const auto rawAura = newHorizonsHeroes::capabilityGlyphsOfFearAura(captured);
	ASSERT_EQ(aura.has_value(), rawAura.has_value());
	if(aura)
	{
		EXPECT_EQ(aura->radius, rawAura->radius);
		EXPECT_EQ(aura->morale, rawAura->morale);
	}
	EXPECT_EQ(world->skeletonTransformerHealthPercent(),
		newHorizonsHeroes::capabilitySkeletonTransformerHealthPercent(global));
	EXPECT_EQ(world->warMachineShopInventory(), newHorizonsHeroes::capabilityWarMachineShopInventory(global));
	for(const auto faction : {FactionID::CASTLE, FactionID::TOWER, FactionID::NECROPOLIS})
		for(const auto artifact : world->warMachineShopInventory())
			for(const int price : {0, 1, 2375, 10000})
				EXPECT_EQ(world->warMachineShopPrice(faction, artifact, price),
					newHorizonsHeroes::capabilityWarMachineShopPrice(global, faction, artifact, price));
}
