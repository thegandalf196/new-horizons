/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../OverkillRandomPrayerRules.h"
#include "HeroCommandFixture.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/battle/ConsecratedArmsChoice.h"
#include "../../../AI/BattleAI/ConsecratedArmsValue.h"
#include "../../../AI/BattleAI/AttackPossibility.h"
#include "../../../client/battle/ConsecratedArmsPrayerChoice.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/serializer/CMemorySerializer.h"

namespace
{
using namespace consecratedArms;
constexpr auto ATTACKER = BattleSide::ATTACKER;

class ConsecratedArmsChoiceTest : public HeroCommandFixture
{
protected:
	bool pinRandomPrayers = false;
	std::vector<const CStack *> friendly;

	void SetUp() override
	{
		useRevisedOrders = true;
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons";
	}

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		if(pinRandomPrayers)
		{
			map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, overkillRandomPrayerRules());
			map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, overkillRandomPrayerHeroes());
		}
	}

	void prepare(MasteryLevel::Type rank = MasteryLevel::BASIC)
	{
		startGame();
		ASSERT_TRUE(enabled(attackerSideHero->getPerkState().getRules()));
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(SKILL)), rank, ChangeValueMode::ABSOLUTE);
		for(int slot = 0; slot < 3; ++slot)
			ASSERT_TRUE(attackerSideHero->setCreature(SlotID(slot), creatureByName("core:pikeman"), 10 + slot));
		startBattle();
		beginCombat();
		friendly.clear();
		for(const auto * unit : battle()->battleGetAllStacks(false))
			if(unit->unitSide() == ATTACKER && unit->alive() && unit->creatureId() == creatureByName("core:pikeman"))
				friendly.push_back(unit);
		ASSERT_GE(friendly.size(), 3u);
	}

	BattleAction charge(std::vector<uint8_t> prayers, uint32_t recipient) const
	{
		auto action = BattleAction::makeHeroCommand(ATTACKER, HeroCommand::CHARGE);
		action.prayerChoices = std::move(prayers);
		action.prayerRecipient = recipient;
		return action;
	}

	bool submit(const BattleAction & action)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	int prayerTotal() const
	{
		int total = 0;
		for(const auto * unit : friendly)
			total += activePrayerCount(*unit);
		return total;
	}

	void expectRejected(const BattleAction & action)
	{
		const auto allowances = battle()->getHeroActionAllowances(ATTACKER);
		EXPECT_FALSE(submit(action));
		EXPECT_EQ(battle()->getHeroActionAllowances(ATTACKER), allowances);
		EXPECT_EQ(prayerTotal(), 0);
	}
};
}

TEST_F(ConsecratedArmsChoiceTest, ValidChoiceAppliesExactlyThatPrayerToExactlyThatRecipient)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(capturePolicy(attackerSideHero).chosen);
	const auto * recipient = friendly.at(1);
	ASSERT_TRUE(submit(charge({static_cast<uint8_t>(Prayer::UNBROKEN_VOW)}, recipient->unitId())));
	EXPECT_TRUE(activePrayer(*recipient, Prayer::UNBROKEN_VOW));
	EXPECT_EQ(activePrayerCount(*recipient), 1);
	EXPECT_EQ(prayerTotal(), 1);
}

TEST_F(ConsecratedArmsChoiceTest, InvalidRecipientIsRejected)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const battle::Unit * enemy = nullptr;
	for(const auto * unit : battle()->battleGetAllStacks(false))
		if(unit->unitSide() == BattleSide::DEFENDER)
			enemy = unit;
	ASSERT_NE(enemy, nullptr);
	expectRejected(charge({0}, enemy->unitId()));
	expectRejected(charge({0}, 999999));
	expectRejected(charge({0}, BattleAction::NO_PRAYER_RECIPIENT));
}

