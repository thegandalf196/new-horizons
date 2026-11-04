/* Part of VCMI engine; GNU General Public License v2.0 or later; see license.txt */
#include "StdInc.h"
#include "NewHorizonsEstatesFixture.h"
#include "battles/BattleTestFixture.h"
#include "../../lib/VCMIDirs.h"
#include "../../lib/entities/hero/CHeroHandler.h"
#include "../../lib/entities/hero/SkillChoiceEligibility.h"
#include "../../lib/filesystem/NhArtSha256.h"
#include "../../lib/serializer/CLoadFile.h"
#include "../../lib/pathfinder/PathfinderCache.h"
#include "../../lib/pathfinder/PathfinderOptions.h"
#include "../../lib/pathfinder/CGPathNode.h"
#include "../../server/processors/TurnOrderProcessor.h"
#include <algorithm>
#include <cstdlib>
#include <limits>
#include <set>
#include <filesystem>
#include <fstream>

namespace
{
namespace fs = std::filesystem;
constexpr auto STEM = "four-skill-ui-prequery";
constexpr auto LEARNING = "new-horizons:learning";

bool contains(const fs::path & parent, const fs::path & child)
{
    return std::mismatch(parent.begin(), parent.end(), child.begin(), child.end()).first == parent.end();
}

fs::path directory(const char * variable)
{
    const auto * value = std::getenv(variable);
    if(!value || !*value)
        throw std::runtime_error("Fixture export requires an explicit existing private directory");
    const fs::path path(value);
    if(!path.is_absolute() || path.lexically_normal() != path || !fs::is_directory(path))
        throw std::runtime_error("Fixture directory must be absolute, normalized and existing");
    for(auto part = path; !part.empty(); part = part.parent_path())
    {
        if(fs::is_symlink(fs::symlink_status(part)))
            throw std::runtime_error("Fixture directory rejects symlink ancestors");
        if(part == part.root_path())
            break;
    }
    return fs::canonical(path);
}

fs::path outputDirectory()
{
    const auto privateRoot = directory("NH_EXPORT_FOUR_SKILL_UI_PRIVATE_ROOT");
    const auto sourceRoot = directory("NH_EXPORT_FOUR_SKILL_UI_SOURCE_ROOT");
    const auto saves = directory("NH_EXPORT_FOUR_SKILL_UI_SAVE_ROOT");
    if(!fs::is_regular_file(sourceRoot / "AGENTS.md") || !fs::is_regular_file(sourceRoot / "CMakeLists.txt")
        || saves == privateRoot || !contains(privateRoot, saves)
        || saves != fs::canonical(fs::path(VCMIDirs::get().userSavePath().string())))
        throw std::runtime_error("Fixture save root must be the isolated profile save mount");
    const auto rejectOverlap = [&](const fs::path & protectedRoot)
    {
        const auto path = fs::weakly_canonical(protectedRoot);
        if(contains(privateRoot, path) || contains(path, privateRoot))
            throw std::runtime_error("Fixture output overlaps protected source or resources");
    };
    rejectOverlap(sourceRoot);
    rejectOverlap(fs::current_path());
    rejectOverlap(fs::path(VCMIDirs::get().binaryPath().string()));
    for(const auto & path : VCMIDirs::get().dataPaths())
        rejectOverlap(fs::path(path.string()));
    const auto cache = fs::weakly_canonical(fs::path(VCMIDirs::get().userCachePath().string()) / "testMaps");
    if(!contains(privateRoot, cache))
        throw std::runtime_error("Fixture map cache must remain private");
    const auto mapName = "NewHorizonsFourSkillUIFixtureExportTest_ExportCurrentPrequeryWorld.h3m";
    for(const auto & path : {cache / mapName, saves / (std::string(STEM) + ".vsgm1")})
        if(fs::symlink_status(path).type() != fs::file_type::not_found)
            throw std::runtime_error("Fixture export never overwrites an existing output");
    return saves;
}

class NewHorizonsFourSkillUIFixtureExportTest : public estatesTest::Fixture
{
protected:
    RecordingGameServer server;
    std::shared_ptr<CGameHandler> handler;
    Services * gameServices() override { return LIBRARY; }
    void configurePlayer(PlayerSettings & settings) const override
    {
        estatesTest::Fixture::configurePlayer(settings);
        settings.connectedPlayerIDs = settings.color == estatesTest::PLAYER
            ? std::set<PlayerConnectionID>{PlayerConnectionID::FIRST_HUMAN} : std::set<PlayerConnectionID>{};
        settings.compOnly = settings.color != estatesTest::PLAYER;
    }
    void TearDown() override
    {
        handler.reset();
        server.gameState.reset();
        estatesTest::Fixture::TearDown();
    }
    void choose(CGHeroInstance * hero, const std::string & skill, const char * basic, const char * advanced)
    {
        const SecondarySkill id(SecondarySkill::decode(skill));
        ASSERT_NE(id, SecondarySkill::NONE);
        hero->setSecSkillLevel(id, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
        hero->applyPerkSelection({skill, skill + "." + basic});
        hero->setSecSkillLevel(id, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
        hero->applyPerkSelection({skill, skill + "." + advanced});
        ASSERT_NO_THROW(hero->getPerkState().validate());
    }
    void createWorld()
    {
        const auto xp = LIBRARY->heroh->reqExp(10) - 1;
        ASSERT_GT(xp, 0);
        ASSERT_LE(xp, std::numeric_limits<uint32_t>::max());
        const SecondarySkill learning(SecondarySkill::decode(LEARNING));
        const SecondarySkill estates(SecondarySkill::decode(estatesTest::ESTATES));
        const HeroTypeID buyer(HeroTypeID::decode("core:orrin"));
        TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
        builder.size(36).name("NH Four Skill UI Prequery")
            .description("Authored current-world UI fixture. Orrin has Connoisseur and Conscription; Valeska has Architect. Visit the nearby hero-specific XP hut for one ordinary level. No pending query or combat is saved. Enter the owned Castle normally and place the buyer in its garrison for upper-army purchases.")
            .playerActive(estatesTest::PLAYER).playerActive(PlayerColor(1))
            .town({7, 7, 0}, FactionID::CASTLE, estatesTest::PLAYER).townGarrison({})
            .town({30, 30, 0}, FactionID::CASTLE, PlayerColor(1)).townGarrison({})
            .hero({12, 10, 0}, buyer, estatesTest::PLAYER).heroExperience(static_cast<uint32_t>(xp))
            .heroSecondarySkills({{learning, MasteryLevel::ADVANCED}, {estates, MasteryLevel::ADVANCED}})
            .heroGarrison({{estatesTest::creature("core:pikeman"), 5}})
            .hero({12, 14, 0}, HeroTypeID(HeroTypeID::decode("core:valeska")), estatesTest::PLAYER)
            .heroExperience(10000).heroSecondarySkills({{estates, MasteryLevel::ADVANCED}})
            .heroGarrison({{estatesTest::creature("core:pikeman"), 5}})
            .seerHut({14, 10, 0}, TinyH3M::TinyH3MBuilder::missionHero(buyer),
                TinyH3M::TinyH3MBuilder::rewardExperience(100));
        startWithMap(std::move(builder), EMapDifficulty::NORMAL);
        first = findHeroAt({12, 10, 0});
        second = findHeroAt({12, 14, 0});
        town = expectAt<CGTownInstance>({7, 7, 0});
        ASSERT_TRUE(first && second && town);
        // Map XP initialization fills ranks without choosing their required perks.
        // Author Orrin's level-nine progression instead: two Basic starting Skills,
        // then eight alternating rank/perk choices for Learning and Estates.
        const SecondarySkill mandate(SecondarySkill::decode("new-horizons:divineMandate"));
        const SecondarySkill archery(SecondarySkill::decode("new-horizons:archery"));
        const auto initializedSkills = first->secSkills;
        for(const auto & [skill, rank] : initializedSkills)
            if(skill != learning && skill != estates && skill != mandate && skill != archery)
                first->setSecSkillLevel(skill, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
        first->setSecSkillLevel(mandate, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
        first->setSecSkillLevel(archery, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
        ASSERT_NO_FATAL_FAILURE(choose(first, LEARNING, "historian", "connoisseur"));
        ASSERT_NO_FATAL_FAILURE(choose(first, estatesTest::ESTATES, "taxCollector", "conscription"));
        ASSERT_NO_FATAL_FAILURE(choose(second, estatesTest::ESTATES, "taxCollector", "architect"));
        server.gameState = gameState();
        handler = std::make_shared<CGameHandler>(server, gameState());
        for(const auto & [color, player] : gameState()->players)
            handler->turnOrder->addPlayer(color);
        handler->start(false);
        handler->onAdvInterfaceReady(estatesTest::PLAYER);
        ResourceSet resources;
        for(const auto resource : {EGameResID::WOOD, EGameResID::ORE, EGameResID::MERCURY,
            EGameResID::SULFUR, EGameResID::CRYSTAL, EGameResID::GEMS})
            resources[resource] = 20;
        resources[EGameResID::GOLD] = 10000;
        setResources(*handler, resources);
        ASSERT_FALSE(town->creatures.empty());
        ASSERT_FALSE(town->creatures.front().second.empty());
        town->creatures.front().first = 40;
        ASSERT_NO_FATAL_FAILURE(checkWorld());
    }
    void checkWorld()
    {
        auto & world = handler->gameState();
        const auto heroes = world.getHeroes(estatesTest::PLAYER);
        ASSERT_EQ(heroes.size(), 2u);
        const auto buyer = std::ranges::find_if(heroes, [](const auto * hero)
            { return hero->getHeroTypeID() == HeroTypeID(HeroTypeID::decode("core:orrin")); });
        const auto architect = std::ranges::find_if(heroes, [](const auto * hero)
            { return hero->getHeroTypeID() == HeroTypeID(HeroTypeID::decode("core:valeska")); });
        ASSERT_NE(buyer, heroes.end());
        ASSERT_NE(architect, heroes.end());
        ASSERT_TRUE((*buyer)->hasActivePerk(LEARNING, "new-horizons:learning.connoisseur"));
        ASSERT_TRUE((*buyer)->hasActivePerk(estatesTest::ESTATES, "new-horizons:estates.conscription"));
        ASSERT_TRUE((*architect)->hasActivePerk(estatesTest::ESTATES, "new-horizons:estates.architect"));
        ASSERT_EQ((*buyer)->level, 9u);
        ASSERT_EQ((*buyer)->exp, LIBRARY->heroh->reqExp(10) - 1);
        ASSERT_FALSE((*buyer)->gainsLevel());
        ASSERT_EQ((*buyer)->secSkills.size(), 4u);
        ASSERT_EQ((*buyer)->getSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:divineMandate"))), MasteryLevel::BASIC);
        ASSERT_EQ((*buyer)->getSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:archery"))), MasteryLevel::BASIC);
        ASSERT_GT(newHorizonsHeroes::connoisseurSkillCandidates(world, **buyer, {}).size(), 2u);
        ASSERT_FALSE(handler->queries->topQuery(estatesTest::PLAYER));
        ASSERT_TRUE(handler->turnOrder->isPlayerMakingTurn(estatesTest::PLAYER));
        ASSERT_TRUE(world.getPlayerState(estatesTest::PLAYER)->isHuman());
        ASSERT_FALSE(world.getPlayerState(PlayerColor(1))->isHuman());
        const auto * castle = dynamic_cast<const CGTownInstance *>(world.getObjInstance(town->id));
        ASSERT_NE(castle, nullptr);
        ASSERT_EQ(castle->getOwner(), estatesTest::PLAYER);
        ASSERT_FALSE(castle->hasBuilt(BuildingID::TOWN_HALL));
        ASSERT_FALSE(castle->creatures.empty());
        ASSERT_EQ(castle->creatures.front().first, 40u);
        ASSERT_FALSE(castle->creatures.front().second.empty());
        const auto creature = castle->creatures.front().second.front();
        ASSERT_LT(castle->getTotalRecruitmentCost(creature, 5, *buyer)[EGameResID::GOLD],
            castle->getTotalRecruitmentCost(creature, 5)[EGameResID::GOLD]);
        ASSERT_EQ(world.getPlayerState(estatesTest::PLAYER)->resources[EGameResID::GOLD], 10000);
        const auto * authoredReward = findObjectAt({14, 10, 0});
        ASSERT_NE(authoredReward, nullptr);
        const auto * reward = world.getObjInstance(authoredReward->id);
        const auto * seer = dynamic_cast<const SeerHut *>(reward);
        ASSERT_NE(seer, nullptr);
        const auto * quest = seer->getActiveQuest();
        ASSERT_NE(quest, nullptr);
        ASSERT_FALSE(quest->isCompleted);
        ASSERT_FALSE(quest->isKnownTo(estatesTest::PLAYER));
        ASSERT_TRUE(quest->reward);
        ASSERT_EQ(quest->reward->reward.heroExperience, 100);
        ASSERT_TRUE(quest->checkQuest(*buyer));
        PathfinderCache paths(&world, PathfinderOptions(world));
        const auto route = paths.getPathsInfo(*buyer);
        CGPath path;
        ASSERT_TRUE(route->getPath(path, reward->visitablePos()));
        const auto * reachable = route->getPathInfo(reward->visitablePos());
        ASSERT_NE(reachable, nullptr);
        ASSERT_EQ(reachable->turns, 0);
    }
    void exportAndLoad(const fs::path & root)
    {
        const auto output = root / (std::string(STEM) + ".vsgm1");
        const auto filename = std::string("Saves/") + STEM;
        handler->save(filename, PlayerColor::NEUTRAL);
        ASSERT_TRUE(fs::is_regular_file(output));
        const auto size = fs::file_size(output);
        ASSERT_GT(size, 0u);
        ASSERT_LT(size, 64u * 1024u * 1024u);
        {
            CLoadFile header(boost::filesystem::path(output.string()), &handler->gameInfo());
            ASSERT_EQ(header.formatVersion(), ESerializationVersion::CURRENT);
        }
        StartInfo load = *handler->gameState().getStartInfo();
        load.mode = EStartMode::LOAD_GAME;
        load.mapname = filename;
        handler->load(load);
        server.gameState = handler->gs;
        ASSERT_NE(handler->gs, gameState());
        ASSERT_NO_FATAL_FAILURE(checkWorld());
        std::ifstream bytes(output, std::ios::binary);
        ASSERT_TRUE(bytes.good());
        RecordProperty("saveSha256", nhart::hex(nhart::sha256(bytes, size)));
        RecordProperty("saveBytes", std::to_string(size));
        RecordProperty("fixtureIdentity", "authored-current-perks-prequery-not-default-hero-proof");
        RecordProperty("saveLoadRoundtrip", "verified");
        RecordProperty("saveFormat", static_cast<int>(ESerializationVersion::CURRENT));
        RecordProperty("pendingQueriesBeforeExport", 0);
        RecordProperty("combatActionsBeforeExport", 0);
    }
};
}

TEST_F(NewHorizonsFourSkillUIFixtureExportTest, ExportCurrentPrequeryWorld)
{
    const auto * enabled = std::getenv("NH_EXPORT_FOUR_SKILL_UI_FIXTURE");
    if(!enabled || std::string(enabled) != "1")
        GTEST_SKIP() << "Opt-in export requires NH_EXPORT_FOUR_SKILL_UI_FIXTURE=1 and private profile roots";
    const auto root = outputDirectory();
    ASSERT_NO_FATAL_FAILURE(createWorld());
    ASSERT_NO_FATAL_FAILURE(exportAndLoad(root));
}
