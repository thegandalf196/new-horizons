/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/nativeRules/CompiledMetamagicRules.h"
#include "../../lib/nativeRules/CapturedPerkRules.h"
#include "../../lib/entities/hero/NewHorizonsPerkRules.h"
#include "../../lib/nativeRules/CapturedRuleSet.h"
#include "../../lib/nativeRules/CapturedRuleExecutionAudit.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/CSpellHandler.h"
#include "../../lib/GameLibrary.h"
#include "../mock/mock_battle_IBattleState.h"
#include "../../lib/spells/NewHorizonsMagicalMemory.h"
#include "../../lib/CStack.h"
#include "../../lib/mapObjects/army/CStackBasicDescriptor.h"
#include <limits>

namespace
{
using namespace nativeRules;
JsonNode perks() { return JsonNode(JsonPath::builtin("config/newHorizonsPerks")); }
JsonNode magic() { return JsonNode(JsonPath::builtin("config/newHorizonsMagic")); }

JsonNode metamagicProfile(int version)
{
	auto source = perks();
	auto & policy = source["metamagicPolicy"];
	policy["version"].Integer() = version;
	policy.Struct().erase("arcaneOverflowPercentByRank");
	policy.Struct().erase("friendlyDamageReductionPercentByRank");
	policy.Struct().erase("debuffRetryChancePercentByRank");
	if(version == 5)
		policy["arcaneOverflowPercentByRank"].Vector() = {JsonNode(0), JsonNode(25), JsonNode(50), JsonNode(75)};
	return source;
}

class MagicSource : public BattleStateMock
{
public:
	JsonNode raw;
	std::shared_ptr<const CapturedRuleDocument> owner;
	bool exposeExactDocument = true;
	const JsonNode & getMagicRules() const override
	{
		return owner && exposeExactDocument ? owner->document() : raw;
	}
	std::shared_ptr<const CapturedRuleDocument> getCapturedMagicRules() const override { return owner; }
};

void expectDefinition(const CompiledMetamagicEnergy & compiled, const JsonNode & source, SpellID spell)
{
	const auto raw = newHorizonsMetamagic::energyDefinition(source, spell);
	const auto * typed = compiled.definition(spell);
	ASSERT_EQ(bool(raw), typed != nullptr);
	if(!raw) return;
	EXPECT_EQ(typed->family, raw->family);
	EXPECT_EQ(typed->listedMana, raw->listedMana);
	EXPECT_EQ(typed->magical, raw->magical);
	EXPECT_EQ(typed->positive, raw->positive);
	EXPECT_EQ(typed->negative, raw->negative);
	EXPECT_EQ(typed->commonHeroSpell, raw->commonHeroSpell);
	const bool fuel = raw->family.hasValue() && raw->listedMana > 0 && raw->magical && raw->commonHeroSpell;
	EXPECT_EQ(compiled.fuelDefinition(spell) != nullptr, fuel);
}
}

