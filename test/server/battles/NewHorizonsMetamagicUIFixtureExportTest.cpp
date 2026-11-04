/*
 * NewHorizonsMetamagicUIFixtureExportTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "BattleTestFixture.h"

#include "../../../lib/CPlayerState.h"
#include "../../../lib/CCreatureHandler.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/VCMIDirs.h"
#include "../../../lib/constants/StringConstants.h"
#include "../../../lib/entities/hero/NewHorizonsCapabilityRules.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CGCreature.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/pathfinder/PathfinderCache.h"
#include "../../../lib/pathfinder/PathfinderOptions.h"
#include "../../../lib/pathfinder/CGPathNode.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
#include <algorithm>
#include <array>
#include <boost/filesystem.hpp>
#include <cstdlib>
#include <zlib.h>
#include "../../../lib/GameSettings.h"
#include "../../../lib/callback/EditorCallback.h"
#include "../../../lib/filesystem/CZipLoader.h"
#include "../../../lib/mapping/CMapService.h"
#include "../../../lib/mapObjects/army/CStackInstance.h"
#include <fstream>
#include <iterator>
#include <limits>
#include <set>


class NewHorizonsMetamagicUIFixtureExportTest : public BattleTestFixture
{
protected:
	void configurePlayer(PlayerSettings & settings) const override
	{
		BattleTestFixture::configurePlayer(settings);
		if(settings.color == PlayerColor(1))
			settings.connectedPlayerIDs.clear();
	}
};

TEST_F(NewHorizonsMetamagicUIFixtureExportTest, ExportCanonicalStartingSolmyrWithReachableDurableBattle)
{
	const auto * enabled = std::getenv("NH_EXPORT_METAMAGIC_UI_FIXTURE");
	if(!enabled || std::string(enabled) != "1")
		GTEST_SKIP() << "Opt-in export requires NH_EXPORT_METAMAGIC_UI_FIXTURE=1";
	if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
		GTEST_SKIP() << "Requires the actual curated New Horizons module";
	const std::string name = "NHMetamagicUISolmyrUP004v1";
	const auto output = VCMIDirs::get().userCachePath() / "testMaps" / (name + ".h3m");
	ASSERT_FALSE(boost::filesystem::exists(output)) << "Use a fresh disposable export profile; never overwrite an existing map";
	const HeroTypeID solmyr(HeroTypeID::decode("core:solmyr"));
	ASSERT_NE(solmyr, HeroTypeID::NONE);
	const int metamagicId = SecondarySkill::decode("new-horizons:metamagic");
	ASSERT_GE(metamagicId, 0);
	const SecondarySkill metamagic(metamagicId);
	const auto durable = creatureByName("core:dendroidGuard");
	ASSERT_NE(durable, CreatureID::NONE);
	const auto pikeman = creatureByName("core:pikeman");
	ASSERT_NE(pikeman, CreatureID::NONE);
	const SpellID missile(SpellID::MAGIC_ARROW);
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).name(name)
		.description("UP004 Metamagic UI diagnostic, not a balanced scenario. Human Red owns Academy8,10. "
			"Solmyr at17,10 retains his canonical starting Basic Metamagic and Basic Havoc; no ranks or "
			"perks are injected. The map authors Knowledge200, a spellbook and ordinary Magic Arrow "
			"for weak repeated casting. Red has10 Pikemen within initial Leadership capacity;100 "
			"durable savage neutral Dendroid Guards at19,10 are reachable this turn. Blue computer "
			"owns Castle30,30 to keep the match active. Attack the neutral normally. During a Red "
			"activation, cast Magic Arrow on the enemy, inspect the optional Metamagic Spell Action "
			"and resource feedback, then cast Magic Arrow again in the same round. Avoid Master "
			"Chain Lightning; defend creatures as needed. This seed verifies initialization and access, "
			"not rendered UI, an executed cast or active-battle saving.")
		.playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
		.town({8, 10, 0}, FactionID::TOWER, PlayerColor(0)).townGarrison({})
		.town({30, 30, 0}, FactionID::CASTLE, PlayerColor(1)).townGarrison({})
		.hero({17, 10, 0}, solmyr, PlayerColor(0)).heroExperience(0)
		.heroPrimary(2, 2, 6, 200).heroGarrison({{pikeman, 10}}).heroSpells({missile})
		.heroEquipped({{ArtifactPosition::SPELLBOOK, ArtifactID::SPELLBOOK}})
		.monster({19, 10, 0}, durable, 100, static_cast<int8_t>(CGCreature::Character::SAVAGE));
	const auto expected = builder.build();
	// Ordinary H3M initialization only: no mapLoaded/settings overrides, no
	// post-initialization rank/perk/bonus/state changes, no battle-state injection.
	startWithMap(builder);
	ASSERT_TRUE(gameState()->getPlayerState(PlayerColor(0))->isHuman());
	ASSERT_FALSE(gameState()->getPlayerState(PlayerColor(1))->isHuman());
	const auto * human = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(human, nullptr);
	ASSERT_EQ(human->getHeroTypeID(), solmyr);
	ASSERT_EQ(human->getSecSkillLevel(metamagic), MasteryLevel::BASIC);
	ASSERT_TRUE(human->hasSpellbook());
	ASSERT_EQ(human->getPrimSkillLevel(PrimarySkill::KNOWLEDGE), 200);
	ASSERT_TRUE(human->getSpellsInSpellbook().count(missile));
	ASSERT_TRUE(human->canCastThisSpell(missile.toSpell()));
	const auto cost = human->getSpellCost(missile.toSpell());
	ASSERT_GT(cost, 0);
	const int64_t twoCastMana = 2LL * cost;
	ASSERT_GE(human->getManaAvailable(), twoCastMana);
	ASSERT_EQ(human->stacksCount(), 1);
	const auto capacity = human->getLeadershipSlotCapacity(pikeman);
	ASSERT_TRUE(capacity);
	ASSERT_TRUE(capacity->accepts(10));
	ASSERT_EQ(human->getCreature(SlotID(0))->getId(), pikeman);
	ASSERT_EQ(human->getStackCount(SlotID(0)), 10);
	const auto * redTown = dynamic_cast<const CGTownInstance *>(findObjectAt({8, 10, 0}));
	const auto * blueTown = dynamic_cast<const CGTownInstance *>(findObjectAt({30, 30, 0}));
	ASSERT_NE(redTown, nullptr);
	ASSERT_NE(blueTown, nullptr);
	ASSERT_EQ(redTown->getFactionID(), FactionID::TOWER);
	ASSERT_EQ(redTown->getOwner(), PlayerColor(0));
	ASSERT_EQ(blueTown->getOwner(), PlayerColor(1));
	const auto * neutral = dynamic_cast<const CGCreature *>(findObjectAt({19, 10, 0}));
	ASSERT_NE(neutral, nullptr);
	ASSERT_EQ(neutral->initialCharacter, CGCreature::Character::SAVAGE);
	ASSERT_EQ(neutral->getStackCount(SlotID(0)), 100);
	PathfinderCache paths(gameState().get(), PathfinderOptions(*gameState()));
	const auto route = paths.getPathsInfo(human);
	CGPath path;
	ASSERT_TRUE(route->getPath(path, neutral->visitablePos()));
	const auto * node = route->getPathInfo(neutral->visitablePos());
	ASSERT_NE(node, nullptr);
	ASSERT_EQ(node->turns, 0);
	ASSERT_EQ(node->action, EPathNodeAction::BATTLE);
	ASSERT_EQ(builder.buildAndDump(name), expected);
	std::unique_ptr<gzFile_s, decltype(&gzclose)> input(gzopen(output.string().c_str(), "rb"), &gzclose);
	ASSERT_NE(input, nullptr);
	std::vector<uint8_t> actual;
	std::array<uint8_t, 4096> chunk;
	int count = 0;
	while((count = gzread(input.get(), chunk.data(), chunk.size())) > 0)
		actual.insert(actual.end(), chunk.begin(), chunk.begin() + count);
	ASSERT_EQ(count, 0);
	ASSERT_TRUE(gzeof(input.get()));
	ASSERT_EQ(gzclose(input.release()), Z_OK);
	ASSERT_EQ(actual, expected);
	MapServiceTinyH3M exported(actual, nullptr);
	ASSERT_NE(exported.loadMap(ResourcePath(name), gameState().get()), nullptr);
	RecordProperty("exported_map", output.string());
	RecordProperty("two_cast_mana", std::to_string(twoCastMana));
	std::cout << "Metamagic fixture " << name << ": " << output.string()
		<< "; initialized rank=Basic; two-cast mana=" << twoCastMana << std::endl;
}

namespace
{
void assertNoTestModReferences(const JsonNode & node)
{
	if(node.isString())
	{
		EXPECT_EQ(node.String().find("vcmi-test"), std::string::npos);
	}
	if(node.isStruct())
		for(const auto & [key, value] : node.Struct())
		{
			EXPECT_EQ(key.find("vcmi-test"), std::string::npos);
			assertNoTestModReferences(value);
		}
	if(node.isVector())
		for(const auto & value : node.Vector())
			assertNoTestModReferences(value);
}

class PassiveSkillFixtureMapService final : public CMapService
{
	std::vector<uint8_t> bytes;
	MapListener & listener;
public:
	PassiveSkillFixtureMapService(std::vector<uint8_t> bytes, MapListener & listener)
		: bytes(std::move(bytes)), listener(listener) {}
	std::unique_ptr<CMap> loadMap(const ResourcePath & name, IGameInfoCallback * cb) const override
	{
		auto result = CMapService::loadMap(bytes.data(), static_cast<int>(bytes.size()), name.getName(), "core", "english", cb);
		listener.mapLoaded(result.get()); // Observe only; no settings/spell injection.
		return result;
	}
	std::unique_ptr<CMapHeader> loadMapHeader(const ResourcePath & name, bool /*isEditor*/ = false) const override
	{
		return CMapService::loadMapHeader(bytes.data(), static_cast<int>(bytes.size()), name.getName(), "core", "english");
	}
};
}


