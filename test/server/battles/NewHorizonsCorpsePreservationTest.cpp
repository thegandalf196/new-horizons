/*
 * NewHorizonsCorpsePreservationTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text in license.txt file in main folder
 */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../SpellPointTestUtils.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/BattleInfo.h"
#include "../../../lib/battle/CUnitState.h"
#include "../../../lib/entities/hero/NewHorizonsNecromancy.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/networkPacks/BattleChanges.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
#include "../../../server/queries/BattleQueries.h"
#include "../../../server/queries/QueriesProcessor.h"

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace
{
constexpr int32_t targetStackCount = 100;
constexpr int32_t shooterStackCount = 50;
constexpr auto DISINTEGRATE_SPELL = "new-horizons:disintegrate";

CreatureID creatureById(const char * id)
{
	return CreatureID(CreatureID::decode(id));
}

SpellID spellById(std::string_view id)
{
	return SpellID(SpellID::decode(std::string(id)));
}

class NewHorizonsCorpsePreservationTest : public BattleTestFixture
{
protected:
	const CStack * target = nullptr;
	const CStack * shooter = nullptr;

	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));

		JsonNode perkRules(JsonPath::builtin("config/newHorizonsPerks"));
		auto & perks = perkRules["skills"][std::string(newHorizonsNecromancy::SKILL_ID)]["perks"].Vector();
		const auto corpsePreservation = std::find_if(perks.begin(), perks.end(), [](const JsonNode & perk)
		{
			return perk["id"].String() == newHorizonsNecromancy::CORPSE_PRESERVATION_ID;
		});
		if(corpsePreservation == perks.end())
			throw std::runtime_error("Missing Corpse Preservation in the New Horizons perk registry");

		const std::string productionStatus = (*corpsePreservation)["effect"]["status"].String();
		RecordProperty("corpse_preservation_registry_status", productionStatus);
		const bool fixtureOverride = productionStatus == "planned";
		RecordProperty("corpse_preservation_test_override_applied", fixtureOverride ? "true" : "false");
		if(fixtureOverride)
			(*corpsePreservation)["effect"]["status"].String() = "active";
		else if(productionStatus != "active")
			throw std::runtime_error("Corpse Preservation must be planned or active in canonical content");

		for(const auto & [perkId, propertyName] : std::array<std::pair<const char *, const char *>, 2>{
			std::pair{newHorizonsNecromancy::DEATH_LORD_ID, "death_lord"},
			std::pair{newHorizonsNecromancy::GRAVE_KNOWLEDGE_ID, "grave_knowledge"}})
		{
			const auto specialPerk = std::find_if(perks.begin(), perks.end(), [perkId](const JsonNode & perk)
			{
				return perk["id"].String() == perkId;
			});
			if(specialPerk == perks.end())
				throw std::runtime_error(std::string("Missing special Necromancy perk in registry: ") + perkId);
			const auto status = (*specialPerk)["effect"]["status"].String();
			RecordProperty(std::string(propertyName) + "_registry_status", status);
			RecordProperty(std::string(propertyName) + "_activation_override_applied", "false");
			if(status != "active")
				throw std::runtime_error(std::string("Special Necromancy perk must be active in canonical content: ") + perkId);
		}
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perkRules));
	}

	void startNecromancerGame(bool selectCorpsePreservation, int32_t attackerSpellPower = 10,
		std::string_view advancedPerkId = {})
	{
		startGame();
		// Keep the real Necromancer faction/rank check while reusing the compact
		// two-hero test map's stable object setup.
		attackerSideHero->setHeroType(HeroTypeID(72)); // Septienna.
		const int necromancyIndex = SecondarySkill::decode(newHorizonsNecromancy::SKILL_ID);
		ASSERT_GE(necromancyIndex, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(necromancyIndex), MasteryLevel::BASIC,
			ChangeValueMode::ABSOLUTE);
		ASSERT_TRUE(attackerSideHero->usesNewHorizonsNecromancy());

		if(selectCorpsePreservation)
			acceptCorpsePreservationThroughOffer(attackerSideHero);
		if(!advancedPerkId.empty())
		{
			gameHandler->onAdvInterfaceReady(PlayerColor(0));
			gameHandler->onAdvInterfaceReady(PlayerColor(1));
			gameHandler->levelUpHero(attackerSideHero, SecondarySkill(necromancyIndex), false);
			ASSERT_EQ(attackerSideHero->getSecSkillLevel(SecondarySkill(necromancyIndex)), MasteryLevel::ADVANCED);
			acceptPerkThroughOffer(attackerSideHero, std::string(advancedPerkId), MasteryLevel::ADVANCED);
			RecordProperty(advancedPerkId == newHorizonsNecromancy::DEATH_LORD_ID
				? "death_lord_selected" : "grave_knowledge_selected", "true");
			RecordProperty(advancedPerkId == newHorizonsNecromancy::DEATH_LORD_ID
				? "death_lord_skill_rank" : "grave_knowledge_skill_rank", "advanced");
		}

		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(SpellID::MAGIC_ARROW);
		const auto disintegrate = spellById(DISINTEGRATE_SPELL);
		ASSERT_NE(disintegrate, SpellID::NONE);
		attackerSideHero->addSpellToSpellbook(disintegrate);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, attackerSpellPower, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);

		giveArtifact(defenderSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		defenderSideHero->addSpellToSpellbook(SpellID::RESURRECTION);
		defenderSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		defenderSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 1, ChangeValueMode::ABSOLUTE);
		const int lightMagicIndex = SecondarySkill::decode("new-horizons:lightMagic");
		if(lightMagicIndex >= 0)
			defenderSideHero->setSecSkillLevel(SecondarySkill(lightMagicIndex), MasteryLevel::EXPERT,
				ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(defenderSideHero, 1000);
	}

	void acceptPerkThroughOffer(CGHeroInstance * hero, const std::string & perkId, int requiredRank)
	{
		const auto rankLookup = [hero](const std::string & skillId)
		{
			return hero->getPerkSkillRank(skillId);
		};
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer(rankLookup, seed);
			for(size_t choice = 0; choice < offer.size(); ++choice)
			{
				if(offer[choice].selection.skillId != newHorizonsNecromancy::SKILL_ID
					|| offer[choice].selection.perkId != perkId)
					continue;

				ASSERT_EQ(offer[choice].requiredRank, requiredRank);
				gameHandler->levelUpHero(hero, offer, choice, seed, false);
				ASSERT_TRUE(hero->hasActivePerk(newHorizonsNecromancy::SKILL_ID,
					perkId));
				return;
			}
		}
		FAIL() << "Perk never appeared in a legal rank-gated Necromancy offer: " << perkId;
	}

	void acceptCorpsePreservationThroughOffer(CGHeroInstance * hero)
	{
		acceptPerkThroughOffer(hero, newHorizonsNecromancy::CORPSE_PRESERVATION_ID,
			static_cast<int>(MasteryLevel::BASIC));
	}

	void prepareBattle(bool selectCorpsePreservation, int32_t attackerSpellPower = 10,
		const char * targetCreatureId = "core:pikeman", int32_t targetCount = targetStackCount,
		std::string_view advancedPerkId = {})
	{
		startNecromancerGame(selectCorpsePreservation, attackerSpellPower, advancedPerkId);
		// Necromancy can award up to 10 Skeletons from this stack. Keep ordinary
		// per-stack Leadership capacity from masking the result-path assertions.
		attackerSideHero->level = 75;
		// Keep casualties on stacks that belong to the pre-battle armies so the
		// battle result can attribute them to the original army baseline.
		attackerSideHero->clearSlots();
		defenderSideHero->clearSlots();
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creatureById("core:archer"), shooterStackCount));
		const auto targetCreature = creatureById(targetCreatureId);
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), targetCreature, targetCount));

		startBattle();
		target = findArmyStack(BattleSide::DEFENDER, targetCreature);
		shooter = findArmyStack(BattleSide::ATTACKER, creatureById("core:archer"));
		ASSERT_NE(target, nullptr);
		ASSERT_NE(shooter, nullptr);
		beginCombat();
	}

	const CStack * findArmyStack(BattleSide side, CreatureID creature) const
	{
		const auto matches = battle()->battleGetStacksIf([side, creature](const CStack * stack)
		{
			return stack->unitSide() == side && stack->creatureId() == creature;
		});
		if(matches.size() != 1)
		{
			ADD_FAILURE() << "Expected one original army stack for " << creature.getNum()
				<< " on side " << static_cast<int>(side) << ", found " << matches.size();
			return nullptr;
		}
		return matches.front();
	}

	bool castHeroSpell(BattleSide side, SpellID spell, const CStack * victim)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = side;
		action.spell = spell;
		action.aimToUnit(victim);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->sideToPlayer(side), action);
	}

	bool shoot(const CStack * attacker, const CStack * victim)
	{
		for(int attempt = 0; attempt < 32 && battle()->battleActiveUnit() != attacker; ++attempt)
		{
			const auto * active = battle()->battleActiveUnit();
			if(!active || !gameHandler->battles->makePlayerBattleAction(BattleID(0),
				battle()->sideToPlayer(active->unitSide()), BattleAction::makeDefend(active)))
				return false;
		}
		if(battle()->battleActiveUnit() != attacker)
			return false;
		return gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(attacker->unitSide()), BattleAction::makeShotAttack(attacker, victim));
	}

	bool defendActiveUnit()
	{
		const auto * active = battle()->battleActiveUnit();
		return active && gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(active->unitSide()), BattleAction::makeDefend(active));
	}

	bool advanceToActiveSideInRound(BattleSide side, int32_t round)
	{
		for(int attempt = 0; attempt < 64; ++attempt)
		{
			const auto * active = battle()->battleActiveUnit();
			if(battle()->getRound() == round && active && active->unitSide() == side)
				return true;
			if(!defendActiveUnit())
				return false;
		}
		return false;
	}

	std::optional<BattleResult> finishAndCaptureResult()
	{
		// BattleTestFixture applies BattleStart directly and therefore omits the
		// CBattleQuery that the authoritative BattleProcessor installs.
		auto query = std::make_shared<CBattleQuery>(gameHandler.get(), battle());
		gameHandler->queries->addQuery(query);
		gameHandler->battles->cheatBattleVictory(PlayerColor(0));
		if(!query->result)
			return std::nullopt;
		auto result = query->result;
		for(const auto player : {PlayerColor(0), PlayerColor(1)})
		{
			auto dialog = gameHandler->queries->topQuery(player);
			if(dialog && dialog->getType() == QueryType::BattleDialog
				&& !gameHandler->queryReply(dialog->queryID, 0, player))
			{
				ADD_FAILURE() << "The normal battle-result dialog should accept its default result";
				return std::nullopt;
			}
		}
		return result;
	}

	void verifyProvenanceSerialization(const JsonNode & snapshot,
		std::unique_ptr<BattleInfo> battleDescriptorBeforeHit)
	{
		const auto expectedMagic = target->acquireState()->getMagicalCasualties();
		ASSERT_GT(expectedMagic, 0);
		auto detachedState = target->acquireState();
		detachedState->load(snapshot);
		EXPECT_EQ(detachedState->getMagicalCasualties(), expectedMagic);
		EXPECT_EQ(detachedState->getKilled(), target->getKilled());

		auto invalidCount = snapshot;
		invalidCount["state"]["health"]["casualtyProvenance"][0]["count"] = JsonNode(0);
		auto invalidTemporary = snapshot;
		const auto cohortCount = snapshot["state"]["health"]["casualtyProvenance"][0]["count"].Integer();
		invalidTemporary["state"]["health"]["casualtyProvenance"][0]["temporarilyRestored"] =
			JsonNode(cohortCount + 1);
		auto invalidProvenance = snapshot;
		invalidProvenance["state"]["health"]["casualtyProvenance"][0]["provenance"] = JsonNode(99);
		for(const auto * malformed : {&invalidCount, &invalidTemporary, &invalidProvenance})
		{
			auto rejectedState = target->acquireState();
			EXPECT_THROW(rejectedState->load(*malformed), std::runtime_error);
		}

		auto malformedOriginalHealthString = snapshot;
		malformedOriginalHealthString["state"]["battleFormOriginalHealth"] = JsonNode("not health data");
		auto malformedOriginalHealthArray = snapshot;
		JsonNode originalHealthArray;
		originalHealthArray.Vector().emplace_back(JsonNode(1));
		malformedOriginalHealthArray["state"]["battleFormOriginalHealth"] = std::move(originalHealthArray);
		for(const auto * malformed : {&malformedOriginalHealthString, &malformedOriginalHealthArray})
		{
			auto rejectedState = target->acquireState();
			EXPECT_THROW(rejectedState->load(*malformed), std::runtime_error);
		}

		// Pre-provenance snapshots omit both optional health objects and the
		// provenance fields. Their old casualties remain unknown/ordinary; a
		// later explicitly classified spell hit must not recolor that history.
		auto legacySnapshot = snapshot;
		legacySnapshot["state"].Struct().erase("battleFormOriginalHealth");
		legacySnapshot["state"]["health"].Struct().erase("casualtyProvenanceInitialized");
		legacySnapshot["state"]["health"].Struct().erase("casualtyProvenance");
		auto legacyState = target->acquireState();
		legacyState->load(legacySnapshot);
		const auto legacyKilledBeforeNewSpell = legacyState->getKilled();
		EXPECT_EQ(legacyState->getMagicalCasualties(), 0);
		EXPECT_EQ(legacyKilledBeforeNewSpell, target->getKilled());
		ASSERT_GT(legacyState->getAvailableHealth(), 0);
		const int64_t expectedSpellDamage = legacyState->getAvailableHealth();
		int64_t nextSpellDamage = expectedSpellDamage;
		legacyState->damage(nextSpellDamage, false, battle::DamageProvenance::SPELL);
		EXPECT_EQ(nextSpellDamage, expectedSpellDamage);
		EXPECT_EQ(legacyState->getKilled(), targetStackCount);
		EXPECT_EQ(legacyState->getMagicalCasualties(), targetStackCount - legacyKilledBeforeNewSpell);

		UnitChanges update(target->unitId(), UnitChanges::EOperation::UPDATE);
		update.data = snapshot;
		CMemorySerializer currentUnitChanges;
		currentUnitChanges.oser.version = ESerializationVersion::CURRENT;
		currentUnitChanges.iser.version = ESerializationVersion::CURRENT;
		currentUnitChanges.oser & update;
		currentUnitChanges.iser.cb = gameState().get();
		UnitChanges decodedUpdate;
		currentUnitChanges.iser & decodedUpdate;
		EXPECT_EQ(decodedUpdate.data["state"]["health"], snapshot["state"]["health"]);

		CMemorySerializer oldUnitChanges;
		oldUnitChanges.oser.version = ESerializationVersion::BATTLE_INITIAL_DEPLOYMENT_ORDER;
		EXPECT_THROW(oldUnitChanges.oser & update, std::runtime_error);
		EXPECT_TRUE(oldUnitChanges.extractBuffer().empty())
			<< "A lossy UnitChanges writer must reject provenance before emitting any bytes";

		BattleStackAttacked hit;
		hit.stackAttacked = target->unitId();
		hit.newState.id = target->unitId();
		hit.newState.operation = UnitChanges::EOperation::UPDATE;
		hit.newState.data = snapshot;
		StacksInjured injury;
		injury.battleID = BattleID(0);
		injury.stacks.push_back(hit);
		CMemorySerializer oldInjury;
		oldInjury.oser.version = ESerializationVersion::BATTLE_INITIAL_DEPLOYMENT_ORDER;
		EXPECT_THROW(oldInjury.oser & injury, std::runtime_error);
		EXPECT_TRUE(oldInjury.extractBuffer().empty())
			<< "The outer StacksInjured packet must reject provenance before its packet header";

		CMemorySerializer currentStack;
		currentStack.oser.version = ESerializationVersion::CURRENT;
		EXPECT_THROW(currentStack.oser & *target, std::runtime_error);
		EXPECT_TRUE(currentStack.extractBuffer().empty());

		CMemorySerializer currentBattle;
		currentBattle.oser.version = ESerializationVersion::CURRENT;
		EXPECT_THROW(currentBattle.oser & *battle(), std::runtime_error);
		EXPECT_TRUE(currentBattle.extractBuffer().empty());

		auto * descriptorTarget = battleDescriptorBeforeHit
			? battleDescriptorBeforeHit->getStack(static_cast<int>(target->unitId()), false) : nullptr;
		ASSERT_NE(descriptorTarget, nullptr);
		descriptorTarget->load(snapshot);
		ASSERT_TRUE(battleDescriptorBeforeHit->hasCasualtyProvenanceState());
		BattleStart start;
		start.battleID = BattleID(0);
		start.info = std::move(battleDescriptorBeforeHit);
		CMemorySerializer currentStart;
		currentStart.oser.version = ESerializationVersion::CURRENT;
		EXPECT_THROW(currentStart.oser & start, std::runtime_error);
		EXPECT_TRUE(currentStart.extractBuffer().empty());
	}

	int32_t raisedSkeletons() const
	{
		int32_t total = 0;
		const auto skeleton = creatureById("core:skeleton");
		for(const auto & [slot, stack] : attackerSideHero->Slots())
		{
			(void)slot;
			if(stack->getCreatureID() == skeleton)
				total += stack->getCount();
		}
		return total;
	}

	int32_t resultCount(const BattleResult & result, BattleSide side, CreatureID creature,
		bool eligible) const
	{
		const auto & counts = eligible ? result.necromancyEligibleCasualties[side] : result.casualties[side];
		const auto found = counts.find(creature);
		return found == counts.end() ? 0 : found->second;
	}

	int32_t specialResultCount(const BattleResult & result, BattleSide side, CreatureID creature,
		bool nonliving) const
	{
		const auto & counts = nonliving ? result.necromancyNonlivingEligibleCasualties[side]
			: result.necromancyUndeadEligibleCasualties[side];
		const auto found = counts.find(creature);
		return found == counts.end() ? 0 : found->second;
	}
};
}

