/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/battle/CommandArmyBonuses.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/CBonusSystemNode.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/bonuses/OwnedBonusCaptureCodec.h"
#include "../../lib/bonuses/OwnedBonusPayloads.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/entities/hero/NewHorizonsSpecialtySerialization.h"
#include "../../lib/serializer/CMemorySerializer.h"

namespace
{
using Version = ESerializationVersion;
auto capabilities(Version version)
{
	CMemorySerializer wire;
	wire.oser.version = version;
	return newHorizonsHeroes::SpecialtySerializationContext::fromHandler(wire.oser);
}
std::vector<Bonus> projected()
{
	newHorizonsHeroes::CommandSlotModifiers values;
	values.enhancements.attack = 2;
	values.enhancements.maximumHealthPercent = 10;
	values.enhancements.magicalReductionPercent = 30;
	values.authority.magicalReductionPercent = 10;
	values.authority.directAttackDamagePercent = 15;
	return commandArmyBonuses::project(values);
}
template<class T>
void rejectsBeforePrefix(T & source, Version version)
{
	CMemorySerializer wire;
	wire.oser.version = version;
	EXPECT_THROW(wire.oser & source, std::runtime_error);
	EXPECT_TRUE(wire.extractBuffer().empty());
}
}

TEST(CommandArmyBonusSerialization, Current1158ClonesOwnProjectionWithAliasesAndNoNativeFallback)
{
	ASSERT_EQ(Version::CURRENT, Version::NEW_HORIZONS_COMMAND_AUTHORITY);
	const auto codec = OwnedBonusCaptureCodec::current();
	ASSERT_EQ(codec.version(), Version::NEW_HORIZONS_COMMAND_AUTHORITY);
	const auto bonuses = projected();
	ASSERT_EQ(bonuses.size(), 5u);
	for(const auto & bonus : bonuses)
	{
		ASSERT_TRUE(commandArmyBonuses::isProjectedBonus(bonus));
		auto source = std::make_shared<Bonus>(bonus);
		const auto copied = ownedBonusPayloads::copy({source, source});
		ASSERT_EQ(copied->roots().size(), 2u);
		EXPECT_EQ(copied->roots()[0], copied->roots()[1]);
		EXPECT_NE(copied->roots()[0], source);
		EXPECT_EQ(copied->roots()[0]->stacking, bonus.stacking);
		EXPECT_EQ(copied->roots()[0]->val, bonus.val);
		EXPECT_EQ(copied->roots()[0]->type, bonus.type);
		EXPECT_EQ(copied->roots()[0]->valType, bonus.valType);
		EXPECT_TRUE(commandArmyBonuses::isProjectedBonus(*copied->roots()[0]));
	}
}

TEST(CommandArmyBonusSerialization, HistoricalProfilesRemainKnownAndRejectProjectedRecordsBeforeBytes)
{
	for(const auto version : {Version::NEW_HORIZONS_ORDINARY_COMBAT_SPECIALTIES,
		Version::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES, Version::NEW_HORIZONS_PASSIVE_FACTION_SKILLS,
		Version::NEW_HORIZONS_FACTION_SKILL_REVISIONS, Version::NEW_HORIZONS_NATIVE_RULES_REDESIGN})
	{
		const auto codec = OwnedBonusCaptureCodec::fromSourceEpoch(version, capabilities(version));
		for(auto bonus : projected())
		{
			rejectsBeforePrefix(bonus, version);
			EXPECT_THROW(ownedBonusPayloads::copy({std::make_shared<Bonus>(bonus)}, codec), std::runtime_error);
		}
		Bonus ordinary(BonusDuration::ONE_BATTLE, BonusType::LUCK, BonusSource::OTHER, 2, BonusSourceID());
		EXPECT_NO_THROW(ownedBonusPayloads::copy({std::make_shared<Bonus>(ordinary)}, codec));
	}
}

