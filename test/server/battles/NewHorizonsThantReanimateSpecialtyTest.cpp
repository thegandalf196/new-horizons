/*
 * NewHorizonsThantReanimateSpecialtyTest.cpp, part of VCMI engine
 * License: GNU General Public License v2 or later; see license.txt
 */
#include "StdInc.h"
#include "../../NewHorizonsHistoricalAdventurePolicyTestUtils.h"
#include "HeroCommandFixture.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/entities/hero/CHero.h"
#include "../../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForLobby.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/effects/Effect.h"
#include "../../../lib/StartInfo.h"

namespace
{
SpellID reanimate()
{
	return SpellID(SpellID::decode(std::string(newHorizonsMagic::SHADOW_REANIMATE_SPELL)));
}

class NewHorizonsThantReanimateSpecialtyTest : public HeroCommandFixture
{
protected:
	bool optIn = true;
	bool legacyMagic = false;
	bool presetBook = false;
	CStack * target = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons content";
	}
	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			legacyMagic ? JsonNode() : JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		rules["startingSkills"].Struct().erase("startingDevelopmentProfiles");
		rules["nonDamageSpellSpecialties"].Struct().erase("remainingStartReplacements");
		rules["damageSpellSpecialties"].Struct().erase("coroniusHolyWrathReplacement");
		rules["startingSkills"].Struct().erase("startingBookReplacements");
		rules.Struct().erase("remainingSpellSpecialtyReplacements");
		rules["skillSpecialties"].Struct().erase("navigationStartReplacements");
		rules.Struct().erase("defaultCreatureLineReplacements");
		std::erase_if(rules["nonDamageSpellSpecialties"]["spells"].Vector(),
			[](const JsonNode & value) { return value.String() == "new-horizons:phantomArmy"; });
		rules.Struct().erase("lighthouseDeparture");
		rules["nonDamageSpellSpecialties"].Struct().erase("aenainFrailtyReplacement");
		rules["nonDamageSpellSpecialties"].Struct().erase("defensiveStartReplacements");
		rules["nonDamageSpellSpecialties"].Struct().erase("offensiveStartReplacements");
		std::erase_if(rules["nonDamageSpellSpecialties"]["spells"].Vector(), [](const JsonNode & spell)
		{
			return spell.String() == "new-horizons:hydrasVitality" || spell.String() == "new-horizons:guardianSpirit"
				|| spell.String() == "new-horizons:crusade" || spell.String() == "new-horizons:focusMagic";
		});
		// This fixture exercises the preceding Thant boundary, not later replacements.
		std::erase_if(rules["nonDamageSpellSpecialties"]["spells"].Vector(),
			[](const JsonNode & spell) { return spell.String() == "new-horizons:frailty"; });
		rules.setOverrideFlag(true);
		if(!optIn)
		{
			std::erase_if(rules["nonDamageSpellSpecialties"]["spells"].Vector(),
				[](const JsonNode & spell) { return spell.String() == newHorizonsMagic::SHADOW_REANIMATE_SPELL; });
			rules.setOverrideFlag(true);
		}
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		isolateHistoricalAdventurePolicies(*loaded);
	}
	void prepare(const std::string & actor = "core:thant", int rawSpellPower = 67)
	{
		const CreatureID pikeman(CreatureID::decode("core:pikeman"));
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(actor)), PlayerColor(0))
			.heroGarrison({{pikeman, 100}});
		if(presetBook)
			builder.heroSpells({});
		builder.hero({7, 7, 0}, HeroTypeID(HeroTypeID::decode("core:aislinn")), PlayerColor(1))
			.heroGarrison({{pikeman, 1}});
		startWithMap(std::move(builder));
		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		gameHandler->randomizer->setSeed(seed);
		attackerSideHero = findHeroAt({5, 5, 0});
		defenderSideHero = findHeroAt({7, 7, 0});
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_NE(defenderSideHero, nullptr);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, rawSpellPower, ChangeValueMode::ABSOLUTE);
		for(const auto skill : {newHorizonsMagic::SHADOW_MAGIC_SKILL, newHorizonsMagic::SPELLCRAFT_SKILL})
			attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(std::string(skill))),
				MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);
	}
	void combat()
	{
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		target = addStack(BattleSide::ATTACKER, CreatureID(CreatureID::decode("core:pikeman")), BattleHex(3, 5), 100);
		addStack(BattleSide::DEFENDER, CreatureID(CreatureID::decode("core:pikeman")), BattleHex(12, 5), 1);
		beginCombat();
		ASSERT_NE(target, nullptr);
		ASSERT_EQ(battle()->battleGetOwner(battle()->battleActiveUnit()), PlayerColor(0));
	}
	void injure(int64_t damage)
	{
		auto state = target->acquireState();
		state->damage(damage);
		UnitChanges change(target->unitId(), UnitChanges::EOperation::UPDATE);
		change.data = state->save();
		change.healthDelta = -damage;
		BattleUnitsChanged changed;
		changed.battleID = BattleID(0);
		changed.changedStacks.push_back(std::move(change));
		gameHandler->sendAndApply(changed);
	}
	void expectPaidRestore(int64_t expectedPool, std::optional<int64_t> effectValueOverride = std::nullopt)
	{
		ASSERT_NO_FATAL_FAILURE(combat());
		ASSERT_NO_FATAL_FAILURE(injure(800));
		const auto before = target->getAvailableHealth();
		const auto countBefore = target->getCount();
		const auto mana = attackerSideHero->getManaAvailable();
		const auto * spell = reanimate().toSpell();
		spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
		if(effectValueOverride)
			cast.setEffectValue(*effectValueOverride);
		const auto mechanics = spell->battleMechanics(&cast);
		EXPECT_EQ(mechanics->getEffectValue(), expectedPool);
		spells::Target aim;
		aim.emplace_back(target);
		ASSERT_TRUE(mechanics->canBeCastAt(aim));
		const auto spellTarget = mechanics->canonicalizeTarget(aim);
		spells::effects::SpellEffectValue forecast;
		mechanics->forEachEffect([&](const spells::effects::Effect & effect)
		{
			forecast += effect.getHealthChange(mechanics.get(),
				effect.transformTarget(mechanics.get(), aim, spellTarget));
			return false;
		});
		EXPECT_EQ(forecast.hpDelta, expectedPool);
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = reanimate();
		action.aimToUnit(target);
		if(effectValueOverride)
		{
			cast.cast(gameHandler->spellcastEnvironment(), aim);
		}
		else
		{
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
		}
		EXPECT_EQ(target->getAvailableHealth() - before, expectedPool);
		EXPECT_EQ(target->health.getResurrected(), target->getCount() - countBefore);
		EXPECT_EQ(mana - attackerSideHero->getManaAvailable(), 16);
		EXPECT_LE(target->getCount(), 100);
	}
};