TEST_F(NewHorizonsCorpsePreservationTest, MagicArrowCasualtiesAreExcludedWithoutTheBasicPerk)
{
	prepareBattle(false);
	auto battleDescriptorBeforeHit = CMemorySerializer::deepCopy(*battle(), gameState().get());
	ASSERT_NE(battleDescriptorBeforeHit, nullptr);
	ASSERT_FALSE(attackerSideHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID,
		newHorizonsNecromancy::CORPSE_PRESERVATION_ID));
	ASSERT_TRUE(castHeroSpell(BattleSide::ATTACKER, SpellID::MAGIC_ARROW, target));

	const auto hitState = target->acquireState();
	const int32_t magicalAtCast = hitState->getMagicalCasualties();
	ASSERT_GT(magicalAtCast, 0) << "The test must record a real Magic Arrow casualty";
	EXPECT_EQ(target->getKilled(), magicalAtCast);
	const auto hitSnapshot = hitState->save();
	EXPECT_TRUE(hitSnapshot["state"]["health"]["casualtyProvenanceInitialized"].Bool());
	ASSERT_FALSE(hitSnapshot["state"]["health"]["casualtyProvenance"].Vector().empty());
	EXPECT_EQ(hitSnapshot["state"]["health"]["casualtyProvenance"][0]["provenance"].Integer(),
		static_cast<int64_t>(battle::DamageProvenance::SPELL));
	verifyProvenanceSerialization(hitSnapshot, std::move(battleDescriptorBeforeHit));

	const auto result = finishAndCaptureResult();
	ASSERT_TRUE(result.has_value());
	ASSERT_TRUE(result->necromancyEligibilityCaptured);
	const auto pikeman = creatureById("core:pikeman");
	EXPECT_EQ(resultCount(*result, BattleSide::DEFENDER, pikeman, false), targetStackCount);
	EXPECT_EQ(resultCount(*result, BattleSide::DEFENDER, pikeman, true), targetStackCount - magicalAtCast);
	EXPECT_EQ(raisedSkeletons(), (targetStackCount - magicalAtCast) / 10)
		<< "The real post-battle Necromancy consumer must use the provenance-filtered result";
}