// Reuse the existing editor -> VMAP -> ordinary initialization export contract.
// H3M cannot encode the named mod Skills or this complete preview spellbook.
class NewHorizonsPassiveSkillUIFixtureExportTest : public BattleTestFixture
{
protected:
	std::unique_ptr<PassiveSkillFixtureMapService> authoredService;
	Services * gameServices() override { return LIBRARY; }
	void TearDown() override
	{
		BattleTestFixture::TearDown();
		authoredService.reset();
	}

	void publishAndInitialize(std::unique_ptr<CMap> authored, const std::string & name)
	{
		ASSERT_FALSE(vstd::contains(LIBRARY->modh->getActiveMods(), std::string("vcmi-test")));
		const auto folder = VCMIDirs::get().userCachePath() / "testMaps";
		boost::filesystem::create_directories(folder);
		const auto output = folder / (name + ".vmap");
		ASSERT_FALSE(boost::filesystem::exists(output)) << "Use a fresh private export profile";
		ASSERT_FALSE(authored->getMagicOverride().has_value());
		authored->players.at(1).canHumanPlay = false;
		authored->players.at(1).canComputerPlay = true;
		for(auto * creature : authored->getObjects<CGCreature>())
		{
			creature->neverFlees = true;
			creature->notGrowingTeam = true;
		}
		CMapService writer;
		writer.saveMap(authored, output);
		CZipLoader archive("", output);
		for(const auto & resource : archive.getFilteredFiles([](const ResourcePath & entry)
			{ return entry.getType() == EResType::JSON; }))
		{
			const auto data = archive.load(resource)->readAll();
			assertNoTestModReferences(JsonNode(reinterpret_cast<const std::byte *>(data.first.get()),
				data.second, resource.getName()));
		}
		ASSERT_FALSE(HasFailure());
		std::ifstream input(output.string(), std::ios::binary);
		ASSERT_TRUE(input.is_open());
		std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
		ASSERT_FALSE(bytes.empty());
		ASSERT_LE(bytes.size(), static_cast<size_t>(std::numeric_limits<int>::max()));
		authoredService = std::make_unique<PassiveSkillFixtureMapService>(std::move(bytes), *this);
		StartInfo info;
		info.mapname = name;
		info.mode = EStartMode::NEW_GAME;
		info.difficulty = static_cast<ui8>(EMapDifficulty::EASY);
		const auto header = authoredService->loadMapHeader(ResourcePath(name));
		ASSERT_NE(header, nullptr);
		ASSERT_FALSE(header->players.at(1).canHumanPlay);
		for(int index : {0, 1})
		{
			auto & settings = info.playerInfos[PlayerColor(index)];
			settings.color = PlayerColor(index);
			if(index == 0)
				settings.connectedPlayerIDs.insert(static_cast<PlayerConnectionID>(index));
			settings.name = "Player";
			settings.castle = header->players[index].defaultCastle();
			settings.hero = header->players[index].defaultHero();
			settings.bonus = PlayerStartingBonus::GOLD;
		}
		GameRandomizer randomizer(*gameState());
		Load::ProgressAccumulator progress;
		ASSERT_NO_THROW(gameState()->init(authoredService.get(), &info, randomizer, progress, false));
		ASSERT_TRUE(gameState()->getPlayerState(PlayerColor(0))->isHuman());
		ASSERT_FALSE(gameState()->getPlayerState(PlayerColor(1))->isHuman());
		ASSERT_FALSE(map()->getMagicOverride().has_value());
		ASSERT_EQ(gameState()->getMagicRules(), LIBRARY->settingsHandler->getValue(EGameSettings::MAGIC_NEW_HORIZONS));
		ASSERT_EQ(gameState()->getHeroPerkRules(), LIBRARY->settingsHandler->getValue(EGameSettings::HEROES_NEW_HORIZONS_PERKS));
		RecordProperty("exported_map", output.string());
	}