TEST_F(NewHorizonsThantReanimateSpecialtyTest, AuthoredDefaultStartPaysForSpecializedTemporaryRestoration)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(reanimate()));
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(SpellID::ANIMATE_DEAD));
	EXPECT_TRUE(attackerSideHero->getHeroType()->spells.contains(SpellID::ANIMATE_DEAD));
	EXPECT_FALSE(attackerSideHero->getHeroType()->spells.contains(reanimate()));
	EXPECT_EQ(attackerSideHero->getNonDamageSpellSpecialtyBonusPercent(reanimate()), 20);
	EXPECT_EQ(attackerSideHero->getNonDamageSpellSpecialtyBonusPercent(SpellID::ANIMATE_DEAD), 0);
	EXPECT_EQ(attackerSideHero->getPerkSkillRank(std::string(newHorizonsMagic::SHADOW_MAGIC_SKILL)), 0);
	EXPECT_TRUE(attackerSideHero->canCastThisSpell(reanimate().toSpell()));
	ASSERT_NO_FATAL_FAILURE(expectPaidRestore(622));
}

TEST_F(NewHorizonsThantReanimateSpecialtyTest, OrdinaryCasterPaidResultRemainsExactlyTheOriginalFormula)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:aislinn"));
	attackerSideHero->addSpellToSpellbook(reanimate());
	EXPECT_EQ(attackerSideHero->getNonDamageSpellSpecialtyBonusPercent(reanimate()), 0);
	ASSERT_NO_FATAL_FAILURE(expectPaidRestore(555));
}

TEST_F(NewHorizonsThantReanimateSpecialtyTest, OrdinaryCasterIgnoresExplicitZeroEffectValueOverride)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:aislinn"));
	attackerSideHero->addSpellToSpellbook(reanimate());
	ASSERT_NO_FATAL_FAILURE(expectPaidRestore(555, 0));
}