TEST_F(NewHorizonsCorpsePreservationTest, LegalBasicPerkIncludesMagicArrowCasualties)
{
	prepareBattle(true);
	ASSERT_TRUE(attackerSideHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID,
		newHorizonsNecromancy::CORPSE_PRESERVATION_ID));
	ASSERT_TRUE(castHeroSpell(BattleSide::ATTACKER, SpellID::MAGIC_ARROW, target));
	const int32_t magicalAtCast = target->acquireState()->getMagicalCasualties();
	ASSERT_GT(magicalAtCast, 0);

	const auto result = finishAndCaptureResult();
	ASSERT_TRUE(result.has_value());
	ASSERT_TRUE(result->necromancyEligibilityCaptured);
	const auto pikeman = creatureById("core:pikeman");
	EXPECT_EQ(resultCount(*result, BattleSide::DEFENDER, pikeman, false), targetStackCount);
	EXPECT_EQ(resultCount(*result, BattleSide::DEFENDER, pikeman, true), targetStackCount)
		<< "Ordinary magical casualties are included only because the legal Basic perk was selected";
	EXPECT_EQ(raisedSkeletons(), targetStackCount / 10)
		<< "Corpse Preservation must affect actual Necromancy, not only the displayed result map";
}

TEST_F(NewHorizonsCorpsePreservationTest, DeathLordCapturesCorpsePreservedMagicalNonlivingCasualties)
{
	prepareBattle(true, 100, "core:ironGolem", targetStackCount, newHorizonsNecromancy::DEATH_LORD_ID);
	ASSERT_TRUE(attackerSideHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID,
		newHorizonsNecromancy::DEATH_LORD_ID));
	ASSERT_TRUE(attackerSideHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID,
		newHorizonsNecromancy::CORPSE_PRESERVATION_ID));
	ASSERT_TRUE(castHeroSpell(BattleSide::ATTACKER, SpellID::MAGIC_ARROW, target));
	const auto magicalAtCast = target->acquireState()->getMagicalCasualties();
	ASSERT_GT(magicalAtCast, 0);

	const auto result = finishAndCaptureResult();
	ASSERT_TRUE(result.has_value());
	ASSERT_TRUE(result->necromancySpecialEligibilityCaptured);
	const auto ironGolem = creatureById("core:ironGolem");
	EXPECT_EQ(resultCount(*result, BattleSide::DEFENDER, ironGolem, false), targetStackCount);
	EXPECT_EQ(resultCount(*result, BattleSide::DEFENDER, ironGolem, true), 0);
	EXPECT_EQ(specialResultCount(*result, BattleSide::DEFENDER, ironGolem, true), targetStackCount)
		<< "The special nonliving pool includes magical casualties only because the Basic perk was legally selected";
	EXPECT_EQ(specialResultCount(*result, BattleSide::DEFENDER, ironGolem, false), 0);
	EXPECT_EQ(raisedSkeletons(), 5)
		<< "Advanced Death Lord generates Skeleton equivalents at one quarter of Necromancy's normal rate";
}