	void initializeSevenPreviewFixture(const std::string & outputName);

	void assertLegalArmyAndReachableEnemy(const CGHeroInstance * hero, const CGHeroInstance * opponent = nullptr)
	{
		ASSERT_NE(hero, nullptr);
		const auto capacity = hero->getLeadershipCapacity();
		ASSERT_TRUE(capacity);
		ASSERT_FALSE(capacity->overCapacity());
		for(const auto & [slot, stack] : hero->Slots())
		{
			const auto slotCapacity = hero->getLeadershipSlotCapacity(stack->getCreatureID());
			ASSERT_TRUE(slotCapacity);
			ASSERT_TRUE(slotCapacity->accepts(stack->getCount()));
		}
		const CArmedInstance * enemy = opponent ? static_cast<const CArmedInstance *>(opponent)
			: dynamic_cast<const CGCreature *>(findObjectAt({19, 10, 0}));
		ASSERT_NE(enemy, nullptr);
		ASSERT_EQ(enemy->getStackPtr(SlotID(0))->getCreatureID(), CreatureID(CreatureID::decode("core:archer")));
		if(!opponent)
			ASSERT_TRUE(dynamic_cast<const CGCreature *>(enemy)->neverFlees);
		PathfinderCache paths(gameState().get(), PathfinderOptions(*gameState()));
		const auto route = paths.getPathsInfo(hero);
		CGPath path;
		ASSERT_TRUE(route->getPath(path, enemy->visitablePos()));
		const auto * node = route->getPathInfo(enemy->visitablePos());
		ASSERT_NE(node, nullptr);
		ASSERT_EQ(node->turns, 0);
		ASSERT_EQ(node->action, EPathNodeAction::BATTLE);
	}
};

