/*
 * Part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include "../mock/TinyMapGameTest.h"
#include "../mock/GameHandlerTestServer.h"
#include "battles/FullGameSnapshotTypes.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/GameSettings.h"
#include "../../lib/StartInfo.h"
#include "../../lib/entities/hero/NewHorizonsPerkState.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapObjects/MiscObjects.h"
#include "../../lib/mapObjects/NewHorizonsEstates.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/networkPacks/StackLocation.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../server/CGameHandler.h"
#include "../../server/queries/MapQueries.h"
#include "../../server/queries/QueriesProcessor.h"

namespace estatesTest
{
inline const PlayerColor PLAYER(0);
inline const std::string ESTATES = "new-horizons:estates";
inline CreatureID creature(const char * name) { return CreatureID(CreatureID::decode(name)); }

class Fixture : public TinyMapGameTest
{
protected:
    CGHeroInstance * first = nullptr;
    CGHeroInstance * second = nullptr;
    CGTownInstance * town = nullptr;
    CGMine * mine = nullptr;
    CGMine * otherMine = nullptr;
    CGDwelling * dwelling = nullptr;
    int incomePercent = 100;
    std::string planned;

    void SetUp() override
    {
        TinyMapGameTest::SetUp();
        if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
            GTEST_SKIP() << "Requires New Horizons content";
    }
    void configurePlayer(PlayerSettings & settings) const override
    {
        settings.bonus = PlayerStartingBonus::GOLD;
        settings.handicap.percentIncome = incomePercent;
    }
    void mapLoaded(CMap * loaded) override
    {
        TinyMapGameTest::mapLoaded(loaded);
        JsonNode days;
        days.Integer() = 7;
        loaded->overrideGameSetting(EGameSettings::GENERAL_DAYS_PER_WEEK, days);
        loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
        loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
        loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
        JsonNode perks(JsonPath::builtin("config/newHorizonsPerks"));
        for(auto & perk : perks["skills"][ESTATES]["perks"].Vector())
            if(perk["id"].String() == planned)
                perk["effect"]["status"].String() = "planned";
        loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perks));
    }
    void startGame()
    {
        TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
        builder.size(36).playerActive(PLAYER)
            .town({7, 7, 0}, FactionID::CASTLE, PLAYER).townGarrison({})
            .hero({30, 30, 0}, HeroTypeID(0), PLAYER).heroGarrison({{creature("core:pikeman"), 1}})
            .hero({30, 26, 0}, HeroTypeID(1), PLAYER).heroGarrison({{creature("core:pikeman"), 1}})
            .mine({15, 15, 0}, MapObjectSubID(0), PLAYER)
            .mine({19, 15, 0}, MapObjectSubID(0), PLAYER)
            .dwelling({24, 20, 0}, MapObjectSubID(56), PLAYER);
        startWithMap(std::move(builder));
        first = findHeroAt({30, 30, 0});
        second = findHeroAt({30, 26, 0});
        town = expectAt<CGTownInstance>({7, 7, 0});
        mine = expectAt<CGMine>({15, 15, 0});
        otherMine = expectAt<CGMine>({19, 15, 0});
        dwelling = expectAt<CGDwelling>({24, 20, 0});
        ASSERT_TRUE(first && second && town && mine && otherMine && dwelling);
    }
    void select(CGHeroInstance * hero, const char * basic = "prosperity", const char * advanced = nullptr, const char * expert = nullptr)
    {
        const auto skill = SecondarySkill(SecondarySkill::decode(ESTATES));
        const_cast<newHorizonsHeroes::PerkState &>(hero->getPerkState()).selected.clear();
        hero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
        hero->applyPerkSelection({ESTATES, ESTATES + "." + basic});
        if(advanced)
        {
            hero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
            hero->applyPerkSelection({ESTATES, ESTATES + "." + advanced});
        }
        if(expert)
        {
            hero->setSecSkillLevel(skill, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
            hero->applyPerkSelection({ESTATES, ESTATES + "." + expert});
        }
    }
    void setResources(CGameHandler & handler, const ResourceSet & amount)
    {
        handler.giveResources(PLAYER, amount - gameState()->getPlayerState(PLAYER)->resources);
    }
    int rogues(const CGMine * target) const
    {
        int result = 0;
        for(const auto & [slot, stack] : target->Slots())
            if(stack->getCreatureID() == creature("core:rogue"))
                result += stack->getCount();
        return result;
    }
};
}