TEST_F(NewHorizonsThantReanimateSpecialtyTest, OrdinaryCasterIgnoresExplicitNonzeroEffectValueOverride)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:aislinn"));
	attackerSideHero->addSpellToSpellbook(reanimate());
	ASSERT_NO_FATAL_FAILURE(expectPaidRestore(555, 1));
}

TEST_F(NewHorizonsThantReanimateSpecialtyTest, SchoolFloorFixedBaseAndReanimatorCompositionRemainSeparate)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto & rules = attackerSideHero->getMagicRules();
	for(int rank = 0; rank <= MasteryLevel::EXPERT; ++rank)
	{
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(
			std::string(newHorizonsMagic::SHADOW_MAGIC_SKILL))), rank, ChangeValueMode::ABSOLUTE);
		const int64_t coefficient = newHorizonsMagic::spellPowerCoefficientBasisPoints(rules, attackerSideHero, reanimate());
		EXPECT_EQ(newHorizonsMagic::reanimateBaseHealingPool(rules, attackerSideHero, reanimate(), 67),
			220 + 5LL * 67 * coefficient * 120 / 1'000'000);
	}
	EXPECT_EQ(newHorizonsMagic::reanimateBaseHealingPool(rules, attackerSideHero, reanimate(), 0), 220);
	const std::string skill(newHorizonsMagic::SHADOW_MAGIC_SKILL);
	attackerSideHero->applyPerkSelection({skill, "new-horizons:shadowMagic.malediction"});
	attackerSideHero->applyPerkSelection({skill, std::string(newHorizonsMagic::SHADOW_NIGHT_FEEDER_PERK)});
	attackerSideHero->applyPerkSelection({skill, std::string(newHorizonsMagic::SHADOW_REANIMATOR_PERK)});
	ASSERT_TRUE(newHorizonsMagic::hasReanimatorPerk(attackerSideHero));
	const auto base = newHorizonsMagic::reanimateBaseHealingPool(rules, attackerSideHero, reanimate(), 67);
	ASSERT_TRUE(base);
	EXPECT_EQ(newHorizonsMagic::reanimateHealingPool(rules, attackerSideHero, reanimate(), 67, 5),
		*base + (*base - 5) / 4);
}

TEST_F(NewHorizonsThantReanimateSpecialtyTest, MapPresetSpellbookIsNotReplacedByAuthoredDefault)
{
	presetBook = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(reanimate()));
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(SpellID::ANIMATE_DEAD));
	EXPECT_EQ(attackerSideHero->getNonDamageSpellSpecialtyBonusPercent(reanimate()), 20);
}

TEST_F(NewHorizonsThantReanimateSpecialtyTest, LegacyOptOutRetainsOriginalInscriptionAndExactProducer)
{
	optIn = false;
	legacyMagic = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(SpellID::ANIMATE_DEAD));
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(reanimate()));
	EXPECT_EQ(attackerSideHero->getNonDamageSpellSpecialtyBonusPercent(reanimate()), 0);
	const auto & producers = attackerSideHero->getHeroType()->nonDamageSpellSpecialtyProducers;
	ASSERT_EQ(producers.size(), 1u);
	const auto & prototype = producers.front().bonus;
	EXPECT_EQ(producers.front().spell, SpellID::ANIMATE_DEAD);
	EXPECT_TRUE(std::ranges::any_of(attackerSideHero->getExportedBonusList(), [&prototype](const auto & bonus)
	{
		return bonus->type == prototype->type && bonus->subtype == prototype->subtype
			&& bonus->source == prototype->source && bonus->sid == prototype->sid
			&& bonus->val == prototype->val && bonus->stacking == prototype->stacking;
	}));
}