TEST_F(NewHorizonsPassiveSkillUIFixtureExportTest, ExportAdelaZeroManaAutomaticLight)
{
	const auto * kind = std::getenv("NH_EXPORT_PASSIVE_SKILL_UI_FIXTURE");
	if(!kind || std::string(kind) != "divine")
		GTEST_SKIP() << "Explicit ordinary-bootstrap divine export required";
	const HeroTypeID adela(HeroTypeID::decode("core:adela"));
	const auto pikeman = CreatureID(CreatureID::decode("core:pikeman"));
	const auto archer = CreatureID(CreatureID::decode("core:archer"));
	ASSERT_NE(adela, HeroTypeID::NONE);
	ASSERT_NE(pikeman, CreatureID::NONE);
	ASSERT_NE(archer, CreatureID::NONE);
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).name("NHDivineZeroManaAdela")
		.description("Functional UI fixture, not balance. Adela retains her canonical Basic Divine Mandate "
			"and Basic Light. Map-authored Knowledge0 starts with0 Mana; only Bless is inscribed. "
			"Attack the nearby Archer stack, issue an Order, inspect automatic free Bless and1-to0 "
			"remaining uses. No extra Hero Action should appear. Defend normally; use the second "
			"friendly stack or wait for Bless expiry to distinguish the combat cap from no useful spell.")
		.playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
		.town({8, 10, 0}, FactionID::CASTLE, PlayerColor(0)).townGarrison({})
		.town({30, 30, 0}, FactionID::CASTLE, PlayerColor(1)).townGarrison({})
		.hero({17, 10, 0}, adela, PlayerColor(0)).heroExperience(0)
		.heroPrimary(0, 255, 0, 0).heroGarrison({{pikeman, 5}, {pikeman, 5}})
		.heroSpells({SpellID::BLESS}).heroEquipped({{ArtifactPosition::SPELLBOOK, ArtifactID::SPELLBOOK}})
		.monster({19, 10, 0}, archer, 5, 4);
	MapServiceTinyH3M initial(builder.build(), nullptr);
	EditorCallback authoring(nullptr);
	auto authored = initial.loadMap(ResourcePath("NHDivineZeroManaAdela"), &authoring);
	ASSERT_NE(authored, nullptr);
	authoring.setMap(authored.get());
	ASSERT_NO_FATAL_FAILURE(publishAndInitialize(std::move(authored), "NHDivineZeroManaAdela"));
	const auto * human = findHeroByOwner(PlayerColor(0));
	ASSERT_NO_FATAL_FAILURE(assertLegalArmyAndReachableEnemy(human));
	ASSERT_EQ(human->getHeroTypeID(), adela);
	ASSERT_EQ(human->getPerkSkillRank("new-horizons:divineMandate"), MasteryLevel::BASIC);
	ASSERT_EQ(human->getPerkSkillRank("new-horizons:lightMagic"), MasteryLevel::BASIC);
	ASSERT_EQ(human->getPerkState().rules["divineMandatePolicy"]["version"].Integer(), 2);
	ASSERT_EQ(human->getPrimSkillLevel(PrimarySkill::KNOWLEDGE), 0);
	ASSERT_TRUE(human->areSpellPointsInitialized());
	ASSERT_EQ(human->manaLimit(), 0);
	ASSERT_EQ(human->getManaAvailable(), 0);
	ASSERT_TRUE(human->hasSpellbook());
	ASSERT_EQ(human->getSpellsInSpellbook(), (std::set<SpellID>{SpellID::BLESS}));
	ASSERT_TRUE(human->canCastThisSpell(SpellID(SpellID::BLESS).toSpell()));
	ASSERT_EQ(human->stacksCount(), 2);
	RecordProperty("initial_mana", 0);
	// This export does not issue an Order, run a battle or claim a free-cast witness.
}

