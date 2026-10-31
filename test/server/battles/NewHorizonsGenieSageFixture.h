/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "HeroCommandFixture.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/battle/Destination.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../server/ServerSpellCastEnvironment.h"
#include "../../../lib/entities/hero/CHeroHandler.h"
#include "../../../lib/entities/hero/NewHorizonsGenieSage.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusSelector.h"
#include "../../../lib/serializer/CMemorySerializer.h"
class NewHorizonsGenieSageFixture : public HeroCommandFixture
{
protected:
	bool historical = false;
	CStack * genie = nullptr;
	CStack * ally = nullptr;
	CStack * enemy = nullptr;
	void mapLoaded(CMap * map) override
	{
		useRevisedOrders = true;
		HeroCommandFixture::mapLoaded(map);
		auto rules = JsonNode(JsonPath::builtin("config/newHorizonsHeroes"));
		if(historical)
		{
			rules.Struct().erase("genieSageSpecialties");
			rules["startingSkills"]["startingDevelopmentProfiles"]["core:thane"]["startingPerks"].Vector()[0]["perk"].String()
				= "new-horizons:learning.scholar";
		}
		rules.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}
	void startAs(const char * key)
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(key)), PlayerColor(0)).heroExperience(0).heroGarrison({{CreatureID(0), 1}})
			.hero({7, 7, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PlayerColor(1)).heroExperience(0).heroGarrison({{CreatureID(0), 1}});
		startWithMap(std::move(builder));
		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		gameHandler->randomizer->setSeed(seed);
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		defenderSideHero = findHeroByOwner(PlayerColor(1));
	}
	void prepareGenie(bool master = false, const char * hero = "core:iona")
	{
		startAs(hero);
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		genie = addStack(BattleSide::ATTACKER, creatureByName(master ? "core:masterGenie" : "core:genie"), BattleHex(2, 2), 1);
		ally = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(9, 5), 20);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(11, 5), 100);
		beginCombat();
		activateGenie(false);
		ASSERT_TRUE(issue(HeroCommand::HOLD_THE_LINE));
	}
	void activateGenie(bool nextRound = true)
	{
		if(nextRound)
			advanceRound();
		BattleSetActiveStack active;
		active.battleID = BattleID(0);
		active.stack = genie->unitId();
		active.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(active);
	}
	static SpellID bestow() { return SpellID(SpellID::decode("new-horizons:bestowLuck")); }
	static SpellID turn() { return SpellID(SpellID::decode("new-horizons:turnOfFate")); }
	bool castGenie(CStack * recipient, SpellID spell)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			BattleAction::makeCreatureSpellcast(genie, {spells::Destination(recipient)}, spell));
	}
	void passive(CStack * recipient, SpellID spell, const CGHeroInstance * caster)
	{
		spells::BattleCast request(battle(), caster, spells::Mode::PASSIVE, spell.toSpell());
		request.setSpellLevel(0);
		const auto mechanics = spell.toSpell()->battleMechanics(&request);
		const spells::Target target{spells::Destination(recipient)};
		ASSERT_TRUE(mechanics->canBeCastAt(target));
		mechanics->cast(gameHandler->spellEnv.get(), target);
	}
	static auto wire(const auto & object)
	{
		CMemorySerializer memory;
		memory.oser & object;
		return memory.extractBuffer();
	}
	static TConstBonusListPtr luckFamily(const CStack & stack, SpellID family)
	{
		return stack.getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(family)));
	}
};