TEST_F(NewHorizonsThantReanimateSpecialtyTest, CurrentWorldRoundtripPreservesStartMarkerAndTemporaryCasualties)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(expectPaidRestore(622));
	const auto heroID = attackerSideHero->id;
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	const auto * loaded = restored.getHero(heroID);
	ASSERT_NE(loaded, nullptr);
	EXPECT_TRUE(loaded->spellbookContainsSpell(reanimate()));
	EXPECT_EQ(loaded->getNonDamageSpellSpecialtyBonusPercent(reanimate()), 20);
	EXPECT_EQ(newHorizonsMagic::reanimateBaseHealingPool(loaded->getMagicRules(), loaded, reanimate(), 67), 622);
	EXPECT_NE(loaded->getSpecialtyDescriptionTranslated().find("healing component"), std::string::npos);
	const auto saved = target->save();
	auto restoredUnit = target->acquireState();
	restoredUnit->load(saved);
	EXPECT_EQ(restoredUnit->health.getResurrected(), target->health.getResurrected());
	restoredUnit->health.takeResurrected();
	EXPECT_EQ(restoredUnit->health.getResurrected(), 0);
}

TEST_F(NewHorizonsThantReanimateSpecialtyTest, PreviousHasteFormatRejectsRulePayloadBeforeOuterPrefixes)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto reject = [](auto & value)
	{
		CMemorySerializer memory;
		memory.oser.version = ESerializationVersion::NEW_HORIZONS_HASTE_SPECIALTIES;
		EXPECT_THROW(value.serialize(memory.oser), std::runtime_error);
		EXPECT_TRUE(memory.extractBuffer().empty());
	};
	reject(*attackerSideHero);
	reject(gameState()->getMap());
	reject(*gameState());
	LobbyStartGame lobby;
	lobby.initializedStartInfo = std::make_shared<StartInfo>(*gameState()->getStartInfo());
	lobby.initializedGameState = gameState();
	reject(lobby);
	JsonNode raw;
	raw["heroes"]["newHorizons"] = JsonNode(JsonPath::builtin("config/newHorizonsHeroes"));
	raw["heroes"]["newHorizons"]["startingSkills"].Struct().erase("startingDevelopmentProfiles");
	raw["heroes"]["newHorizons"]["nonDamageSpellSpecialties"].Struct().erase("remainingStartReplacements");
	raw["heroes"]["newHorizons"]["damageSpellSpecialties"].Struct().erase("coroniusHolyWrathReplacement");
	raw["heroes"]["newHorizons"]["startingSkills"].Struct().erase("startingBookReplacements");
	raw["heroes"]["newHorizons"].Struct().erase("remainingSpellSpecialtyReplacements");
	raw["heroes"]["newHorizons"]["skillSpecialties"].Struct().erase("navigationStartReplacements");
	raw["heroes"]["newHorizons"].Struct().erase("defaultCreatureLineReplacements");
	std::erase_if(raw["heroes"]["newHorizons"]["nonDamageSpellSpecialties"]["spells"].Vector(),
		[](const JsonNode & value) { return value.String() == "new-horizons:phantomArmy"; });
	raw["heroes"]["newHorizons"].Struct().erase("lighthouseDeparture");
	raw["heroes"]["newHorizons"]["nonDamageSpellSpecialties"].Struct().erase("aenainFrailtyReplacement");
	raw["heroes"]["newHorizons"]["nonDamageSpellSpecialties"].Struct().erase("defensiveStartReplacements");
	raw["heroes"]["newHorizons"]["nonDamageSpellSpecialties"].Struct().erase("offensiveStartReplacements");
	std::erase_if(raw["heroes"]["newHorizons"]["nonDamageSpellSpecialties"]["spells"].Vector(), [](const JsonNode & spell)
	{
		return spell.String() == "new-horizons:hydrasVitality" || spell.String() == "new-horizons:guardianSpirit"
				|| spell.String() == "new-horizons:crusade" || spell.String() == "new-horizons:focusMagic";
	});
	std::erase_if(raw["heroes"]["newHorizons"]["nonDamageSpellSpecialties"]["spells"].Vector(),
		[](const JsonNode & spell) { return spell.String() == "new-horizons:frailty"; });
	CMemorySerializer incoming;
	incoming.oser & raw;
	incoming.iser.version = ESerializationVersion::NEW_HORIZONS_HASTE_SPECIALTIES;
	GameSettings decoded;
	EXPECT_THROW(decoded.serialize(incoming.iser), std::runtime_error);
}

