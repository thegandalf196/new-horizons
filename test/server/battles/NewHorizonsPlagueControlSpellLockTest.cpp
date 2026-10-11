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
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusList.h"
#include "../../../lib/bonuses/BonusSelector.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/NewHorizonsSorcery.h"

#include <optional>
#include <string>

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

	void prepareControlledInfection()
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
