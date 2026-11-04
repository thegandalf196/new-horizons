/*
 * SpellbookEntryPresentationTest.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../../client/windows/SpellbookEntryPresentation.h"
#include "../../lib/spells/SpellCostBreakdown.h"
#include "../../lib/spells/SpellEffectPreview.h"
#include "../../lib/texts/ITranslator.h"
#include <map>

namespace
{
class BookTranslator final : public ITranslator
{
public:
	std::map<std::string, std::string> texts{
		{"core.genrltxt.387", "Spell Points"},
		{"new-horizons.spellbook.costShort", "SP: %d"},
		{"unit", "%s"}, {"percent", "%s%"},
		{"damage", "Damage %s"}, {"damageShort", "%s dmg"},
		{"regen", "Heal %s per turn"}, {"regenShort", "%s/turn"},
		{"new-horizons.spellbook.currentEffect", "Current effect: %s"},
		{"beforeTarget", "Before target defenses; no random outcome."}};
	const std::string & translateString(const TextIdentifier & id) const override
	{
		return texts.at(id.get());
	}
};

int textWidth(const std::string & text) { return static_cast<int>(text.size() * 5); }

newHorizonsMagic::SpellEffectPreview damage(int64_t value)
{
	return {"damage", "damageShort", newHorizonsMagic::SpellEffectPreview::Quantity{value, 1, "unit"}, {}, "beforeTarget"};
}
}

TEST(SpellbookEntryPresentationTest, FinalCostIsVisibleWithoutReplacingItWithAvailabilityOrListedCost)
{
	BookTranslator translator;
	SpellCostBreakdown cost{12, 7, {{SpellCostStage::Kind::WISDOM, 12, 7}}};
	EXPECT_EQ(spellbookPresentation::costLine(cost, translator, textWidth), "Spell Points: 7");
	translator.texts["core.genrltxt.387"] = "Localized long Spell Points label";
	EXPECT_EQ(spellbookPresentation::costLine(cost, translator, textWidth), "SP: 7");
	EXPECT_EQ(cost.listedCost, 12);
	EXPECT_EQ(cost.finalCost, 7);
}

TEST(SpellbookEntryPresentationTest, CompactAndDetailedViewsKeepTheSameResolvedValueAndQualification)
{
	BookTranslator translator;
	const auto preview = damage(1234);
	EXPECT_EQ(spellbookPresentation::effectLine(preview, translator, textWidth), "Damage 1234");
	EXPECT_EQ(spellbookPresentation::effectLine(preview, translator, textWidth, 45), "1234 dmg");
	EXPECT_EQ(spellbookPresentation::effectDetail(preview, translator),
		"Current effect: Damage 1234\nBefore target defenses; no random outcome.");
	EXPECT_EQ(preview, damage(1234));
	EXPECT_NE(spellbookPresentation::effectDetail(preview, translator),
		spellbookPresentation::effectDetail(damage(2345), translator));
}

TEST(SpellbookEntryPresentationTest, FixedPointValuesSurviveLocalizationAndWidthFallback)
{
	BookTranslator translator;
	newHorizonsMagic::SpellEffectPreview preview{
		"regen", "regenShort", newHorizonsMagic::SpellEffectPreview::Quantity{1875, 100, "percent"}, {}, {}};
	EXPECT_EQ(spellbookPresentation::effectLine(preview, translator, textWidth), "18.75%/turn");
	EXPECT_EQ(spellbookPresentation::effectDetail(preview, translator), "Current effect: Heal 18.75% per turn");
	translator.texts["regen"] = "Localized long regeneration phrase %s";
	EXPECT_EQ(spellbookPresentation::effectLine(preview, translator, textWidth), "18.75%/turn");
	preview.primary->value = -25;
	EXPECT_EQ(spellbookPresentation::effectLine(preview, translator, textWidth), "-0.25%/turn");
	preview.primary->scale = 0;
	EXPECT_THROW(spellbookPresentation::effectLine(preview, translator, textWidth), std::invalid_argument);
}

TEST(SpellbookEntryPresentationTest, RecipientSummaryKeepsExactQuantitiesWithoutWidthRounding)
{
	BookTranslator translator;
	EXPECT_EQ(spellbookPresentation::effectSummary(damage(123456789), translator), "123456789 dmg");
	newHorizonsMagic::SpellEffectPreview preview{
		"regen", "regenShort", newHorizonsMagic::SpellEffectPreview::Quantity{-1875, 100, "percent"}, {}, {}};
	EXPECT_EQ(spellbookPresentation::effectSummary(preview, translator), "-18.75%/turn");
	preview.compactLabelKey.clear();
	EXPECT_EQ(spellbookPresentation::effectSummary(preview, translator), "Heal -18.75% per turn");
	EXPECT_EQ(spellbookPresentation::effectSummary({}, translator), "");
}

TEST(SpellbookEntryPresentationTest, ArmyGroupsKeepDifferentMagnitudesAndTimersWithoutDroppingRecipients)
{
	BookTranslator translator;
	spellbookPresentation::GaleArmyPreview army{12, true, {}, {}};
	const auto row = [](uint32_t id, const char * name, int hex, int64_t value, int rounds)
	{
		newHorizonsMagic::RecipientSpellEffectPreview effect{damage(value), rounds};
		return spellbookPresentation::ArmySpellRecipient{id, name, 10, static_cast<int16_t>(hex), effect};
	};
	army.recipients = {row(1, "Pikemen", 54, 5000, 2), row(2, "Archers", 88, 8000, 3),
		row(3, "Pikemen", 122, 5000, 2), row(4, "Swordsmen", 124, 5000, 3)};
	const auto before = army.recipients;
	const auto text = spellbookPresentation::armyEffectDetail(army, translator);
	EXPECT_EQ(text, "Current army:\n5000 dmg | 2 rounds: 10 Pikemen (D4), 10 Pikemen (D8)"
		"\n8000 dmg | 3 rounds: 10 Archers (D6)\n5000 dmg | 3 rounds: 10 Swordsmen (F8)");
	EXPECT_EQ(spellbookPresentation::armyEffectDetail(army, translator), text);
	for(size_t index = 0; index < army.recipients.size(); ++index)
	{
		EXPECT_EQ(army.recipients[index].unitId, before[index].unitId);
		EXPECT_EQ(army.recipients[index].effect.effect, before[index].effect.effect);
		EXPECT_EQ(army.recipients[index].effect.durationRounds, before[index].effect.durationRounds);
	}
	army.canCast = false;
	army.problems = {"Not enough Mana."};
	EXPECT_EQ(spellbookPresentation::armyEffectDetail(army, translator),
		text + "\n\nCannot cast now.\nNot enough Mana.");
}
