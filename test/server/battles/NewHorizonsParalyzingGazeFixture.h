/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include "BattleTestFixture.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/NewHorizonsParalyzingGaze.h"
#include "../../../lib/battle/NewHorizonsArmorer.h"
#include "../../../lib/entities/hero/NewHorizonsParalyzingGaze.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
#include "../../../server/battles/NewHorizonsParalyzingGaze.h"
class NewHorizonsParalyzingGazeBattleTest : public BattleTestFixture
{
protected:
	bool captured = true;
	CStack * source = nullptr;
	CStack * target = nullptr;
	CStack * melee = nullptr;
	void mapLoaded(CMap * map) override
	{
		BattleTestFixture::mapLoaded(map);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		if(!captured)
			rules["additionalSpecialties"].Struct().erase("paralyzingGaze");
		rules.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
		for(const auto & object : map->objects)
			if(auto * hero = dynamic_cast<CGHeroInstance *>(object.get()); hero && hero->getOwner() == PlayerColor(0))
				hero->setHeroType(HeroTypeID(HeroTypeID::decode("core:ajit")));
	}
	void prepare(bool evilEye = false)
	{
		startGame();
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		source = addStack(BattleSide::ATTACKER, creatureByName(evilEye ? "core:evilEye" : "core:beholder"), BattleHex(3, 5), 1);
		target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(12, 5), 100);
		melee = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(11, 5), 100);
		for(auto * unit : {source, target, melee})
		{
			forceMaximumDamage(unit);
			unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
				BonusType::NO_MORALE, BonusSource::OTHER, 1, BonusSourceID()));
		}
		beginCombat();
		battle()->activeStack = source->unitId();
	}
	void install(const CStack * recipient)
	{
		SetStackEffect effect;
		effect.battleID = battle()->getBattleID();
		effect.toAdd.emplace_back(recipient->unitId(), std::vector<Bonus>{newHorizonsParalyzingGaze::marker()});
		gameHandler->sendAndApply(effect);
	}
	bool shoot()
	{
		battle()->activeStack = source->unitId();
		return gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(), PlayerColor(0),
			BattleAction::makeShotAttack(source, target));
	}
	void give(CStack * recipient, BonusType type, int value = 1)
	{
		recipient->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			type, BonusSource::OTHER, value, BonusSourceID()));
	}
	void accept(CGHeroInstance * hero, const char * perk)
	{
		const auto rank = [hero](const std::string & id) { return hero->getPerkSkillRank(id); };
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer(rank, seed, hero->getPerkOfferWeightContext());
			const auto choice = std::ranges::find_if(offer, [perk](const auto & entry)
				{ return entry.selection.skillId == "new-horizons:armorer" && entry.selection.perkId == perk; });
			if(choice == offer.end()) continue;
			gameHandler->levelUpHero(hero, offer, std::distance(offer.begin(), choice), seed, false);
			ASSERT_TRUE(hero->hasActivePerk("new-horizons:armorer", perk));
			return;
		}
		FAIL() << "No legitimate Defiant offer";
	}
	void acquireDefiant()
	{
		const SecondarySkill skill(SecondarySkill::decode("new-horizons:armorer"));
		defenderSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		ASSERT_NO_FATAL_FAILURE(accept(defenderSideHero, "new-horizons:armorer.ironDiscipline"));
		defenderSideHero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		ASSERT_NO_FATAL_FAILURE(accept(defenderSideHero, "new-horizons:armorer.defiant"));
	}
};
