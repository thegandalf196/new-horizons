/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../HistoricalFactionPerkRules.h"
#include "../server/battles/BattleTestFixture.h"
#include "../server/battles/FullGameSnapshotTypes.h"
#include "../../lib/GameSettings.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/networkPacks/PacksForLobby.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/spells/NewHorizonsDivineRetribution.h"

namespace
{
using Version = ESerializationVersion;
constexpr auto current = Version::NEW_HORIZONS_PASSIVE_FACTION_SKILLS;
constexpr auto historical = Version::NEW_HORIZONS_OPTIONAL_QUICK_STUDY;
Bonus marker(BonusType type, std::optional<int> ratio = std::nullopt)
{
	Bonus result(BonusDuration::N_TURNS, type, BonusSource::SPELL_EFFECT, 100,
		BonusSourceID(SpellID(SpellID::decode("new-horizons:divineRetribution"))));
	if(ratio)
	{
		JsonNode parameters;
		parameters["payoutRatioBasisPoints"].Integer() = *ratio;
		result.parameters = std::make_shared<BonusParameters>(parameters);
	}
	return result;
}
template<class T> void refusesOldPrefix(T & value)
{
	CMemorySerializer wire;
	wire.oser.version = historical;
	EXPECT_THROW(wire.oser & value, std::runtime_error);
	EXPECT_TRUE(wire.extractBuffer().empty());
}
std::shared_ptr<BonusParameters> owned(std::shared_ptr<const Bonus> child)
{
	BonusParametersOnCombatEvent event;
	event.effects.push_back(BonusParametersOnCombatEvent::CombatEffectBonus{child, false});
	return std::make_shared<BonusParameters>(event);
}
}
class DivinePayoutSerializationTest : public BattleTestFixture
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		BattleTestFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, historicalFactionPerkRules());
	}
};
TEST_F(DivinePayoutSerializationTest, AbsentRatioRetainsHistoricalThirtyPercentAndOldCodecs)
{
	startGame();
	for(const auto type : {BonusType::DIVINE_RETRIBUTION, BonusType::DIVINE_RETRIBUTION_JUDGED})
		for(const auto epoch : {Version::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES, historical, current})
		for(const int shape : {0, 1, 2})
		{
			auto source = marker(type);
			if(shape == 1) source.parameters = std::make_shared<BonusParameters>(int32_t{42});
			if(shape == 2)
			{
				JsonNode legacy; legacy["storedDamage"].Integer() = 1;
				source.parameters = std::make_shared<BonusParameters>(legacy);
			}
			CMemorySerializer wire; wire.oser.version = wire.iser.version = epoch;
			ASSERT_NO_THROW(wire.oser & source);
			Bonus restored; ASSERT_NO_THROW(wire.iser & restored);
			EXPECT_FALSE(newHorizonsDivineRetribution::capturedPayoutRatioBasisPoints(restored));
			EXPECT_EQ(newHorizonsDivineRetribution::payoutRatioBasisPoints(restored), 3000);
		}
}
TEST_F(DivinePayoutSerializationTest, PresentBoundsRoundTrip1155AndEvenThirtyPercentRefuses1154)
{
	startGame();
	for(const auto type : {BonusType::DIVINE_RETRIBUTION, BonusType::DIVINE_RETRIBUTION_JUDGED})
		for(const int ratio : {3000, 5250, 6900})
		{
			auto source = marker(type, ratio); refusesOldPrefix(source);
			CMemorySerializer wire; wire.oser.version = wire.iser.version = current;
			ASSERT_NO_THROW(wire.oser & source);
			Bonus restored; ASSERT_NO_THROW(wire.iser & restored);
			EXPECT_EQ(newHorizonsDivineRetribution::capturedPayoutRatioBasisPoints(restored), ratio);
		}
}
TEST_F(DivinePayoutSerializationTest, PresentMalformedRatioRefusesBeforeDirectBonusWrite)
{
	startGame();
	std::vector<JsonNode> invalid(1);
	JsonNode value; value.Bool() = true; invalid.push_back(value);
	value = JsonNode(); value.String() = "3000"; invalid.push_back(value);
	value = JsonNode(); value.Float() = 3000.0; invalid.push_back(value);
	for(const int ratio : {-1, 2999, 6901})
	{
		value = JsonNode(); value.Integer() = ratio; invalid.push_back(value);
	}
	for(const auto & bad : invalid)
	{
		auto source = marker(BonusType::DIVINE_RETRIBUTION);
		JsonNode parameters; parameters["payoutRatioBasisPoints"] = bad;
		source.parameters = std::make_shared<BonusParameters>(parameters);
		CMemorySerializer wire; wire.oser.version = current;
		EXPECT_THROW(wire.oser & source, std::runtime_error);
		EXPECT_TRUE(wire.extractBuffer().empty());
		EXPECT_EQ(source.parameters->toJsonNode(), parameters);
	}
}
TEST_F(DivinePayoutSerializationTest, ReservedReceiptRequiresSpellEffectButUnrelatedJsonStaysLegacy)
{
	startGame();
	auto source = marker(BonusType::DIVINE_RETRIBUTION, 3000);
	source.source = BonusSource::OTHER;
	CMemorySerializer wire; wire.oser.version = current;
	EXPECT_THROW(wire.oser & source, std::runtime_error);
	EXPECT_TRUE(wire.extractBuffer().empty());
	source.type = BonusType::STACKS_SPEED;
	wire.oser.version = wire.iser.version = historical;
	ASSERT_NO_THROW(wire.oser & source);
	Bonus restored; EXPECT_NO_THROW(wire.iser & restored);
}
TEST_F(DivinePayoutSerializationTest, NestedCombatEffectOwnersRejectBeforeTheirOldPrefixes)
{
	startGame();
	auto parent = marker(BonusType::STACKS_SPEED);
	parent.parameters = owned(std::make_shared<Bonus>(marker(BonusType::DIVINE_RETRIBUTION, 3000)));
	BonusList list; list.push_back(std::make_shared<Bonus>(parent));
	refusesOldPrefix(parent); refusesOldPrefix(*parent.parameters); refusesOldPrefix(list);
}
TEST_F(DivinePayoutSerializationTest, ChangeListsAndExchangeRetainTheirOuterPrefixGuards)
{
	startGame();
	SetStackEffect packet; packet.battleID = BattleID(0);
	packet.toAdd.push_back({1, {marker(BonusType::DIVINE_RETRIBUTION_JUDGED, 6900)}});
	refusesOldPrefix(packet);
	SetCommanderProperty commander;
	commander.which = SetCommanderProperty::BONUS;
	commander.accumulatedBonus = marker(BonusType::DIVINE_RETRIBUTION, 3000);
	refusesOldPrefix(commander);
	battle::BattleEffectExchange exchange;
	for(size_t i = 0; i < exchange.endpoints.size(); ++i)
	{
		exchange.endpoints[i].id = static_cast<uint32_t>(i + 1);
		exchange.endpoints[i].expected.recipientHealth.Struct();
		exchange.endpoints[i].replacement.recipientHealth.Struct();
	}
	exchange.endpoints[0].replacement.effects.push_back(marker(BonusType::DIVINE_RETRIBUTION, 3000));
	refusesOldPrefix(exchange);
}
TEST_F(DivinePayoutSerializationTest, HistoricalReadersRejectAnActual1155ReceiptBeforeAnyApply)
{
	startGame();
	auto source = marker(BonusType::DIVINE_RETRIBUTION, 3000);
	CMemorySerializer wire; wire.oser.version = current;
	ASSERT_NO_THROW(wire.oser & source);
	CMemorySerializer old(wire.extractBuffer()); old.iser.version = historical;
	Bonus discarded; // Failed direct codec values have no rollback contract and are never applied.
	EXPECT_THROW(old.iser & discarded, std::runtime_error);
	EXPECT_EQ(newHorizonsDivineRetribution::payoutRatioBasisPoints(source), 3000);
}
TEST_F(DivinePayoutSerializationTest, HeroMapWorldAndLobbyRejectBeforeTheirOldPrefixes)
{
	startGame(true);
	CMemorySerializer baseline; baseline.oser.version = historical;
	ASSERT_NO_THROW(baseline.oser & *gameState());
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(marker(BonusType::DIVINE_RETRIBUTION, 3000)));
	refusesOldPrefix(*attackerSideHero); refusesOldPrefix(*map()); refusesOldPrefix(*gameState());
	LobbyStartGame lobby; lobby.initializedGameState = gameState();
	refusesOldPrefix(lobby);
}
