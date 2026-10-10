/*
 * NewHorizonsStartingBookReplacementTest.cpp, part of VCMI engine
 * License: GNU General Public License v2 or later; see license.txt.
 */
#include "StdInc.h"
#include "battles/HeroCommandFixture.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/GameSettings.h"
#include "../../lib/entities/hero/CHeroHandler.h"
#include "../../lib/entities/hero/CHeroClass.h"
#include "../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../lib/gameState/QuestInfo.h"
#include "../../lib/spells/CSpellHandler.h"
#include "../../lib/spells/ISpellMechanics.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForLobby.h"
#include "battles/FullGameSnapshotTypes.h"
#include "../../lib/serializer/CMemorySerializer.h"
#ifdef ENABLE_BATTLE_AI
#include "../../AI/BattleAI/SpellTargetsEvaluator.h"
#endif

namespace
{
struct BookRow
{
	const char * hero;
	const char * from;
	const char * to;
};
const std::array<BookRow, 22> books{{
	{"core:rion", "core:stoneSkin", "new-horizons:guardianSpirit"},
	{"core:aeris", "core:protectAir", "new-horizons:holyArmor"},
	{"core:piquedram", "core:shield", "core:slow"},
	{"core:neela", "core:shield", "core:slow"},
	{"core:theodorus", "core:shield", "core:slow"},
	{"core:ayden", "core:viewEarth", "new-horizons:confusion"},
	{"core:axsis", "core:protectAir", "core:forgetfulness"},
	{"core:zydar", "core:stoneSkin", "new-horizons:blink"},
	{"core:vokial", "core:stoneSkin", "new-horizons:lifeDrain"},
	{"core:galthran", "core:shield", "core:slow"},
	{"core:nimbus", "core:shield", "core:dispel"},
	{"core:nagash", "core:protectAir", "core:dispel"},
	{"core:jaegar", "core:shield", "core:curse"},
	{"core:malekith", "core:bloodlust", "new-horizons:shadowGift"},
	{"core:sephinroth", "core:protectAir", "core:curse"},
	{"core:gird", "core:bloodlust", "new-horizons:vengefulVines"},
	{"core:dessa", "core:stoneSkin", "new-horizons:regeneration"},
	{"core:oris", "core:protectAir", "core:forgetfulness"},
	{"core:saurug", "core:bloodlust", "new-horizons:vengefulVines"},
	{"core:verdish", "core:protectFire", "new-horizons:regeneration"},
	{"core:styg", "core:shield", "new-horizons:entangle"},
	{"core:tiva", "core:stoneSkin", "new-horizons:regeneration"}
}};
SpellID spell(const char * key) { return SpellID(SpellID::decode(key)); }

class NewHorizonsStartingBookReplacementTest : public HeroCommandFixture
{
protected:
	bool absent = false;
	bool legacy = false;
	bool preset = false;
	bool banTarget = false;
	BookRow selected = books.front();
	CStack * friendly = nullptr;
	CStack * enemy = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
	}
	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		if(absent)
		{
			rules["startingSkills"].Struct().erase("startingBookReplacements");
			rules.Struct().erase("remainingSpellSpecialtyReplacements");
			rules["skillSpecialties"].Struct().erase("navigationStartReplacements");
			rules.Struct().erase("defaultCreatureLineReplacements");
			std::erase_if(rules["nonDamageSpellSpecialties"]["spells"].Vector(),
				[](const JsonNode & value) { return value.String() == "new-horizons:phantomArmy"; });
		}
		if(legacy)
			rules = JsonNode();
		rules.setOverrideFlag(true);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			legacy ? JsonNode() : JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			legacy ? JsonNode() : JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		if(banTarget)
			loaded->allowedSpells.erase(spell(selected.to));
	}
	void prepare(const BookRow & row = books.front())
	{
		selected = row;
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(row.hero)), PlayerColor(0)).heroExperience(0);
		if(preset)
			builder.heroSpells({SpellID(SpellID::MAGIC_ARROW)});
		builder.hero({7, 7, 0}, HeroTypeID(HeroTypeID::decode("core:aislinn")), PlayerColor(1));
		startWithMap(std::move(builder));
		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		gameHandler->randomizer->setSeed(seed);
		attackerSideHero = findHeroAt({5, 5, 0});
		defenderSideHero = findHeroAt({7, 7, 0});
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_NE(defenderSideHero, nullptr);
	}
	void combat(const BookRow & row)
	{
		ASSERT_NO_FATAL_FAILURE(prepare(row));
		ASSERT_TRUE(attackerSideHero->spellbookContainsSpell(spell(row.to)));
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		friendly = addStack(BattleSide::ATTACKER, CreatureID(CreatureID::decode("core:pikeman")), BattleHex(3, 5), 100);
		enemy = addStack(BattleSide::DEFENDER, CreatureID(CreatureID::decode("core:pikeman")), BattleHex(12, 5), 100);
		beginCombat();
		BattleSetActiveStack active;
		active.battleID = BattleID(0);
		active.stack = friendly->unitId();
		active.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(active);
	}
	bool cast(const CStack * first, const CStack * second = nullptr, int sacrifice = 0)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = spell(selected.to);
		action.spellShadowGiftSacrificePercent = sacrifice;
		action.aimToUnit(first);
		if(second)
			action.aimToUnit(second);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}
	void checkDefault(const BookRow & row)
	{
		const JsonNode shipped(JsonPath::builtin("config/newHorizonsHeroes"));
		const auto & table = shipped["startingSkills"]["startingBookReplacements"];
		ASSERT_EQ(table.Struct().size(), books.size());
		EXPECT_EQ(table[row.hero]["from"].String(), row.from);
		EXPECT_EQ(table[row.hero]["to"].String(), row.to);
		EXPECT_TRUE(attackerSideHero->getHeroType()->spells.contains(spell(row.from)));
		EXPECT_FALSE(attackerSideHero->getHeroType()->spells.contains(spell(row.to)));
		EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(spell(row.from)));
		EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(spell(row.to)));
		EXPECT_TRUE(attackerSideHero->getInscribedSpellsForCasting().contains(spell(row.to)));
		EXPECT_TRUE(attackerSideHero->getSpellsInSpellbook().contains(spell(row.to)));
		EXPECT_EQ(attackerSideHero->getHeroClass(), attackerSideHero->getHeroType()->heroClass);
		EXPECT_EQ(attackerSideHero->getHeroTypeID(), HeroTypeID(HeroTypeID::decode(row.hero)));
		EXPECT_EQ(attackerSideHero->getNonDamageSpellSpecialtyBonusPercent(spell(row.to)), 0);
		// A single starting-stack roll may omit later prototype rows. Leadership
		// also clamps the chosen quantity; neither is changed by the book replacement.
		ASSERT_FALSE(attackerSideHero->Slots().empty());
		for(const auto & [slot, stack] : attackerSideHero->Slots())
		{
			SCOPED_TRACE(slot.getNum());
			ASSERT_NE(stack, nullptr);
			EXPECT_GT(stack->getCount(), 0);
			EXPECT_TRUE(std::ranges::any_of(attackerSideHero->getHeroType()->initialArmy, [&](const auto & entry)
			{
				if(stack->getCreatureID() != entry.creature)
					return false;
				int64_t minimum = entry.minAmount;
				int64_t maximum = entry.maxAmount;
				if(const auto capacity = attackerSideHero->getLeadershipSlotCapacity(entry.creature))
				{
					minimum = std::min<int64_t>(minimum, capacity->maximum);
					maximum = std::min<int64_t>(maximum, capacity->maximum);
				}
				return stack->getCount() >= minimum && stack->getCount() <= maximum;
			}));
		}
	}
};
class ApprovedStartingBooks : public NewHorizonsStartingBookReplacementTest,
	public ::testing::WithParamInterface<BookRow> {};
