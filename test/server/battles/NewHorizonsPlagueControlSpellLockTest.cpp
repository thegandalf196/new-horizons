/*
 * NewHorizonsPlagueControlSpellLockTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/NewHorizonsPlague.h"
#include "../../../lib/battle/NewHorizonsPuppetMaster.h"
#include "../../../lib/battle/CUnitState.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusList.h"
#include "../../../lib/bonuses/BonusSelector.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/NewHorizonsSorcery.h"

#include <optional>
#include <string>
#include <utility>

namespace
{
SpellID plagueSpell()
{
	return SpellID(SpellID::decode("new-horizons:plague"));
}

SpellID puppetSpell()
{
	return SpellID(SpellID::decode(std::string(newHorizonsPuppetMaster::SPELL_ID)));
}

SpellID lockSpell()
{
	return SpellID(SpellID::decode(std::string(newHorizonsSorcery::SPELL_LOCK_SPELL)));
}

class NewHorizonsPlagueControlSpellLockTest : public HeroCommandFixture
{
protected:
	CStack * infected = nullptr;
	CStack * spreadRecipient = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
	}

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}

	void configure(CGHeroInstance * hero)
	{
		giveArtifact(hero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		for(const auto * key : {"new-horizons:shadowMagic", "new-horizons:chaosMagic", "new-horizons:sorceryMagic"})
		{
			const auto id = SecondarySkill::decode(key);
			ASSERT_GE(id, 0);
			hero->setSecSkillLevel(SecondarySkill(id), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		}
		hero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
		hero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		for(const auto spell : {plagueSpell(), puppetSpell(), lockSpell()})
		{
			ASSERT_TRUE(spell.hasValue());
			hero->addSpellToSpellbook(spell);
		}
		setTestSpellPointTotal(hero, 1000);
	}

	void prepareControlledInfection(bool addSpreadRecipient = false)
	{
		startGame();
		ASSERT_NO_FATAL_FAILURE(configure(attackerSideHero));
		ASSERT_NO_FATAL_FAILURE(configure(defenderSideHero));
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		ASSERT_NE(addStack(BattleSide::ATTACKER, creatureByName("core:peasant"), BattleHex(3, 5), 1000), nullptr);
		infected = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 1000);
		ASSERT_NE(infected, nullptr);
		if(addSpreadRecipient)
		{
			spreadRecipient = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(11, 5), 1000);
			ASSERT_NE(spreadRecipient, nullptr);
		}
		beginCombat();
		const auto manaBefore = attackerSideHero->getManaAvailable();
		ASSERT_TRUE(castOn(attackerSideHero, plagueSpell(), infected));
		ASSERT_TRUE(newHorizonsPlague::hasPlague(infected));
		EXPECT_LT(attackerSideHero->getManaAvailable(), manaBefore);
		// Like the existing Plague composition fixture, explicitly advance the
		// round receipt to restore the next paid Hero Action. No unit activation
		// or controller effect is fabricated; this isolates cast-time polarity.
		advanceRound();
		ASSERT_TRUE(castOn(attackerSideHero, puppetSpell(), infected));
		ASSERT_TRUE(newHorizonsPuppetMaster::hasValidControlMarker(*battle(), infected));
		ASSERT_TRUE(newHorizonsPlague::hasPlague(infected));
		ASSERT_EQ(infected->unitSide(), BattleSide::DEFENDER);
		ASSERT_EQ(battle()->battleGetOwner(infected), PlayerColor(1));
		ASSERT_EQ(battle()->battleGetActionController(infected), PlayerColor(0));
	}

	std::shared_ptr<const Bonus> plagueMarker() const
	{
		const auto bonuses = infected->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(plagueSpell())).And(Selector::type()(BonusType::COMBAT_EVENT_TRIGGER)));
		return bonuses->empty() ? std::shared_ptr<const Bonus>{} : bonuses->front();
	}
};
}

TEST_F(NewHorizonsPlagueControlSpellLockTest, OriginalAllegianceFriendlySealCleansesInfectionAndControl)
{
	ASSERT_NO_FATAL_FAILURE(prepareControlledInfection());
	ASSERT_EQ(battle()->battleGetOwner(infected), defenderSideHero->getOwner());
	ASSERT_NE(battle()->battleGetActionController(infected), defenderSideHero->getOwner());
	const auto healthBefore = infected->getAvailableHealth();
	const auto manaBefore = defenderSideHero->getManaAvailable();
	ASSERT_TRUE(castOn(defenderSideHero, lockSpell(), infected));
	EXPECT_LT(defenderSideHero->getManaAvailable(), manaBefore);
	EXPECT_FALSE(newHorizonsPlague::hasPlague(infected));
	EXPECT_FALSE(newHorizonsPuppetMaster::hasControlMarker(infected));
	EXPECT_EQ(battle()->battleGetOwner(infected), PlayerColor(1));
	EXPECT_EQ(battle()->battleGetActionController(infected), PlayerColor(1));
	ASSERT_TRUE(newHorizonsSorcery::isSpellLocked(*infected));
	EXPECT_EQ(newHorizonsSorcery::spellLockPreservesBeneficial(*infected), std::optional<bool>(true));
	EXPECT_EQ(infected->getAvailableHealth(), healthBefore);
	EXPECT_EQ(server.castsOf(plagueSpell()).size(), 1u);
	EXPECT_EQ(server.castsOf(puppetSpell()).size(), 1u);
	EXPECT_EQ(server.castsOf(lockSpell()).size(), 1u);
}

TEST_F(NewHorizonsPlagueControlSpellLockTest, PuppetInputControllerHostileSealPreservesInfectionAndControl)
{
	ASSERT_NO_FATAL_FAILURE(prepareControlledInfection());
	// Puppet changes input authority, not the hostile allegiance used by Lock.
	ASSERT_NE(battle()->battleGetOwner(infected), attackerSideHero->getOwner());
	ASSERT_EQ(battle()->battleGetActionController(infected), attackerSideHero->getOwner());
	advanceRound();
	const auto before = plagueMarker();
	ASSERT_NE(before, nullptr);
	const auto turnsBefore = before->turnsRemain;
	const auto manaBefore = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(castOn(attackerSideHero, lockSpell(), infected));
	EXPECT_LT(attackerSideHero->getManaAvailable(), manaBefore);
	ASSERT_TRUE(newHorizonsPlague::hasPlague(infected));
	ASSERT_NE(plagueMarker(), nullptr);
	EXPECT_EQ(plagueMarker()->turnsRemain, turnsBefore);
	EXPECT_TRUE(newHorizonsPuppetMaster::hasValidControlMarker(*battle(), infected));
	EXPECT_EQ(battle()->battleGetOwner(infected), PlayerColor(1));
	EXPECT_EQ(battle()->battleGetActionController(infected), PlayerColor(0));
	ASSERT_TRUE(newHorizonsSorcery::isSpellLocked(*infected));
	EXPECT_EQ(newHorizonsSorcery::spellLockPreservesBeneficial(*infected), std::optional<bool>(false));
	EXPECT_TRUE(newHorizonsSorcery::durationPausedBySpellLock(*infected, *plagueMarker()));
	EXPECT_EQ(server.castsOf(plagueSpell()).size(), 1u);
	EXPECT_EQ(server.castsOf(puppetSpell()).size(), 1u);
	EXPECT_EQ(server.castsOf(lockSpell()).size(), 1u);
}

TEST_F(NewHorizonsPlagueControlSpellLockTest, ControlledCompletionTicksLockedPlagueOnceThenExpiresSealAndFinalInfection)
{
	ASSERT_NO_FATAL_FAILURE(prepareControlledInfection(true));
	advanceRound(); // Restore the paid controller's Hero Action for the hostile seal.
	ASSERT_TRUE(castOn(attackerSideHero, lockSpell(), infected));
	ASSERT_TRUE(newHorizonsSorcery::isSpellLocked(*infected));
	ASSERT_NE(plagueMarker(), nullptr);
	ASSERT_TRUE(newHorizonsSorcery::durationPausedBySpellLock(*infected, *plagueMarker()));
	const auto initialRemaining = plagueMarker()->turnsRemain;
	ASSERT_GT(initialRemaining, 0);
	const auto originalOwner = battle()->battleGetOwner(infected);
	const auto controller = battle()->battleGetActionController(infected);
	ASSERT_NE(controller, originalOwner);
	ASSERT_FALSE(newHorizonsPlague::hasPlague(spreadRecipient));

	// Every activation below is reached through accepted ordinary actions; no
	// activeStack, round, status duration, or lastProcessedRound is overwritten.
	const auto reachInfected = [&]()
	{
		const size_t limit = battle()->stacks.size() * 12 + 12;
		for(size_t step = 0; step < limit; ++step)
		{
			const auto * active = battle()->battleActiveUnit();
			if(!active) return false;
			if(active->unitId() == infected->unitId()) return true;
			if(!gameHandler->battles->makePlayerBattleAction(BattleID(0),
				battle()->battleGetActionController(active), BattleAction::makeDefend(active)))
				return false;
		}
		return false;
	};
	const auto countTargetHits = [&](size_t first)
	{
		std::pair<size_t, int64_t> result{};
		for(size_t index = first; index < server.injuries.size(); ++index)
			for(const auto & hit : server.injuries[index].stacks)
				if(hit.stackAttacked == infected->unitId() && hit.spellID == plagueSpell())
				{
					++result.first;
					result.second += hit.damageAmount;
				}
		return result;
	};
	ASSERT_TRUE(reachInfected());
	ASSERT_EQ(battle()->battleGetActionController(infected), controller);
	const auto initialState = infected->acquireState()->save();
	const auto startedBeforeWrongOwner = server.startedActions.size();
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), originalOwner,
		BattleAction::makeDefend(infected)));
	EXPECT_EQ(server.startedActions.size(), startedBeforeWrongOwner);
	EXPECT_EQ(infected->acquireState()->save(), initialState);

	const auto waitRound = battle()->getRound();
	const auto healthBeforeWait = infected->getAvailableHealth();
	const auto markerBeforeWait = plagueMarker()->toJsonNode();
	const auto injuriesBeforeWait = server.injuries.size();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), controller,
		BattleAction::makeWait(infected)));
	EXPECT_EQ(countTargetHits(injuriesBeforeWait), (std::pair<size_t, int64_t>{0, 0}));
	EXPECT_EQ(infected->getAvailableHealth(), healthBeforeWait);
	ASSERT_NE(plagueMarker(), nullptr);
	EXPECT_EQ(plagueMarker()->toJsonNode(), markerBeforeWait);
	EXPECT_TRUE(newHorizonsPuppetMaster::hasValidControlMarker(*battle(), infected));
	EXPECT_FALSE(newHorizonsPuppetMaster::hasLucidity(infected));
	ASSERT_TRUE(reachInfected());
	ASSERT_EQ(battle()->getRound(), waitRound);

	int lockedCompletions = 0;
	int unsealedCompletions = 0;
	int32_t previousCompletedRound = -1;
	for(int completion = 0; completion < initialRemaining + 5; ++completion)
	{
		ASSERT_TRUE(reachInfected());
		const auto before = plagueMarker();
		ASSERT_NE(before, nullptr);
		const auto round = battle()->getRound();
		ASSERT_GT(round, previousCompletedRound);
		previousCompletedRound = round;
		const bool sealed = newHorizonsSorcery::isSpellLocked(*infected);
		ASSERT_EQ(newHorizonsSorcery::durationPausedBySpellLock(*infected, *before), sealed);
		ASSERT_NE(before->parameters, nullptr);
		auto parameters = before->parameters->toCustom<JsonNode>();
		const auto expectedDamage = newHorizonsPlague::adjustedTickDamage(*battle(), BattleSide::ATTACKER,
			infected, before->val, &parameters["mdrPenetration"]);
		ASSERT_GT(expectedDamage, 0);
		const auto health = infected->getAvailableHealth();
		ASSERT_GT(health, expectedDamage); // Final removal must be expiry, not death.
		const auto previousAttempts = parameters["spreadAttempts"].Integer();
		const auto firstInjury = server.injuries.size();
		const auto firstAction = server.startedActions.size();
		const auto action = BattleAction::makeDefend(infected);
		const auto actionController = battle()->battleGetActionController(infected);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), actionController, action));
		EXPECT_EQ(server.startedActions.size(), firstAction + 1);
		EXPECT_EQ(countTargetHits(firstInjury), (std::pair<size_t, int64_t>{1, expectedDamage}));
		EXPECT_EQ(infected->getAvailableHealth(), health - expectedDamage);
		EXPECT_FALSE(newHorizonsPuppetMaster::hasControlMarker(infected));
		EXPECT_EQ(battle()->battleGetActionController(infected), originalOwner);
		if(completion == 0)
		{
			EXPECT_TRUE(newHorizonsPuppetMaster::hasLucidity(infected));
			EXPECT_TRUE(newHorizonsPlague::hasPlague(spreadRecipient));
			EXPECT_EQ(actionController, controller);
		}
		if(sealed)
			++lockedCompletions;
		else
			++unsealedCompletions;
		const auto after = plagueMarker();
		if(sealed || before->turnsRemain > 1)
		{
			ASSERT_NE(after, nullptr);
			ASSERT_NE(after->parameters, nullptr);
			EXPECT_EQ(after->turnsRemain, before->turnsRemain - (sealed ? 0 : 1));
			EXPECT_EQ(after->parameters->toCustom<JsonNode>()["lastProcessedRound"].Integer(), round);
			EXPECT_EQ(after->parameters->toCustom<JsonNode>()["spreadAttempts"].Integer(), previousAttempts + 1);
		}
		else
			EXPECT_EQ(after, nullptr);
		// Within the same round a completed Defend is stale. Across a new round
		// the same payload may be a legal new activation, not a duplicate receipt.
		if(battle()->getRound() == round)
		{
			const auto stateBeforeDuplicate = infected->acquireState()->save();
			const auto actionCount = server.startedActions.size();
			const auto injuryCount = server.injuries.size();
			EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), actionController, action));
			EXPECT_EQ(server.startedActions.size(), actionCount);
			EXPECT_EQ(server.injuries.size(), injuryCount);
			EXPECT_EQ(infected->acquireState()->save(), stateBeforeDuplicate);
		}
		if(!after) break;
	}
	EXPECT_GT(lockedCompletions, 0);
	EXPECT_EQ(unsealedCompletions, initialRemaining);
	EXPECT_FALSE(newHorizonsSorcery::isSpellLocked(*infected));
	EXPECT_EQ(plagueMarker(), nullptr);
	EXPECT_TRUE(infected->alive());
	EXPECT_EQ(server.castsOf(plagueSpell()).size(), 1u);
	EXPECT_EQ(server.castsOf(puppetSpell()).size(), 1u);
	EXPECT_EQ(server.castsOf(lockSpell()).size(), 1u);
}