TEST(CompiledMetamagicRules, CapturedRankHalonPayoutAndRefundPoliciesMatchRawArithmetic)
{
	const auto owner = CapturedPerkRules::compile(perks());
	const auto & typed = owner->metamagic();
	const auto raw = newHorizonsMetamagic::resolutionPolicy(owner->document());
	ASSERT_TRUE(raw);
	ASSERT_NE(typed.policy(), nullptr);
	const HeroTypeID halon(HeroTypeID::decode("core:halon"));
	for(int rank = -1; rank <= 4; ++rank)
	{
		EXPECT_EQ(typed.removalChance(rank, HeroTypeID::NONE), newHorizonsMetamagic::removalChancePercent(*raw, rank, false));
		EXPECT_EQ(typed.removalChance(rank, halon), newHorizonsMetamagic::removalChancePercent(*raw, rank, true));
	}
	for(const int64_t mana : {int64_t(0), int64_t(4), int64_t(15), std::numeric_limits<int64_t>::max()})
	{
		EXPECT_EQ(typed.discharge(mana), newHorizonsMetamagic::energyHitPoints(mana, raw->damagePerMana));
		EXPECT_EQ(typed.restoration(mana), newHorizonsMetamagic::energyHitPoints(mana, raw->restorationPerMana));
		for(const int paid : {0, 1, 4, 5, 12})
			for(const int prior : {0, paid})
				for(const int room : {0, 1, 100})
				{
					EXPECT_EQ(typed.recyclingRefund(true, mana, paid, prior, room),
						typed.supports(MetamagicPerk::ManaRecycling) ? newHorizonsMetamagic::recyclingRefund(mana, paid, prior, room) : 0);
					EXPECT_EQ(typed.recyclingRefund(false, mana, paid, prior, room), 0);
					EXPECT_EQ(typed.conservedRefund(true, paid, prior, room, true, true),
						newHorizonsMetamagic::conservedFormulaRefund(paid, prior, room, true, true));
					EXPECT_EQ(typed.conservedRefund(true, paid, prior, room, false, true), 0);
				}
	}
}

TEST(CompiledMetamagicRules, PolicyFourRemainsDistinctAndPolicyFiveAddsOnlyOverflow)
{
	const auto historical = CapturedPerkRules::compile(metamagicProfile(4));
	const auto current = CapturedPerkRules::compile(metamagicProfile(5));
	const auto inactive = CapturedPerkRules::compile(JsonNode());
	EXPECT_NE(historical->document(), current->document());
	constexpr std::array expected{0, 25, 50, 75};
	const HeroTypeID halon(HeroTypeID::decode("core:halon"));
	for(int rank = -1; rank <= 4; ++rank)
	{
		EXPECT_EQ(historical->metamagic().overflowPercent(rank), 0);
		EXPECT_EQ(inactive->metamagic().overflowPercent(rank), 0);
		EXPECT_EQ(current->metamagic().overflowPercent(rank), expected[std::clamp(rank, 0, 3)]);
		for(const auto hero : {HeroTypeID::NONE, halon})
			EXPECT_EQ(historical->metamagic().removalChance(rank, hero), current->metamagic().removalChance(rank, hero));
	}
	EXPECT_EQ(current->metamagic().discharge(4), historical->metamagic().discharge(4));
	EXPECT_EQ(current->metamagic().restoration(4), historical->metamagic().restoration(4));
	for(size_t key = 0; key < static_cast<size_t>(MetamagicPerk::Count); ++key)
		EXPECT_EQ(current->metamagic().supports(static_cast<MetamagicPerk>(key)),
			historical->metamagic().supports(static_cast<MetamagicPerk>(key)));
}

TEST(CompiledMetamagicRules, BothCompleteProfilesMatchReaderAndDetachFromChangedDefaults)
{
	for(const int version : {4, 5})
	{
		auto source = metamagicProfile(version);
		const auto owner = CapturedPerkRules::compile(source);
		const auto raw = newHorizonsMetamagic::resolutionPolicy(owner->document());
		ASSERT_TRUE(raw);
		source["metamagicPolicy"].Struct().clear();
		for(int rank = -1; rank <= 4; ++rank)
			EXPECT_EQ(owner->metamagic().overflowPercent(rank), raw->overflowPercentByRank[std::clamp(rank, 0, 3)]);
		EXPECT_EQ(owner->metamagic().policy()->chance, raw->chance);
		EXPECT_EQ(owner->metamagic().policy()->damagePerMana, raw->damagePerMana);
		EXPECT_EQ(owner->metamagic().policy()->restorationPerMana, raw->restorationPerMana);
		EXPECT_EQ(owner->metamagic().policy()->halonChanceBonus, raw->halonChanceBonus);
	}
}