TEST_F(NewHorizonsCorpsePreservationTest, GraveKnowledgeCapturesOnlyUndeadCasualtiesAndRaisesAtFixedRate)
{
	prepareBattle(true, 100, "core:vampire", targetStackCount, newHorizonsNecromancy::GRAVE_KNOWLEDGE_ID);
	ASSERT_TRUE(attackerSideHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID,
		newHorizonsNecromancy::GRAVE_KNOWLEDGE_ID));
	ASSERT_TRUE(advanceToActiveSideInRound(BattleSide::ATTACKER, 1));
	ASSERT_TRUE(castHeroSpell(BattleSide::ATTACKER, SpellID::MAGIC_ARROW, target));
	ASSERT_GT(target->acquireState()->getMagicalCasualties(), 0);

	const auto result = finishAndCaptureResult();
	ASSERT_TRUE(result.has_value());
	ASSERT_TRUE(result->necromancySpecialEligibilityCaptured);
	const auto vampire = creatureById("core:vampire");
	EXPECT_EQ(resultCount(*result, BattleSide::DEFENDER, vampire, false), targetStackCount);
	EXPECT_EQ(resultCount(*result, BattleSide::DEFENDER, vampire, true), 0);
	EXPECT_EQ(specialResultCount(*result, BattleSide::DEFENDER, vampire, false), targetStackCount);
	EXPECT_EQ(specialResultCount(*result, BattleSide::DEFENDER, vampire, true), 0);
	EXPECT_EQ(raisedSkeletons(), 20)
		<< "Grave Knowledge uses its fixed 20% rate on eligible Undead casualties";
}