TEST_F(ConsecratedArmsChoiceTest, WrongCountUnknownOrMissingPrayersAreRejected)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto id = friendly.front()->unitId();
	expectRejected(charge({}, BattleAction::NO_PRAYER_RECIPIENT));
	expectRejected(charge({}, id));
	expectRejected(charge({0, 1}, id));
	expectRejected(charge({6}, id));
}

TEST_F(ConsecratedArmsChoiceTest, AntiphonalAppliesTwoDifferentChosenPrayers)
{
	ASSERT_NO_FATAL_FAILURE(prepare(MasteryLevel::EXPERT));
	for(const auto * perk : {".enduringPrayer", ".sacredAccord", ".antiphonalPrayer"})
		attackerSideHero->applyPerkSelection({SKILL, std::string(SKILL) + perk});
	ASSERT_TRUE(capturePolicy(attackerSideHero).has(Perk::ANTIPHONAL_PRAYER));
	const auto id = friendly.at(2)->unitId();
	expectRejected(charge({1}, id));
	expectRejected(charge({1, 1}, id));
	ASSERT_TRUE(submit(charge({1, 4}, id)));
	EXPECT_TRUE(activePrayer(*friendly.at(2), Prayer::DAUNTLESS_ASSAULT));
	EXPECT_TRUE(activePrayer(*friendly.at(2), Prayer::UNBROKEN_VOW));
	EXPECT_EQ(prayerTotal(), 2);
}

TEST_F(ConsecratedArmsChoiceTest, AiFallbackChoiceWithoutABoardKeepsOrdersLegal)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto action = BattleAction::makeHeroCommand(ATTACKER, HeroCommand::CHARGE);
	consecratedArmsAI::applyChosenPrayers(*battle(), nullptr, {}, action);
	ASSERT_EQ(action.prayerChoices, std::vector<uint8_t>{0});
	ASSERT_NE(action.prayerRecipient, BattleAction::NO_PRAYER_RECIPIENT);
	ASSERT_TRUE(submit(action));
	EXPECT_EQ(prayerTotal(), 1);
	EXPECT_TRUE(activePrayer(*battle()->battleGetUnitByID(action.prayerRecipient), Prayer::DIVINE_PURPOSE));
}

TEST_F(ConsecratedArmsChoiceTest, ChoiceRequestMatchesAuthorityAndPlayerPickerChoiceIsAccepted)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto action = BattleAction::makeHeroCommand(ATTACKER, HeroCommand::CHARGE);
	const auto request = choiceRequest(*battle(), action);
	ASSERT_EQ(request.prayers, 1u);
	EXPECT_TRUE(request.policy.chosen);
	for(const auto * unit : friendly)
		EXPECT_TRUE(std::binary_search(request.recipients.begin(), request.recipients.end(), unit->unitId()));
	consecratedArmsPrayerChoice::Selection selection(request.prayers, request.recipients);
	const auto recipient = friendly.at(1)->unitId();
	EXPECT_FALSE(selection.chooseRecipient(recipient)); // The prayer is chosen first.
	ASSERT_TRUE(selection.togglePrayer(Prayer::STEADFAST_SOUL));
	ASSERT_TRUE(selection.chooseRecipient(recipient));
	ASSERT_TRUE(selection.apply(action, choiceRequest(*battle(), action)));
	ASSERT_TRUE(submit(action));
	EXPECT_TRUE(activePrayer(*friendly.at(1), Prayer::STEADFAST_SOUL));
	EXPECT_EQ(prayerTotal(), 1);
}