TEST(CompiledMetamagicRules, InvalidProfilesRejectBeforeReplacingAnExistingOwner)
{
	std::vector<JsonNode> invalid;
	const auto changed = [&](int version, const std::function<void(JsonNode &)> & mutate)
	{
		auto source = metamagicProfile(version);
		mutate(source["metamagicPolicy"]);
		invalid.push_back(std::move(source));
	};
	for(const int version : {4, 5})
	{
		for(const auto * field : {"version", "removalChancePercent", "damagePerMana", "restorationPerMana", "halonRemovalChanceBonusPercent"})
			changed(version, [&](JsonNode & p) { p.Struct().erase(field); });
		for(const auto * field : {"damagePerMana", "restorationPerMana", "halonRemovalChanceBonusPercent"})
			changed(version, [&](JsonNode & p) { p[field].Integer() += 1; });
		changed(version, [](JsonNode & p) { p["removalChancePercent"].Vector()[0].Integer() = 1; });
		changed(version, [](JsonNode & p) { p["removalChancePercent"].Vector()[1].Integer() = 21; });
		changed(version, [](JsonNode & p) { p["unknown"].Integer() = 1; });
		changed(version, [](JsonNode & p) { p["version"].Integer() = 6; });
	}
	changed(4, [](JsonNode & p) { p["arcaneOverflowPercentByRank"].Vector() = {JsonNode(0), JsonNode(25), JsonNode(50), JsonNode(75)}; });
	changed(5, [](JsonNode & p) { p.Struct().erase("arcaneOverflowPercentByRank"); });
	changed(5, [](JsonNode & p) { p["arcaneOverflowPercentByRank"].Vector().pop_back(); });
	changed(5, [](JsonNode & p) { p["arcaneOverflowPercentByRank"].Vector().push_back(JsonNode(100)); });
	for(size_t rank = 0; rank < 4; ++rank)
	{
		changed(5, [&](JsonNode & p) { p["arcaneOverflowPercentByRank"].Vector()[rank].Integer() += 1; });
		changed(5, [&](JsonNode & p) { p["arcaneOverflowPercentByRank"].Vector()[rank] = JsonNode("25"); });
	}
	changed(5, [](JsonNode & p) { p["arcaneOverflowPercentByRank"].Vector()[1].Integer() = -1; });
	changed(5, [](JsonNode & p) { p["arcaneOverflowPercentByRank"].Vector()[3].Integer() = 101; });
	auto published = CapturedPerkRules::compile(metamagicProfile(4));
	const auto original = published;
	const auto before = original->document();
	for(const auto & source : invalid)
	{
		EXPECT_FALSE(newHorizonsMetamagic::resolutionPolicy(source));
		EXPECT_THROW(published = CapturedPerkRules::compile(source), std::runtime_error);
		EXPECT_EQ(published.get(), original.get());
		EXPECT_EQ(published->document(), before);
		EXPECT_EQ(published->metamagic().overflowPercent(3), 0);
		EXPECT_EQ(published->metamagic().removalChance(3, HeroTypeID::NONE), 40);
	}
}

TEST(CompiledMetamagicRules, NonObjectPolicyNodesRejectWithoutReplacingAnExistingOwner)
{
	JsonNode emptyArray;
	emptyArray.Vector();
	JsonNode populatedArray;
	populatedArray.Vector() = {JsonNode(5)};
	const std::vector<JsonNode> malformed{
		JsonNode(), JsonNode("invalid"), JsonNode(true), JsonNode(5), JsonNode(5.0),
		emptyArray, populatedArray};
	auto published = CapturedPerkRules::compile(metamagicProfile(4));
	const auto original = published;
	const auto before = original->document();
	for(const auto * key : {"metamagicPolicy", "divineMandatePolicy", "demonicGatingPolicy", "bloodragePolicy"})
		for(const auto & value : malformed)
		{
			auto source = metamagicProfile(5);
			source[key] = value;
			EXPECT_THROW(newHorizonsHeroes::validatePassiveFactionPolicies(source), std::runtime_error);
			if(std::string_view(key) == "metamagicPolicy")
				EXPECT_FALSE(newHorizonsMetamagic::resolutionPolicy(source));
			EXPECT_THROW(published = CapturedPerkRules::compile(source), std::runtime_error);
			EXPECT_EQ(published.get(), original.get());
			EXPECT_EQ(published->document(), before);
			EXPECT_EQ(published->metamagic().overflowPercent(3), 0);
			EXPECT_EQ(published->metamagic().removalChance(3, HeroTypeID::NONE), 40);
		}
}