TEST_F(NewHorizonsCorpsePreservationTest, PhysicalBattleAttackCasualtiesRemainEligibleWithoutThePerk)
{
	prepareBattle(false);
	ASSERT_TRUE(shoot(shooter, target));
	ASSERT_EQ(server.attacks.size(), 1u);
	const auto state = target->acquireState();
	const int32_t physicalKills = target->getKilled();
	ASSERT_GT(physicalKills, 0);
	EXPECT_EQ(state->getMagicalCasualties(), 0);

	const auto result = finishAndCaptureResult();
	ASSERT_TRUE(result.has_value());
	ASSERT_TRUE(result->necromancyEligibilityCaptured);
	const auto pikeman = creatureById("core:pikeman");
	EXPECT_EQ(resultCount(*result, BattleSide::DEFENDER, pikeman, false), targetStackCount);
	EXPECT_EQ(resultCount(*result, BattleSide::DEFENDER, pikeman, true), targetStackCount);
}

TEST_F(NewHorizonsCorpsePreservationTest, DisintegrateRemainsExcludedWithCorpsePreservation)
{
	prepareBattle(true);
	const auto havocMagicIndex = SecondarySkill::decode("new-horizons:havocMagic");
	ASSERT_GE(havocMagicIndex, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(havocMagicIndex), MasteryLevel::EXPERT,
		ChangeValueMode::ABSOLUTE);
	const auto disintegrate = spellById(DISINTEGRATE_SPELL);
	ASSERT_NE(disintegrate, SpellID::NONE);
	ASSERT_TRUE(castHeroSpell(BattleSide::ATTACKER, disintegrate, target));

	const auto hitState = target->acquireState();
	const int32_t destroyedBySpell = hitState->getUnusableRemains();
	ASSERT_GT(destroyedBySpell, 0) << "Disintegrate must create destroyed remains in the actual cast path";
	EXPECT_EQ(hitState->getMagicalCasualties(), 0)
		<< "Disintegrate casualties are tracked as unusable remains, not usable SPELL cohorts";

	const auto result = finishAndCaptureResult();
	ASSERT_TRUE(result.has_value());
	ASSERT_TRUE(result->necromancyEligibilityCaptured);
	const auto pikeman = creatureById("core:pikeman");
	EXPECT_EQ(resultCount(*result, BattleSide::DEFENDER, pikeman, false), targetStackCount);
	EXPECT_EQ(resultCount(*result, BattleSide::DEFENDER, pikeman, true), targetStackCount - destroyedBySpell)
		<< "Corpse Preservation never restores remains explicitly destroyed by Disintegrate";
	EXPECT_EQ(raisedSkeletons(), (targetStackCount - destroyedBySpell) / 10);
}

