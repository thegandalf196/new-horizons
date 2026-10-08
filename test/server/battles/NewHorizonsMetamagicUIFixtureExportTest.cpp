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
#include <array>
#include <boost/filesystem.hpp>
#include <cstdlib>
#include <zlib.h>

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
