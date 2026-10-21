/*
 * NewHorizonsCreaturePopupTextTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/CConfigHandler.h"
#include "../../../lib/CCreatureHandler.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/callback/CGameInfoCallback.h"
#include "../../../lib/entities/creature/NewHorizonsCreatureCategoryRules.h"
#include "../../../lib/entities/faction/CTownHandler.h"
#include "../../../lib/mapObjects/CGCreature.h"
#include "../../../lib/texts/ITranslator.h"

namespace
{
using newHorizonsCreatures::CreatureCategoryRules;

class PopupSettings
{
	JsonNode enhancements = settings["general"]["enableUiEnhancements"];
public:
	PopupSettings()
	{
		Settings general = settings.write["general"];
		general["enableUiEnhancements"].Bool() = true;
	}
	~PopupSettings()
	{
		Settings general = settings.write["general"];
		general["enableUiEnhancements"] = enhancements;
	}
};

// Same actual-world callback pattern as CreatureCategoryStateTest, without a battle.
class PopupWorld final : public CGameInfoCallback
{
	CGameState & state;
	CreatureCategoryRules captured;
public:
	PopupWorld(CGameState & state, const JsonNode & rules)
		: state(state), captured(newHorizonsCreatures::captureCreatureCategoryRules(rules)) {}
	CGameState & gameState() override { return state; }
	const CGameState & gameState() const override { return state; }
	const CreatureCategoryRules & getCreatureCategoryRules() const override { return captured; }
};

class InstalledCategories
{
	std::unique_ptr<GameSettings> previous;
public:
	explicit InstalledCategories(const JsonNode & rules)
	{
		auto config = LIBRARY->settingsHandler->getFullConfig();
		config["creatures"]["newHorizonsCategories"] = rules;
		auto replacement = std::make_unique<GameSettings>();
		replacement->loadBase(config);
		previous = std::move(LIBRARY->settingsHandler);
		LIBRARY->settingsHandler = std::move(replacement);
	}
	~InstalledCategories() { LIBRARY->settingsHandler = std::move(previous); }
};

class PopupTranslator final : public ITranslator
{
public:
	mutable std::map<std::string, std::string> texts;
	PopupTranslator()
	{
		// Require the staged production resource; never invent the new template here.
		const JsonNode english(JsonPath::builtin("config/translations/english"));
		for(const auto & [key, value] : english.Struct())
			texts.emplace(key, value.String());
	}
	const std::string & translateString(const TextIdentifier & identifier) const override
	{
		return texts.try_emplace(identifier.get(), identifier.get()).first->second;
	}
};
}

class NewHorizonsCreaturePopupTextTest : public HeroCommandFixture
{
protected:
	std::unique_ptr<PopupSettings> popupSettings;
	PopupTranslator translator;
	JsonNode rules = JsonNode(JsonPath::builtin("config/newHorizonsCreatureCategories"));

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		popupSettings = std::make_unique<PopupSettings>();
		ASSERT_TRUE(translator.texts.contains("vcmi.adventureMap.monsterCategory"))
			<< "Stage the corrected base English resource with this candidate";
		ASSERT_EQ(translator.texts.at("vcmi.adventureMap.monsterCategory"),
			"\n\n%CATEGORY %TOWN %ATTACK_TYPE unit");
		startGame();
	}
	void TearDown() override
	{
		popupSettings.reset();
		HeroCommandFixture::TearDown();
	}
	std::unique_ptr<CGCreature> neutral(IGameInfoCallback * world, const std::string & key)
	{
		auto result = std::make_unique<CGCreature>(world);
		const CreatureID id(CreatureID::decode(key));
		result->ID = Obj::MONSTER;
		result->subID = MapObjectSubID(id.getNum());
		result->addToSlot(SlotID(0), id, 5);
		translator.texts[id.toCreature()->getFactionID().toEntity(LIBRARY)->getNameTextID()] = "Faction";
		return result;
	}
	std::string heading(const CGCreature & creature)
	{
		return creature.getHoverText(PlayerColor(0)).toString(&translator);
	}
	std::string popup(const CGCreature & creature)
	{
		return creature.getPopupText(PlayerColor(0)).toString(&translator);
	}
};

TEST_F(NewHorizonsCreaturePopupTextTest, CapturedNoncontiguousBandsAndAttackTypeReachPublicPopup)
{
	// Adversarial edits to real definitions, not another production category table.
	rules["creatures"]["core:unicorn"].String() = "core";
	rules["creatures"]["core:archer"].String() = "champion";
	PopupWorld world(*gameState(), rules);
	auto unicorn = neutral(&world, "core:unicorn");
	auto archer = neutral(&world, "core:archer");
	translator.texts["new-horizons.category.core.name"] = "Core";
	translator.texts["new-horizons.category.champion.name"] = "Champion";
	EXPECT_EQ(popup(*unicorn), heading(*unicorn) + "\n\nCore Faction melee unit");
	EXPECT_EQ(popup(*archer), heading(*archer) + "\n\nChampion Faction ranged unit");

	// The ordinary authored Unicorn category is Elite, independent of level 6.
	const JsonNode authored(JsonPath::builtin("config/newHorizonsCreatureCategories"));
	PopupWorld ordinary(*gameState(), authored);
	auto elite = neutral(&ordinary, "core:unicorn");
	translator.texts["new-horizons.category.elite.name"] = "Elite";
	EXPECT_EQ(popup(*elite), heading(*elite) + "\n\nElite Faction melee unit");
}

TEST_F(NewHorizonsCreaturePopupTextTest, AbsentAndUnmappedContextsKeepLegacyDespiteInstalledCategories)
{
	ASSERT_EQ(rules["creatures"]["core:unicorn"].String(), "elite");
	ASSERT_EQ(rules["creatures"]["core:warUnicorn"].String(), "elite");
	InstalledCategories installed(rules);
	PopupWorld absent(*gameState(), JsonNode());
	auto old = neutral(&absent, "core:unicorn");
	const auto expected = heading(*old) + "\n\nLevel 6 Faction melee unit";
	EXPECT_EQ(popup(*old), expected);

	rules["creatures"].Struct().erase("core:warUnicorn");
	PopupWorld unmapped(*gameState(), rules);
	auto upgrade = neutral(&unmapped, "core:warUnicorn");
	EXPECT_EQ(popup(*upgrade), heading(*upgrade) + "\n\nLevel 6 Faction melee unit");

	{
		Settings general = settings.write["general"];
		general["enableUiEnhancements"].Bool() = false;
	}
	EXPECT_EQ(popup(*old), heading(*old));
	EXPECT_EQ(popup(*upgrade), heading(*upgrade));
}

TEST_F(NewHorizonsCreaturePopupTextTest, CaptureSurvivesInstalledChangesAndTranslationIsDeferred)
{
	PopupWorld captured(*gameState(), rules);
	auto unicorn = neutral(&captured, "core:unicorn");
	const auto text = unicorn->getPopupText(PlayerColor(0));
	rules["creatures"]["core:unicorn"].String() = "champion";
	InstalledCategories changedInstalled(rules);
	PopupWorld otherWorld(*gameState(), rules);
	auto other = neutral(&otherWorld, "core:unicorn");

	// Both the template order and category words are resolved after MetaString creation.
	translator.texts["vcmi.adventureMap.monsterCategory"] = "\n\n%ATTACK_TYPE / %TOWN / %CATEGORY";
	translator.texts["vcmi.adventureMap.monsterMeleeType"] = "corps-a-corps";
	translator.texts["new-horizons.category.elite.name"] = "Elite-traduite";
	translator.texts["new-horizons.category.champion.name"] = "Champion-traduit";
	EXPECT_EQ(text.toString(&translator), heading(*unicorn) + "\n\ncorps-a-corps / Faction / Elite-traduite");
	EXPECT_EQ(popup(*unicorn), text.toString(&translator));
	EXPECT_EQ(popup(*other), heading(*other) + "\n\ncorps-a-corps / Faction / Champion-traduit");
}