TEST_F(NewHorizonsCorpsePreservationTest, DeathLordSpecialPoolExcludesDisintegrateDestroyedRemains)
{
	constexpr int32_t golemStackCount = 1000;
	prepareBattle(true, 100, "core:ironGolem", golemStackCount, newHorizonsNecromancy::DEATH_LORD_ID);
	const auto havocMagicIndex = SecondarySkill::decode("new-horizons:havocMagic");
	ASSERT_GE(havocMagicIndex, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(havocMagicIndex), MasteryLevel::EXPERT,
		ChangeValueMode::ABSOLUTE);
	const auto disintegrate = spellById(DISINTEGRATE_SPELL);
	ASSERT_NE(disintegrate, SpellID::NONE);
	ASSERT_TRUE(castHeroSpell(BattleSide::ATTACKER, disintegrate, target));

	const auto state = target->acquireState();
	const auto destroyedBySpell = state->getUnusableRemains();
	ASSERT_GT(destroyedBySpell, 0);
	ASSERT_LT(destroyedBySpell, golemStackCount);
	EXPECT_EQ(state->getMagicalCasualties(), 0);

	const auto result = finishAndCaptureResult();
	ASSERT_TRUE(result.has_value());
	ASSERT_TRUE(result->necromancySpecialEligibilityCaptured);
	const auto ironGolem = creatureById("core:ironGolem");
	const auto usableSpecialCasualties = golemStackCount - destroyedBySpell;
	EXPECT_EQ(specialResultCount(*result, BattleSide::DEFENDER, ironGolem, true), usableSpecialCasualties)
		<< "Death Lord excludes remains explicitly destroyed by Disintegrate";
	EXPECT_EQ(raisedSkeletons(), usableSpecialCasualties * 20 / 400);
}