void NewHorizonsPassiveSkillUIFixtureExportTest::initializeSevenPreviewFixture(const std::string & outputName)
{
	const HeroTypeID solmyr(HeroTypeID::decode("core:solmyr"));
	const auto pikeman = CreatureID(CreatureID::decode("core:pikeman"));
	const auto archer = CreatureID(CreatureID::decode("core:archer"));
	ASSERT_NE(solmyr, HeroTypeID::NONE);
	const HeroTypeID orrin(HeroTypeID::decode("core:orrin"));
	const ArtifactID shackles(ArtifactID::decode("core:shacklesOfWar"));
	ASSERT_NE(orrin, HeroTypeID::NONE);
	ASSERT_GE(shackles.getNum(), 0);
	ASSERT_NE(pikeman, CreatureID::NONE);
	ASSERT_NE(archer, CreatureID::NONE);
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).name(outputName)
		.description("Functional UI fixture, not balance. Authored Expert Metamagic and relevant schools, "
			"SP0, Knowledge200, two original Pikeman stacks and nearby ordinary Orrin with one Archer. "
			"Build caster-specific marks through real Bless/Curse casts in successive rounds, not injections. "
			"Hover then cast Holy Armor, Heavenly Gale, Shield of Chaos, Entangle, Misfortune, "
			"Forgetfulness and Bless; compare prior-mark preview with actual effect and duration. "
			"Orrin legally carries Ammo Cart and Shackles of War, with no spellbook/spells. Defend after checking each active stack. No extra Metamagic Hero Action should appear.")
		.playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
		.town({8, 10, 0}, FactionID::TOWER, PlayerColor(0)).townGarrison({})
		.town({30, 30, 0}, FactionID::CASTLE, PlayerColor(1)).townGarrison({})
		.hero({17, 10, 0}, solmyr, PlayerColor(0)).heroExperience(0)
		.heroPrimary(0, 255, 0, 200).heroSecondarySkills({})
		.heroGarrison({{pikeman, 5}, {pikeman, 5}}).heroSpells({})
		.heroEquipped({{ArtifactPosition::SPELLBOOK, ArtifactID::SPELLBOOK}})
		.hero({19, 10, 0}, orrin, PlayerColor(1)).heroExperience(0)
		.heroPrimary(0, 0, 0, 0).heroGarrison({{archer, 1}}).heroSpells({})
		.heroEquipped({{ArtifactPosition::MACH2, ArtifactID::AMMO_CART},
			{ArtifactPosition::MISC1, shackles}});
	MapServiceTinyH3M initial(builder.build(), nullptr);
	EditorCallback authoring(nullptr);
	auto authored = initial.loadMap(ResourcePath(outputName), &authoring);
	ASSERT_NE(authored, nullptr);
	authoring.setMap(authored.get());
	const auto heroes = authored->getObjects<CGHeroInstance>();
	ASSERT_EQ(heroes.size(), 2u);
	const auto humanEntry = std::find_if(heroes.begin(), heroes.end(), [](const auto * entry)
		{ return entry->getOwner() == PlayerColor(0); });
	ASSERT_NE(humanEntry, heroes.end());
	auto * hero = *humanEntry;
	// Editor-time input only, serialized BEFORE ordinary game initialization.
	// H3M normalizes the empty roster to NONE; mixed default/explicit skills
	// serialize as defaults, so replace that sentinel before authoring ranks.
	hero->secSkills.clear();
	for(const char * id : {"metamagic", "lightMagic", "shadowMagic", "chaosMagic", "natureMagic", "sorceryMagic"})
	{
		const auto skill = SecondarySkill::decode(std::string("new-horizons:") + id);
		ASSERT_GE(skill, 0);
		hero->secSkills.emplace_back(SecondarySkill(skill), MasteryLevel::EXPERT);
	}
	// Solmyr retains his canonical Stormcaller starting perk and its Basic parent.
	const auto havoc = SecondarySkill::decode("new-horizons:havocMagic");
	ASSERT_GE(havoc, 0);
	hero->secSkills.emplace_back(SecondarySkill(havoc), MasteryLevel::BASIC);
	std::set<SpellID> book;
	for(const char * id : {"new-horizons:holyArmor", "new-horizons:heavenlyGale", "new-horizons:shieldOfChaos",
		"new-horizons:entangle", "core:misfortune", "core:forgetfulness", "core:bless", "core:curse"})
	{
		const SpellID spell(SpellID::decode(id));
		ASSERT_NE(spell, SpellID::NONE);
		hero->addSpellToSpellbook(spell);
		authored->allowedSpells.insert(spell);
		book.insert(spell);
	}
	ASSERT_NO_FATAL_FAILURE(publishAndInitialize(std::move(authored), outputName));
	const auto * human = findHeroByOwner(PlayerColor(0));
	const auto * computer = findHeroByOwner(PlayerColor(1));
	ASSERT_NE(computer, nullptr);
	ASSERT_NO_FATAL_FAILURE(assertLegalArmyAndReachableEnemy(human, computer));
	const auto enemyCapacity = computer->getLeadershipCapacity();
	ASSERT_TRUE(enemyCapacity);
	ASSERT_FALSE(enemyCapacity->overCapacity());
	const auto enemySlot = computer->getLeadershipSlotCapacity(archer);
	ASSERT_TRUE(enemySlot);
	ASSERT_TRUE(enemySlot->accepts(1));
	ASSERT_EQ(computer->getHeroTypeID(), orrin);
	ASSERT_EQ(computer->stacksCount(), 1);
	ASSERT_EQ(computer->getStackCount(SlotID(0)), 1);
	ASSERT_FALSE(computer->hasSpellbook());
	ASSERT_TRUE(computer->getSpellsInSpellbook().empty());
	ASSERT_EQ(computer->getManaAvailable(), 0);
	ASSERT_TRUE(computer->hasArt(ArtifactID::AMMO_CART));
	ASSERT_TRUE(computer->hasArt(shackles));
	ASSERT_EQ(human->getHeroTypeID(), solmyr);
	for(const char * id : {"metamagic", "lightMagic", "shadowMagic", "chaosMagic", "natureMagic", "sorceryMagic"})
		ASSERT_EQ(human->getPerkSkillRank(std::string("new-horizons:") + id), MasteryLevel::EXPERT) << id;
	ASSERT_EQ(human->getPerkSkillRank("new-horizons:havocMagic"), MasteryLevel::BASIC);
	ASSERT_TRUE(human->hasActivePerk("new-horizons:havocMagic", "new-horizons:havocMagic.stormcaller"));
	ASSERT_TRUE(human->hasSpellbook());
	ASSERT_EQ(human->getPerkState().rules["metamagicPolicy"]["version"].Integer(), 2);
	ASSERT_EQ(human->getPrimSkillLevel(PrimarySkill::SPELL_POWER), 0);
	ASSERT_EQ(human->getManaAvailable(), 200);
	ASSERT_EQ(human->getSpellsInSpellbook(), book);
	for(const auto spell : book)
		ASSERT_TRUE(human->canCastThisSpell(spell.toSpell())) << spell.toSpell()->getJsonKey();
	ASSERT_EQ(human->stacksCount(), 2);
	// No marks are authored: the rendered continuation must earn them by casting.
}