TEST(CompiledMetamagicRules, RetiredPoliciesCannotAcquireOverflowExecution)
{
	for(const int version : {1, 2, 3})
	{
		JsonNode source;
		source["metamagicPolicy"]["version"].Integer() = version;
		EXPECT_FALSE(newHorizonsMetamagic::resolutionPolicy(source));
	}
}

TEST(CompiledMetamagicRules, PerkHandlesBelongToTheirIndependentCapturedOwner)
{
	const auto first = CapturedPerkRules::compile(perks());
	const auto second = CapturedPerkRules::compile(perks());
	const auto & typed = first->metamagic();
	for(size_t key = 0; key < static_cast<size_t>(MetamagicPerk::Count); ++key)
	{
		const auto perk = static_cast<MetamagicPerk>(key);
		ASSERT_TRUE(typed.supports(perk));
		EXPECT_NE(first->catalogue().definition(typed.perk(perk)), nullptr);
		EXPECT_EQ(second->catalogue().definition(typed.perk(perk)), nullptr);
	}
	EXPECT_THROW(typed.perk(MetamagicPerk::Count), std::out_of_range);
}

TEST(CompiledMetamagicRules, ChangedDefaultsAndInactiveCapturesCannotReplaceExistingPolicy)
{
	auto source = perks();
	const auto current = CapturedPerkRules::compile(source);
	source["metamagicPolicy"]["damagePerMana"].Integer() = 999;
	const auto legacy = CapturedPerkRules::compile(JsonNode());
	EXPECT_EQ(current->metamagic().discharge(4), 40);
	EXPECT_EQ(current->metamagic().restoration(4), 60);
	EXPECT_EQ(legacy->metamagic().policy(), nullptr);
	EXPECT_EQ(legacy->metamagic().removalChance(3, HeroTypeID::NONE), 0);
	EXPECT_EQ(legacy->metamagic().recyclingRefund(true, 100, 12, 0, 100), 0);
	EXPECT_FALSE(legacy->metamagic().supports(MetamagicPerk::ManaRecycling));
}

TEST(CompiledMetamagicEnergy, EveryInstalledDefinitionMatchesTheSameCapturedDocument)
{
	const auto source = magic();
	const auto owner = CapturedRuleDocument::compile(CapturedRuleKind::WorldMagic, source);
	const auto table = owner->metamagicEnergy();
	ASSERT_TRUE(table);
	ASSERT_NE(LIBRARY->spellh, nullptr);
	EXPECT_EQ(table->registeredSlotCount(), LIBRARY->spellh->objects.size());
	for(size_t id = 0; id < table->registeredSlotCount(); ++id)
		expectDefinition(*table, owner->document(), SpellID(static_cast<int32_t>(id)));
	EXPECT_EQ(table->definition(SpellID::NONE), nullptr);
	EXPECT_EQ(table->fuelDefinition(SpellID(std::numeric_limits<int32_t>::max())), nullptr);
}

TEST(CompiledMetamagicEnergy, FamilyVariantsUseCapturedOrdinaryListedManaExactlyOnce)
{
	const auto source = magic();
	const auto owner = CapturedRuleDocument::compile(CapturedRuleKind::WorldMagic, source);
	const auto table = owner->metamagicEnergy();
	int admittedVariants = 0;
	for(const auto & [key, row] : source["spells"].Struct())
	{
		if(!row["variant"].isStruct()) continue;
		const SpellID spell(SpellID::decode(key));
		const auto * value = table->fuelDefinition(spell);
		if(!value || value->family == spell) continue;
		++admittedVariants;
		EXPECT_EQ(value->listedMana, source["spells"][value->family.toSpell()->getJsonKey()]["costs"].Vector().front().Integer());
		expectDefinition(*table, source, spell);
	}
	EXPECT_GT(admittedVariants, 0);
}

