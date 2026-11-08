/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/battle/NewHorizonsCommandResources.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/NewHorizonsRunicReflection.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/NewHorizonsPandemonium.h"
#include "../../../lib/spells/NewHorizonsNaturesWrath.h"
#include "../../../lib/battle/NewHorizonsFrozen.h"
#include "../../../lib/battle/NewHorizonsDebuffStatuses.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/spells/RunicCreatureCaster.h"
#include "../../../lib/spells/AbilityCaster.h"
#include "../../../lib/spells/BonusCaster.h"
#include "../../../lib/spells/ExternalCaster.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/BattleInfo.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/effects/native/Damage.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/texts/CGeneralTextHandler.h"

class NewHorizonsRunicReflectionCombatTest : public HeroCommandFixture
{
protected:
	CStack * casterArmy = nullptr;
	CStack * dwarf = nullptr;
	std::string attackerHeroIdentity = "core:orrin";
	void SetUp() override
	{
		HeroCommandFixture::SetUp(); useRevisedOrders = true;
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires shipped New Horizons content";
	}
	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		for(const auto & [setting, path] : std::vector<std::pair<EGameSettings, std::string>>{
			{EGameSettings::HEROES_NEW_HORIZONS, "config/newHorizonsHeroes"},
			{EGameSettings::HEROES_NEW_HORIZONS_PERKS, "config/newHorizonsPerks"},
			{EGameSettings::MAGIC_NEW_HORIZONS, "config/newHorizonsMagic"}})
			map->overrideGameSetting(setting, JsonNode(JsonPath::builtin(path)));
		auto * attacker = findHeroByOwner(PlayerColor(0));
		auto * defender = findHeroByOwner(PlayerColor(1));
		ASSERT_NE(attacker, nullptr);
		ASSERT_NE(defender, nullptr);
		attacker->setHeroType(HeroTypeID(HeroTypeID::decode(attackerHeroIdentity)));
		defender->setHeroType(HeroTypeID(HeroTypeID::decode("core:ufretin")));
	}
	void prepare(int additionalResistance = 0, bool bothRunic = false, bool conduit = false, bool twist = false)
	{
		attackerHeroIdentity = conduit ? "core:brissa" : bothRunic ? "core:ufretin" : "core:orrin";
		startGame();
		if(twist)
		{
			const SecondarySkill luck(SecondarySkill::decode("new-horizons:luck"));
			for(const auto & [rank, perk] : {std::pair{1, "fortuneSFavor"}, std::pair{2, "serendipity"}, std::pair{3, "twistOfFate"}})
			{
				defenderSideHero->setSecSkillLevel(luck, rank, ChangeValueMode::ABSOLUTE);
				defenderSideHero->applyPerkSelection({"new-horizons:luck", std::string("new-horizons:luck.") + perk});
			}
		}
		if(conduit)
		{
			attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:elementalRebirth")), 1, ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({"new-horizons:elementalRebirth", "new-horizons:elementalRebirth.elementalConduit"});
		}
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 0, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:sorceryMagic")), 1, ChangeValueMode::ABSOLUTE);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(SpellID::MAGIC_ARROW);
		setTestSpellPointTotal(attackerSideHero, 1000);
		startBattle();
		BattleUnitsChanged remove; remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		casterArmy = addStack(BattleSide::ATTACKER, creatureByName(bothRunic ? "core:dwarf" : "core:pikeman"), BattleHex(70), 1000);
		dwarf = addStack(BattleSide::DEFENDER, creatureByName("core:battleDwarf"), BattleHex(72), 1000);
		for(auto * unit : {casterArmy, dwarf})
			unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
				BonusType::NO_MORALE, BonusSource::OTHER, 1, BonusSourceID()));
		if(additionalResistance)
			dwarf->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
				BonusType::MAGIC_RESISTANCE, BonusSource::OTHER, additionalResistance, BonusSourceID()));
		beginCombat();
		BattleSetActiveStack active; active.battleID = BattleID(0);
		active.stack = casterArmy->unitId(); active.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(active);
		ASSERT_EQ(newHorizonsRunicReflection::nativeChance(*dwarf), 40);
	}
	static std::vector<std::byte> wire(const auto & value)
	{
		CMemorySerializer bytes; bytes.oser & value; return bytes.extractBuffer();
	}
	std::vector<std::byte> chooseSample(int low, int high, bool reflected, int redirectedLow = 0, int redirectedHigh = 100)
	{
		for(int seed = 0; seed != 4096; ++seed)
		{
			GameRandomizer expected(*gameState()); expected.setSeed(seed);
			int sample = -1;
			for(const auto * unit : battle()->battleGetAllUnits(false))
			{
				const auto value = expected.getDefault().nextInt(0, 99);
				if(unit == dwarf) sample = value;
			}
			if(sample < low || sample >= high) continue;
			if(reflected)
			{
				const std::vector<const battle::Unit *> candidates{casterArmy};
				(void)RandomGeneratorUtil::nextItem(candidates, expected.getDefault());
				const int redirected = expected.getDefault().nextInt(0, 99);
				if(redirected < redirectedLow || redirected >= redirectedHigh) continue;
			}
			gameHandler->randomizer->setSeed(seed);
			return wire(expected);
		}
		throw std::logic_error("No deterministic resistance sample found");
	}
	std::vector<std::byte> chooseSingleHostileSample()
	{
		if(battle()->battleGetAllUnits(false).size() != 2)
			throw std::logic_error("Hostile-only RNG fixture requires exactly two stacks");
		for(int seed = 0; seed != 4096; ++seed)
		{
			GameRandomizer expected(*gameState()); expected.setSeed(seed);
			if(expected.getDefault().nextInt(0, 99) >= 40) continue;
			const std::vector<const battle::Unit *> candidates{casterArmy};
			(void)RandomGeneratorUtil::nextItem(candidates, expected.getDefault());
			(void)expected.getDefault().nextInt(0, 99);
			gameHandler->randomizer->setSeed(seed);
			return wire(expected);
		}
		throw std::logic_error("No hostile-only deterministic sample found");
	}

	bool castArrow()
	{
		BattleAction action; action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER; action.spell = SpellID::MAGIC_ARROW; action.aimToUnit(dwarf);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}
};

