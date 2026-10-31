/* SpellbookNativeFontPresentationTest.cpp, part of VCMI / New Horizons; GPL v2.0 or later. */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../lib/modding/CModHandler.h"
#include "../../client/windows/SpellbookEntryPresentation.h"
#include "../../lib/filesystem/Filesystem.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/SpellEffectPreview.h"
#include "../../lib/texts/ITranslator.h"
#include "../../lib/vcmi_endian.h"
namespace
{
class NativeBookTranslator final : public ITranslator
{
	JsonNode texts{JsonPath::builtin("config/newHorizonsCombatTexts")};
public:
	const std::string & translateString(const TextIdentifier & id) const override { return texts[id.get()].String(); }
};
class SpellbookNativeFontPresentationTest : public HeroCommandFixture
{
protected:
	JsonNode magicRules{JsonPath::builtin("config/newHorizonsMagic")};
	CGHeroInstance * hero = nullptr;
	NativeBookTranslator translator;
	std::pair<std::unique_ptr<ui8[]>,si64> font;
	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magicRules);
	}
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires curated New Horizons content";
		useRevisedOrders = true;
		ASSERT_NO_FATAL_FAILURE(startGame());
		hero = attackerSideHero;
		ASSERT_TRUE(hero->usesPrimaryGrowth());
		hero->setPrimarySkill(PrimarySkill::SPELL_POWER, 1, ChangeValueMode::ABSOLUTE);
		font = CResourceHandler::get()->load(ResourcePath("DATA/TINY",EResType::BMP_FONT))->readAll();
		ASSERT_GE(font.second,4128);
		ASSERT_EQ(font.first[5],11);
	}
	int width(const std::string & text) const
	{
		int result = 0;
		for(const unsigned char character : text)
		{
			const auto * entry = font.first.get() + 32 + character * 12;
			result += static_cast<int32_t>(read_le_u32(entry)) + static_cast<int32_t>(read_le_u32(entry+4))
				+ static_cast<int32_t>(read_le_u32(entry+8));
		}
		return result;
	}
	std::string line(const newHorizonsMagic::SpellEffectPreview & preview) const
	{
		return spellbookPresentation::effectLine(preview,translator,[this](const std::string & text){ return width(text); });
	}
};
}
TEST_F(SpellbookNativeFontPresentationTest, ActualCanonicalFormatterFitsEveryCurrentRowAtFourPowerProfiles)
{
	int covered = 0;
	for(const auto & [key,rules] : magicRules["spells"].Struct())
	{
		if((rules["active"].isBool() && !rules["active"].Bool()) || (rules["heroAccess"].isBool() && !rules["heroAccess"].Bool()))
			continue;
		const auto * spell = SpellID(SpellID::decode(key)).toSpell();
		if(!spell->isCombat())
			continue;
		for(const int power : {0,1,100,500})
		{
			hero->setPrimarySkill(PrimarySkill::SPELL_POWER,power,ChangeValueMode::ABSOLUTE);
			ASSERT_EQ(hero->getEffectPower(spell), power);
			const auto preview = newHorizonsMagic::spellEffectPreview(*hero,*spell);
			const auto text = line(preview);
			ASSERT_FALSE(text.empty()) << key;
			EXPECT_NE(text,"!") << key;
			EXPECT_LE(width(text),78) << key << ": " << text;
			EXPECT_EQ(text.find("%%"),std::string::npos) << key;
			EXPECT_EQ(preview,newHorizonsMagic::spellEffectPreview(*hero,*spell));
		}
		++covered;
	}
	EXPECT_EQ(covered,75);
}
TEST_F(SpellbookNativeFontPresentationTest, ExpertTransfigurePreservesBothOrdinaryFootprintValues)
{
	hero->setHeroType(HeroTypeID(HeroTypeID::decode("core:iona")));
	hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:sorceryMagic")),MasteryLevel::EXPERT,ChangeValueMode::ABSOLUTE);
	hero->setPrimarySkill(PrimarySkill::SPELL_POWER,500,ChangeValueMode::ABSOLUTE);
	ASSERT_EQ(hero->getPrimSkillLevel(PrimarySkill::SPELL_POWER), 500);
	const auto preview = newHorizonsMagic::spellEffectPreview(*hero,*SpellID(SpellID::decode("new-horizons:transfigureMatter")).toSpell());
	ASSERT_TRUE(preview.primary);
	ASSERT_TRUE(preview.secondary);
	EXPECT_EQ(preview.primary->value,1580);
	EXPECT_EQ(preview.secondary->value,1630);
	const auto text = line(preview);
	EXPECT_NE(text.find("1580"),std::string::npos);
	EXPECT_NE(text.find("1630"),std::string::npos);
	EXPECT_NE(text.find("1h"),std::string::npos);
	EXPECT_NE(text.find("2h"),std::string::npos);
	EXPECT_LE(width(text),78);
}
TEST_F(SpellbookNativeFontPresentationTest, HugeDamageAndHealingKeepBothUnitsAndMeaningfulPrecision)
{
	newHorizonsMagic::SpellEffectPreview preview{
		"new-horizons.spellbook.effect.naturesWrath","new-horizons.spellbook.effect.naturesWrath.short",
		newHorizonsMagic::SpellEffectPreview::Quantity{9223000000000000000LL,1,"new-horizons.spellbook.unit.number"},
		newHorizonsMagic::SpellEffectPreview::Quantity{8112000000000000000LL,1,"new-horizons.spellbook.unit.number"},
		"new-horizons.spellbook.naturesWrathRoute","new-horizons.spellbook.effect.naturesWrath.short.narrow"};
	const auto text = line(preview);
	EXPECT_LE(width(text),78);
	EXPECT_NE(text.find("9.2e18"),std::string::npos);
	EXPECT_NE(text.find("8.1e18"),std::string::npos);
	EXPECT_NE(text.find("D"),std::string::npos);
	EXPECT_NE(text.find("H"),std::string::npos);
	const auto detail = spellbookPresentation::effectDetail(preview,translator);
	EXPECT_NE(detail.find("9223000000000000000"),std::string::npos);
	EXPECT_NE(detail.find("8112000000000000000"),std::string::npos);
}