TEST(CompiledMetamagicEnergy, MissingFreeUnknownAndInternalSourcesCannotManufactureFuel)
{
	auto source = magic();
	source["spells"]["core:haste"].Struct().erase("costs");
	const CompiledMetamagicEnergy missing(source);
	EXPECT_EQ(missing.fuelDefinition(SpellID::HASTE), nullptr);
	ASSERT_NE(missing.identity(SpellID::HASTE), nullptr);
	EXPECT_EQ(missing.identity(SpellID::HASTE)->family, SpellID::HASTE);
	source["spells"]["core:haste"]["costs"].Vector() = {JsonNode(0), JsonNode(0), JsonNode(0), JsonNode(0)};
	const CompiledMetamagicEnergy free(source);
	EXPECT_EQ(free.fuelDefinition(SpellID::HASTE), nullptr);
	ASSERT_NE(free.identity(SpellID::HASTE), nullptr);
	EXPECT_EQ(free.identity(SpellID::HASTE)->family, SpellID::HASTE);
	for(size_t id = 0; id < LIBRARY->spellh->objects.size(); ++id)
		if(const auto & spell = LIBRARY->spellh->objects[id]; spell && !spell->isCommonHeroSpell())
		{
			source["spells"][spell->getJsonKey()]["costs"].Vector() = {JsonNode(100), JsonNode(100), JsonNode(100), JsonNode(100)};
			const CompiledMetamagicEnergy internal(source);
			EXPECT_EQ(internal.fuelDefinition(SpellID(static_cast<int32_t>(id))), nullptr);
			return;
		}
	FAIL() << "Configured native registry must include a creature/internal ability";
}

TEST(CompiledMetamagicEnergy, IndependentWorldOwnersAndRepeatedQueriesCannotRefreshFromChangedDefaults)
{
	auto source = magic();
	const CompiledMetamagicEnergy first(source);
	const auto * original = first.fuelDefinition(SpellID::HASTE);
	ASSERT_NE(original, nullptr);
	const auto before = original->listedMana;
	source["spells"]["core:haste"]["costs"].Vector().front().Integer() = before + 7;
	const CompiledMetamagicEnergy second(source);
	const auto count = first.sourceResolutionCount();
	for(int iteration = 0; iteration < 1000; ++iteration)
	{
		EXPECT_EQ(first.fuelDefinition(SpellID::HASTE)->listedMana, before);
		EXPECT_EQ(second.fuelDefinition(SpellID::HASTE)->listedMana, before + 7);
	}
	EXPECT_EQ(first.sourceResolutionCount(), count);
}

TEST(CompiledMetamagicEnergy, RawCallbackBoundaryAdmitsBeforePublishingAndDetachesItsInput)
{
	auto source = magic();
	const auto compiled = CompiledMetamagicEnergy::compileRawBoundary(source);
	ASSERT_NE(compiled->fuelDefinition(SpellID::HASTE), nullptr);
	const auto before = compiled->fuelDefinition(SpellID::HASTE)->listedMana;
	source["spells"]["core:haste"].Struct().erase("costs");
	EXPECT_EQ(compiled->fuelDefinition(SpellID::HASTE)->listedMana, before);
	source["rulesetVersion"].Integer() = 999;
	EXPECT_THROW(CompiledMetamagicEnergy::compileRawBoundary(source), std::runtime_error);
}

