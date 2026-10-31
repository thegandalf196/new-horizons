/*
 * OptionalHydraIdentityTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/modding/IdentifierStorage.h"
#include "../../../lib/modding/ModScope.h"
#include "../../../lib/spells/NewHorizonsSorcery.h"
#include "../../../lib/spells/NewHorizonsDivineRetribution.h"
#include "../../../AI/BattleAI/HypotheticBattle.h"
#include "../../../lib/battle/NewHorizonsPlague.h"
#include "../../../lib/battle/NewHorizonsSoulChain.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/BonusList.h"
#include "mock/mock_battle_Unit.h"
#include "../../../lib/battle/BattleEffectExchange.h"
#include "../../../lib/constants/StringConstants.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/spells/HydrasVitalityIdentity.h"
#include "../../../server/CGameHandler.h"
#include <stdexcept>

namespace
{
bool hydraModuleLoaded()
{
	return vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE);
}
}

TEST(OptionalHydraIdentity, LegacyAbsenceMatchesNothingAndRequiredCaptureThrows)
{
	if(hydraModuleLoaded())
		GTEST_SKIP() << "Requires the core-only test profile";
	const Bonus effect(BonusDuration::PERMANENT, BonusType::HP_REGENERATION,
		BonusSource::SPELL_EFFECT, 10, BonusSourceID(SpellID(SpellID::HASTE)));
	EXPECT_EQ(newHorizonsMagic::hydrasVitalitySourceId(JsonNode()), std::nullopt);
	EXPECT_FALSE(newHorizonsMagic::hydrasVitalitySource(JsonNode())(&effect));
	JsonNode empty;
	empty.Struct();
	EXPECT_EQ(newHorizonsMagic::hydrasVitalitySourceId(empty), std::nullopt);
	JsonNode required;
	required["rulesetVersion"].Integer() = 3;
	EXPECT_THROW(newHorizonsMagic::hydrasVitalitySourceId(required), std::runtime_error);
	EXPECT_THROW(newHorizonsMagic::hydrasVitalitySourceId(JsonNode(false)), std::runtime_error);
}

TEST(OptionalHydraIdentity, InstalledHistoricalIdentityPreservesIdAndOrdinarySourcePredicates)
{
	if(!hydraModuleLoaded())
		GTEST_SKIP() << "Requires New Horizons content";
	const SpellID hydra(SpellID::decode("new-horizons:hydrasVitality"));
	const BonusSourceID expected(hydra);
	JsonNode inactive;
	inactive["spells"]["new-horizons:hydrasVitality"]["active"].Bool() = false;
	for(const auto & rules : {JsonNode(), inactive})
	{
		const auto actual = newHorizonsMagic::hydrasVitalitySourceId(rules);
		ASSERT_TRUE(actual);
		EXPECT_EQ(*actual, expected);
		const auto selector = newHorizonsMagic::hydrasVitalitySource(rules);
		const auto oldSelector = Selector::source(BonusSource::SPELL_EFFECT, expected);
		for(const auto source : {BonusSource::SPELL_EFFECT, BonusSource::OTHER, BonusSource::CREATURE_ABILITY})
		{
			for(const auto & sid : {expected, BonusSourceID(SpellID(SpellID::HASTE)), BonusSourceID(CreatureID(hydra.getNum()))})
			{
				for(const auto type : {BonusType::STACK_HEALTH, BonusType::HP_REGENERATION})
				{
					const Bonus effect(BonusDuration::PERMANENT, type, source, 10, sid);
					EXPECT_EQ(selector(&effect), oldSelector(&effect));
					// Exchange historically compares SID without filtering source.
					EXPECT_EQ(effect.sid == *actual, effect.sid == expected);
				}
			}
		}
	}
}

class OptionalHydraBattleTest : public BattleTestFixture
{
protected:
	CStack * subject = nullptr;
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(hydraModuleLoaded())
			GTEST_SKIP() << "Requires the core-only test profile";
		startGame();
		startBattle();
		subject = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 10);
		addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 10);
	}
};

TEST_F(OptionalHydraBattleTest, FirstRoundAndActivationRequireNoOptionalHydra)
{
	ASSERT_NO_THROW(beginCombat());
	ASSERT_NE(battle()->battleActiveUnit(), nullptr);
	EXPECT_FALSE(server.stackActivations.empty());
}

TEST_F(OptionalHydraBattleTest, OrdinaryEffectAddUpdateAndRemoveRequireNoOptionalHydra)
{
	Bonus effect(BonusDuration::N_TURNS, BonusType::STACKS_SPEED,
		BonusSource::SPELL_EFFECT, 2, BonusSourceID(SpellID(SpellID::HASTE)));
	effect.turnsRemain = 2;
	SetStackEffect add;
	add.battleID = BattleID(0);
	add.toAdd.emplace_back(subject->unitId(), std::vector<Bonus>{effect});
	ASSERT_NO_THROW(gameHandler->sendAndApply(add));
	const auto selector = Selector::source(BonusSource::SPELL_EFFECT, effect.sid)
		.And(Selector::type()(effect.type));
	ASSERT_TRUE(subject->hasBonus(selector));
	effect.turnsRemain = 3;
	SetStackEffect update;
	update.battleID = BattleID(0);
	update.toUpdate.emplace_back(subject->unitId(), std::vector<Bonus>{effect});
	ASSERT_NO_THROW(gameHandler->sendAndApply(update));
	const auto updated = subject->getBonuses(selector);
	ASSERT_EQ(updated->size(), 1u);
	EXPECT_EQ(updated->front()->turnsRemain, 3);
	SetStackEffect remove;
	remove.battleID = BattleID(0);
	remove.toRemove.emplace_back(subject->unitId(), std::vector<Bonus>{effect});
	ASSERT_NO_THROW(gameHandler->sendAndApply(remove));
	EXPECT_FALSE(subject->hasBonus(selector));
}

TEST_F(OptionalHydraBattleTest, DetachedHealthPreparationNeedsNoOptionalHydraAndPreservesLiveUnit)
{
	const auto snapshot = battle()->captureBattleEffects(subject->unitId());
	const auto before = subject->save();
	battle::PreparedBattleEffectHealth prepared;
	ASSERT_NO_THROW(prepared = battle::prepareBattleEffectHealth(*subject, snapshot, snapshot, battle()->getMagicRules()));
	ASSERT_TRUE(prepared.unit);
	EXPECT_EQ(prepared.unit->save(), before);
	EXPECT_EQ(subject->save(), before);
}

TEST(OptionalPlagueIdentity, LegacyAbsenceMatchesNothingAndRequiredCaptureThrows)
{
	if(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
		GTEST_SKIP() << "Requires the core-only test profile";
	const Bonus unrelated(BonusDuration::PERMANENT, BonusType::COMBAT_EVENT_TRIGGER, BonusSource::OTHER, 1, BonusSourceID());
	EXPECT_FALSE(newHorizonsPlague::markerSelector(JsonNode())(&unrelated));
	JsonNode empty;
	empty.Struct();
	EXPECT_FALSE(newHorizonsPlague::markerSelector(empty)(&unrelated));
	JsonNode required;
	required["rulesetVersion"].Integer() = 3;
	EXPECT_THROW(newHorizonsPlague::markerSelector(required), std::runtime_error);
	JsonNode malformed(false);
	EXPECT_THROW(newHorizonsPlague::markerSelector(malformed), std::runtime_error);
}

TEST(OptionalPlagueIdentity, InstalledHistoricalIdentityMatchesOriginalPredicate)
{
	if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
		GTEST_SKIP() << "Requires the New Horizons content module";
	const auto plague = SpellID(SpellID::decode(std::string(newHorizonsPlague::SPELL_ID)));
	const auto old = Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(plague))
		.And(Selector::type()(BonusType::COMBAT_EVENT_TRIGGER));
	JsonNode inactive;
	inactive["spells"][std::string(newHorizonsPlague::SPELL_ID)]["active"].Bool() = false;
	for(const auto & rules : {JsonNode(), inactive})
		for(const auto source : {BonusSource::SPELL_EFFECT, BonusSource::OTHER, BonusSource::CREATURE_ABILITY})
			for(const auto & sid : {BonusSourceID(plague), BonusSourceID(SpellID(SpellID::HASTE))})
				for(const auto type : {BonusType::COMBAT_EVENT_TRIGGER, BonusType::HP_REGENERATION})
				{
					const Bonus bonus(BonusDuration::PERMANENT, type, source, 1, sid);
					EXPECT_EQ(newHorizonsPlague::markerSelector(rules)(&bonus), old(&bonus));
				}
}

namespace
{
class OptionalSpellLockEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit OptionalSpellLockEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
}

TEST_F(OptionalHydraBattleTest, HypotheticOpeningAndOrdinaryRoundNeedNoOptionalSpellLock)
{
	ASSERT_FALSE(hydraModuleLoaded());
	ASSERT_FALSE(LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "spell",
		newHorizonsSorcery::SPELL_LOCK_SPELL, true).has_value());
	ASSERT_FALSE(subject->hasBattleForm());
	ASSERT_EQ(battle()->getRound(), 0);

	Bonus haste(BonusDuration::N_TURNS, BonusType::STACKS_SPEED,
		BonusSource::SPELL_EFFECT, 2, BonusSourceID(SpellID(SpellID::HASTE)));
	haste.turnsRemain = 2;
	SetStackEffect add;
	add.battleID = BattleID(0);
	add.toAdd.emplace_back(subject->unitId(), std::vector<Bonus>{haste});
	ASSERT_NO_THROW(gameHandler->sendAndApply(add));
	const auto selector = Selector::source(BonusSource::SPELL_EFFECT, haste.sid)
		.And(Selector::type()(haste.type));
	const auto liveBefore = subject->save();
	auto environment = std::make_shared<OptionalSpellLockEnvironment>(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle model(environment.get(), callback);
	ASSERT_NO_THROW(model.nextRound()); //0→1 retains opening effects; no form means no optional lookup.
	EXPECT_EQ(model.getRound(), 1);
	const auto projected = model.getForUpdate(subject->unitId());
	ASSERT_NE(projected, nullptr);
	ASSERT_FALSE(projected->hasBattleForm());
	const auto opening = projected->getBonuses(selector);
	ASSERT_EQ(opening->size(), 1u);
	EXPECT_EQ(opening->front()->turnsRemain, 2);

	ASSERT_NO_THROW(model.nextRound()); //1→2 ages the ordinary core effect once.
	EXPECT_EQ(model.getRound(), 2);
	const auto aged = projected->getBonuses(selector);
	ASSERT_EQ(aged->size(), 1u);
	EXPECT_EQ(aged->front()->turnsRemain, 1);
	EXPECT_EQ(projected->getAvailableHealth(), subject->getAvailableHealth());
	EXPECT_EQ(projected->getPosition(), subject->getPosition());
	EXPECT_EQ(subject->save(), liveBefore);
	EXPECT_EQ(battle()->getRound(), 0);
}

TEST_F(OptionalHydraBattleTest, PassiveSoulChainAbsenceKeepsRequiredProfileStrict)
{
	ASSERT_FALSE(hydraModuleLoaded());
	ASSERT_FALSE(LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "spell",
		std::string(newHorizonsSoulChain::SPELL_ID), true).has_value());
	const auto before = subject->save();
	EXPECT_FALSE(newHorizonsSoulChain::passiveLinkFor(subject, battle()->getMagicRules()));
	JsonNode empty; empty.Struct();
	EXPECT_FALSE(newHorizonsSoulChain::passiveLinkFor(subject, empty));
	const JsonNode required(JsonPath::builtin("config/newHorizonsMagic"));
	EXPECT_THROW(newHorizonsSoulChain::passiveLinkFor(subject, required), std::runtime_error);
	EXPECT_THROW(newHorizonsSoulChain::passiveLinkFor(subject, JsonNode(false)), std::runtime_error);
	EXPECT_THROW(newHorizonsSoulChain::isEnabled(required), std::runtime_error);
	EXPECT_FALSE(newHorizonsSoulChain::passiveLinkFor(nullptr, required));
	EXPECT_FALSE(newHorizonsSoulChain::linkFor(nullptr));
	EXPECT_EQ(subject->save(), before);
}

TEST(OptionalSoulChainIdentity, InstalledHistoricalAndInactivePassiveReadsRetainStrictLinks)
{
	if(!hydraModuleLoaded())
		GTEST_SKIP() << "Requires the separate New Horizons test profile";
	const SpellID soul(SpellID::decode(std::string(newHorizonsSoulChain::SPELL_ID)));
	const ScriptID status(ScriptID::decode("core:soulChainStatus"));
	auto marker = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::COMBAT_EVENT_TRIGGER,
		BonusSource::SPELL_EFFECT, 3000, BonusSourceID(soul), BonusSubtypeID(status));
	marker->turnsRemain = 2;
	JsonNode payload;
	payload["primaryUnitId"].Integer() = 7;
	payload["casterSide"].Integer() = static_cast<int32_t>(BattleSide::ATTACKER);
	payload["damageMultiplierPercent"].Integer() = 125;
	payload["mdrPenetration"]["captured"].Integer() = 1;
	marker->parameters = std::make_shared<BonusParameters>(payload);
	::testing::NiceMock<UnitMock> unit;
	ON_CALL(unit, getAllBonuses(::testing::_, ::testing::_)).WillByDefault(
		[&](const CSelector & selector, const std::string &) -> TConstBonusListPtr
		{
			auto values = std::make_shared<BonusList>();
			if(selector(marker.get())) values->push_back(marker);
			return values;
		});
	const auto strict = newHorizonsSoulChain::linkFor(&unit);
	ASSERT_TRUE(strict);
	JsonNode empty; empty.Struct();
	const JsonNode current(JsonPath::builtin("config/newHorizonsMagic"));
	auto inactive = current;
	inactive["spells"][std::string(newHorizonsSoulChain::SPELL_ID)]["active"].Bool() = false;
	for(const auto & rules : {JsonNode(), empty, current, inactive})
	{
		const auto actual = newHorizonsSoulChain::passiveLinkFor(&unit, rules);
		ASSERT_TRUE(actual);
		EXPECT_EQ(actual->primaryUnitId, strict->primaryUnitId);
		EXPECT_EQ(actual->casterSide, strict->casterSide);
		EXPECT_EQ(actual->echoBasisPoints, strict->echoBasisPoints);
		EXPECT_EQ(actual->mdrPenetration, strict->mdrPenetration);
		EXPECT_EQ(actual->damageMultiplierPercent, strict->damageMultiplierPercent);
	}
}

TEST(OptionalDivineRetributionIdentity, InstalledIdentityOrLegacyAbsenceKeepsRequiredProfilesStrict)
{
	const auto installed = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "spell",
		"new-horizons:divineRetribution", true);
	JsonNode empty; empty.Struct();
	const JsonNode current(JsonPath::builtin("config/newHorizonsMagic"));
	if(installed)
	{
		auto inactive = current;
		inactive["spells"]["new-horizons:divineRetribution"]["active"].Bool() = false;
		for(const auto & rules : {JsonNode(), empty, current, inactive})
			EXPECT_EQ(newHorizonsDivineRetribution::spellId(rules), SpellID(*installed));
	}
	else
	{
		ASSERT_FALSE(hydraModuleLoaded());
		EXPECT_FALSE(newHorizonsDivineRetribution::spellId(JsonNode()).hasValue());
		EXPECT_FALSE(newHorizonsDivineRetribution::spellId(empty).hasValue());
		EXPECT_THROW(newHorizonsDivineRetribution::spellId(current), std::runtime_error);
		EXPECT_THROW(newHorizonsDivineRetribution::spellId(JsonNode(false)), std::runtime_error);
	}
}

TEST_F(OptionalHydraBattleTest, OrdinaryMeleeAndNextRoundNeedNoOptionalDivineRetribution)
{
	ASSERT_FALSE(newHorizonsDivineRetribution::spellId(battle()->getMagicRules()).hasValue());
	const auto before = subject->save();
	EXPECT_EQ(newHorizonsDivineRetribution::recipientDamage(*battle(), subject, 150), 150);
	EXPECT_EQ(newHorizonsDivineRetribution::recipientDamage(*battle(), subject, 0), 0);
	EXPECT_EQ(subject->save(), before);
	const auto * enemy = battle()->battleGetStackByPos(BattleHex(rightHex));
	ASSERT_NE(enemy, nullptr);
	const auto enemyHealth = enemy->getAvailableHealth();
	ASSERT_NO_THROW(beginCombat());
	ASSERT_EQ(battle()->getRound(), 1);
	ASSERT_TRUE(attack(subject, enemy->getPosition()));
	EXPECT_LT(enemy->getAvailableHealth(), enemyHealth);
	ASSERT_TRUE(enemy->alive());
	ASSERT_NO_THROW(endRound());
	EXPECT_EQ(battle()->getRound(), 2);
	EXPECT_NE(battle()->battleActiveUnit(), nullptr);
}