TEST(CommandArmyBonusSerialization, NestedProjectionRejectsOuterParametersListAndNodeBeforePrefixes)
{
	auto child = std::make_shared<Bonus>(projected().front());
	BonusParametersOnCombatEvent event;
	event.effects.push_back(BonusParametersOnCombatEvent::CombatEffectBonus{child, false});
	Bonus outer(BonusDuration::ONE_BATTLE, BonusType::LUCK, BonusSource::OTHER, 1, BonusSourceID());
	outer.parameters = std::make_shared<BonusParameters>(event);
	BonusList list;
	list.push_back(std::make_shared<Bonus>(outer));
	CBonusSystemNode node(BonusNodeType::ARMY);
	node.addNewBonus(std::make_shared<Bonus>(outer));
	for(const auto version : {Version::NEW_HORIZONS_PASSIVE_FACTION_SKILLS, Version::NEW_HORIZONS_NATIVE_RULES_REDESIGN})
	{
		rejectsBeforePrefix(outer, version);
		rejectsBeforePrefix(*outer.parameters, version);
		rejectsBeforePrefix(list, version);
		rejectsBeforePrefix(node, version);
	}
	CMemorySerializer current;
	ASSERT_NO_THROW(current.oser & outer);
	Bonus restored;
	ASSERT_NO_THROW(current.iser & restored);
	ASSERT_TRUE(restored.parameters);
	const auto * effects = restored.parameters->combatEffects();
	ASSERT_NE(effects, nullptr);
	ASSERT_EQ(effects->effects.size(), 1u);
	const auto nested = std::get<BonusParametersOnCombatEvent::CombatEffectBonus>(effects->effects.front()).bonus;
	ASSERT_TRUE(nested);
	EXPECT_TRUE(commandArmyBonuses::isProjectedBonus(*nested));
}

TEST(CommandArmyBonusSerialization, CurrentBytesReadAs1157RejectAndPartialMasksCannotAdmit1158)
{
	auto bonus = projected().front();
	CMemorySerializer wire;
	ASSERT_NO_THROW(wire.oser & bonus);
	wire.iser.version = Version::NEW_HORIZONS_NATIVE_RULES_REDESIGN;
	Bonus restored;
	EXPECT_THROW(wire.iser & restored, std::runtime_error);
	const auto current = capabilities(Version::NEW_HORIZONS_COMMAND_AUTHORITY);
	const auto native = capabilities(Version::NEW_HORIZONS_NATIVE_RULES_REDESIGN);
	EXPECT_NE(current, native);
	EXPECT_THROW(OwnedBonusCaptureCodec::fromSourceEpoch(Version::NEW_HORIZONS_COMMAND_AUTHORITY, native), std::invalid_argument);
	EXPECT_THROW(OwnedBonusCaptureCodec::fromSourceEpoch(Version::NEW_HORIZONS_NATIVE_RULES_REDESIGN, current), std::invalid_argument);
	auto partial = current;
	partial.nativeRulesRedesign = false;
	EXPECT_THROW(OwnedBonusCaptureCodec::fromSourceCapabilities(partial), std::invalid_argument);
	partial = current;
	partial.adjacencyFilters = false;
	EXPECT_THROW(OwnedBonusCaptureCodec::fromSourceCapabilities(partial), std::invalid_argument);
}

TEST(CommandArmyBonusSerialization, Native1157KeepsItsNativeBonusEnvelopeAnd1155RemainsStrict)
{
	const auto native = OwnedBonusCaptureCodec::fromSourceEpoch(Version::NEW_HORIZONS_NATIVE_RULES_REDESIGN,
		capabilities(Version::NEW_HORIZONS_NATIVE_RULES_REDESIGN));
	const auto passive = OwnedBonusCaptureCodec::fromSourceEpoch(Version::NEW_HORIZONS_PASSIVE_FACTION_SKILLS,
		capabilities(Version::NEW_HORIZONS_PASSIVE_FACTION_SKILLS));
	auto source = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::MAGICAL_DAMAGE_IMMUNITY,
		BonusSource::OTHER, 1, BonusSourceID());
	const auto copied = ownedBonusPayloads::copy({source}, native);
	ASSERT_EQ(copied->roots().size(), 1u);
	EXPECT_NE(copied->roots().front(), source);
	EXPECT_EQ(copied->roots().front()->type, BonusType::MAGICAL_DAMAGE_IMMUNITY);
	EXPECT_THROW(ownedBonusPayloads::copy({source}, passive), std::runtime_error);
	rejectsBeforePrefix(*source, Version::NEW_HORIZONS_PASSIVE_FACTION_SKILLS);
}