TEST(CompiledMetamagicRules, RepeatedSealedQueriesPerformNoAdmissionOrIdentifierResolution)
{
	using namespace nativeRules::executionAudit;
	if(!available()) GTEST_SKIP() << "Requires native rule work-counter build";
	Counters counters;
	Scope scope(counters);
	const auto owner = CapturedPerkRules::compile(perks());
	const auto world = CapturedRuleDocument::compile(CapturedRuleKind::WorldMagic, magic());
	ASSERT_GT(counters.count(Event::MetamagicAdmission), 0u);
	ASSERT_GT(counters.count(Event::MetamagicEnergyAdmission), 0u);
	const auto sealed = counters;
	const auto & policy = owner->metamagic();
	const auto table = world->metamagicEnergy();
	for(int query = 0; query < 100; ++query)
	{
		EXPECT_EQ(policy.removalChance(3, HeroTypeID::NONE), 40);
		EXPECT_TRUE(policy.supports(MetamagicPerk::ManaRecycling));
		EXPECT_EQ(policy.overflowPercent(3), policy.policy()->overflowPercentByRank[3]);
		EXPECT_EQ(policy.recyclingRefund(true, 20, 12, 0, 100),
			newHorizonsMetamagic::recyclingRefund(20, 12, 0, 100));
		EXPECT_NE(table->fuelDefinition(SpellID::HASTE), nullptr);
	}
	EXPECT_EQ(counters, sealed);
}

TEST(CompiledMetamagicEnergy, CallbackOwnerRequiresExactDocumentIdentityAndLegacyFallbackIsScoped)
{
	::testing::NiceMock<MagicSource> source;
	source.owner = CapturedRuleDocument::compile(CapturedRuleKind::WorldMagic, magic());
	const auto trusted = captureMetamagicEnergy(source);
	EXPECT_EQ(trusted.get(), source.owner->metamagicEnergy().get());
	source.raw = source.owner->document();
	source.exposeExactDocument = false;
	const auto mismatched = captureMetamagicEnergy(source);
	EXPECT_NE(mismatched.get(), trusted.get());
	ASSERT_NE(mismatched->fuelDefinition(SpellID::HASTE), nullptr);
	EXPECT_EQ(mismatched->fuelDefinition(SpellID::HASTE)->listedMana, trusted->fuelDefinition(SpellID::HASTE)->listedMana);
	source.owner.reset();
	const auto detached = captureMetamagicEnergy(source);
	EXPECT_NE(detached.get(), mismatched.get());
	source.raw["rulesetVersion"].Integer() = 999;
	EXPECT_THROW(captureMetamagicEnergy(source), std::runtime_error);
	EXPECT_NE(detached->fuelDefinition(SpellID::HASTE), nullptr);
}

TEST(CompiledMetamagicEnergy, TypedMemoryPreservesPolarityForFreeNonFuelAndFamilyVariants)
{
	auto rules = magic();
	// Free enchantments remain known identities and cannot manufacture fuel.
	rules["spells"]["core:haste"]["costs"].Vector() = {JsonNode(0), JsonNode(0), JsonNode(0), JsonNode(0)};
	for(auto & [key, row] : rules["spells"].Struct())
		if(const auto & variant = std::as_const(row)["variant"]; variant.isStruct()
			&& variant["base"].isString() && variant["base"].String() == "core:haste")
			row["costs"].Vector() = {JsonNode(0), JsonNode(0), JsonNode(0), JsonNode(0)};
	::testing::NiceMock<MagicSource> source;
	source.owner = CapturedRuleDocument::compile(CapturedRuleKind::WorldMagic, rules);
	const auto table = source.owner->metamagicEnergy();
	ASSERT_TRUE(table->memorySkill());
	EXPECT_EQ(table->fuelDefinition(SpellID::HASTE), nullptr);
	CStackBasicDescriptor descriptor{CreatureID(0), 1};
	CStack recipient{&descriptor, PlayerColor(1), 7, BattleSide::DEFENDER};
	recipient.addNewBonus(std::make_shared<Bonus>(newHorizonsMetamagic::magicalMemoryMarker(
		SpellID::HASTE, newHorizonsMetamagic::ResolutionKind::OFFENSE)));
	EXPECT_TRUE(newHorizonsMetamagic::blocksEnchantment(recipient, source, SpellID::HASTE, true, false));
	EXPECT_FALSE(newHorizonsMetamagic::blocksEnchantment(recipient, source, SpellID::HASTE, false, true));
	EXPECT_FALSE(newHorizonsMetamagic::blocksEnchantment(recipient, source, SpellID::NONE, true, false));
	const SpellID massBless(SpellID::decode("new-horizons:massBless"));
	recipient.addNewBonus(std::make_shared<Bonus>(newHorizonsMetamagic::magicalMemoryMarker(
		SpellID::BLESS, newHorizonsMetamagic::ResolutionKind::OFFENSE)));
	ASSERT_NE(table->identity(massBless), nullptr);
	EXPECT_EQ(table->identity(massBless)->family, SpellID::BLESS);
	EXPECT_TRUE(newHorizonsMetamagic::blocksEnchantment(recipient, source, massBless, true, false));
	EXPECT_FALSE(newHorizonsMetamagic::blocksEnchantment(recipient, source, massBless, false, true));
}