TEST_F(NewHorizonsCorpsePreservationTest, PartialResurrectionConsumesNewestMixedCasualtiesFirst)
{
	prepareBattle(false, 100);
	ASSERT_TRUE(shoot(shooter, target));
	ASSERT_EQ(battle()->getRound(), 1);
	const int32_t physicalKills = target->getKilled();
	ASSERT_GT(physicalKills, 0);
	EXPECT_EQ(target->acquireState()->getMagicalCasualties(), 0);

	ASSERT_TRUE(advanceToActiveSideInRound(BattleSide::ATTACKER, 2));
	ASSERT_TRUE(castHeroSpell(BattleSide::ATTACKER, SpellID::MAGIC_ARROW, target));
	const int32_t magicalBeforeResurrection = target->acquireState()->getMagicalCasualties();
	ASSERT_GT(magicalBeforeResurrection, 0);
	const int32_t killedBeforeResurrection = target->getKilled();
	ASSERT_TRUE(defendActiveUnit());
	ASSERT_NE(battle()->battleActiveUnit(), nullptr);
	ASSERT_EQ(battle()->battleActiveUnit()->unitSide(), BattleSide::DEFENDER);
	ASSERT_TRUE(castHeroSpell(BattleSide::DEFENDER, SpellID::RESURRECTION, target));

	const int32_t killedAfterResurrection = target->getKilled();
	const int32_t resurrected = killedBeforeResurrection - killedAfterResurrection;
	ASSERT_GT(resurrected, 0) << "The actual Resurrection cast must restore at least one casualty";
	ASSERT_LT(resurrected, magicalBeforeResurrection)
		<< "The test needs a partial restoration of the newest magical cohort";
	const auto afterResurrection = target->acquireState();
	const int32_t magicalAfterResurrection = afterResurrection->getMagicalCasualties();
	EXPECT_EQ(magicalAfterResurrection, magicalBeforeResurrection - resurrected)
		<< "The most recent usable (magical) cohort is restored before the earlier physical deaths";
	EXPECT_EQ(killedAfterResurrection - magicalAfterResurrection, physicalKills);

	const auto snapshot = afterResurrection->save();
	const auto & cohorts = snapshot["state"]["health"]["casualtyProvenance"].Vector();
	ASSERT_EQ(cohorts.size(), 2u);
	EXPECT_EQ(cohorts[0]["provenance"].Integer(),
		static_cast<int64_t>(battle::DamageProvenance::PHYSICAL_CREATURE));
	EXPECT_EQ(cohorts[0]["count"].Integer(), physicalKills);
	EXPECT_EQ(cohorts[1]["provenance"].Integer(), static_cast<int64_t>(battle::DamageProvenance::SPELL));
	EXPECT_EQ(cohorts[1]["count"].Integer(), magicalAfterResurrection);

	const auto result = finishAndCaptureResult();
	ASSERT_TRUE(result.has_value());
	ASSERT_TRUE(result->necromancyEligibilityCaptured);
	const auto pikeman = creatureById("core:pikeman");
	EXPECT_EQ(resultCount(*result, BattleSide::DEFENDER, pikeman, false), targetStackCount);
	EXPECT_EQ(resultCount(*result, BattleSide::DEFENDER, pikeman, true),
		targetStackCount - magicalAfterResurrection)
		<< "The result preserves cause for remaining corpses while excluding their restored successors";
}