TEST_F(NewHorizonsThantReanimateSpecialtyTest, PreviousHasteFormatStillWritesAndReadsOptOutRules)
{
	optIn = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(SpellID::ANIMATE_DEAD));
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(reanimate()));
	const auto & producers = attackerSideHero->getHeroType()->nonDamageSpellSpecialtyProducers;
	ASSERT_EQ(producers.size(), 1u);
	const auto & prototype = producers.front().bonus;
	EXPECT_TRUE(std::ranges::any_of(attackerSideHero->getExportedBonusList(), [&prototype](const auto & bonus)
	{
		return bonus->type == prototype->type && bonus->subtype == prototype->subtype
			&& bonus->source == prototype->source && bonus->sid == prototype->sid
			&& bonus->val == prototype->val && bonus->stacking == prototype->stacking;
	}));
	CMemorySerializer heroBytes;
	heroBytes.oser.version = ESerializationVersion::NEW_HORIZONS_HASTE_SPECIALTIES;
	EXPECT_NO_THROW(attackerSideHero->serialize(heroBytes.oser));
	EXPECT_FALSE(heroBytes.extractBuffer().empty());
	JsonNode raw;
	raw["heroes"]["newHorizons"] = JsonNode(JsonPath::builtin("config/newHorizonsHeroes"));
	raw["heroes"]["newHorizons"]["startingSkills"].Struct().erase("startingDevelopmentProfiles");
	raw["heroes"]["newHorizons"]["nonDamageSpellSpecialties"].Struct().erase("remainingStartReplacements");
	raw["heroes"]["newHorizons"]["damageSpellSpecialties"].Struct().erase("coroniusHolyWrathReplacement");
	raw["heroes"]["newHorizons"]["startingSkills"].Struct().erase("startingBookReplacements");
	raw["heroes"]["newHorizons"].Struct().erase("remainingSpellSpecialtyReplacements");
	raw["heroes"]["newHorizons"]["skillSpecialties"].Struct().erase("navigationStartReplacements");
	raw["heroes"]["newHorizons"].Struct().erase("defaultCreatureLineReplacements");
	std::erase_if(raw["heroes"]["newHorizons"]["nonDamageSpellSpecialties"]["spells"].Vector(),
		[](const JsonNode & value) { return value.String() == "new-horizons:phantomArmy"; });
	raw["heroes"]["newHorizons"].Struct().erase("lighthouseDeparture");
	raw["heroes"]["newHorizons"]["nonDamageSpellSpecialties"].Struct().erase("aenainFrailtyReplacement");
	raw["heroes"]["newHorizons"]["nonDamageSpellSpecialties"].Struct().erase("defensiveStartReplacements");
	raw["heroes"]["newHorizons"]["nonDamageSpellSpecialties"].Struct().erase("offensiveStartReplacements");
	std::erase_if(raw["heroes"]["newHorizons"]["nonDamageSpellSpecialties"]["spells"].Vector(), [](const JsonNode & spell)
	{
		return spell.String() == "new-horizons:hydrasVitality" || spell.String() == "new-horizons:guardianSpirit"
				|| spell.String() == "new-horizons:crusade" || spell.String() == "new-horizons:focusMagic";
	});
	std::erase_if(raw["heroes"]["newHorizons"]["nonDamageSpellSpecialties"]["spells"].Vector(),
		[](const JsonNode & spell) { return spell.String() == "new-horizons:frailty"; });
	std::erase_if(raw["heroes"]["newHorizons"]["nonDamageSpellSpecialties"]["spells"].Vector(),
		[](const JsonNode & spell) { return spell.String() == newHorizonsMagic::SHADOW_REANIMATE_SPELL; });
	CMemorySerializer incoming;
	incoming.oser & raw;
	incoming.iser.version = ESerializationVersion::NEW_HORIZONS_HASTE_SPECIALTIES;
	GameSettings decoded;
	EXPECT_NO_THROW(decoded.serialize(incoming.iser));
	EXPECT_NO_THROW(decoded.validateNewHorizonsThantReanimateSerialization(false));
	CMemorySerializer outgoing;
	outgoing.oser.version = ESerializationVersion::NEW_HORIZONS_HASTE_SPECIALTIES;
	outgoing.iser.version = ESerializationVersion::NEW_HORIZONS_HASTE_SPECIALTIES;
	ASSERT_NO_THROW(decoded.serialize(outgoing.oser));
	JsonNode emitted;
	ASSERT_NO_THROW(outgoing.iser & emitted);
	EXPECT_FALSE(newHorizonsHeroes::hasReanimateSpecialtyRules(
		emitted["heroes"]["newHorizons"]));
	EXPECT_EQ(emitted, raw);
}
}