TEST_P(ApprovedStartingBooks, FreshDefaultUsesExactSuccessorAndOrdinaryBookEnumeration)
{
	ASSERT_NO_FATAL_FAILURE(prepare(GetParam()));
	checkDefault(GetParam());
}
INSTANTIATE_TEST_SUITE_P(ApprovedTwentyTwo, ApprovedStartingBooks, ::testing::ValuesIn(books),
	[](const auto & info) { return std::string(info.param.hero).substr(5); });

TEST_F(NewHorizonsStartingBookReplacementTest, GuardianSpiritDefaultActuallyCastsWithoutExtraSchoolRank)
{
	ASSERT_NO_FATAL_FAILURE(combat(books[0]));
	const auto skills = attackerSideHero->secSkills;
	const auto mana = (attackerSideHero->getNormalSpellPoints() + attackerSideHero->getBufferSpellPoints());
	ASSERT_TRUE(cast(friendly));
	EXPECT_LT((attackerSideHero->getNormalSpellPoints() + attackerSideHero->getBufferSpellPoints()), mana);
	EXPECT_TRUE(friendly->hasBonus(Selector::type()(BonusType::GUARDIAN_SPIRIT)));
	EXPECT_EQ(attackerSideHero->secSkills, skills);
}
TEST_F(NewHorizonsStartingBookReplacementTest, SlowDefaultActuallySuppressesEnemyInitiative)
{
	ASSERT_NO_FATAL_FAILURE(combat(books[2]));
	const auto mana = (attackerSideHero->getNormalSpellPoints() + attackerSideHero->getBufferSpellPoints());
	ASSERT_TRUE(cast(enemy));
	EXPECT_LT((attackerSideHero->getNormalSpellPoints() + attackerSideHero->getBufferSpellPoints()), mana);
	EXPECT_FALSE(enemy->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell(selected.to))))->empty());
}
TEST_F(NewHorizonsStartingBookReplacementTest, LifeDrainDefaultActuallyDamagesEnemyUsingRequiredPair)
{
	ASSERT_NO_FATAL_FAILURE(combat(books[8]));
	const auto health = enemy->getAvailableHealth();
	const auto mana = (attackerSideHero->getNormalSpellPoints() + attackerSideHero->getBufferSpellPoints());
	ASSERT_TRUE(cast(enemy, friendly));
	EXPECT_LT(enemy->getAvailableHealth(), health);
	EXPECT_LT((attackerSideHero->getNormalSpellPoints() + attackerSideHero->getBufferSpellPoints()), mana);
}
TEST_F(NewHorizonsStartingBookReplacementTest, ShadowGiftDefaultRequiresAndPaysVitalityChoice)
{
	ASSERT_NO_FATAL_FAILURE(combat(books[13]));
	const auto health = friendly->getShadowGiftCurrentHealth();
	const auto capacity = friendly->getShadowGiftMaximumHealth();
	const auto mana = (attackerSideHero->getNormalSpellPoints() + attackerSideHero->getBufferSpellPoints());
	EXPECT_FALSE(cast(friendly));
	EXPECT_EQ(friendly->getShadowGiftCurrentHealth(), health);
	EXPECT_EQ((attackerSideHero->getNormalSpellPoints() + attackerSideHero->getBufferSpellPoints()), mana);
	ASSERT_TRUE(cast(friendly, nullptr, 20));
	EXPECT_LT(friendly->getShadowGiftCurrentHealth(), health);
	EXPECT_LT(friendly->getShadowGiftMaximumHealth(), capacity);
	EXPECT_LT((attackerSideHero->getNormalSpellPoints() + attackerSideHero->getBufferSpellPoints()), mana);
}
TEST_F(NewHorizonsStartingBookReplacementTest, PiquedramKeepsApprovedDevelopmentAndPrototypeSpecialty)
{
	ASSERT_NO_FATAL_FAILURE(prepare(books[2]));
	EXPECT_EQ(attackerSideHero->getPerkSkillRank("new-horizons:logistics"), MasteryLevel::BASIC);
	EXPECT_EQ(attackerSideHero->getPerkSkillRank("new-horizons:metamagic"), MasteryLevel::BASIC);
	EXPECT_TRUE(attackerSideHero->hasActivePerk("new-horizons:logistics", "new-horizons:logistics.scouting"));
	EXPECT_EQ(attackerSideHero->secSkills.size(), 2u);
	EXPECT_EQ(attackerSideHero->getHeroType()->specialty.size(),
		HeroTypeID(HeroTypeID::decode(books[2].hero)).toHeroType()->specialty.size());
}
TEST_F(NewHorizonsStartingBookReplacementTest, ExplicitMapBookIsUntouched)
{
	preset = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(SpellID(SpellID::MAGIC_ARROW)));
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(spell(selected.to)));
}
TEST_F(NewHorizonsStartingBookReplacementTest, AbsentCapturedTableDoesNotInventSuccessor)
{
	absent = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(spell(selected.to)));
	EXPECT_FALSE(attackerSideHero->getPrimaryGrowthRules()["startingSkills"].Struct().contains("startingBookReplacements"));
}
TEST_F(NewHorizonsStartingBookReplacementTest, LegacyKeepsOriginalDefaultAndSkills)
{
	legacy = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(spell(selected.from)));
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(spell(selected.to)));
	EXPECT_EQ(attackerSideHero->secSkills, attackerSideHero->getHeroType()->secSkillsInit);
}
TEST_F(NewHorizonsStartingBookReplacementTest, CurrentWorldRoundtripAndReinitNeverRecaptureDefaultBook)
{
	ASSERT_NO_FATAL_FAILURE(prepare(books[2]));
	CGameState firstRead;
	firstRead.preInit(LIBRARY);
	firstRead.loadFromMemory(gameState()->saveToMemory());
	const auto * firstHero = firstRead.getMap().getHero(attackerSideHero->getHeroTypeID());
	ASSERT_NE(firstHero, nullptr);
	EXPECT_TRUE(firstHero->spellbookContainsSpell(spell(selected.to)));
	EXPECT_EQ(firstHero->getPrimaryGrowthRules(), attackerSideHero->getPrimaryGrowthRules());
	attackerSideHero->removeSpellFromSpellbook(spell(selected.to));
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	auto * hero = restored.getMap().getHero(attackerSideHero->getHeroTypeID());
	ASSERT_NE(hero, nullptr);
	EXPECT_EQ(hero->getPrimaryGrowthRules(), attackerSideHero->getPrimaryGrowthRules());
	EXPECT_FALSE(hero->spellbookContainsSpell(spell(selected.to)));
	GameRandomizer randomizer(restored);
	ASSERT_NO_THROW(hero->initHero(randomizer));
	EXPECT_FALSE(hero->spellbookContainsSpell(spell(selected.to)));
}
TEST_F(NewHorizonsStartingBookReplacementTest, HeroIdentityAndExactSourceBothRequired)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_EQ(newHorizonsHeroes::startingBookReplacement(attackerSideHero->getPrimaryGrowthRules(),
		attackerSideHero->getMagicRules(), attackerSideHero->getHeroTypeID(), SpellID(SpellID::HASTE)), std::nullopt);
	EXPECT_EQ(newHorizonsHeroes::startingBookReplacement(attackerSideHero->getPrimaryGrowthRules(),
		attackerSideHero->getMagicRules(), HeroTypeID(HeroTypeID::decode("core:solmyr")), spell(selected.from)), std::nullopt);
}
TEST_F(NewHorizonsStartingBookReplacementTest, InvalidRawTableAndCapturedRosterRejectWithoutBookMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto original = attackerSideHero->getSpellsInSpellbook();
	const auto reject = [&](const std::function<void(JsonNode &)> & corrupt)
	{
		auto rules = attackerSideHero->getPrimaryGrowthRules();
		corrupt(rules["startingSkills"]["startingBookReplacements"]);
		EXPECT_THROW(newHorizonsHeroes::startingBookReplacement(rules, attackerSideHero->getMagicRules(),
			attackerSideHero->getHeroTypeID(), spell(selected.from)), std::runtime_error);
		EXPECT_EQ(attackerSideHero->getSpellsInSpellbook(), original);
	};
	reject([](JsonNode & table) { table = JsonNode(); });
	reject([](JsonNode & table) { table["core:rion"]["from"].String() = "core:shield"; });
	reject([](JsonNode & table) { table["core:rion"]["to"].String() = "core:missingSpell"; });
	reject([](JsonNode & table) { table["core:rion"]["to"].String() = "core:slow"; });
	reject([](JsonNode & table) { table["core:rion"]["to"] = JsonNode(); });
	reject([](JsonNode & table) { table["core:solmyr"] = table["core:rion"]; table.Struct().erase("core:rion"); });
	auto magic = attackerSideHero->getMagicRules();
	magic["spells"][selected.to]["active"].Bool() = false;
	EXPECT_THROW(newHorizonsHeroes::startingBookReplacement(attackerSideHero->getPrimaryGrowthRules(), magic,
		attackerSideHero->getHeroTypeID(), spell(selected.from)), std::runtime_error);
}
TEST_F(NewHorizonsStartingBookReplacementTest, AllOuterOldWritersRejectBeforePrefix)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto old = static_cast<ESerializationVersion>(
		static_cast<int>(ESerializationVersion::NEW_HORIZONS_STARTING_BOOK_REPLACEMENTS) - 1);
	const auto reject = [old](auto & value)
	{
		CMemorySerializer bytes;
		bytes.oser.version = old;
		EXPECT_THROW(value.serialize(bytes.oser), std::runtime_error);
		EXPECT_TRUE(bytes.extractBuffer().empty());
	};
	reject(*attackerSideHero);
	reject(gameState()->getMap());
	reject(*gameState());
	GameSettings settings;
	settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS, attackerSideHero->getPrimaryGrowthRules());
	reject(settings);
	LobbyStartGame lobby;
	lobby.initializedGameState = gameState();
	lobby.initializedStartInfo = std::make_shared<StartInfo>(*gameState()->getStartInfo());
	reject(lobby);
}
TEST_F(NewHorizonsStartingBookReplacementTest, RawReadRejectsExplicitNullAndOldTableButAbsentNeverBackfills)
{
	JsonNode raw;
	raw["heroes"]["newHorizons"] = JsonNode(JsonPath::builtin("config/newHorizonsHeroes"));
	raw["heroes"]["newHorizons"]["startingSkills"]["startingBookReplacements"] = JsonNode();
	CMemorySerializer invalid;
	invalid.oser & raw;
	GameSettings decoded;
	EXPECT_THROW(decoded.serialize(invalid.iser), std::runtime_error);
	raw["heroes"]["newHorizons"] = JsonNode(JsonPath::builtin("config/newHorizonsHeroes"));
	CMemorySerializer old;
	old.oser & raw;
	old.iser.version = static_cast<ESerializationVersion>(
		static_cast<int>(ESerializationVersion::NEW_HORIZONS_STARTING_BOOK_REPLACEMENTS) - 1);
	EXPECT_THROW(decoded.serialize(old.iser), std::runtime_error);
	raw["heroes"]["newHorizons"]["startingSkills"].Struct().erase("startingBookReplacements");
	raw["heroes"]["newHorizons"].Struct().erase("remainingSpellSpecialtyReplacements");
	raw["heroes"]["newHorizons"]["skillSpecialties"].Struct().erase("navigationStartReplacements");
	raw["heroes"]["newHorizons"].Struct().erase("defaultCreatureLineReplacements");
	std::erase_if(raw["heroes"]["newHorizons"]["nonDamageSpellSpecialties"]["spells"].Vector(),
		[](const JsonNode & value) { return value.String() == "new-horizons:phantomArmy"; });
	CMemorySerializer absentBytes;
	absentBytes.oser & raw;
	ASSERT_NO_THROW(decoded.serialize(absentBytes.iser));
	CMemorySerializer outgoing;
	decoded.serialize(outgoing.oser);
	JsonNode savedRaw;
	outgoing.iser & savedRaw;
	EXPECT_FALSE(savedRaw["heroes"]["newHorizons"]["startingSkills"].Struct().contains("startingBookReplacements"));
}
#ifdef ENABLE_BATTLE_AI
TEST_F(NewHorizonsStartingBookReplacementTest, ExistingAiTargetEnumeratorUsesActualInscribedSlow)
{
	ASSERT_NO_FATAL_FAILURE(combat(books[2]));
	ASSERT_TRUE(attackerSideHero->getSpellsInSpellbook().contains(spell(selected.to)));
	spells::BattleCast castInfo(battle(), attackerSideHero, spells::Mode::HERO, spell(selected.to).toSpell());
	auto mechanics = spell(selected.to).toSpell()->battleMechanics(&castInfo);
	const auto targets = SpellTargetEvaluator::getViableTargets(mechanics.get());
	EXPECT_TRUE(std::ranges::any_of(targets, [this](const spells::Target & target)
	{
		return !target.empty() && target.front().unitValue == enemy;
	}));
}
#endif
}
