/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "HeroCommandFixture.h"
#include "FullGameSnapshotTypes.h"
#include "../../../lib/nativeRules/PreparedPilotRules.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/campaign/CampaignState.h"
#include <algorithm>
class PreparedPilotFixture : public HeroCommandFixture
{
protected:
	bool authored = true;
	bool customValues = false;
	std::string heroName = "core:fiona";
	std::string creatureName = "core:hellHound";
	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		JsonNode perks(JsonPath::builtin("config/newHorizonsPerks"));
		JsonNode heroes(JsonPath::builtin("config/newHorizonsHeroes"));
		if(!authored)
		{
			for(auto & [key, skill] : perks["skills"].Struct())
			{
				for(auto & [rank, row] : skill["ranks"].Struct()) row.Struct().erase("mechanics");
				for(auto & row : skill["perks"].Vector()) row.Struct().erase("mechanics");
			}
			heroes["additionalSpecialties"].Struct().erase("preparedCreatureSpecialties");
			auto & legacy = heroes["additionalSpecialties"]["ordinaryCreatureSpecialties"];
			legacy["packPursuitSpeed"].Integer() = 5;
			legacy["resistanceAuraPoints"].Integer() = 10;
			legacy["prismaticScreenReductionPercent"].Integer() = 15;
		}
		else if(customValues)
		{
			int value = 9;
			for(const auto * rank : {"basic", "advanced", "expert"})
			{
				perks["skills"]["new-horizons:discipline"]["ranks"][rank]["mechanics"]["operations"].Vector()[0]["value"].Integer() = value;
				value += 2;
			}
			heroes["additionalSpecialties"]["preparedCreatureSpecialties"]["heroes"]["core:fiona"]["mechanics"]["operations"].Vector()[0]["value"].Integer() = 13;
		}
		perks.setOverrideFlag(true);
		heroes.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perks);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, heroes);
	}
	void prepareWorld()
	{
		useRevisedOrders = true;
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(heroName)), PlayerColor(0)).heroExperience(0)
			.heroGarrison({{CreatureID(CreatureID::decode(creatureName)), 10}})
			.hero({7, 7, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PlayerColor(1))
			.heroGarrison({{CreatureID(CreatureID::decode("core:archer")), 100}});
		startWithMap(std::move(builder));
		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		defenderSideHero = findHeroByOwner(PlayerColor(1));
	}
	void learnPerk(const std::string & skill, const std::string & perk)
	{
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:wisdom")), 3, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(skill)), 3, ChangeValueMode::ABSOLUTE);
		const auto & rows = attackerSideHero->getPerkState().rules["skills"][skill]["perks"].Vector();
		const auto target = std::find_if(rows.begin(), rows.end(), [&](const auto & row) { return row["id"].String() == perk; });
		ASSERT_NE(target, rows.end());
		const int required = newHorizonsHeroes::perkRequiredRank((*target)["requires"].String());
		for(int tier = 1; tier <= required; ++tier)
		{
			const auto candidate = tier == required ? target : std::find_if(rows.begin(), rows.end(), [&](const auto & row)
			{
				return newHorizonsHeroes::perkRequiredRank(row["requires"].String()) == tier && row["effect"]["status"].String() == "active";
			});
			ASSERT_NE(candidate, rows.end());
			attackerSideHero->applyPerkSelection({skill, (*candidate)["id"].String()});
		}
		ASSERT_TRUE(attackerSideHero->hasActivePerk(skill, perk));
	}
	const battle::Unit * mainUnit(BattleSide side) const
	{
		for(const auto * unit : battle()->battleGetAllUnits(false))
			if(unit->unitSide() == side && unit->unitSlot() != SlotID::WAR_MACHINES_SLOT
				&& unit->unitSlot() != SlotID::COMMANDER_SLOT_PLACEHOLDER && !unit->isTurret()) return unit;
		return nullptr;
	}
	bool cast(SpellID spell, const battle::Unit * target)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = spell;
		action.aimToUnit(target);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}
};