TEST_F(SpellbookNativeFontPresentationTest, LifeDrainKeepsDamageAndHealingRateVisibleAtNativeWidth)
{
	const auto * spell = SpellID(SpellID::decode("new-horizons:lifeDrain")).toSpell();
	hero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
	auto preview = newHorizonsMagic::spellEffectPreview(*hero, *spell);
	ASSERT_TRUE(preview.primary);
	ASSERT_TRUE(preview.secondary);
	EXPECT_EQ(preview.primary->value, 205);
	EXPECT_EQ(preview.secondary->value, 60);
	for(const int rate : {60, 75})
	{
		// The same-rank real-cast tests exercise actual Blood Drinker selection;
		// this synthetic second state isolates the formatter's width contract.
		preview.secondary->value = rate;
		const auto text = line(preview);
		EXPECT_NE(text, "!");
		EXPECT_LE(width(text), 78);
		EXPECT_NE(text.find("205"), std::string::npos);
		EXPECT_NE(text.find(std::to_string(rate) + "%"), std::string::npos);
		const auto detail = spellbookPresentation::effectDetail(preview, translator);
		EXPECT_NE(detail.find("205"), std::string::npos);
		EXPECT_NE(detail.find(std::to_string(rate) + "% of damage"), std::string::npos);
		EXPECT_NE(detail.find("actual damage"), std::string::npos);
		EXPECT_NE(detail.find("wounds"), std::string::npos);
		EXPECT_NE(detail.find("immunities"), std::string::npos);
		EXPECT_NE(detail.find("recipient-dependent"), std::string::npos);
		EXPECT_NE(detail.find("unselected default"), std::string::npos);
		EXPECT_NE(detail.find("no random proc"), std::string::npos);
	}
	const auto followup = newHorizonsMagic::spellEffectPreview(*hero, *spell, nullptr, true);
	const auto detail = spellbookPresentation::effectDetail(followup, translator);
	EXPECT_NE(detail.find("Split Focus"), std::string::npos);
	EXPECT_NE(detail.find("Focused Pairing"), std::string::npos);
	EXPECT_NE(detail.find("unselected default"), std::string::npos);
	EXPECT_NE(detail.find("no random proc"), std::string::npos);
	EXPECT_NE(detail.find("actual damage"), std::string::npos);
	EXPECT_NE(detail.find("wounds"), std::string::npos);
}
TEST_F(SpellbookNativeFontPresentationTest, HugeLifeDrainRetainsBothDamageAndHealingRate)
{
	auto preview = newHorizonsMagic::spellEffectPreview(*hero,
		*SpellID(SpellID::decode("new-horizons:lifeDrain")).toSpell());
	ASSERT_TRUE(preview.primary);
	ASSERT_TRUE(preview.secondary);
	preview.primary->value = 9223000000000000000LL;
	preview.secondary->value = 75;
	const auto text = line(preview);
	EXPECT_NE(text, "!");
	EXPECT_LE(width(text), 78);
	EXPECT_NE(text.find("e18"), std::string::npos);
	EXPECT_NE(text.find("75%"), std::string::npos);
	const auto detail = spellbookPresentation::effectDetail(preview, translator);
	EXPECT_NE(detail.find("9223000000000000000"), std::string::npos);
	EXPECT_NE(detail.find("75% of damage"), std::string::npos);
}