TEST(CompiledMetamagicRules, PolicySixCapturesIndependentCoreProcessesAndOptionalRemoval)
{
	auto source = perks();
	const auto owner = CapturedPerkRules::compile(source);
	const auto & rules = owner->metamagic();
	ASSERT_NE(rules.policy(), nullptr);
	ASSERT_TRUE(rules.policy()->optionalRemoval);
	for(int rank = 1; rank <= 3; ++rank)
	{
		const int percent = 15 + 15 * rank;
		EXPECT_EQ(rules.overflowPercent(rank), percent);
		EXPECT_EQ(rules.friendlyReductionPercent(rank), percent);
		EXPECT_EQ(rules.debuffRetryPercent(rank), percent);
	}
	for(int rank : {-1, 0, 4})
	{
		EXPECT_EQ(rules.friendlyReductionPercent(rank), 0);
		EXPECT_EQ(rules.debuffRetryPercent(rank), 0);
	}
	source["metamagicPolicy"]["debuffRetryChancePercentByRank"].Vector()[3].Integer() = 61;
	EXPECT_FALSE(newHorizonsMetamagic::resolutionPolicy(source));
	EXPECT_THROW(CapturedPerkRules::compile(source), std::exception);
	EXPECT_EQ(rules.debuffRetryPercent(3), 60); // Captured owner never follows changed defaults.
}
TEST(CompiledMetamagicRules, ColdSpellClassificationSeparatesDamageDebuffsAndDisplacement)
{
	const auto owner = CapturedRuleDocument::compile(CapturedRuleKind::WorldMagic, magic());
	const auto energy = owner->metamagicEnergy();
	ASSERT_NE(energy, nullptr);
	const auto * arrow = energy->identity(SpellID::MAGIC_ARROW);
	const auto * slow = energy->identity(SpellID::SLOW);
	const auto * recall = energy->identity(SpellID(SpellID::decode("new-horizons:forcedRecall")));
	ASSERT_NE(arrow, nullptr); ASSERT_NE(slow, nullptr); ASSERT_NE(recall, nullptr);
	EXPECT_TRUE(arrow->singleTargetByLevel[0]);
	EXPECT_FALSE(arrow->debuffByLevel[0]);
	EXPECT_TRUE(slow->singleTargetByLevel[0]);
	EXPECT_TRUE(slow->debuffByLevel[0]);
	EXPECT_TRUE(recall->singleTargetByLevel[0]);
	EXPECT_FALSE(recall->debuffByLevel[0]);
	const auto count = energy->sourceResolutionCount();
	for(int query = 0; query < 100; ++query) EXPECT_EQ(energy->identity(SpellID::SLOW), slow);
	EXPECT_EQ(energy->sourceResolutionCount(), count);
}