TEST_F(ConsecratedArmsChoiceTest, ChoiceRequestAsksForTwoPrayersWithAntiphonal)
{
	ASSERT_NO_FATAL_FAILURE(prepare(MasteryLevel::EXPERT));
	for(const auto * perk : {".enduringPrayer", ".sacredAccord", ".antiphonalPrayer"})
		attackerSideHero->applyPerkSelection({SKILL, std::string(SKILL) + perk});
	auto action = BattleAction::makeHeroCommand(ATTACKER, HeroCommand::CHARGE);
	const auto request = choiceRequest(*battle(), action);
	ASSERT_EQ(request.prayers, 2u);
	consecratedArmsPrayerChoice::Selection selection(request.prayers, request.recipients);
	ASSERT_TRUE(selection.togglePrayer(Prayer::DIVINE_PURPOSE));
	ASSERT_TRUE(selection.togglePrayer(Prayer::HEAVENS_JUDGMENT));
	ASSERT_TRUE(selection.chooseRecipient(friendly.front()->unitId()));
	ASSERT_TRUE(selection.apply(action, choiceRequest(*battle(), action)));
	ASSERT_TRUE(submit(action));
	EXPECT_TRUE(activePrayer(*friendly.front(), Prayer::DIVINE_PURPOSE));
	EXPECT_TRUE(activePrayer(*friendly.front(), Prayer::HEAVENS_JUDGMENT));
	EXPECT_EQ(prayerTotal(), 2);
}

TEST_F(ConsecratedArmsChoiceTest, HeroWithoutTheSkillCarriesNoChoice)
{
	ASSERT_NO_FATAL_FAILURE(prepare(MasteryLevel::NONE));
	EXPECT_FALSE(submit(charge({0}, friendly.front()->unitId())));
	auto action = BattleAction::makeHeroCommand(ATTACKER, HeroCommand::CHARGE);
	EXPECT_EQ(choiceRequest(*battle(), action).prayers, 0u);
	EXPECT_TRUE(choiceRequest(*battle(), action).recipients.empty());
	consecratedArmsAI::applyChosenPrayers(*battle(), nullptr, {}, action);
	EXPECT_TRUE(action.prayerChoices.empty());
	EXPECT_TRUE(submit(action));
	EXPECT_EQ(prayerTotal(), 0);
}

TEST_F(ConsecratedArmsChoiceTest, VersionThreeRejectsChoiceFieldsAndStaysRandom)
{
	pinRandomPrayers = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_FALSE(capturePolicy(attackerSideHero).chosen);
	expectRejected(charge({0}, friendly.front()->unitId()));
	auto action = BattleAction::makeHeroCommand(ATTACKER, HeroCommand::CHARGE);
	EXPECT_EQ(choiceRequest(*battle(), action).prayers, 0u); // Random prayers open no picker.
	consecratedArmsAI::applyChosenPrayers(*battle(), nullptr, {}, action);
	EXPECT_TRUE(action.prayerChoices.empty());
	ASSERT_TRUE(submit(action));
	// Version 3 draws one random prayer for every recipient of the Order.
	for(const auto * unit : friendly)
		EXPECT_EQ(activePrayerCount(*unit), 1);
}

TEST(ConsecratedArmsChoiceSerialization, ChoiceFieldsRoundTripAndRefuseOlderFormats)
{
	auto action = BattleAction::makeHeroCommand(ATTACKER, HeroCommand::CHARGE);
	action.prayerChoices = {2, 5};
	action.prayerRecipient = 7;
	CMemorySerializer current;
	current.oser & action;
	BattleAction decoded;
	current.iser & decoded;
	EXPECT_EQ(decoded, action);

	CMemorySerializer older;
	older.oser.version = ESerializationVersion::NEW_HORIZONS_CONSECRATED_ARMS;
	EXPECT_THROW(older.oser & action, std::runtime_error);

	const auto plain = BattleAction::makeHeroCommand(ATTACKER, HeroCommand::CHARGE);
	CMemorySerializer legacy;
	legacy.oser.version = ESerializationVersion::NEW_HORIZONS_CONSECRATED_ARMS;
	legacy.iser.version = ESerializationVersion::NEW_HORIZONS_CONSECRATED_ARMS;
	legacy.oser & plain;
	BattleAction loaded;
	loaded.prayerChoices = {1};
	loaded.prayerRecipient = 3;
	legacy.iser & loaded;
	EXPECT_EQ(loaded, plain);
}
