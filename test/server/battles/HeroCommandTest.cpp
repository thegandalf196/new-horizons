/*
 * HeroCommandTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "FullGameSnapshotTypes.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/SideInBattle.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../../lib/battle/Unit.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"

class HeroCommandTest : public HeroCommandFixture {};

class ShockAssaultTest : public HeroCommandFixture
{
protected:
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void prepareShockAssault(bool selectPerk)
	{
		prepareCommands();
		const auto decoded = SecondarySkill::decode("new-horizons:offense");
		ASSERT_GE(decoded, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(decoded), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		if(selectPerk)
			attackerSideHero->applyPerkSelection({
				"new-horizons:offense", "new-horizons:offense.shockAssault"});
		ASSERT_EQ(attackerSideHero->hasActivePerk(
			"new-horizons:offense", "new-horizons:offense.shockAssault"), selectPerk);
	}
};

class CounterchargeTest : public HeroCommandFixture
{
protected:
	bool enableWarcasting = false;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		if(enableWarcasting)
		{
			auto magicRules = JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
			magicRules["warcasting"] = JsonNode(true);
			loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magicRules);
		}
	}

	void prepareCountercharge(bool selectPerk, bool prepareWarcasting = false)
	{
		enableWarcasting = prepareWarcasting;
		if(prepareWarcasting)
		{
			startGame();
			const int warcasting = SecondarySkill::decode("new-horizons:warcasting");
			ASSERT_GE(warcasting, 0);
			attackerSideHero->setSecSkillLevel(SecondarySkill(warcasting), MasteryLevel::BASIC,
				ChangeValueMode::ABSOLUTE);
			attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 1000, ChangeValueMode::ABSOLUTE);
			setTestSpellPointTotal(attackerSideHero, 1000);
			giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
			attackerSideHero->addSpellToSpellbook(SpellID::HASTE);
			startBattle();
			beginCombat();
		}
		else
			prepareCommands();
		const int decoded = SecondarySkill::decode("new-horizons:armorer");
		ASSERT_GE(decoded, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(decoded), MasteryLevel::BASIC,
			ChangeValueMode::ABSOLUTE);
		if(selectPerk)
			attackerSideHero->applyPerkSelection({"new-horizons:armorer", "new-horizons:armorer.countercharge"});
		EXPECT_EQ(attackerSideHero->hasActivePerk("new-horizons:armorer", "new-horizons:armorer.countercharge"),
			selectPerk);
	}
};

class EncirclementTest : public HeroCommandFixture
{
protected:
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void prepareEncirclement()
	{
		startGame();
		const int decoded = SecondarySkill::decode("new-horizons:offense");
		ASSERT_GE(decoded, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(decoded), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		ASSERT_FALSE(attackerSideHero->hasActivePerk(
			"new-horizons:offense", "new-horizons:offense.encirclement"));
		startBattle();
		beginCombat();
	}
};

class ShieldMasterTest : public HeroCommandFixture
{
protected:
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void prepareShieldMaster(bool selectPerk = true)
	{
		startGame();
		const int armorer = SecondarySkill::decode("new-horizons:armorer");
		ASSERT_GE(armorer, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(armorer), MasteryLevel::BASIC,
			ChangeValueMode::ABSOLUTE);
		if(selectPerk)
			attackerSideHero->applyPerkSelection({"new-horizons:armorer", "new-horizons:armorer.shieldMaster"});
		ASSERT_EQ(attackerSideHero->hasActivePerk("new-horizons:armorer", "new-horizons:armorer.shieldMaster"),
			selectPerk);
		startBattle();
		beginCombat();
	}
};

class IronDisciplineTest : public HeroCommandFixture
{
protected:
	bool enableWarcasting = false;

	void attachCombatScript(CStack * unit, const std::string & scriptName, int value)
	{
		const auto script = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "script", scriptName);
		ASSERT_TRUE(script.has_value());
		unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::COMBAT_EVENT_TRIGGER, BonusSource::OTHER, value, BonusSourceID(),
			BonusSubtypeID(ScriptID(*script))));
	}

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		if(enableWarcasting)
		{
			auto magicRules = JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
			magicRules["warcasting"] = JsonNode(true);
			loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magicRules);
		}
	}

	void prepareIronDiscipline(bool selectPerk = true, bool prepareWarcasting = false)
	{
		enableWarcasting = prepareWarcasting;
		startGame();
		const int armorer = SecondarySkill::decode(newHorizonsIronDiscipline::SKILL);
		ASSERT_GE(armorer, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(armorer), MasteryLevel::BASIC,
			ChangeValueMode::ABSOLUTE);
		if(selectPerk)
			attackerSideHero->applyPerkSelection({newHorizonsIronDiscipline::SKILL,
				newHorizonsIronDiscipline::PERK});
		ASSERT_EQ(attackerSideHero->hasActivePerk(newHorizonsIronDiscipline::SKILL,
			newHorizonsIronDiscipline::PERK), selectPerk);

		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 1000, ChangeValueMode::ABSOLUTE);
		defenderSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 1000, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);
		setTestSpellPointTotal(defenderSideHero, 1000);
		giveArtifact(defenderSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		defenderSideHero->addSpellToSpellbook(SpellID::MAGIC_ARROW);
		if(prepareWarcasting)
		{
			const int warcasting = SecondarySkill::decode("new-horizons:warcasting");
			ASSERT_GE(warcasting, 0);
			attackerSideHero->setSecSkillLevel(SecondarySkill(warcasting), MasteryLevel::BASIC,
				ChangeValueMode::ABSOLUTE);
			giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
			attackerSideHero->addSpellToSpellbook(SpellID::HASTE);
		}
		startBattle();
		beginCombat();

		if(prepareWarcasting)
		{
			BattleAction spell;
			spell.actionType = EActionType::HERO_SPELL;
			spell.side = BattleSide::ATTACKER;
			spell.spell = SpellID::HASTE;
			spell.aimToUnit(battle()->battleActiveUnit());
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), spell));
			advanceRound();
		}
	}
};

TEST_F(EncirclementTest, EncirclementChangesOnlyAdditionalSideDamageAfterARecordedFlank)
{
	prepareEncirclement();

	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 100);
	auto * secondAttacker = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(54), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(71), 100);
	ASSERT_NE(attacker, nullptr);
	ASSERT_NE(secondAttacker, nullptr);
	ASSERT_NE(defender, nullptr);
	EXPECT_EQ(battle()->battleHeroOrderFlankAdditionalSidePercent(BattleSide::ATTACKER), 4);

	const BattleAttackInfo firstAttack(attacker, defender, 0, false);
	const BattleAttackInfo secondAttack(secondAttacker, defender, 0, false);
	const auto firstWithoutOrder = battle()->calculateDmgRange(firstAttack).damage.min;
	const auto secondWithoutOrder = battle()->calculateDmgRange(secondAttack).damage.min;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeTargetedHeroCommand(BattleSide::ATTACKER, HeroCommand::FLANK, defender->unitId())));

	const auto firstSide = battle()->battleHeroOrderFlankSide(attacker, defender);
	ASSERT_NE(firstSide, 0);
	const auto firstFlankDamage = battle()->calculateDmgRange(firstAttack);
	EXPECT_GT(firstFlankDamage.damage.min, firstWithoutOrder);
	EXPECT_EQ(firstFlankDamage.attackerOrderCause, HeroCommand::FLANK);

	blockRetaliation(attacker);
	const int32_t attackerCountBeforeStrike = attacker->getCount();
	ASSERT_TRUE(this->attack(attacker, defender->getPosition()));
	ASSERT_EQ(attacker->getCount(), attackerCountBeforeStrike)
		<< "The repeated-side comparison requires the attacking stack to survive unchanged";
	const auto afterFirstAttack = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(afterFirstAttack);
	const auto * flank = afterFirstAttack->flankFor(defender->unitId());
	ASSERT_NE(flank, nullptr);
	EXPECT_EQ(flank->sideMask, firstSide);

	const auto repeatedSideBeforeSelection = battle()->calculateDmgRange(firstAttack);
	EXPECT_EQ(repeatedSideBeforeSelection.damage.min, firstFlankDamage.damage.min);
	EXPECT_EQ(repeatedSideBeforeSelection.damage.max, firstFlankDamage.damage.max);
	EXPECT_EQ(repeatedSideBeforeSelection.attackerOrderCause, HeroCommand::FLANK);

	const auto secondSide = battle()->battleHeroOrderFlankSide(secondAttacker, defender);
	ASSERT_NE(secondSide, 0);
	const auto newlyContactingSides = static_cast<uint8_t>(secondSide & ~flank->sideMask);
	ASSERT_NE(newlyContactingSides, 0);
	const auto extraSideBeforeSelection = battle()->calculateDmgRange(secondAttack);
	EXPECT_GT(extraSideBeforeSelection.damage.min, secondWithoutOrder);
	EXPECT_EQ(extraSideBeforeSelection.attackerOrderCause, HeroCommand::FLANK);

	attackerSideHero->applyPerkSelection({"new-horizons:offense", "new-horizons:offense.encirclement"});
	ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:offense", "new-horizons:offense.encirclement"));
	EXPECT_EQ(battle()->battleHeroOrderFlankAdditionalSidePercent(BattleSide::ATTACKER), 7);
	const auto repeatedSideAfterSelection = battle()->calculateDmgRange(firstAttack);
	EXPECT_EQ(repeatedSideAfterSelection.damage.min, repeatedSideBeforeSelection.damage.min);
	EXPECT_EQ(repeatedSideAfterSelection.damage.max, repeatedSideBeforeSelection.damage.max);
	const auto extraSideAfterSelection = battle()->calculateDmgRange(secondAttack);
	EXPECT_GT(extraSideAfterSelection.damage.min, extraSideBeforeSelection.damage.min)
		<< "Encirclement must affect a newly distinct side in the authoritative damage calculation";
	EXPECT_EQ(extraSideAfterSelection.attackerOrderCause, HeroCommand::FLANK);
	EXPECT_EQ(battle()->battleGetHeroOrderState(BattleSide::ATTACKER)->flankFor(defender->unitId())->sideMask,
		firstSide) << "Hypothetical damage estimation must not record a Flank approach";
}

TEST_F(EncirclementTest, FlankProjectedAttackerPositionMatchesMovedGeometryAndDamage)
{
	prepareEncirclement();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(70), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(71), 100);
	ASSERT_NE(attacker, nullptr);
	ASSERT_NE(defender, nullptr);

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeTargetedHeroCommand(BattleSide::ATTACKER, HeroCommand::FLANK, defender->unitId())));
	const auto firstContact = battle()->battleHeroOrderFlankSide(attacker, defender);
	ASSERT_NE(firstContact, 0);
	blockRetaliation(attacker);
	const int32_t attackerCountBeforeStrike = attacker->getCount();
	ASSERT_TRUE(this->attack(attacker, defender->getPosition()));
	ASSERT_EQ(attacker->getCount(), attackerCountBeforeStrike)
		<< "A Flank preview fixture must not lose attackers to retaliation";
	const auto savedOrder = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(savedOrder);
	ASSERT_NE(savedOrder->flankFor(defender->unitId()), nullptr);
	const uint8_t recordedMask = savedOrder->flankFor(defender->unitId())->sideMask;
	ASSERT_EQ(recordedMask, firstContact);

	const auto repeatedAttack = BattleAttackInfo(attacker, defender, 0, false);
	const int repeatedPercent = battle()->battleHeroOrderFlankMeleeDamagePercent(repeatedAttack);
	ASSERT_GT(repeatedPercent, 0);
	const auto repeatedDamage = battle()->calculateDmgRange(repeatedAttack);
	ASSERT_EQ(repeatedDamage.attackerOrderCause, HeroCommand::FLANK);

	const auto freeFootprint = [this](const battle::Unit * moving, const BattleHex & position)
	{
		bool hasAvailableHex = false;
		for(const auto & hex : moving->getHexes(position))
		{
			if(!hex.isValid())
				continue;
			if(!hex.isAvailable())
				return false;
			hasAvailableHex = true;
			const auto * occupant = battle()->battleGetUnitByPos(hex);
			if(occupant && occupant->unitId() != moving->unitId())
				return false;
		}
		return hasAvailableHex;
	};
	std::optional<BattleHex> projectedPosition;
	for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
	{
		const BattleHex position(index);
		if(position == attacker->getPosition() || !freeFootprint(attacker, position))
			continue;
		const auto projectedMask = battle()->battleHeroOrderFlankSide(attacker, defender,
			position, BattleHex::INVALID);
		if((projectedMask & static_cast<uint8_t>(~recordedMask)) != 0)
		{
			projectedPosition = position;
			break;
		}
	}
	ASSERT_TRUE(projectedPosition) << "The marked target should be reachable from an unrecorded side";

	BattleAttackInfo projectedAttack(attacker, defender, 0, false);
	projectedAttack.attackerPos = *projectedPosition;
	const auto originalAttackerPosition = attacker->getPosition();
	const auto originalDefenderPosition = defender->getPosition();
	const auto projectedMask = battle()->battleHeroOrderFlankSide(attacker, defender,
		projectedAttack.attackerPos, projectedAttack.defenderPos);
	const int projectedPercent = battle()->battleHeroOrderFlankMeleeDamagePercent(projectedAttack);
	const auto projectedDamage = battle()->calculateDmgRange(projectedAttack);
	EXPECT_NE(projectedMask & static_cast<uint8_t>(~recordedMask), 0);
	EXPECT_GT(projectedPercent, repeatedPercent);
	EXPECT_GT(projectedDamage.damage.min, repeatedDamage.damage.min);
	EXPECT_EQ(attacker->getPosition(), originalAttackerPosition);
	EXPECT_EQ(defender->getPosition(), originalDefenderPosition);
	EXPECT_EQ(battle()->battleGetHeroOrderState(BattleSide::ATTACKER)->flankFor(defender->unitId())->sideMask,
		recordedMask) << "A projected attack must not record a Flank side";

	auto movedState = attacker->acquireState();
	movedState->setPosition(*projectedPosition);
	BattleUnitsChanged moved;
	moved.battleID = BattleID(0);
	UnitChanges update(attacker->unitId(), UnitChanges::EOperation::UPDATE);
	update.data = movedState->save();
	moved.changedStacks.push_back(std::move(update));
	gameHandler->sendAndApply(moved);

	const auto movedSideMask = battle()->battleHeroOrderFlankSide(attacker, defender);
	const auto movedAttack = BattleAttackInfo(attacker, defender, 0, false);
	const auto movedDamage = battle()->calculateDmgRange(movedAttack);
	EXPECT_EQ(movedSideMask, projectedMask);
	EXPECT_EQ(battle()->battleHeroOrderFlankMeleeDamagePercent(movedAttack), projectedPercent);
	EXPECT_EQ(movedDamage.damage.min, projectedDamage.damage.min);
	EXPECT_EQ(movedDamage.damage.max, projectedDamage.damage.max);
	EXPECT_EQ(battle()->battleGetHeroOrderState(BattleSide::ATTACKER)->flankFor(defender->unitId())->sideMask,
		recordedMask) << "Moving without attacking must preserve accepted side history";
}

TEST_F(EncirclementTest, FlankProjectedDefenderPositionMatchesMovedGeometryAndDamage)
{
	prepareEncirclement();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(70), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(71), 100);
	ASSERT_NE(attacker, nullptr);
	ASSERT_NE(defender, nullptr);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeTargetedHeroCommand(BattleSide::ATTACKER, HeroCommand::FLANK, defender->unitId())));
	const auto firstContact = battle()->battleHeroOrderFlankSide(attacker, defender);
	ASSERT_NE(firstContact, 0);
	blockRetaliation(attacker);
	const int32_t attackerCountBeforeStrike = attacker->getCount();
	ASSERT_TRUE(this->attack(attacker, defender->getPosition()));
	ASSERT_EQ(attacker->getCount(), attackerCountBeforeStrike)
		<< "A Flank preview fixture must not lose attackers to retaliation";
	const auto savedOrder = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(savedOrder);
	ASSERT_NE(savedOrder->flankFor(defender->unitId()), nullptr);
	const uint8_t recordedMask = savedOrder->flankFor(defender->unitId())->sideMask;
	ASSERT_EQ(recordedMask, firstContact);

	const auto repeatedAttack = BattleAttackInfo(attacker, defender, 0, false);
	const int repeatedPercent = battle()->battleHeroOrderFlankMeleeDamagePercent(repeatedAttack);
	const auto repeatedDamage = battle()->calculateDmgRange(repeatedAttack);

	const auto freeFootprint = [this](const battle::Unit * moving, const BattleHex & position)
	{
		bool hasAvailableHex = false;
		for(const auto & hex : moving->getHexes(position))
		{
			if(!hex.isValid())
				continue;
			if(!hex.isAvailable())
				return false;
			hasAvailableHex = true;
			const auto * occupant = battle()->battleGetUnitByPos(hex);
			if(occupant && occupant->unitId() != moving->unitId())
				return false;
		}
		return hasAvailableHex;
	};
	std::optional<BattleHex> projectedPosition;
	for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
	{
		const BattleHex position(index);
		if(position == defender->getPosition() || !freeFootprint(defender, position))
			continue;
		const auto projectedMask = battle()->battleHeroOrderFlankSide(attacker, defender,
			BattleHex::INVALID, position);
		if((projectedMask & static_cast<uint8_t>(~recordedMask)) != 0)
		{
			projectedPosition = position;
			break;
		}
	}
	ASSERT_TRUE(projectedPosition) << "A proposed defender position should expose an unrecorded side";

	BattleAttackInfo projectedAttack(attacker, defender, 0, false);
	projectedAttack.defenderPos = *projectedPosition;
	const auto originalAttackerPosition = attacker->getPosition();
	const auto originalDefenderPosition = defender->getPosition();
	const auto projectedMask = battle()->battleHeroOrderFlankSide(attacker, defender,
		projectedAttack.attackerPos, projectedAttack.defenderPos);
	const int projectedPercent = battle()->battleHeroOrderFlankMeleeDamagePercent(projectedAttack);
	const auto projectedDamage = battle()->calculateDmgRange(projectedAttack);
	EXPECT_NE(projectedMask & static_cast<uint8_t>(~recordedMask), 0);
	EXPECT_GT(projectedPercent, repeatedPercent);
	EXPECT_GT(projectedDamage.damage.min, repeatedDamage.damage.min);
	EXPECT_EQ(attacker->getPosition(), originalAttackerPosition);
	EXPECT_EQ(defender->getPosition(), originalDefenderPosition);
	EXPECT_EQ(battle()->battleGetHeroOrderState(BattleSide::ATTACKER)->flankFor(defender->unitId())->sideMask,
		recordedMask) << "A projected defender position must not record a Flank side";

	auto movedState = defender->acquireState();
	movedState->setPosition(*projectedPosition);
	BattleUnitsChanged moved;
	moved.battleID = BattleID(0);
	UnitChanges update(defender->unitId(), UnitChanges::EOperation::UPDATE);
	update.data = movedState->save();
	moved.changedStacks.push_back(std::move(update));
	gameHandler->sendAndApply(moved);

	const auto movedSideMask = battle()->battleHeroOrderFlankSide(attacker, defender);
	const auto movedAttack = BattleAttackInfo(attacker, defender, 0, false);
	const auto movedDamage = battle()->calculateDmgRange(movedAttack);
	EXPECT_EQ(movedSideMask, projectedMask);
	EXPECT_EQ(battle()->battleHeroOrderFlankMeleeDamagePercent(movedAttack), projectedPercent);
	EXPECT_EQ(movedDamage.damage.min, projectedDamage.damage.min);
	EXPECT_EQ(movedDamage.damage.max, projectedDamage.damage.max);
	EXPECT_EQ(battle()->battleGetHeroOrderState(BattleSide::ATTACKER)->flankFor(defender->unitId())->sideMask,
		recordedMask) << "Moving without attacking must preserve accepted side history";
}

TEST_F(HeroCommandTest, WideFlankContactReportsEachDistinctContactSide)
{
	ASSERT_NO_FATAL_FAILURE(prepareCommands());
	const auto blackDragon = creatureByName("core:blackDragon");
	ASSERT_TRUE(blackDragon.toCreature()->isDoubleWide());

	const auto cellsAreFree = [this](const BattleHexArray & cells)
	{
		bool hasValidCell = false;
		for(const auto & cell : cells)
		{
			if(!cell.isValid())
				continue;
			hasValidCell = true;
			if(battle()->battleGetUnitByPos(cell))
				return false;
		}
		return hasValidCell;
	};
	const auto contactMask = [](const BattleHexArray & attackerCells, const BattleHexArray & defenderCells)
	{
		uint8_t mask = 0;
		for(const auto & attackerHex : attackerCells)
			for(const auto & defenderHex : defenderCells)
				if(attackerHex.isValid() && defenderHex.isValid()
					&& BattleHex::getDistance(defenderHex, attackerHex) == 1)
				{
					const auto direction = BattleHex::mutualPosition(defenderHex, attackerHex);
					if(direction >= BattleHex::TOP_LEFT && direction <= BattleHex::LEFT)
						mask |= static_cast<uint8_t>(1u << static_cast<unsigned>(direction));
				}
		return mask;
	};
	const auto sideCount = [](uint8_t mask)
	{
		int count = 0;
		for(auto bits = mask; bits; bits &= static_cast<uint8_t>(bits - 1))
			++count;
		return count;
	};

	std::optional<std::pair<BattleHex, BattleHex>> positions;
	for(int attackerIndex = 0; attackerIndex < GameConstants::BFIELD_SIZE && !positions; ++attackerIndex)
	{
		const BattleHex attackerPosition(attackerIndex);
		const auto & attackerCells = battle::Unit::getHexes(attackerPosition, true, BattleSide::ATTACKER);
		if(attackerCells.size() != 2 || !cellsAreFree(attackerCells))
			continue;
		for(int defenderIndex = 0; defenderIndex < GameConstants::BFIELD_SIZE; ++defenderIndex)
		{
			const BattleHex defenderPosition(defenderIndex);
			const auto & defenderCells = battle::Unit::getHexes(defenderPosition, false, BattleSide::DEFENDER);
			if(!cellsAreFree(defenderCells))
				continue;
			bool overlaps = false;
			for(const auto & attackerHex : attackerCells)
				for(const auto & defenderHex : defenderCells)
					if(attackerHex.isValid() && defenderHex.isValid() && attackerHex == defenderHex)
						overlaps = true;
			if(overlaps || sideCount(contactMask(attackerCells, defenderCells)) < 2)
				continue;
			positions = std::make_pair(attackerPosition, defenderPosition);
			break;
		}
	}
	ASSERT_TRUE(positions) << "A wide stack should be able to contact one target from multiple hex sides";

	auto * wide = addStack(BattleSide::ATTACKER, blackDragon, positions->first, 1);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("angel"), positions->second, 1);
	ASSERT_NE(wide, nullptr);
	ASSERT_NE(target, nullptr);
	const auto actualMask = battle()->battleHeroOrderFlankSide(wide, target);
	EXPECT_GE(sideCount(actualMask), 2);
	EXPECT_EQ(actualMask, contactMask(wide->getHexes(), target->getHexes()));
	std::optional<BattleHex> projectedPosition;
	for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
	{
		const BattleHex position(index);
		if(position == wide->getPosition())
			continue;
		const auto & cells = wide->getHexes(position);
		const bool completeFootprint = std::ranges::all_of(cells, [](const BattleHex & cell)
		{
			return cell.isAvailable();
		});
		if(cells.size() != 2 || !completeFootprint || !cellsAreFree(cells))
			continue;
		const auto projectedMask = battle()->battleHeroOrderFlankSide(wide, target,
			position, target->getPosition());
		if(projectedMask != 0 && projectedMask != actualMask)
		{
			projectedPosition = position;
			break;
		}
	}
	ASSERT_TRUE(projectedPosition) << "A double-wide unit should have a second legal projected contact position";
	const auto originalWidePosition = wide->getPosition();
	const auto originalTargetPosition = target->getPosition();
	const auto projectedMask = battle()->battleHeroOrderFlankSide(wide, target,
		*projectedPosition, originalTargetPosition);
	EXPECT_EQ(projectedMask, contactMask(wide->getHexes(*projectedPosition), target->getHexes()));
	EXPECT_EQ(wide->getPosition(), originalWidePosition);
	EXPECT_EQ(target->getPosition(), originalTargetPosition);
	auto movedState = wide->acquireState();
	movedState->setPosition(*projectedPosition);
	BattleUnitsChanged moved;
	moved.battleID = BattleID(0);
	UnitChanges update(wide->unitId(), UnitChanges::EOperation::UPDATE);
	update.data = movedState->save();
	moved.changedStacks.push_back(std::move(update));
	gameHandler->sendAndApply(moved);
	EXPECT_EQ(battle()->battleHeroOrderFlankSide(wide, target), projectedMask);
	EXPECT_EQ(battle()->battleHeroOrderFlankSide(wide, target), contactMask(wide->getHexes(), target->getHexes()));
}

TEST_F(HeroCommandTest, HeroOrderStatePacketRoundTripsThroughClientPackPointer)
{
	BattleHeroOrderStateChanged outgoing;
	outgoing.battleID = BattleID(7);
	outgoing.side = BattleSide::ATTACKER;
	HeroOrderState state;
	state.command = HeroCommand::PROTECT;
	state.issuedRound = 3;
	state.primaryTargetUnitId = 11;
	state.secondaryTargetUnitId = 12;
	state.protectInterceptionsConsumed = 2;
	state.protectInterceptionLimit = 2;
	outgoing.state = state;
	outgoing.states = std::vector<HeroOrderState>{state};

	const CPackForClient & base = outgoing;
	auto polymorphic = CMemorySerializer::deepCopy(base);
	const auto * registered = dynamic_cast<const BattleHeroOrderStateChanged *>(polymorphic.get());
	ASSERT_NE(registered, nullptr);
	EXPECT_EQ(registered->battleID, outgoing.battleID);
	EXPECT_EQ(registered->side, outgoing.side);
	ASSERT_TRUE(registered->state);
	EXPECT_EQ(*registered->state, *outgoing.state);
}

TEST_F(HeroCommandTest, ShieldMasterProtectCountRoundTripsAndRejectsLossyDownsave)
{
	HeroOrderState twoUses;
	twoUses.command = HeroCommand::PROTECT;
	twoUses.issuedRound = 3;
	twoUses.primaryTargetUnitId = 11;
	twoUses.secondaryTargetUnitId = 12;
	twoUses.protectInterceptionsConsumed = 2;
	twoUses.protectInterceptionLimit = 2;
	CMemorySerializer current;
	current.oser & twoUses;
	HeroOrderState currentRestored;
	current.iser & currentRestored;
	EXPECT_EQ(currentRestored, twoUses);

	CMemorySerializer legacy;
	legacy.oser.version = ESerializationVersion::NEW_HORIZONS_NO_QUARTER;
	legacy.iser.version = ESerializationVersion::NEW_HORIZONS_NO_QUARTER;
	HeroOrderState oneUse = twoUses;
	oneUse.protectInterceptionsConsumed = 1;
	oneUse.protectInterceptionLimit = 1;
	legacy.oser & oneUse;
	HeroOrderState legacyRestored;
	legacy.iser & legacyRestored;
	EXPECT_EQ(legacyRestored.protectInterceptionsConsumed, 1);
	EXPECT_EQ(legacyRestored.protectInterceptionLimit, 1);
	EXPECT_THROW(legacy.oser & twoUses, std::runtime_error);
	HeroOrderState unspentShieldMaster = twoUses;
	unspentShieldMaster.protectInterceptionsConsumed = 0;
	EXPECT_THROW(legacy.oser & unspentShieldMaster, std::runtime_error);
}

TEST_F(HeroCommandTest, FlankSideMaskRoundTripsThroughClientPackPointer)
{
	BattleHeroOrderStateChanged outgoing;
	outgoing.battleID = BattleID(7);
	outgoing.side = BattleSide::ATTACKER;
	HeroOrderState state;
	state.command = HeroCommand::FLANK;
	state.issuedRound = 3;
	state.primaryTargetUnitId = 11;
	state.flankTargets.push_back({11, 0b100101});
	outgoing.state = state;
	outgoing.states = std::vector<HeroOrderState>{state};

	const CPackForClient & base = outgoing;
	auto polymorphic = CMemorySerializer::deepCopy(base);
	const auto * registered = dynamic_cast<const BattleHeroOrderStateChanged *>(polymorphic.get());
	ASSERT_NE(registered, nullptr);
	ASSERT_TRUE(registered->state);
	ASSERT_NE(registered->state->flankFor(11), nullptr);
	EXPECT_EQ(registered->state->flankFor(11)->sideMask, 0b100101);
}

TEST_F(HeroCommandTest, FlankSideMaskSurvivesSaveStateRoundTrip)
{
	HeroOrderState original;
	original.command = HeroCommand::FLANK;
	original.issuedRound = 3;
	original.primaryTargetUnitId = 11;
	original.flankTargets.push_back({11, 0b100101});

	CMemorySerializer memory;
	memory.oser & original;
	HeroOrderState restored;
	memory.iser & restored;
	EXPECT_EQ(restored, original);
}

TEST_F(HeroCommandTest, CreatureLocationSpellPacketPreservesUnitZeroAndLanding)
{
	ASSERT_NO_FATAL_FAILURE(prepareCommands());
	const auto * source = battle()->battleGetUnitByID(0);
	ASSERT_NE(source, nullptr);
	ASSERT_EQ(source->unitId(), 0u);
	const BattleHex landing(71);

	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = SpellID::TELEPORT;
	action.aimToHex(BattleHex(88)); // setTarget must discard this stale destination.
	action.setTarget(battle::Target{battle::Destination(source), battle::Destination(landing)});

	CMemorySerializer memory;
	memory.oser & action;
	BattleAction decoded;
	memory.iser & decoded;
	EXPECT_EQ(decoded.actionType, EActionType::HERO_SPELL);
	EXPECT_EQ(decoded.side, BattleSide::ATTACKER);
	EXPECT_EQ(decoded.spell, SpellID::TELEPORT);
	const auto target = decoded.getTarget(battle());
	ASSERT_EQ(target.size(), 2u);
	EXPECT_EQ(target[0].unitValue, source);
	EXPECT_EQ(target[0].hexValue, source->getPosition());
	EXPECT_EQ(target[1].unitValue, nullptr);
	EXPECT_EQ(target[1].hexValue, landing);
	EXPECT_FALSE(battle()->getHeroCommandUsed(BattleSide::ATTACKER));
}

TEST_F(HeroCommandTest, LegacyGameHasNoCommands)
{
	useCommands = false;
	prepareCommands();
	EXPECT_FALSE(battle()->battleUsesHeroCommands());
	EXPECT_FALSE(issue(HeroCommand::CHARGE));
	EXPECT_TRUE(gameState()->getHeroCommandRules().isNull());
}

TEST_F(HeroCommandTest, ChargeChangesRealDamageWithoutManaOrCreatureTurn)
{
	prepareCommands();
	auto * from = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 100);
	auto * to = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(71), 100);
	const auto active = battle()->getActiveStackID();
	const auto mana = attackerSideHero->getManaAvailable();
	const auto before = battle()->calculateDmgRange(BattleAttackInfo(from, to, 3, false)).damage.min;
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	const auto charged = battle()->calculateDmgRange(BattleAttackInfo(from, to, 3, false));
	EXPECT_GT(charged.damage.min, before);
	EXPECT_EQ(charged.attackerOrderCause, HeroCommand::CHARGE);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(battle()->getActiveStackID(), active);
	EXPECT_EQ(battle()->battleGetActiveOrder(BattleSide::ATTACKER), HeroCommand::CHARGE);
	EXPECT_FALSE(battle()->battleCanUseHeroCommand(BattleSide::ATTACKER, HeroCommand::HOLD_THE_LINE));
	advanceRound();
	EXPECT_EQ(battle()->battleGetActiveOrder(BattleSide::ATTACKER), HeroCommand::NONE);
	EXPECT_EQ(battle()->calculateDmgRange(BattleAttackInfo(from, to, 3, false)).damage.min, before);
}

TEST_F(HeroCommandTest, ChargeResolvedHitLogsItsCauseAndDamageAfterTheOneShotIsConsumed)
{
	prepareCommands();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(89), 100);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(93), 1);
	forceMaximumDamage(attacker);
	blockRetaliation(attacker);
	blockRetaliation(target);
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	server.attacks.clear();
	server.battleLogLines.clear();

	battle()->activeStack = attacker->unitId();
	const auto action = BattleAction::makeMeleeAttack(attacker, target, BattleHex(92), false);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));

	const auto attack = std::ranges::find_if(server.attacks, [attacker](const BattleAttack & value)
	{
		return value.stackAttacking == attacker->unitId();
	});
	ASSERT_NE(attack, server.attacks.end());
	const auto hit = std::ranges::find(attack->bsa, target->unitId(), &BattleStackAttacked::stackAttacked);
	ASSERT_NE(hit, attack->bsa.end());
	const auto causalLine = std::ranges::find_if(server.battleLogLines, [](const std::string & line)
	{
		return line.find("Charge:") != std::string::npos;
	});
	ASSERT_NE(causalLine, server.battleLogLines.end()) << ::testing::PrintToString(server.battleLogLines);
	EXPECT_THAT(*causalLine, ::testing::HasSubstr(attacker->unitType()->getNamePluralTranslated()));
	EXPECT_THAT(*causalLine, ::testing::HasSubstr(target->unitType()->getNameSingularTranslated()));
	EXPECT_THAT(*causalLine, ::testing::HasSubstr("for " + std::to_string(hit->damageAmount) + " damage"));
	const auto order = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(order);
	EXPECT_TRUE(order->containsConsumed(attacker->unitId()));
}

TEST_F(HeroCommandTest, HoldTheLineReducesRealIncomingPhysicalDamage)
{
	prepareCommands();
	auto * ours = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 100);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(71), 100);
	const auto before = battle()->calculateDmgRange(BattleAttackInfo(enemy, ours, 0, false)).damage.min;
	const auto shotBefore = battle()->calculateDmgRange(BattleAttackInfo(enemy, ours, 0, true)).damage.min;
	ASSERT_TRUE(issue(HeroCommand::HOLD_THE_LINE));
	ASSERT_EQ(server.battleLogLines.size(), 1);
	EXPECT_THAT(server.battleLogLines.front(), ::testing::HasSubstr("Hold the Line!"));
	EXPECT_THAT(server.battleLogLines.front(), ::testing::HasSubstr("hold position"));
	const auto heldMelee = battle()->calculateDmgRange(BattleAttackInfo(enemy, ours, 0, false));
	EXPECT_LT(heldMelee.damage.min, before);
	EXPECT_EQ(heldMelee.defenderOrderCause, HeroCommand::HOLD_THE_LINE);
	// Hold the Line covers all physical creature damage, including missiles.
	EXPECT_LT(battle()->calculateDmgRange(BattleAttackInfo(enemy, ours, 0, true)).damage.min, shotBefore);
	BattleAttackInfo spellLike(enemy, ours, 0, false);
	spellLike.physicalDamage = false;
	EXPECT_EQ(battle()->calculateDmgRange(spellLike).defenderOrderCause, HeroCommand::NONE);
	advanceRound();
	EXPECT_EQ(battle()->calculateDmgRange(BattleAttackInfo(enemy, ours, 0, false)).damage.min, before);
}

TEST_F(HeroCommandTest, HoldTheLineLogsResolvedIncomingDamage)
{
	prepareCommands();
	auto * ours = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 100);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(71), 100);
	ASSERT_TRUE(issue(HeroCommand::HOLD_THE_LINE));
	blockRetaliation(ours);
	blockRetaliation(enemy);
	server.attacks.clear();
	server.battleLogLines.clear();

	ASSERT_TRUE(attack(enemy, ours->getPosition()));
	const auto resolvedAttack = std::ranges::find_if(server.attacks, [enemy](const BattleAttack & value)
	{
		return value.stackAttacking == enemy->unitId() && !value.counter();
	});
	ASSERT_NE(resolvedAttack, server.attacks.end());
	const auto hit = std::ranges::find(resolvedAttack->bsa, ours->unitId(), &BattleStackAttacked::stackAttacked);
	ASSERT_NE(hit, resolvedAttack->bsa.end());
	const auto causalLine = std::ranges::find_if(server.battleLogLines, [](const std::string & line)
	{
		return line.find("Hold the Line reduced the damage") != std::string::npos;
	});
	ASSERT_NE(causalLine, server.battleLogLines.end()) << ::testing::PrintToString(server.battleLogLines);
	EXPECT_THAT(*causalLine, ::testing::HasSubstr(std::to_string(hit->damageAmount) + " damage"));
}

TEST_F(IronDisciplineTest, HoldTheLineCapturesHalfClampedPhysicalReductionIncludingWarcasting)
{
	prepareIronDiscipline(true, true);
	attackerSideHero->setPrimarySkill(PrimarySkill::DEFENSE, 50, ChangeValueMode::ABSOLUTE);
	auto * held = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 100);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(71), 100);

	ASSERT_TRUE(issue(HeroCommand::HOLD_THE_LINE));
	const auto state = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(state);
	EXPECT_EQ(state->warcastingBonusPercent, 10);
	const auto & holdEffect = battle()->getHeroCommandRules()["commands"]["holdTheLine"]["effects"]["damageReductionPercent"];
	const int physicalReduction = heroCommands::coefficient(holdEffect, *attackerSideHero,
		state->warcastingBonusPercent);
	const int physicalReductionWithoutWarcasting = heroCommands::coefficient(holdEffect, *attackerSideHero, 0);
	ASSERT_GT(physicalReduction, physicalReductionWithoutWarcasting);
	const int expectedBasisPoints = physicalReduction * newHorizonsIronDiscipline::BASIS_POINTS_PER_PHYSICAL_PERCENT;
	EXPECT_EQ(state->holdMagicalReductionBasisPoints, expectedBasisPoints);
	EXPECT_EQ(battle()->battleGetHoldTheLineMagicalReductionBasisPoints(held), expectedBasisPoints);

	const BattleAttackInfo incoming(enemy, held, 0, false);
	const auto physicalWithPerk = battle()->calculateDmgRange(incoming).damage;
	auto & savedState = *battle()->getSide(BattleSide::ATTACKER).findOrder(HeroCommand::HOLD_THE_LINE);
	savedState.holdMagicalReductionBasisPoints = 0;
	const auto physicalWithoutMagicalComponent = battle()->calculateDmgRange(incoming).damage;
	EXPECT_EQ(physicalWithPerk.min, physicalWithoutMagicalComponent.min);
	EXPECT_EQ(physicalWithPerk.max, physicalWithoutMagicalComponent.max)
		<< "Iron Discipline changes only magical spell damage";
	savedState.holdMagicalReductionBasisPoints = static_cast<uint16_t>(expectedBasisPoints);

	attackerSideHero->setPrimarySkill(PrimarySkill::DEFENSE, 0, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(battle()->battleGetHoldTheLineMagicalReductionBasisPoints(held), expectedBasisPoints)
		<< "The public magical value remains the issue-time snapshot after hero changes";
}

TEST_F(IronDisciplineTest, HoldTheLineMagicalReductionRequiresAnAnchoredUnbrokenCurrentOrder)
{
	prepareIronDiscipline();
	auto * held = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 100);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(71), 100);
	ASSERT_TRUE(issue(HeroCommand::HOLD_THE_LINE));
	ASSERT_GT(battle()->battleGetHoldTheLineMagicalReductionBasisPoints(held), 0);

	auto * lateArrival = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(73), 100);
	EXPECT_EQ(battle()->battleGetHoldTheLineMagicalReductionBasisPoints(lateArrival), 0);

	const BattleAttackInfo incoming(enemy, held, 0, false);
	const auto heldDamage = battle()->calculateDmgRange(incoming).damage.min;
	auto & order = *battle()->getSide(BattleSide::ATTACKER).findOrder(HeroCommand::HOLD_THE_LINE);
	order.holdBrokenUnitIds.push_back(held->unitId());
	EXPECT_EQ(battle()->battleGetHoldTheLineMagicalReductionBasisPoints(held), 0);
	EXPECT_GT(battle()->calculateDmgRange(incoming).damage.min, heldDamage)
		<< "The shared recipient check also removes the physical Hold reduction after its anchor breaks";

	order.holdBrokenUnitIds.clear();
	advanceRound();
	EXPECT_EQ(battle()->battleGetHoldTheLineMagicalReductionBasisPoints(held), 0)
		<< "Hold expires at the round boundary";
}

TEST_F(IronDisciplineTest, HypnosisSuspendsPhysicalAndMagicalHoldProtectionUntilControlReturns)
{
	prepareIronDiscipline();
	auto * held = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 100);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(71), 100);
	ASSERT_TRUE(issue(HeroCommand::HOLD_THE_LINE));
	const BattleAttackInfo incoming(enemy, held, 0, false);
	const auto protectedPhysicalDamage = battle()->calculateDmgRange(incoming).damage;
	const int protectedMagicalReduction = battle()->battleGetHoldTheLineMagicalReductionBasisPoints(held);
	ASSERT_GT(protectedMagicalReduction, 0);

	auto control = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID());
	held->addNewBonus(control);
	ASSERT_EQ(battle()->battleGetOwner(held), PlayerColor(1));
	EXPECT_EQ(battle()->battleGetHoldTheLineMagicalReductionBasisPoints(held), 0);
	const auto controlledPhysicalDamage = battle()->calculateDmgRange(incoming).damage;
	EXPECT_GT(controlledPhysicalDamage.min, protectedPhysicalDamage.min);
	EXPECT_GT(controlledPhysicalDamage.max, protectedPhysicalDamage.max);

	held->removeBonus(control);
	ASSERT_EQ(battle()->battleGetOwner(held), PlayerColor(0));
	EXPECT_EQ(battle()->battleGetHoldTheLineMagicalReductionBasisPoints(held), protectedMagicalReduction);
	const auto restoredPhysicalDamage = battle()->calculateDmgRange(incoming).damage;
	EXPECT_EQ(restoredPhysicalDamage.min, protectedPhysicalDamage.min);
	EXPECT_EQ(restoredPhysicalDamage.max, protectedPhysicalDamage.max);
}

TEST_F(IronDisciplineTest, NonHolderKeepsZeroSnapshotEvenIfPerkIsLearnedAfterIssuingHold)
{
	prepareIronDiscipline(false);
	auto * held = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 100);
	ASSERT_TRUE(issue(HeroCommand::HOLD_THE_LINE));
	ASSERT_TRUE(battle()->battleGetHeroOrderState(BattleSide::ATTACKER));
	EXPECT_EQ(battle()->battleGetHeroOrderState(BattleSide::ATTACKER)->holdMagicalReductionBasisPoints, 0);

	attackerSideHero->applyPerkSelection({newHorizonsIronDiscipline::SKILL,
		newHorizonsIronDiscipline::PERK});
	ASSERT_TRUE(attackerSideHero->hasActivePerk(newHorizonsIronDiscipline::SKILL,
		newHorizonsIronDiscipline::PERK));
	EXPECT_EQ(battle()->battleGetHoldTheLineMagicalReductionBasisPoints(held), 0)
		<< "Eligibility is captured when Hold is issued";
}

TEST_F(IronDisciplineTest, MagicalReductionStacksWithOrdinaryReductionAndMatchesAppliedSpellDamage)
{
	prepareIronDiscipline();
	attackerSideHero->setPrimarySkill(PrimarySkill::DEFENSE, 50, ChangeValueMode::ABSOLUTE);
	auto * held = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 100);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(71), 100);
	ASSERT_TRUE(issue(HeroCommand::HOLD_THE_LINE));
	ASSERT_NE(enemy, nullptr);
	held->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELL_DAMAGE_REDUCTION, BonusSource::OTHER, 50, BonusSourceID(),
		BonusSubtypeID(SpellSchool::ANY)));

	const auto * spell = SpellID(SpellID::MAGIC_ARROW).toSpell();
	spells::BattleCast event(battle(), defenderSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&event);
	const auto savedStateBeforeForecast = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(savedStateBeforeForecast);
	auto & savedState = *battle()->getSide(BattleSide::ATTACKER).findOrder(HeroCommand::HOLD_THE_LINE);
	const int reductionBasisPoints = savedState.holdMagicalReductionBasisPoints;
	savedState.holdMagicalReductionBasisPoints = 0;
	const int64_t ordinaryMagicalReductionDamage = mechanics->adjustEffectValue(held);
	savedState.holdMagicalReductionBasisPoints = reductionBasisPoints;
	const int64_t forecastDamage = mechanics->adjustEffectValue(held);
	const int64_t remainingBasisPoints = 10000 - reductionBasisPoints;
	const int64_t expectedDamage = ordinaryMagicalReductionDamage / 10000 * remainingBasisPoints
		+ ordinaryMagicalReductionDamage % 10000 * remainingBasisPoints / 10000;
	EXPECT_EQ(forecastDamage, expectedDamage);
	const int effectivePenetratedHoldBasisPoints = reductionBasisPoints * 80 / 100;
	const int64_t spellDamageWithoutHold = spell->adjustRawDamage(defenderSideHero, held, 10000, 20, 0);
	const int64_t spellDamageWithPenetratedHold = spell->adjustRawDamage(defenderSideHero, held,
		10000, 20, reductionBasisPoints);
	const int64_t expectedPenetratedDamage = spellDamageWithoutHold / 10000
		* (10000 - effectivePenetratedHoldBasisPoints)
		+ spellDamageWithoutHold % 10000 * (10000 - effectivePenetratedHoldBasisPoints) / 10000;
	EXPECT_EQ(spellDamageWithPenetratedHold, expectedPenetratedDamage)
		<< "Spell penetration reduces Iron Discipline as part of current magical damage reduction";
	EXPECT_EQ(battle()->battleGetHeroOrderState(BattleSide::ATTACKER), savedStateBeforeForecast)
		<< "Spell damage evaluation does not consume or mutate Hold state";

	const int64_t healthBefore = held->getAvailableHealth();
	ASSERT_TRUE(castOn(defenderSideHero, SpellID::MAGIC_ARROW, held));
	EXPECT_EQ(healthBefore - held->getAvailableHealth(), forecastDamage)
		<< "The authoritative cast and spell forecast use the shared adjustment";
}

TEST_F(IronDisciplineTest, HoldTheLineReducesScriptedFireShieldReflection)
{
	prepareIronDiscipline();
	attackerSideHero->setPrimarySkill(PrimarySkill::DEFENSE, 50, ChangeValueMode::ABSOLUTE);
	auto * held = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 100);
	auto * shielded = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(71), 1000);
	ASSERT_TRUE(issue(HeroCommand::HOLD_THE_LINE));
	auto * lateArrival = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(72), 100);
	ASSERT_GT(battle()->battleGetHoldTheLineMagicalReductionBasisPoints(held), 0);
	ASSERT_EQ(battle()->battleGetHoldTheLineMagicalReductionBasisPoints(lateArrival), 0);
	blockRetaliation(held);
	blockRetaliation(lateArrival);
	forceMaximumDamage(held);
	forceMaximumDamage(lateArrival);
	attachCombatScript(shielded, "fireShield", 100);

	const int reductionBasisPoints = battle()->battleGetHoldTheLineMagicalReductionBasisPoints(held);
	const int64_t heldHealthBefore = held->getAvailableHealth();
	ASSERT_TRUE(attack(held, shielded->getPosition()));
	const int64_t protectedReflection = heldHealthBefore - held->getAvailableHealth();
	const int64_t lateHealthBefore = lateArrival->getAvailableHealth();
	ASSERT_TRUE(attack(lateArrival, shielded->getPosition()));
	const int64_t ordinaryReflection = lateHealthBefore - lateArrival->getAvailableHealth();
	ASSERT_GT(ordinaryReflection, 0);
	const int64_t expected = ordinaryReflection / 10000 * (10000 - reductionBasisPoints)
		+ ordinaryReflection % 10000 * (10000 - reductionBasisPoints) / 10000;
	EXPECT_EQ(protectedReflection, expected)
		<< "Lua Fire Shield reflection must use the same saved Iron Discipline reduction";
}

TEST_F(HeroCommandTest, ChargeExpiresAtTheRoundBoundary)
{
	prepareCommands();
	auto * from = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 100);
	auto * to = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(71), 100);
	const auto before = battle()->calculateDmgRange(BattleAttackInfo(from, to, 3, false)).damage.min;
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_GT(battle()->calculateDmgRange(BattleAttackInfo(from, to, 3, false)).damage.min, before);
	advanceRound();
	EXPECT_EQ(battle()->calculateDmgRange(BattleAttackInfo(from, to, 3, false)).damage.min, before);
}

TEST_F(ShockAssaultTest, ChargeWithShockAssaultIgnoresExactlyQuarterTargetDefense)
{
	prepareShockAssault(true);
	auto * from = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 100);
	auto * to = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(71), 100);

	const auto ordinary = battle()->calculateDmgRange(BattleAttackInfo(from, to, 0, false)).damage.min;
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	// Basic Offense already contributes 10%. Shock Assault leaves 15 Defense,
	// giving a further 25% attack/defense factor; Charge adds its ordinary 10%.
	EXPECT_EQ(ordinary, 5500);
	EXPECT_EQ(battle()->calculateDmgRange(BattleAttackInfo(from, to, 3, false)).damage.min, 7250);
}

TEST_F(ShockAssaultTest, ShockAssaultNeedsTheChargeOrderAndThreeHexThreshold)
{
	prepareShockAssault(true);
	auto * from = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 100);
	auto * to = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(71), 100);

	EXPECT_EQ(battle()->calculateDmgRange(BattleAttackInfo(from, to, 3, false)).damage.min, 5500);
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_EQ(battle()->calculateDmgRange(BattleAttackInfo(from, to, 2, false)).damage.min, 5500);
	// Charge is side-owned: issuing it for the attacker does not empower an
	// otherwise identical melee blow made by the defender.
	EXPECT_EQ(battle()->calculateDmgRange(BattleAttackInfo(to, from, 3, false)).damage.min, 5000);
}

TEST_F(ShockAssaultTest, ShockAssaultDoesNotChangeUnselectedOrRangedOrNonPhysicalDamage)
{
	prepareShockAssault(false);
	auto * from = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 100);
	auto * to = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(71), 100);

	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_EQ(battle()->calculateDmgRange(BattleAttackInfo(from, to, 3, false)).damage.min, 6000);
}

TEST_F(ShockAssaultTest, ShockAssaultDoesNotApplyToRangedOrNonPhysicalDamage)
{
	prepareShockAssault(true);
	auto * from = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(72), 100);
	auto * to = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(73), 100);
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	BattleAttackInfo ranged(from, to, 3, true);
	BattleAttackInfo rangedWithoutCharge(from, to, 0, true);
	EXPECT_EQ(battle()->calculateDmgRange(ranged).damage.min,
		battle()->calculateDmgRange(rangedWithoutCharge).damage.min);
	BattleAttackInfo nonPhysical(from, to, 3, false);
	nonPhysical.physicalDamage = false;
	// The Charge bonus itself is not a physical-only rule; only Shock Assault's
	// defense penetration is suppressed for this synthetic non-physical blow.
	EXPECT_EQ(battle()->calculateDmgRange(nonPhysical).damage.min, 6000);
}

TEST_F(ShockAssaultTest, ChargeAndShockAssaultApplyOnlyToTheFirstPrimaryBlow)
{
	prepareShockAssault(true);
	auto * from = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 100);
	auto * to = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(71), 100);
	ASSERT_TRUE(issue(HeroCommand::CHARGE));

	BattleAttackInfo collateral(from, to, 3, false);
	collateral.secondaryAttack = true;
	EXPECT_EQ(battle()->calculateDmgRange(collateral).damage.min, 5500);

	auto * mutableBattle = const_cast<BattleInfo *>(dynamic_cast<const BattleInfo *>(battle()->getBattle()));
	ASSERT_NE(mutableBattle, nullptr);
	ASSERT_TRUE(mutableBattle->consumeHeroOrderUnit(BattleSide::ATTACKER, from->unitId()));
	EXPECT_EQ(battle()->calculateDmgRange(BattleAttackInfo(from, to, 3, false)).damage.min, 5500);
}

TEST_F(HeroCommandTest, RiposteBoostsOnlyRetaliationDamage)
{
	prepareCommands();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(71), 100);
	BattleAttackInfo ordinary(attacker, defender, 0, false);
	const auto before = battle()->calculateDmgRange(ordinary).damage.min;
	ASSERT_TRUE(issue(HeroCommand::RIPOSTE));
	ASSERT_EQ(server.battleLogLines.size(), 1);
	EXPECT_THAT(server.battleLogLines.front(), ::testing::HasSubstr("Riposte!"));
	EXPECT_THAT(server.battleLogLines.front(), ::testing::HasSubstr("take less melee damage"));
	EXPECT_THAT(server.battleLogLines.front(), ::testing::HasSubstr("retaliate more fiercely"));
	ordinary.retaliation = true;
	const auto retaliation = battle()->calculateDmgRange(ordinary);
	EXPECT_GT(retaliation.damage.min, before);
	EXPECT_EQ(retaliation.attackerOrderCause, HeroCommand::RIPOSTE);

	forceMaximumDamage(attacker);
	server.attacks.clear();
	server.battleLogLines.clear();
	ASSERT_TRUE(attack(defender, attacker->getPosition()));
	const auto riposte = std::ranges::find_if(server.attacks, [attacker](const BattleAttack & value)
	{
		return value.stackAttacking == attacker->unitId() && value.counter();
	});
	ASSERT_NE(riposte, server.attacks.end());
	const auto hit = std::ranges::find(riposte->bsa, defender->unitId(), &BattleStackAttacked::stackAttacked);
	ASSERT_NE(hit, riposte->bsa.end());
	const auto causalLine = std::ranges::find_if(server.battleLogLines, [](const std::string & line)
	{
		return line.find("Riposte:") != std::string::npos;
	});
	ASSERT_NE(causalLine, server.battleLogLines.end()) << ::testing::PrintToString(server.battleLogLines);
	EXPECT_THAT(*causalLine, ::testing::HasSubstr(std::to_string(hit->damageAmount) + " damage"));
}

TEST_F(HeroCommandTest, BracePreemptiveStrikeUsesItsOwnDamageFormula)
{
	prepareCommands();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(71), 100);
	BattleAttackInfo incoming(attacker, defender, 0, false);
	const auto before = battle()->calculateDmgRange(incoming).damage.min;
	ASSERT_TRUE(issue(HeroCommand::BRACE));
	ASSERT_EQ(server.battleLogLines.size(), 1);
	EXPECT_THAT(server.battleLogLines.front(), ::testing::HasSubstr("Brace!"));
	EXPECT_THAT(server.battleLogLines.front(), ::testing::HasSubstr("moves at least 3 hexes"));
	EXPECT_THAT(server.battleLogLines.front(), ::testing::HasSubstr("before a melee attack"));
	incoming.bracePreemptive = true;
	// Brace is a final multiplier: with the fixture's zero hero defense it is
	// exactly 50% of the ordinary blow, independent of additive Offense.
	EXPECT_EQ(battle()->calculateDmgRange(incoming).damage.min, before / 2);
	EXPECT_TRUE(battle()->battleCanTriggerHeroOrderBrace(defender, attacker, 3, false, false));
	EXPECT_TRUE(battle()->battleCanTriggerHeroOrderBrace(defender, attacker, 3, false, false));
	EXPECT_FALSE(battle()->battleCanTriggerHeroOrderBrace(defender, attacker, 2, false, false));
}

TEST_F(CounterchargeTest, CounterchargeAddsTwentyFivePointsOnlyToBracePreemptiveHit)
{
	prepareCountercharge(true);
	auto * braced = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(70), 100);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(71), 100);
	const BattleAttackInfo ordinary(braced, target, 0, false);
	const auto normal = battle()->calculateDmgRange(ordinary);

	ASSERT_TRUE(issue(HeroCommand::BRACE));
	const auto orderState = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(orderState);
	EXPECT_EQ(orderState->command, HeroCommand::BRACE);

	BattleAttackInfo braceStrike(braced, target, 0, false);
	braceStrike.bracePreemptive = true;
	const auto forecast = battle()->calculateDmgRange(braceStrike);
	EXPECT_EQ(forecast.damage.min, normal.damage.min * 75 / 100);
	EXPECT_EQ(forecast.damage.max, normal.damage.max * 75 / 100);

	const auto normalAfterOrder = battle()->calculateDmgRange(ordinary);
	EXPECT_EQ(normalAfterOrder.damage.min, normal.damage.min);
	EXPECT_EQ(normalAfterOrder.damage.max, normal.damage.max)
		<< "A normal attack does not inherit Brace's Countercharge modifier";
}

TEST_F(CounterchargeTest, AuthoritativeBraceTriggerUsesCounterchargeDamage)
{
	prepareCountercharge(true);
	auto * braced = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(93), 100);
	auto * mover = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(89), 100);
	forceMaximumDamage(braced);
	blockRetaliation(braced);
	blockRetaliation(mover);
	const auto normalDamage = battle()->calculateDmgRange(BattleAttackInfo(braced, mover, 0, false)).damage.max;
	ASSERT_TRUE(issue(HeroCommand::BRACE));
	server.attacks.clear();

	battle()->activeStack = mover->unitId();
	const auto action = BattleAction::makeMeleeAttack(mover, braced, BattleHex(92), false);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1), action));
	const auto braceAttack = std::ranges::find_if(server.attacks, [braced](const BattleAttack & value)
	{
		return value.stackAttacking == braced->unitId();
	});
	ASSERT_NE(braceAttack, server.attacks.end());
	const auto hit = std::ranges::find(braceAttack->bsa, mover->unitId(), &BattleStackAttacked::stackAttacked);
	ASSERT_NE(hit, braceAttack->bsa.end());
	EXPECT_EQ(hit->damageAmount, normalDamage * 75 / 100)
		<< "The server's actual pre-emptive attack uses the same capped Countercharge percentage";
}

TEST_F(CounterchargeTest, UnselectedCounterchargeDoesNotEmpowerBrace)
{
	prepareCountercharge(false);
	auto * braced = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(70), 100);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(71), 100);
	const auto normal = battle()->calculateDmgRange(BattleAttackInfo(braced, target, 0, false));
	ASSERT_TRUE(issue(HeroCommand::BRACE));
	BattleAttackInfo braceStrike(braced, target, 0, false);
	braceStrike.bracePreemptive = true;
	EXPECT_EQ(battle()->calculateDmgRange(braceStrike).damage.min, normal.damage.min / 2)
		<< "Basic Armorer without the selected Countercharge perk keeps ordinary Brace damage";
}

TEST_F(CounterchargeTest, CounterchargeDoesNotBuffBulwarkOrOrdinaryRetaliations)
{
	prepareCountercharge(true);
	auto * braced = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(70), 100);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(71), 100);
	const auto normal = battle()->calculateDmgRange(BattleAttackInfo(braced, target, 0, false));
	ASSERT_TRUE(issue(HeroCommand::BRACE));

	BattleAttackInfo bulwarkPreemptive(braced, target, 0, false);
	bulwarkPreemptive.preemptiveDamagePercent = 50;
	EXPECT_EQ(battle()->calculateDmgRange(bulwarkPreemptive).damage.min, normal.damage.min / 2)
		<< "The separate Bulwark pre-emptive multiplier is not a Brace pre-emptive hit";

	BattleAttackInfo retaliation(braced, target, 0, false);
	retaliation.retaliation = true;
	EXPECT_EQ(battle()->calculateDmgRange(retaliation).damage.min, normal.damage.min)
		<< "Ordinary retaliation is not amplified by Countercharge";
}

TEST_F(CounterchargeTest, PerkSelectionSurvivesGameSaveAndLoad)
{
	startGame();
	const int decoded = SecondarySkill::decode("new-horizons:armorer");
	ASSERT_GE(decoded, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(decoded), MasteryLevel::BASIC,
		ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({"new-horizons:armorer", "new-horizons:armorer.countercharge"});
	ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:armorer", "new-horizons:armorer.countercharge"));

	auto restored = std::make_shared<CGameState>();
	restored->preInit(LIBRARY);
	restored->loadFromMemory(gameState()->saveToMemory());
	const auto * restoredHero = restored->getHero(attackerSideHero->id);
	ASSERT_NE(restoredHero, nullptr);
	EXPECT_TRUE(restoredHero->hasActivePerk("new-horizons:armorer", "new-horizons:armorer.countercharge"));
	EXPECT_EQ(newHorizonsCombatSkills::bracePreemptivePercent(50, restoredHero), 75)
		<< "A restored Countercharge hero keeps the same shared Brace resolver result";
}

TEST_F(CounterchargeTest, BraceWarcastingSnapshotSurvivesBattleDeepCopyWithCounterchargeDamage)
{
	prepareCountercharge(true, true);
	attackerSideHero->setPrimarySkill(PrimarySkill::DEFENSE, 20, ChangeValueMode::ABSOLUTE);
	auto * braced = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(70), 100);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(71), 100);
	const auto ordinary = battle()->calculateDmgRange(BattleAttackInfo(braced, target, 0, false));
	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = braced->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);
	BattleAction spell;
	spell.actionType = EActionType::HERO_SPELL;
	spell.side = BattleSide::ATTACKER;
	spell.spell = SpellID::HASTE;
	spell.aimToUnit(braced);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), spell));
	EXPECT_EQ(battle()->getWarcastingState(BattleSide::ATTACKER).empowermentPercent, 10);
	advanceRound();
	ASSERT_TRUE(issue(HeroCommand::BRACE));

	auto order = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(order);
	ASSERT_EQ(order->command, HeroCommand::BRACE);
	ASSERT_EQ(order->warcastingBonusPercent, 10);
	const auto coefficient = heroCommands::coefficient(
		battle()->getHeroCommandRules()["commands"]["brace"]["effects"]["preemptiveDamagePercent"],
		*attackerSideHero, order->warcastingBonusPercent);
	ASSERT_EQ(coefficient, 56);
	const auto expectedPercent = newHorizonsCombatSkills::bracePreemptivePercent(coefficient, attackerSideHero);
	ASSERT_EQ(expectedPercent, 81);
	BattleAttackInfo braceStrike(braced, target, 0, false);
	braceStrike.bracePreemptive = true;
	const auto liveDamage = battle()->calculateDmgRange(braceStrike);
	EXPECT_EQ(liveDamage.damage.min, ordinary.damage.min * expectedPercent / 100);
	EXPECT_EQ(liveDamage.damage.max, ordinary.damage.max * expectedPercent / 100);

	const auto restored = CMemorySerializer::deepCopy(*battle(), gameState().get());
	ASSERT_NE(restored, nullptr);
	const auto restoredOrder = restored->getHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(restoredOrder);
	EXPECT_EQ(*restoredOrder, *order);
	EXPECT_EQ(restoredOrder->warcastingBonusPercent, 10);
	const auto * restoredBraced = restored->getStack(braced->unitId(), false);
	const auto * restoredTarget = restored->getStack(target->unitId(), false);
	ASSERT_NE(restoredBraced, nullptr);
	ASSERT_NE(restoredTarget, nullptr);
	EXPECT_EQ(restoredBraced->unitId(), braced->unitId());
	EXPECT_EQ(restoredTarget->unitId(), target->unitId());
	const auto * restoredHero = restored->battleGetFightingHero(BattleSide::ATTACKER);
	ASSERT_NE(restoredHero, nullptr);
	EXPECT_TRUE(restoredHero->hasActivePerk("new-horizons:armorer", "new-horizons:armorer.countercharge"));

	BattleAttackInfo restoredBraceStrike(restoredBraced, restoredTarget, 0, false);
	restoredBraceStrike.bracePreemptive = true;
	const auto restoredDamage = restored->calculateDmgRange(restoredBraceStrike);
	EXPECT_EQ(restoredDamage.damage.min, liveDamage.damage.min);
	EXPECT_EQ(restoredDamage.damage.max, liveDamage.damage.max)
		<< "The restored active Order snapshot and selected Countercharge hero reproduce the same Brace forecast";
}

TEST_F(HeroCommandTest, BraceTriggerLogsResolvedDamageAndCasualties)
{
	prepareCommands();
	auto * braced = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(93), 100);
	auto * mover = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(89), 1);
	forceMaximumDamage(braced);
	blockRetaliation(braced);
	blockRetaliation(mover);
	battle()->activeStack = mover->unitId();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
		BattleAction::makeHeroCommand(BattleSide::DEFENDER, HeroCommand::RIPOSTE)));
	battle()->activeStack = braced->unitId();
	ASSERT_TRUE(issue(HeroCommand::BRACE));
	server.attacks.clear();
	server.battleLogLines.clear();

	// The mover crosses three hexes to attack the braced stack at hex 93.
	battle()->activeStack = mover->unitId();
	const auto action = BattleAction::makeMeleeAttack(mover, braced, BattleHex(92), false);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1), action));

	const auto braceAttack = std::ranges::find_if(server.attacks, [braced](const BattleAttack & attack)
	{
		return attack.stackAttacking == braced->unitId();
	});
	ASSERT_NE(braceAttack, server.attacks.end());
	const auto hit = std::ranges::find(braceAttack->bsa, mover->unitId(), &BattleStackAttacked::stackAttacked);
	ASSERT_NE(hit, braceAttack->bsa.end());
	ASSERT_EQ(hit->killedAmount, 1u);

	const auto causalLine = std::ranges::find_if(server.battleLogLines, [](const std::string & line)
	{
		return line.find("Brace preemptive strike:") != std::string::npos;
	});
	ASSERT_NE(causalLine, server.battleLogLines.end()) << ::testing::PrintToString(server.battleLogLines);
	EXPECT_THAT(*causalLine, ::testing::HasSubstr(braced->unitType()->getNamePluralTranslated()));
	EXPECT_THAT(*causalLine, ::testing::HasSubstr(mover->unitType()->getNameSingularTranslated()));
	EXPECT_THAT(*causalLine, ::testing::HasSubstr("for " + std::to_string(hit->damageAmount) + " damage"));
	EXPECT_THAT(*causalLine, ::testing::HasSubstr("(" + std::to_string(hit->killedAmount) + " killed)"));
	EXPECT_THAT(*causalLine, ::testing::HasSubstr("Riposte reducing the damage"));
	EXPECT_THAT(*causalLine, ::testing::HasSubstr("before the incoming melee attack."));
	EXPECT_EQ(std::ranges::count_if(server.battleLogLines, [](const std::string & line)
	{
		return line.find("Brace preemptive strike:") != std::string::npos;
	}), 1);
	EXPECT_EQ(std::ranges::count_if(server.battleLogLines, [](const std::string & line)
	{
		return line.find("Riposte") != std::string::npos;
	}), 1);
}

TEST_F(HeroCommandTest, ProtectRedirectsOneAdjacentWardAttack)
{
	prepareCommands();
	auto * protector = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 100);
	auto * ward = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(71), 100);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(72), 100);
	ASSERT_EQ(BattleHex::getDistance(protector->getPosition(), ward->getPosition()), 1);
	ASSERT_EQ(battle()->battleResolveHeroOrderTarget(enemy, ward, false), ward);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makePairedHeroCommand(BattleSide::ATTACKER, HeroCommand::PROTECT,
			protector->unitId(), ward->unitId())));
	EXPECT_EQ(battle()->battleHeroOrderProtectInterceptionLimit(BattleSide::ATTACKER), 1);
	ASSERT_EQ(server.battleLogLines.size(), 1);
	EXPECT_THAT(server.battleLogLines.front(), ::testing::HasSubstr("Protect! Protector: Angels. Ward: Angels."));
	EXPECT_THAT(server.battleLogLines.front(), ::testing::HasSubstr("first qualifying melee attack"));
	const auto state = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(state);
	EXPECT_EQ(state->primaryTargetUnitId, protector->unitId());
	EXPECT_EQ(state->secondaryTargetUnitId, ward->unitId());
	EXPECT_EQ(battle()->battleResolveHeroOrderTarget(enemy, ward, false), protector);
	ASSERT_TRUE(battle()->interceptHeroOrderProtect(BattleSide::ATTACKER));
	EXPECT_EQ(battle()->battleResolveHeroOrderTarget(enemy, ward, false), ward);
}

TEST_F(ShieldMasterTest, ProtectRedirectsAndReducesExactlyTheFirstTwoOfThreeMeleeAttacks)
{
	prepareShieldMaster();
	auto * protector = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(70), 1);
	auto * ward = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(71), 1);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(72), 1);
	ASSERT_NE(protector, nullptr);
	ASSERT_NE(ward, nullptr);
	ASSERT_NE(enemy, nullptr);
	ASSERT_EQ(BattleHex::getDistance(protector->getPosition(), ward->getPosition()), 1);
	enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::ADDITIONAL_ATTACK,
		BonusSource::OTHER, 2, BonusSourceID()));
	forceMaximumDamage(enemy);
	blockRetaliation(protector);
	blockRetaliation(ward);
	blockRetaliation(enemy);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makePairedHeroCommand(BattleSide::ATTACKER, HeroCommand::PROTECT,
			protector->unitId(), ward->unitId())));
	EXPECT_EQ(battle()->battleHeroOrderProtectInterceptionLimit(BattleSide::ATTACKER), 2);
	ASSERT_EQ(server.battleLogLines.size(), 1);
	EXPECT_THAT(server.battleLogLines.front(), ::testing::HasSubstr("first two qualifying melee attacks"));

	const auto ordinaryDamage = battle()->calculateDmgRange(BattleAttackInfo(enemy, ward, 0, false)).damage.max;
	BattleAttackInfo interceptedDamage(enemy, protector, 0, false);
	interceptedDamage.protectIntercepted = true;
	const auto reducedDamage = battle()->calculateDmgRange(interceptedDamage).damage.max;
	ASSERT_GT(ordinaryDamage, reducedDamage);
	server.attacks.clear();
	server.battleLogLines.clear();

	ASSERT_TRUE(attack(enemy, ward->getPosition()));
	std::vector<const BattleAttack *> strikes;
	for(const auto & candidate : server.attacks)
		if(candidate.stackAttacking == enemy->unitId() && !candidate.counter())
			strikes.push_back(&candidate);
	ASSERT_EQ(strikes.size(), 3u) << ::testing::PrintToString(server.attacks);
	for(size_t i = 0; i < strikes.size(); ++i)
	{
		const auto expectedTarget = i < 2 ? protector->unitId() : ward->unitId();
		const auto hit = std::ranges::find(strikes[i]->bsa, expectedTarget, &BattleStackAttacked::stackAttacked);
		ASSERT_NE(hit, strikes[i]->bsa.end()) << "Strike " << i << " hit the wrong stack";
		EXPECT_EQ(hit->damageAmount, i < 2 ? reducedDamage : ordinaryDamage);
	}
	const auto finalState = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(finalState);
	EXPECT_EQ(finalState->protectInterceptionsConsumed, 2);
	EXPECT_EQ(std::ranges::count_if(server.battleLogLines, [](const std::string & line)
	{
		return line.find("Protect reduced the damage") != std::string::npos;
	}), 2);
}

TEST_F(ShieldMasterTest, RangedAttackDoesNotConsumeProtectInterception)
{
	prepareShieldMaster();
	auto * protector = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(70), 10);
	auto * ward = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(71), 10);
	auto * shooter = addStack(BattleSide::DEFENDER, creatureByName("core:archer"), BattleHex(93), 10);
	ASSERT_NE(protector, nullptr);
	ASSERT_NE(ward, nullptr);
	ASSERT_NE(shooter, nullptr);
	blockRetaliation(protector);
	blockRetaliation(ward);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makePairedHeroCommand(BattleSide::ATTACKER, HeroCommand::PROTECT,
			protector->unitId(), ward->unitId())));
	ASSERT_TRUE(battle()->battleCanShoot(shooter, ward->getPosition()));
	server.attacks.clear();
	battle()->activeStack = shooter->unitId();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
		BattleAction::makeShotAttack(shooter, ward)));
	const auto state = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(state);
	EXPECT_EQ(state->protectInterceptionsConsumed, 0);
	EXPECT_EQ(battle()->battleResolveHeroOrderTarget(shooter, ward, false), protector);
	const auto shot = std::ranges::find_if(server.attacks, [shooter](const BattleAttack & value)
	{
		return value.stackAttacking == shooter->unitId() && value.shot();
	});
	ASSERT_NE(shot, server.attacks.end());
	EXPECT_NE(std::ranges::find(shot->bsa, ward->unitId(), &BattleStackAttacked::stackAttacked), shot->bsa.end());
}

TEST_F(ShieldMasterTest, DeadProtectorBreaksPairWithoutSpendingAUseAndNewRoundResetsCount)
{
	prepareShieldMaster();
	auto * protector = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(70), 1);
	auto * ward = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(71), 1);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(72), 1);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makePairedHeroCommand(BattleSide::ATTACKER, HeroCommand::PROTECT,
			protector->unitId(), ward->unitId())));
	ASSERT_TRUE(battle()->interceptHeroOrderProtect(BattleSide::ATTACKER));
	ASSERT_TRUE(battle()->interceptHeroOrderProtect(BattleSide::ATTACKER));
	EXPECT_FALSE(battle()->interceptHeroOrderProtect(BattleSide::ATTACKER));
	auto state = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(state);
	EXPECT_EQ(state->protectInterceptionsConsumed, 2);

	advanceRound();
	EXPECT_FALSE(battle()->battleGetHeroOrderState(BattleSide::ATTACKER));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makePairedHeroCommand(BattleSide::ATTACKER, HeroCommand::PROTECT,
			protector->unitId(), ward->unitId())));
	state = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(state);
	EXPECT_EQ(state->protectInterceptionsConsumed, 0);

	auto deadProtector = protector->acquireState();
	int64_t lethalDamage = deadProtector->getAvailableHealth();
	deadProtector->damage(lethalDamage);
	ASSERT_FALSE(deadProtector->alive());
	BattleUnitsChanged killed;
	killed.battleID = BattleID(0);
	killed.changedStacks.emplace_back(protector->unitId(), UnitChanges::EOperation::UPDATE);
	killed.changedStacks.back().data = deadProtector->save();
	killed.changedStacks.back().healthDelta = -lethalDamage;
	gameHandler->sendAndApply(killed);
	const auto broken = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(broken);
	EXPECT_TRUE(broken->protectBroken);
	EXPECT_EQ(broken->protectInterceptionsConsumed, 0);
	EXPECT_EQ(battle()->battleResolveHeroOrderTarget(enemy, ward, false), ward);
}

TEST_F(HeroCommandTest, ProtectReductionIsScopedToTheInterceptedBlowAndStateIsReplicated)
{
	prepareCommands();
	auto * protector = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 100);
	auto * ward = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(71), 100);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(72), 100);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makePairedHeroCommand(BattleSide::ATTACKER, HeroCommand::PROTECT,
			protector->unitId(), ward->unitId())));
	const auto normal = battle()->calculateDmgRange(BattleAttackInfo(enemy, protector, 0, false)).damage.min;
	EXPECT_EQ(battle()->calculateDmgRange(BattleAttackInfo(enemy, protector, 0, false)).defenderOrderCause,
		HeroCommand::NONE);
	BattleAttackInfo intercepted(enemy, protector, 0, false);
	intercepted.protectIntercepted = true;
	const auto reduced = battle()->calculateDmgRange(intercepted).damage.min;
	EXPECT_EQ(battle()->calculateDmgRange(intercepted).defenderOrderCause, HeroCommand::PROTECT);
	EXPECT_LT(reduced, normal);
	EXPECT_EQ(battle()->calculateDmgRange(BattleAttackInfo(enemy, protector, 0, false)).damage.min, normal);
	blockRetaliation(protector);
	blockRetaliation(ward);
	server.attacks.clear();
	server.battleLogLines.clear();
	const auto statePacketsBeforeAttack = server.orderStateUpdates.size();
	ASSERT_TRUE(attack(enemy, ward->getPosition()));
	const auto interceptedAttack = std::ranges::find_if(server.attacks, [enemy](const BattleAttack & value)
	{
		return value.stackAttacking == enemy->unitId() && !value.counter();
	});
	ASSERT_NE(interceptedAttack, server.attacks.end());
	const auto hit = std::ranges::find(interceptedAttack->bsa, protector->unitId(), &BattleStackAttacked::stackAttacked);
	ASSERT_NE(hit, interceptedAttack->bsa.end());
	const auto causalLine = std::ranges::find_if(server.battleLogLines, [](const std::string & line)
	{
		return line.find("Protect reduced the damage") != std::string::npos;
	});
	ASSERT_NE(causalLine, server.battleLogLines.end()) << ::testing::PrintToString(server.battleLogLines);
	EXPECT_THAT(*causalLine, ::testing::HasSubstr(std::to_string(hit->damageAmount) + " damage"));
	ASSERT_GT(server.orderStateUpdates.size(), statePacketsBeforeAttack);
	ASSERT_TRUE(server.orderStateUpdates.back().state);
	EXPECT_EQ(server.orderStateUpdates.back().state->protectInterceptionsConsumed, 1);
}

TEST_F(HeroCommandTest, ProtectExpiresPermanentlyAfterFullFootprintSeparation)
{
	prepareCommands();
	auto * protector = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 100);
	auto * ward = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(71), 100);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(72), 100);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makePairedHeroCommand(BattleSide::ATTACKER, HeroCommand::PROTECT,
			protector->unitId(), ward->unitId())));
	BattleStackMoved separated;
	separated.battleID = BattleID(0);
	separated.stack = ward->unitId();
	separated.tilesToMove.insert(BattleHex(74));
	gameHandler->sendAndApply(separated);
	EXPECT_TRUE(battle()->battleGetHeroOrderState(BattleSide::ATTACKER)->protectBroken);
	BattleStackMoved reunited = separated;
	reunited.tilesToMove.clear();
	reunited.tilesToMove.insert(BattleHex(71));
	gameHandler->sendAndApply(reunited);
	EXPECT_EQ(battle()->battleResolveHeroOrderTarget(enemy, ward, false), ward);
}

TEST_F(HeroCommandTest, FlankRaisesTheFirstDistinctSideAttack)
{
	prepareCommands();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 100);
	auto * secondAttacker = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(54), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(71), 100);
	BattleAttackInfo attack(attacker, defender, 0, false);
	const auto before = battle()->calculateDmgRange(attack).damage.min;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeTargetedHeroCommand(BattleSide::ATTACKER, HeroCommand::FLANK, defender->unitId())));
	ASSERT_EQ(server.battleLogLines.size(), 1);
	EXPECT_THAT(server.battleLogLines.front(), ::testing::HasSubstr("Flank! Target: Angels"));
	EXPECT_THAT(server.battleLogLines.front(), ::testing::HasSubstr("exploit new sides this round"));
	const auto side = battle()->battleHeroOrderFlankSide(attacker, defender);
	ASSERT_NE(side, 0);
	const auto firstSideEstimate = battle()->calculateDmgRange(attack);
	const auto firstSide = firstSideEstimate.damage.min;
	EXPECT_GT(firstSide, before);
	EXPECT_EQ(firstSideEstimate.attackerOrderCause, HeroCommand::FLANK);
	forceMaximumDamage(attacker);
	blockRetaliation(defender);
	server.attacks.clear();
	server.battleLogLines.clear();
	ASSERT_TRUE(this->attack(attacker, defender->getPosition()));
	const auto resolvedAttack = std::ranges::find_if(server.attacks, [attacker](const BattleAttack & value)
	{
		return value.stackAttacking == attacker->unitId() && !value.counter();
	});
	ASSERT_NE(resolvedAttack, server.attacks.end());
	const auto resolvedHit = std::ranges::find(resolvedAttack->bsa, defender->unitId(), &BattleStackAttacked::stackAttacked);
	ASSERT_NE(resolvedHit, resolvedAttack->bsa.end());
	const auto causalLine = std::ranges::find_if(server.battleLogLines, [](const std::string & line)
	{
		return line.find("Flank:") != std::string::npos;
	});
	ASSERT_NE(causalLine, server.battleLogLines.end()) << ::testing::PrintToString(server.battleLogLines);
	EXPECT_THAT(*causalLine, ::testing::HasSubstr(std::to_string(resolvedHit->damageAmount) + " damage"));
	BattleAttackInfo secondary(attacker, defender, 0, false);
	secondary.secondaryAttack = true;
	// Flank already applies to melee contact with its marked target, including
	// collateral contact. Provenance follows that calculation, not Charge's
	// primary-hit-only restriction.
	const auto collateral = battle()->calculateDmgRange(secondary);
	const auto currentPrimary = battle()->calculateDmgRange(BattleAttackInfo(attacker, defender, 0, false));
	EXPECT_EQ(collateral.damage.min, currentPrimary.damage.min);
	EXPECT_EQ(collateral.attackerOrderCause, HeroCommand::FLANK);
	EXPECT_EQ(battle()->calculateDmgRange(BattleAttackInfo(attacker, defender, 0, true)).attackerOrderCause,
		HeroCommand::NONE);
	EXPECT_EQ(battle()->battleHeroOrderFlankMeleeDamagePercent(BattleAttackInfo(attacker, defender, 0, true)), 0);
	EXPECT_EQ(battle()->battleGetHeroOrderState(BattleSide::ATTACKER)->flankFor(defender->unitId())->sideMask, side);
	EXPECT_EQ(battle()->battleHeroOrderFlankMeleeDamagePercent(BattleAttackInfo(defender, attacker, 0, false)), 0)
		<< "An opposing attack must not borrow the attacker's Flank Order";
	BattleAttackInfo secondAttack(secondAttacker, defender, 0, false);
	const auto secondSide = battle()->battleHeroOrderFlankSide(secondAttacker, defender);
	ASSERT_NE(secondSide, 0);
	ASSERT_NE(secondSide, side);
	EXPECT_GT(battle()->calculateDmgRange(secondAttack).damage.min, firstSide);
	EXPECT_EQ(battle()->calculateDmgRange(BattleAttackInfo(attacker, secondAttacker, 0, false)).attackerOrderCause,
		HeroCommand::NONE);
}

TEST_F(HeroCommandTest, SecondWindActivatesMovedStackWithDirectDamagePenalty)
{
	prepareCommands();
	auto * target = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 100);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(71), 100);
	target->movedThisRound = true;
	BattleAttackInfo attack(target, enemy, 0, false);
	const auto before = battle()->calculateDmgRange(attack).damage.min;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeTargetedHeroCommand(BattleSide::ATTACKER, HeroCommand::SECOND_WIND, target->unitId())));
	ASSERT_EQ(server.battleLogLines.size(), 1);
	EXPECT_THAT(server.battleLogLines.front(), ::testing::HasSubstr("Second Wind! Target: Angels"));
	EXPECT_THAT(server.battleLogLines.front(), ::testing::HasSubstr("One reduced-strength activation"));
	const auto state = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(state);
	EXPECT_TRUE(state->secondWindActive);
	EXPECT_EQ(battle()->getActiveStackID(), target->unitId());
	const auto followUp = battle()->calculateDmgRange(attack);
	EXPECT_LT(followUp.damage.min, before);
	EXPECT_EQ(followUp.attackerOrderCause, HeroCommand::SECOND_WIND);
	EXPECT_EQ(battle()->calculateDmgRange(BattleAttackInfo(enemy, target, 0, false)).attackerOrderCause,
		HeroCommand::NONE);

	blockRetaliation(target);
	blockRetaliation(enemy);
	server.attacks.clear();
	server.battleLogLines.clear();
	const auto action = BattleAction::makeMeleeAttack(target, enemy, target->getPosition(), false);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	const auto resolvedAttack = std::ranges::find_if(server.attacks, [target](const BattleAttack & value)
	{
		return value.stackAttacking == target->unitId();
	});
	ASSERT_NE(resolvedAttack, server.attacks.end());
	const auto hit = std::ranges::find(resolvedAttack->bsa, enemy->unitId(), &BattleStackAttacked::stackAttacked);
	ASSERT_NE(hit, resolvedAttack->bsa.end());
	const auto causalLine = std::ranges::find_if(server.battleLogLines, [](const std::string & line)
	{
		return line.find("Second Wind") != std::string::npos;
	});
	ASSERT_NE(causalLine, server.battleLogLines.end()) << ::testing::PrintToString(server.battleLogLines);
	EXPECT_THAT(*causalLine, ::testing::HasSubstr("reduced-strength follow-up"));
	EXPECT_THAT(*causalLine, ::testing::HasSubstr(std::to_string(hit->damageAmount) + " damage"));
}

TEST_F(HeroCommandTest, LegacyDoctrineIdsAreNeverIssuableOrExposed)
{
	prepareCommands();
	for(const auto command : {HeroCommand::AGGRESSIVE, HeroCommand::DEFENSIVE})
	{
		EXPECT_FALSE(heroCommands::supportedByRules(battle()->getHeroCommandRules(), command));
		EXPECT_FALSE(issue(command));
	}
	EXPECT_EQ(battle()->battleGetActiveDoctrine(BattleSide::ATTACKER), HeroCommand::NONE);
	EXPECT_TRUE(battle()->battleActiveUnit()->getAllBonuses(Selector::sourceTypeSel(BonusSource::HERO_COMMAND))->empty());
	EXPECT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_EQ(battle()->battleGetActiveDoctrine(BattleSide::ATTACKER), HeroCommand::NONE);
}

TEST_F(HeroCommandTest, WrongSideTargetsAndInvalidIdentifierAreRejectedBeforeState)
{
	prepareCommands();
	const auto starts = server.startedActions.size();
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
		BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE)));
	EXPECT_FALSE(issue(static_cast<HeroCommand>(127)));
	auto malformed = BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE);
	malformed.aimToUnit(battle()->battleActiveUnit());
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), malformed));
	EXPECT_EQ(server.startedActions.size(), starts);
	EXPECT_FALSE(battle()->getHeroCommandUsed(BattleSide::ATTACKER));
	EXPECT_TRUE(issue(HeroCommand::CHARGE));
}

class HeroActionBudgetTest : public HeroCommandFixture, public ::testing::WithParamInterface<std::tuple<int, int>> {};

TEST_P(HeroActionBudgetTest, EverySecondSpellOrderCombinationIsRejected)
{
	prepareCommands(true);
	const auto [first, second] = GetParam();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), heroAction(first)));
	const auto mana = attackerSideHero->getManaAvailable();
	const auto starts = server.startedActions.size();
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), heroAction(second)));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(server.startedActions.size(), starts);
	advanceRound();
	EXPECT_TRUE(battle()->battleCanUseHeroCommand(BattleSide::ATTACKER, HeroCommand::HOLD_THE_LINE));
}

INSTANTIATE_TEST_SUITE_P(AllNine, HeroActionBudgetTest,
	::testing::Combine(::testing::Values(0, 1, 2), ::testing::Values(0, 1, 2)));

TEST_F(HeroCommandTest, BattleSideAndActionRoundTripAndOldSideDefaults)
{
	prepareCommands();
	CMemorySerializer memory;
	SideInBattle source(gameState().get());
	source = battle()->getSide(BattleSide::ATTACKER);
	source.heroCommandUsed = true;
	source.activeDoctrine = HeroCommand::AGGRESSIVE;
	auto action = BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::DEFENSIVE);
	memory.oser & source;
	memory.oser & action;
	SideInBattle restored(gameState().get());
	BattleAction decoded;
	memory.iser & restored;
	memory.iser & decoded;
	EXPECT_TRUE(restored.heroCommandUsed);
	EXPECT_EQ(restored.activeDoctrine, HeroCommand::NONE);
	EXPECT_EQ(decoded.command, HeroCommand::DEFENSIVE);
	EXPECT_EQ(decoded.actionType, EActionType::HERO_COMMAND);

	CMemorySerializer legacy;
	legacy.oser.version = ESerializationVersion::TOWN_CUSTOM_INITIAL_GARRISON;
	legacy.iser.version = ESerializationVersion::TOWN_CUSTOM_INITIAL_GARRISON;
	SideInBattle old(gameState().get());
	legacy.oser & old;
	legacy.iser & restored;
	EXPECT_FALSE(restored.heroCommandUsed);
	EXPECT_EQ(restored.activeDoctrine, HeroCommand::NONE);
	EXPECT_EQ(restored.activeOrder, HeroCommand::NONE);
}

TEST_F(HeroCommandTest, LegacyAdvanceIdentitySurvivesSideDecodeUntilBattleNormalization)
{
	prepareCommands();
	SideInBattle source(gameState().get());
	source = battle()->getSide(BattleSide::ATTACKER);
	source.heroCommandUsed = true;
	source.activeOrder = HeroCommand::ADVANCE;
	// This fixture represents a pre-ledger side, not a lossy downgrade of an
	// active modern battle. Modern allowance downgrade rejection is tested separately.
	source.heroActionAllowances = {};

	CMemorySerializer memory;
	memory.oser.version = ESerializationVersion::HERO_COMMANDS;
	memory.iser.version = ESerializationVersion::HERO_COMMANDS;
	memory.oser & source;
	SideInBattle restored(gameState().get());
	memory.iser & restored;

	EXPECT_TRUE(restored.heroCommandUsed);
	EXPECT_EQ(restored.activeDoctrine, HeroCommand::NONE);
	EXPECT_EQ(restored.activeOrder, HeroCommand::ADVANCE);
}

TEST_F(HeroCommandTest, PerGameRulesSnapshotRoundTripsAndRefuseLossyLegacyWrites)
{
	startGame();
	ASSERT_EQ(gameState()->getHeroCommandRules()["rulesetVersion"].Integer(), heroCommands::ORDERS_ONLY_RULESET_VERSION);
	CMemorySerializer current;
	ASSERT_NO_THROW(current.oser & *gameState());
	CGameState restored;
	current.iser.cb = &restored;
	ASSERT_NO_THROW(current.iser & restored);
	EXPECT_EQ(restored.getHeroCommandRules(), gameState()->getHeroCommandRules());

	CMemorySerializer legacy;
	legacy.oser.version = ESerializationVersion::TOWN_CUSTOM_INITIAL_GARRISON;
	EXPECT_THROW(legacy.oser & *gameState(), std::runtime_error);
}

TEST(HeroCommandRulesTest, NamedSettingsArrayLoadsRealContent)
{
	GameSettings settings;
	JsonNode files;
	files.Vector().emplace_back("config/newHorizonsCombat");
	files.setModScope(ModScope::scopeBuiltin());
	settings.loadBase(files);
	const auto & rules = settings.getValue(EGameSettings::COMBAT_HERO_COMMANDS);
	EXPECT_EQ(rules["rulesetVersion"].Integer(), heroCommands::ORDERS_ONLY_RULESET_VERSION);
	EXPECT_NO_THROW(heroCommands::validateRules(rules));
}

TEST(HeroCommandRulesTest, FormulaIsCoefficientBasedAndUnknownRulesFailClosed)
{
	const JsonNode file(JsonPath::builtin("config/newHorizonsCombat"));
	auto rules = file["combat"]["heroCommands"];
	EXPECT_EQ(heroCommands::coefficient(rules["commands"]["charge"]["effects"]["meleeDamagePercent"], 20, 0), 14);
	rules["rulesetVersion"].Integer() = 2;
	EXPECT_THROW(heroCommands::validateRules(rules), std::runtime_error);
}
