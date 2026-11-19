/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "BattleTestFixture.h"
#include "../../SpellPointTestUtils.h"
#include "../../../lib/battle/NewHorizonsSoulHarvest.h"
#include "../../../lib/battle/BattleLayout.h"
#include "../../../lib/CStack.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/modding/CModHandler.h"

class NewHorizonsSoulHarvestTest : public BattleTestFixture
{
protected:
	bool captured = true;
	bool vidomina = true;
	bool prepareForgetfulness = false;
	bool useVulnerabilityOrb = true;
	int attackerBulwark = 0;
	int defenderBulwark = 0;
	CStack * ghost = nullptr;
	CStack * target = nullptr;
	CStack * spare = nullptr;
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires curated New Horizons preset";
	}
	void mapLoaded(CMap * loaded) override
	{
		BattleTestFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		if(!captured)
		{
			rules.Struct().erase("ghostSpecialty");
			auto & old = rules["reclamationSpecialty"];
			old["version"].Integer() = 1;
			old["conversionPercentPerLevel"].Integer() = 5;
			old["conversionMaximumPercent"].Integer() = 100;
		}
		rules.setOverrideFlag(true);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
		for(const auto & object : loaded->objects)
			if(auto * hero = dynamic_cast<CGHeroInstance *>(object.get()); hero && hero->getOwner() == PlayerColor(0))
				hero->setHeroType(HeroTypeID(HeroTypeID::decode(vidomina ? "core:vidomina" : "core:charna")));
	}
	void prepare(int count = 10, int victims = 1)
	{
		startGame();
		const auto bulwark = SecondarySkill(SecondarySkill::decode("new-horizons:bulwarkOfTheMire"));
		attackerSideHero->setSecSkillLevel(bulwark, attackerBulwark, ChangeValueMode::ABSOLUTE);
		defenderSideHero->setSecSkillLevel(bulwark, defenderBulwark, ChangeValueMode::ABSOLUTE);
		if(prepareForgetfulness)
		{
			giveArtifact(defenderSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
			defenderSideHero->addSpellToSpellbook(SpellID::FORGETFULNESS);
			defenderSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 0, ChangeValueMode::ABSOLUTE);
			defenderSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 1000, ChangeValueMode::ABSOLUTE);
			setTestSpellPointTotal(defenderSideHero, 1000);
			if(useVulnerabilityOrb)
				giveArtifact(defenderSideHero, ArtifactID(ArtifactID::decode("core:orbOfVulnerability")),
					ArtifactPosition::MISC1);
		}
		// Synthetic combat quantities retain the original test arithmetic. These
		// are original army slots, not acquisition/capacity-command evidence;
		// ProductionCasualtyCommit below separately uses a legal five-Ghost army.
		attackerSideHero->clearSlots();
		defenderSideHero->clearSlots();
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creatureByName("new-horizons:ghost"), count));
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), creatureByName("core:pikeman"), victims));
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(1), creatureByName("core:pikeman"), 1));
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(2), creatureByName("core:peasant"), 1000));
		startBattle();
		ghost = originalStack(BattleSide::ATTACKER, SlotID(0));
		target = originalStack(BattleSide::DEFENDER, SlotID(0));
		spare = originalStack(BattleSide::DEFENDER, SlotID(1));
		auto * reserve = originalStack(BattleSide::DEFENDER, SlotID(2));
		ASSERT_NE(ghost, nullptr);
		ASSERT_NE(target, nullptr);
		ASSERT_NE(spare, nullptr);
		ASSERT_NE(reserve, nullptr);
		ghost->setPosition(BattleHex(leftHex));
		target->setPosition(BattleHex(rightHex));
		spare->setPosition(BattleHex(rightHex + 1));
		reserve->setPosition(BattleHex(rightHex + 4));
		forceMaximumDamage(ghost);
		blockRetaliation(target);
		beginCombat();
		ASSERT_TRUE(newHorizonsSoulHarvest::qualifies(*battle(), *ghost, *target, true));
	}
	CStack * originalStack(BattleSide side, SlotID slot) const
	{
		const auto units = battle()->battleGetStacksIf([=](const CStack * unit)
		{
			return unit->unitSide() == side && unit->unitSlot() == slot;
		});
		EXPECT_EQ(units.size(), 1u);
		if(units.size() != 1)
			return nullptr;
		auto * result = battle()->getStack(units.front()->unitId());
		const auto * army = side == BattleSide::ATTACKER ? attackerSideHero : defenderSideHero;
		EXPECT_EQ(result->base, &army->getStack(slot));
		EXPECT_FALSE(result->isSummoned());
		return result;
	}
};
