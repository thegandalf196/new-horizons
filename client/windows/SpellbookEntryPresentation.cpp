/*
 * SpellbookEntryPresentation.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include <array>
#include "SpellbookEntryPresentation.h"
#include "../../lib/texts/ITranslator.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/SpellCostBreakdown.h"
#include "../../lib/spells/SpellEffectPreview.h"
#include "../../lib/spells/SpellSchoolHandler.h"
#include "../../lib/texts/CGeneralTextHandler.h"

namespace spellbookPresentation
{
namespace
{
std::string translated(const ITranslator & translator, const std::string & key)
{
	return translator.translate(key);
}

std::string compactDecimal(const std::string & exact, int significantDigits)
{
	if(significantDigits == 0)
		return exact;
	const bool negative = exact.starts_with('-');
	const auto unsignedValue = negative ? exact.substr(1) : exact;
	const auto point = unsignedValue.find('.');
	const int integerDigits = point == std::string::npos ? unsignedValue.size() : point;
	std::string digits = unsignedValue;
	if(point != std::string::npos)
		digits.erase(point,1);
	const auto first = digits.find_first_not_of('0');
	if(first == std::string::npos)
		return "0";
	const int exponent = integerDigits - static_cast<int>(first) - 1;
	std::string shortened;
	if(exponent >= 5 || exponent <= -5)
	{
		const auto meaningful = digits.substr(first,significantDigits);
		shortened = meaningful.substr(0,1);
		if(meaningful.size() > 1)
			shortened += "." + meaningful.substr(1);
		while(shortened.ends_with('0') && shortened.find('.') != std::string::npos)
			shortened.pop_back();
		if(shortened.ends_with('.'))
			shortened.pop_back();
		shortened += "e" + std::to_string(exponent);
	}
	else
	{
		shortened = unsignedValue;
		if(point != std::string::npos)
		{
			const auto places = std::max(0,significantDigits - exponent - 1);
			shortened.resize(std::min(shortened.size(),point + (places ? places + 1 : 0)));
			while(shortened.ends_with('0') && shortened.find('.') != std::string::npos)
				shortened.pop_back();
			if(shortened.ends_with('.'))
				shortened.pop_back();
		}
	}
	if(negative)
		shortened.insert(0,"-");
	return shortened.size() < exact.size() ? shortened : exact;
}

std::string number(const newHorizonsMagic::SpellEffectPreview::Quantity & quantity, const ITranslator & translator, int significantDigits = 0)
{
	auto decimalScale = quantity.scale;
	while(decimalScale > 1 && decimalScale % 10 == 0)
		decimalScale /= 10;
	if(decimalScale != 1)
		throw std::invalid_argument("Invalid spell preview display scale");
	const auto whole = quantity.value / quantity.scale;
	auto remainder = std::abs(quantity.value % quantity.scale);
	std::string result = (whole == 0 && quantity.value < 0 ? "-" : "") + std::to_string(whole);
	if(remainder != 0)
	{
		std::string digits = std::to_string(remainder);
		const auto places = std::to_string(quantity.scale).size() - 1;
		digits.insert(0, places - digits.size(), '0');
		while(digits.back() == '0')
			digits.pop_back();
		result += "." + digits;
	}
	MetaString text = MetaString::createFromTextID(quantity.unitKey);
	text.replaceRawString(compactDecimal(result,significantDigits));
	return text.toString(&translator);
}

std::string effectText(const newHorizonsMagic::SpellEffectPreview & preview, const std::string & key, const ITranslator & translator, int significantDigits = 0)
{
	MetaString text = MetaString::createFromTextID(key);
	if(preview.primary)
		text.replaceRawString(number(*preview.primary, translator, significantDigits));
	if(preview.secondary)
		text.replaceRawString(number(*preview.secondary, translator, significantDigits));
	return text.toString(&translator);
}
}

Access access(const CGHeroInstance & hero, const CSpell & spell, const ITranslator & translator)
{
	const int required = newHorizonsMagic::requiredSchoolRank(hero.getMagicRules(), spell.getId());
	const bool inscribed = hero.hasSpellbook() && hero.isSpellInscribedForCasting(spell.getId());
	Access result;
	result.schoolLocked = required > 0 && !inscribed
		&& !newHorizonsMagic::hasSchoolProficiency(&hero, spell.getId()) && !hero.canCastThisSpell(&spell);
	if(!result.schoolLocked)
		return result;
	std::string schools;
	for(const auto skill : newHorizonsMagic::spellSchoolSkills(hero.getMagicRules(), spell.getId()))
	{
		if(!schools.empty())
			schools += translated(translator, "new-horizons.spellbook.or");
		schools += translator.translate(skill.toEntity(LIBRARY)->getNameTextID());
	}
	MetaString requirement = MetaString::createFromTextID("new-horizons.spellbook.requirement");
	requirement.replaceRawString(translated(translator, TextIdentifier("core.skilllev", required - 1).get()));
	requirement.replaceRawString(schools);
	result.requirement = requirement.toString(&translator);
	return result;
}

std::string levelAndSchools(int savedLevel, int mastery,
	const std::set<SpellSchool> & savedSchools, const ITranslator & translator)
{
	std::string result = translated(translator, savedLevel > 0 ? TextIdentifier("core.genrltxt", 171 + savedLevel).get()
		: "vcmi.spellBook.zero_level.hint");
	if(mastery > 0)
		result += "/" + translated(translator, TextIdentifier("core.skilllev", 3 + mastery - 1).get());
	for(const auto school : savedSchools)
		result += " | " + translator.translate(LIBRARY->spellSchoolHandler->getById(school)->getNameTextID());
	return result;
}

std::string costLine(const SpellCostBreakdown & cost, const ITranslator & translator, const Width & width, int maximumWidth)
{
	MetaString full = MetaString::createFromRawString("%s: %d");
	full.replaceTextID("core.genrltxt.387");
	full.replaceNumber(cost.finalCost);
	const auto result = full.toString(&translator);
	if(width(result) <= maximumWidth)
		return result;
	MetaString compact = MetaString::createFromTextID("new-horizons.spellbook.costShort");
	compact.replaceNumber(cost.finalCost);
	const auto compactText = compact.toString(&translator);
	if(width(compactText) <= maximumWidth)
		return compactText;
	for(const int precision : {4,3,2})
	{
		MetaString scientific = MetaString::createFromTextID("new-horizons.spellbook.costScientific");
		scientific.replaceRawString(compactDecimal(std::to_string(cost.finalCost),precision));
		const auto shortened = scientific.toString(&translator);
		if(width(shortened) <= maximumWidth)
			return shortened;
	}
	logGlobal->error("Spellbook cost cannot fit %d pixels",maximumWidth);
	return width("!") <= maximumWidth ? "!" : std::string();
}

std::string effectLine(const newHorizonsMagic::SpellEffectPreview & preview,
	const ITranslator & translator, const Width & width, int maximumWidth)
{
	if(preview.labelKey.empty())
		return {};
	const auto full = effectText(preview, preview.labelKey, translator);
	if(width(full) <= maximumWidth)
		return full;
	const std::array<std::string,2> compactKeys{preview.compactLabelKey,preview.narrowLabelKey};
	for(const int precision : {0,4,3,2})
		for(const auto & key : compactKeys)
			if(!key.empty())
			{
				const auto compact = effectText(preview,key,translator,precision);
				if(width(compact) <= maximumWidth)
					return compact;
			}
	logGlobal->error("Spellbook effect cannot fit %d pixels: %s",maximumWidth,preview.labelKey);
	return width("!") <= maximumWidth ? "!" : std::string();
}

std::string effectDetail(const newHorizonsMagic::SpellEffectPreview & preview, const ITranslator & translator)
{
	if(preview.labelKey.empty())
		return {};
	const auto value = effectText(preview, preview.labelKey, translator);
	MetaString heading = MetaString::createFromTextID("new-horizons.spellbook.currentEffect");
	heading.replaceRawString(value);
	return heading.toString(&translator) + (preview.qualificationKey.empty()
		? "" : "\n" + translated(translator, preview.qualificationKey))
		+ (preview.narrowLabelKey.empty() ? "" : "\n" + translated(translator,"new-horizons.spellbook.compactNotation"));
}
}