TEST_F(NewHorizonsPassiveSkillUIFixtureExportTest, ExportSevenMarkedRecipientPreviews)
{
	const auto * kind = std::getenv("NH_EXPORT_PASSIVE_SKILL_UI_FIXTURE");
	if(!kind || std::string(kind) != "metamagic")
		GTEST_SKIP() << "Explicit ordinary-bootstrap metamagic export required";
	ASSERT_NO_FATAL_FAILURE(initializeSevenPreviewFixture("NHMetamagicSevenPreviews"));
}

TEST_F(NewHorizonsPassiveSkillUIFixtureExportTest, OrdinaryMetaOpponentSustainsEligiblePreviewRecipients)
{
	const auto * kind = std::getenv("NH_EXPORT_PASSIVE_SKILL_UI_FIXTURE");
	if(!kind || std::string(kind) != "metamagic")
		GTEST_SKIP() << "Explicit ordinary-bootstrap metamagic export required";
	ASSERT_NO_FATAL_FAILURE(initializeSevenPreviewFixture("NHMetamagicOpponentWitness"));
	auto * human = findHeroByOwner(PlayerColor(0));
	auto * computer = findHeroByOwner(PlayerColor(1));
	server.gameState = gameState();
	gameHandler = std::make_shared<CGameHandler>(server, gameState());
	gameHandler->battles->startBattle(human, computer);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);
	EXPECT_FALSE(battle()->battleCanFlee(PlayerColor(1)));
	EXPECT_FALSE(battle()->battleCanSurrender(PlayerColor(1)));
	CPlayerBattleCallback visible(battle(), PlayerColor(0));
	const CStack * shooter = nullptr;
	std::vector<const CStack *> friends;
	for(const auto * unit : visible.battleGetAllStacks(false))
	{
		if(unit->unitSide() == BattleSide::ATTACKER)
			friends.push_back(unit);
		else if(unit->creatureId() == CreatureID(CreatureID::decode("core:archer")))
			shooter = unit;
		EXPECT_EQ(unit->metamagicMarks(BattleSide::ATTACKER), 0);
	}
	ASSERT_EQ(friends.size(), 2u);
	ASSERT_NE(shooter, nullptr);
	ASSERT_EQ(shooter->getCount(), 1);
	EXPECT_FALSE(shooter->shots.isLimited()); // Actual ordinary Ammo Cart, not edited shot counters.
	for(const auto * recipient : friends)
	{
		EXPECT_LE(battle()->battleGetAttackLuck(shooter, recipient, true), 0);
		const auto damage = battle()->calculateDmgRange(BattleAttackInfo(shooter, recipient, 0, true));
		// Conservative initial threat: two shots/round for18 rounds, no protective spell credit.
		EXPECT_LT(36 * damage.damage.max, recipient->getAvailableHealth());
	}
	for(const char * id : {"new-horizons:holyArmor", "new-horizons:heavenlyGale", "new-horizons:shieldOfChaos",
		"new-horizons:entangle", "core:misfortune", "core:forgetfulness", "core:bless"})
	{
		SCOPED_TRACE(id);
		const SpellID spell(SpellID::decode(id));
		const bool hostile = spell == SpellID(SpellID::MISFORTUNE) || spell == SpellID(SpellID::FORGETFULNESS)
			|| std::string(id) == "new-horizons:entangle";
		const auto * recipient = hostile ? shooter : friends.front();
		const spells::Target aim = std::string(id) == "new-horizons:heavenlyGale"
			? spells::Target{spells::Destination(BattleHex::INVALID)} : spells::Target{spells::Destination(recipient)};
		spells::BattleCast event(battle(), human, spells::Mode::HERO, spell.toSpell());
		const auto mechanics = event.mechanicsForTarget(aim);
		ASSERT_TRUE(mechanics);
		EXPECT_TRUE(mechanics->canBeCastAt(aim));
		const auto affected = mechanics->getAffectedStacks(aim);
		EXPECT_NE(std::find(affected.begin(), affected.end(), recipient), affected.end());
	}
	EXPECT_EQ(human->getManaAvailable(), 200);
	for(const auto * unit : visible.battleGetAllStacks(false))
		EXPECT_EQ(unit->metamagicMarks(BattleSide::ATTACKER), 0);
}
