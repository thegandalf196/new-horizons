/*
 * NewHorizonsResurrectionFoundationTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"

#include "../../SpellPointTestUtils.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/BattleInfo.h"
#include "../../../lib/battle/BattleUnitTurnReason.h"
#include "../../../lib/battle/CUnitState.h"
#include "../../../lib/battle/Unit.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/entities/hero/CHero.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/NewHorizonsSorcery.h"
#include "../../../lib/spells/effects/Effect.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

#include <array>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
constexpr std::string_view RESURRECTION_KEY = "core:resurrection";
constexpr int LEGACY_RESURRECTION_LEVEL = 3;
constexpr std::array<int, 4> LEGACY_RESURRECTION_COSTS = {12, 12, 10, 10};

HeroTypeID heroType(std::string_view identifier)
{
	return HeroTypeID(HeroTypeID::decode(std::string(identifier)));
}

CreatureID creature(std::string_view identifier)
{
	return CreatureID(CreatureID::decode(std::string(identifier)));
}

class NewHorizonsResurrectionFoundationTest : public HeroCommandFixture
{
protected:
	bool useRestorationMarker = true;
	CStack * friendly = nullptr;
	CStack * survivingAlly = nullptr;
	CStack * cloneCandidate = nullptr;
	CStack * phantomCandidate = nullptr;
	CStack * enemy = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the activated New Horizons preset";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
		if(!useRestorationMarker)
		{
			auto & resurrection = rules["spells"][std::string(RESURRECTION_KEY)];
			resurrection.Struct().erase("restoration");
			resurrection["level"].Integer() = LEGACY_RESURRECTION_LEVEL;
			auto & costs = resurrection["costs"].Vector();
			ASSERT_EQ(costs.size(), LEGACY_RESURRECTION_COSTS.size());
			for(size_t index = 0; index < LEGACY_RESURRECTION_COSTS.size(); ++index)
				costs[index].Integer() = LEGACY_RESURRECTION_COSTS[index];
			// This is an old resolved save snapshot, not a merge with current mod data.
			rules.setOverrideFlag(true);
		}
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, std::move(rules));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
	}

	void prepare(int spellPower = 25, int32_t friendlyCount = 3)
	{
		startGame();
		// Solmyr's historical specialty is unrelated to Resurrection; neither side
		// may inherit Alamar/Jeddite's legacy Resurrection modifier.
		attackerSideHero->setHeroType(heroType("core:solmyr"));
		defenderSideHero->setHeroType(heroType("core:solmyr"));
		attackerSideHero->clearSlots();
		defenderSideHero->clearSlots();
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creature("core:archangel"), friendlyCount));
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(1), creature("core:pikeman"), 1));
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(2), creature("core:archangel"), 1));
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(3), creature("core:archangel"), 1));
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), creature("core:peasant"), 1));

		for(auto * hero : {attackerSideHero, defenderSideHero})
		{
			if(!hero->getArt(ArtifactPosition::SPELLBOOK))
				giveArtifact(hero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
			hero->addSpellToSpellbook(SpellID::RESURRECTION);
			setTestSpellPointTotal(hero, 1000);
		}
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);

		startBattle();
		for(const auto * unit : battle()->battleGetAllUnits(false))
		{
			if(unit->unitSide() == BattleSide::ATTACKER && unit->unitSlot() == SlotID(0))
				friendly = const_cast<CStack *>(dynamic_cast<const CStack *>(unit));
			if(unit->unitSide() == BattleSide::ATTACKER && unit->unitSlot() == SlotID(1))
				survivingAlly = const_cast<CStack *>(dynamic_cast<const CStack *>(unit));
			if(unit->unitSide() == BattleSide::ATTACKER && unit->unitSlot() == SlotID(2))
				phantomCandidate = const_cast<CStack *>(dynamic_cast<const CStack *>(unit));
			if(unit->unitSide() == BattleSide::ATTACKER && unit->unitSlot() == SlotID(3))
				cloneCandidate = const_cast<CStack *>(dynamic_cast<const CStack *>(unit));
			if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == creature("core:peasant"))
				enemy = const_cast<CStack *>(dynamic_cast<const CStack *>(unit));
		}
		ASSERT_NE(friendly, nullptr);
		ASSERT_NE(survivingAlly, nullptr);
		ASSERT_NE(cloneCandidate, nullptr);
		ASSERT_NE(phantomCandidate, nullptr);
		ASSERT_NE(enemy, nullptr);
		beginCombat();
		ASSERT_EQ(battle()->battleGetOwner(battle()->battleActiveUnit()), PlayerColor(0));
	}

	SecondarySkill lightMagic() const
	{
		return SecondarySkill(SecondarySkill::decode(std::string(newHorizonsMagic::LIGHT_MAGIC_SKILL)));
	}

	SecondarySkill spellcraft() const
	{
		return SecondarySkill(SecondarySkill::decode(std::string(newHorizonsMagic::SPELLCRAFT_SKILL)));
	}

	const CSpell * resurrection() const
	{
		return SpellID(SpellID::RESURRECTION).toSpell();
	}

	void damage(CStack * stack, int64_t requestedDamage)
	{
		auto state = stack->acquireState();
		int64_t appliedDamage = requestedDamage;
		state->damage(appliedDamage);
		UnitChanges change(stack->unitId(), UnitChanges::EOperation::UPDATE);
		change.data = state->save();
		change.healthDelta = -appliedDamage;
		BattleUnitsChanged update;
		update.battleID = BattleID(0);
		update.changedStacks.push_back(std::move(change));
		gameHandler->sendAndApply(update);
	}

	CStack * addTemporaryStack(BattleSide side, BattleHex position, bool summoned)
	{
		battle::UnitInfo info;
		info.id = battle()->battleNextUnitId();
		info.count = 1;
		info.type = creature("core:archangel");
		info.side = side;
		info.position = position;
		info.summoned = summoned;
		BattleUnitsChanged add;
		add.battleID = BattleID(0);
		add.changedStacks.emplace_back(info.id, UnitChanges::EOperation::ADD);
		info.save(add.changedStacks.back().data);
		gameHandler->sendAndApply(add);
		return battle()->getStack(info.id);
	}

	void markClone(CStack * stack)
	{
		auto state = stack->acquireState();
		state->cloned = true;
		UnitChanges change(stack->unitId(), UnitChanges::EOperation::UPDATE);
		change.data = state->save();
		BattleUnitsChanged update;
		update.battleID = BattleID(0);
		update.changedStacks.push_back(std::move(change));
		gameHandler->sendAndApply(update);
	}

	void injureClone(CStack * stack, int64_t requestedDamage)
	{
		auto state = stack->acquireState();
		int64_t appliedDamage = requestedDamage;
		// A live Clone disappears on its first hit. Copying an already-wounded source
		// produces the same valid injured-Clone state without exercising that lifecycle.
		state->health.damage(appliedDamage);
		UnitChanges change(stack->unitId(), UnitChanges::EOperation::UPDATE);
		change.data = state->save();
		change.healthDelta = -appliedDamage;
		BattleUnitsChanged update;
		update.battleID = BattleID(0);
		update.changedStacks.push_back(std::move(change));
		gameHandler->sendAndApply(update);
	}

	void markPhantom(CStack * stack)
	{
		auto state = stack->acquireState();
		// The ordinary army slot keeps isSummoned() false; the explicit profile lets
		// this injured target exercise the separate Phantom exclusion.
		state->summoned = true;
		state->initializePhantomProfile(stack->getMaxHealth(),
			newHorizonsSorcery::PHANTOM_ARMY_DURATION_ROUNDS);
		UnitChanges change(stack->unitId(), UnitChanges::EOperation::UPDATE);
		change.data = state->save();
		BattleUnitsChanged update;
		update.battleID = BattleID(0);
		update.changedStacks.push_back(std::move(change));
		gameHandler->sendAndApply(update);
	}

	void activate(CStack * stack)
	{
		BattleSetActiveStack pack;
		pack.battleID = BattleID(0);
		pack.stack = stack->unitId();
		pack.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(pack);
		ASSERT_EQ(battle()->battleActiveUnit(), stack);
	}

	spells::effects::SpellEffectValue healthForecast(const CStack * target,
		const spells::Mechanics * mechanics) const
	{
		spells::Target aim;
		aim.emplace_back(target);
		const auto spellTarget = mechanics->canonicalizeTarget(aim);
		spells::effects::SpellEffectValue total;
		mechanics->forEachEffect([&](const spells::effects::Effect & effect)
		{
			const auto affected = effect.transformTarget(mechanics, aim, spellTarget);
			total += effect.getHealthChange(mechanics, affected);
			return false;
		});
		return total;
	}

	BattleAction resurrectionAction(const CStack * target) const
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = SpellID::RESURRECTION;
		action.aimToUnit(target);
		return action;
	}

	void expectRejected(const CStack * target)
	{
		ASSERT_EQ(battle()->battleActiveUnit(), survivingAlly);
		ASSERT_EQ(battle()->battleGetOwner(battle()->battleActiveUnit()), PlayerColor(0));
		ASSERT_TRUE(target->isValidTarget(false));
		ASSERT_LT(target->getAvailableHealth(), target->getTotalHealth());
		const auto * spell = resurrection();
		ASSERT_NE(spell, nullptr);
		spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
		const auto mechanics = spell->battleMechanics(&cast);
		spells::Target aim;
		aim.emplace_back(target);
		EXPECT_FALSE(mechanics->canBeCastAt(aim));
		const auto manaBefore = attackerSideHero->getManaAvailable();
		EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(
			BattleID(0), PlayerColor(0), resurrectionAction(target)));
		EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
		EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 0);
	}
};
}

TEST_F(NewHorizonsResurrectionFoundationTest, SavedV3RowSetsAllRanksAndScalesOnlySpellPower)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto * spell = resurrection();
	ASSERT_NE(spell, nullptr);
	const auto & rules = battle()->getMagicRules();
	ASSERT_NO_THROW(newHorizonsMagic::validateRules(rules));
	ASSERT_TRUE(newHorizonsMagic::resurrectionRestorationEnabled(rules, SpellID::RESURRECTION));
	EXPECT_EQ(newHorizonsMagic::spellLevel(rules, SpellID::RESURRECTION), 5);

	const auto light = lightMagic();
	const auto spellcraftSkill = spellcraft();
	ASSERT_TRUE(light.hasValue());
	ASSERT_TRUE(spellcraftSkill.hasValue());
	attackerSideHero->setSecSkillLevel(spellcraftSkill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(light, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 0, ChangeValueMode::ABSOLUTE);
	spells::BattleCast fixedBaseCast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	EXPECT_EQ(spell->battleMechanics(&fixedBaseCast)->getEffectValue(), 100)
		<< "School and Spellcraft modifiers cannot amplify the fixed restoration pool";
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 25, ChangeValueMode::ABSOLUTE);
	for(int rank = 0; rank < 4; ++rank)
	{
		attackerSideHero->setSecSkillLevel(light, rank, ChangeValueMode::ABSOLUTE);
		EXPECT_EQ(newHorizonsMagic::spellCost(rules, SpellID::RESURRECTION, rank), 22);
		EXPECT_EQ(battle()->battleGetSpellCost(spell, attackerSideHero), 22);
		spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
		const auto mechanics = spell->battleMechanics(&cast);
		ASSERT_TRUE(mechanics->isNewHorizonsResurrection());
		const int coefficient = newHorizonsMagic::spellPowerCoefficientBasisPoints(
			rules, attackerSideHero, SpellID::RESURRECTION);
		const int64_t expectedPool = 100 + 100LL * coefficient / 10000;
		EXPECT_EQ(mechanics->getEffectValue(), expectedPool) << "Light rank " << rank;
	}

	JsonNode malformed = rules;
	malformed["spells"][std::string(RESURRECTION_KEY)]["restoration"]["version"].Integer() = 2;
	EXPECT_THROW(newHorizonsMagic::validateRules(malformed), std::runtime_error);
	malformed = rules;
	malformed["spells"][std::string(RESURRECTION_KEY)]["level"].Integer() = 4;
	EXPECT_THROW(newHorizonsMagic::validateRules(malformed), std::runtime_error);
	malformed = rules;
	malformed["spells"][std::string(RESURRECTION_KEY)]["costs"].Vector()[0].Integer() = 21;
	EXPECT_THROW(newHorizonsMagic::validateRules(malformed), std::runtime_error);
	malformed = rules;
	malformed["spells"]["core:cure"]["restoration"] =
		malformed["spells"][std::string(RESURRECTION_KEY)]["restoration"];
	EXPECT_THROW(newHorizonsMagic::validateRules(malformed), std::runtime_error);

	JsonNode oldSnapshot = rules;
	auto & oldRow = oldSnapshot["spells"][std::string(RESURRECTION_KEY)];
	oldRow.Struct().erase("restoration");
	oldRow["level"].Integer() = LEGACY_RESURRECTION_LEVEL;
	for(size_t index = 0; index < LEGACY_RESURRECTION_COSTS.size(); ++index)
		oldRow["costs"].Vector()[index].Integer() = LEGACY_RESURRECTION_COSTS[index];
	oldSnapshot.setOverrideFlag(true);
	ASSERT_NO_THROW(newHorizonsMagic::validateRules(oldSnapshot));
	EXPECT_FALSE(newHorizonsMagic::resurrectionRestorationEnabled(oldSnapshot, SpellID::RESURRECTION));
	EXPECT_EQ(newHorizonsMagic::spellLevel(oldSnapshot, SpellID::RESURRECTION), 3);
	for(int rank = 0; rank < 4; ++rank)
		EXPECT_EQ(newHorizonsMagic::spellCost(oldSnapshot, SpellID::RESURRECTION, rank),
			LEGACY_RESURRECTION_COSTS[static_cast<size_t>(rank)]);

	CMemorySerializer memory;
	memory.oser.version = ESerializationVersion::CURRENT;
	memory.iser.version = ESerializationVersion::CURRENT;
	memory.oser & *gameState();
	CGameState restored;
	memory.iser.cb = &restored;
	memory.iser.loadingGamestate = true;
	memory.iser & restored;
	EXPECT_TRUE(newHorizonsMagic::resurrectionRestorationEnabled(
		restored.getMagicRules(), SpellID::RESURRECTION));
}

TEST_F(NewHorizonsResurrectionFoundationTest, AcceptedCastMatchesLuaForecastAndPermanentlyRestoresCasualties)
{
	ASSERT_NO_FATAL_FAILURE(prepare(50));
	attackerSideHero->setSecSkillLevel(lightMagic(), MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(spellcraft(), MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	const auto maxHealth = static_cast<int64_t>(friendly->getMaxHealth());
	ASSERT_GT(maxHealth, 1);
	ASSERT_NO_FATAL_FAILURE(damage(friendly, maxHealth + maxHealth * 2 / 5));
	ASSERT_EQ(friendly->getCount(), 2);
	ASSERT_EQ(friendly->getAvailableHealth(), 2 * maxHealth - maxHealth * 2 / 5);
	const int countBefore = friendly->getCount();
	const int64_t healthBefore = friendly->getAvailableHealth();
	ASSERT_LT(countBefore, friendly->unitBaseAmount());

	auto curse = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::PRIMARY_SKILL,
		BonusSource::SPELL_EFFECT, -1, BonusSourceID(SpellID(SpellID::CURSE)),
		BonusSubtypeID(PrimarySkill::ATTACK));
	curse->turnsRemain = 3;
	friendly->addNewBonus(curse);
	ASSERT_TRUE(friendly->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(SpellID(SpellID::CURSE)))));

	const auto * spell = resurrection();
	ASSERT_NE(spell, nullptr);
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&cast);
	ASSERT_TRUE(mechanics->isNewHorizonsResurrection());
	spells::Target aim;
	aim.emplace_back(friendly);
	ASSERT_TRUE(mechanics->canBeCastAt(aim));
	const auto forecast = healthForecast(friendly, mechanics.get());
	ASSERT_EQ(mechanics->getEffectValue(), 300);
	EXPECT_EQ(forecast.hpDelta, 300);
	EXPECT_EQ(forecast.unitsDelta, 1);
	const int manaBefore = attackerSideHero->getManaAvailable();
	ASSERT_EQ(battle()->battleGetSpellCost(spell, attackerSideHero), 22);

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(
		BattleID(0), PlayerColor(0), resurrectionAction(friendly)));
	EXPECT_EQ(friendly->getAvailableHealth() - healthBefore, forecast.hpDelta);
	EXPECT_EQ(friendly->getCount() - countBefore, forecast.unitsDelta);
	EXPECT_EQ(friendly->getCount(), 3);
	EXPECT_LE(friendly->getCount(), friendly->unitBaseAmount()) << "Resurrection cannot exceed its battle-start count";
	EXPECT_EQ(friendly->health.getResurrected(), 0) << "Restored creatures are real, not one-battle casualties";
	EXPECT_EQ(manaBefore - attackerSideHero->getManaAvailable(), 22);
	EXPECT_TRUE(battle()->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	EXPECT_TRUE(friendly->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(SpellID(SpellID::CURSE))))) << "New Horizons Resurrection does not apply legacy magical dispel";
}

TEST_F(NewHorizonsResurrectionFoundationTest, TemporaryStacksAreRejectedButAccessibleFullCorpseCanReturn)
{
	ASSERT_NO_FATAL_FAILURE(prepare(200));
	const auto maxHealth = static_cast<int64_t>(friendly->getMaxHealth());
	ASSERT_NO_FATAL_FAILURE(damage(friendly, maxHealth * friendly->unitBaseAmount()));
	ASSERT_TRUE(friendly->isDead());
	ASSERT_TRUE(friendly->getPosition().isValid());
	activate(survivingAlly);

	auto * summoned = addTemporaryStack(BattleSide::ATTACKER, BattleHex(8, 3), true);
	ASSERT_NE(summoned, nullptr);
	ASSERT_NO_FATAL_FAILURE(damage(summoned, 1));
	ASSERT_NO_FATAL_FAILURE(damage(cloneCandidate, 1));
	markClone(cloneCandidate);
	markPhantom(phantomCandidate);
	ASSERT_NO_FATAL_FAILURE(damage(phantomCandidate, 1));
	ASSERT_TRUE(summoned->isSummoned());
	ASSERT_LT(summoned->getAvailableHealth(), summoned->getTotalHealth());
	ASSERT_FALSE(cloneCandidate->isSummoned());
	ASSERT_TRUE(cloneCandidate->isClone());
	ASSERT_LT(cloneCandidate->getAvailableHealth(), cloneCandidate->getTotalHealth());
	ASSERT_FALSE(phantomCandidate->isSummoned());
	ASSERT_GT(phantomCandidate->getPhantomInitialIntegrity(), 0);
	ASSERT_LT(phantomCandidate->getAvailableHealth(), phantomCandidate->getTotalHealth());

	const auto manaBeforeRejections = attackerSideHero->getManaAvailable();
	for(const auto * temporary : {summoned, cloneCandidate, phantomCandidate})
		expectRejected(temporary);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBeforeRejections);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 0);

	const auto * spell = resurrection();
	ASSERT_NE(spell, nullptr);
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&cast);
	spells::Target corpse;
	corpse.emplace_back(friendly);
	ASSERT_TRUE(mechanics->isNewHorizonsResurrection());
	ASSERT_TRUE(mechanics->canBeCastAt(corpse)) << "Accessible corpse remains a valid restoration target";
	ASSERT_EQ(mechanics->getEffectValue(), 900);
	const auto forecast = healthForecast(friendly, mechanics.get());
	ASSERT_GT(mechanics->getEffectValue(), maxHealth * friendly->unitBaseAmount());
	ASSERT_EQ(forecast.hpDelta, maxHealth * friendly->unitBaseAmount());
	ASSERT_EQ(forecast.unitsDelta, friendly->unitBaseAmount());
	const BattleHex corpsePosition = friendly->getPosition();

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(
		BattleID(0), PlayerColor(0), resurrectionAction(friendly)));
	EXPECT_TRUE(friendly->alive());
	EXPECT_EQ(friendly->getCount(), forecast.unitsDelta);
	EXPECT_EQ(friendly->getAvailableHealth(), forecast.hpDelta);
	EXPECT_EQ(friendly->getPosition(), corpsePosition);
	EXPECT_LE(friendly->getCount(), friendly->unitBaseAmount());
	EXPECT_EQ(friendly->health.getResurrected(), 0);
}

TEST_F(NewHorizonsResurrectionFoundationTest, SmallWoundNeedsNoFullCreatureThreshold)
{
	ASSERT_NO_FATAL_FAILURE(prepare(0));
	const int64_t healthBeforeDamage = friendly->getAvailableHealth();
	ASSERT_NO_FATAL_FAILURE(damage(friendly, 10));
	ASSERT_EQ(friendly->getCount(), friendly->unitBaseAmount());
	ASSERT_EQ(friendly->getTotalHealth() - friendly->getAvailableHealth(), 10);
	const int64_t woundedHealth = friendly->getAvailableHealth();

	const auto * spell = resurrection();
	ASSERT_NE(spell, nullptr);
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&cast);
	ASSERT_TRUE(mechanics->isNewHorizonsResurrection());
	spells::Target aim;
	aim.emplace_back(friendly);
	ASSERT_TRUE(mechanics->canBeCastAt(aim));
	ASSERT_EQ(mechanics->getEffectValue(), 100);
	const auto forecast = healthForecast(friendly, mechanics.get());
	ASSERT_EQ(forecast.hpDelta, 10);
	ASSERT_EQ(forecast.unitsDelta, 0);

	const int manaBefore = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(
		BattleID(0), PlayerColor(0), resurrectionAction(friendly)));
	EXPECT_EQ(friendly->getAvailableHealth() - woundedHealth, forecast.hpDelta);
	EXPECT_EQ(friendly->getAvailableHealth(), healthBeforeDamage);
	EXPECT_EQ(friendly->getCount(), friendly->unitBaseAmount());
	EXPECT_EQ(friendly->health.getResurrected(), 0);
	EXPECT_EQ(manaBefore - attackerSideHero->getManaAvailable(), 22);
}

TEST_F(NewHorizonsResurrectionFoundationTest, MarkerlessSavedV3SnapshotKeepsLegacyLevelCostAndEffect)
{
	useRestorationMarker = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto & rules = battle()->getMagicRules();
	ASSERT_NO_THROW(newHorizonsMagic::validateRules(rules));
	EXPECT_FALSE(newHorizonsMagic::resurrectionRestorationEnabled(rules, SpellID::RESURRECTION));
	EXPECT_EQ(newHorizonsMagic::spellLevel(rules, SpellID::RESURRECTION), LEGACY_RESURRECTION_LEVEL);
	for(int rank = 0; rank < 4; ++rank)
		EXPECT_EQ(newHorizonsMagic::spellCost(rules, SpellID::RESURRECTION, rank),
			LEGACY_RESURRECTION_COSTS[static_cast<size_t>(rank)]);

	const auto * spell = resurrection();
	ASSERT_NE(spell, nullptr);
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&cast);
	EXPECT_FALSE(mechanics->isNewHorizonsResurrection());
	EXPECT_EQ(mechanics->getEffectValue(), spell->calculateRawEffectValue(mechanics->getEffectLevel(),
		mechanics->getEffectPower(), 1, mechanics->getEffectPowerDivisor()));

	CMemorySerializer memory;
	memory.oser.version = ESerializationVersion::CURRENT;
	memory.iser.version = ESerializationVersion::CURRENT;
	memory.oser & *gameState();
	CGameState restored;
	memory.iser.cb = &restored;
	memory.iser.loadingGamestate = true;
	memory.iser & restored;
	EXPECT_FALSE(newHorizonsMagic::resurrectionRestorationEnabled(
		restored.getMagicRules(), SpellID::RESURRECTION));
}
