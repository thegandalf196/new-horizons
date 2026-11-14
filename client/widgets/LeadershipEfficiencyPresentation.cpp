/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "LeadershipEfficiencyPresentation.h"
#include "render/Canvas.h"
#include "../render/Colors.h"
#include "../render/EFont.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/texts/CGeneralTextHandler.h"
#include <boost/algorithm/string/replace.hpp>

#include <iomanip>
#include <sstream>

namespace leadershipPresentation
{
namespace
{
constexpr ColorRGBA BACKGROUND(23, 17, 14, 255);
constexpr ColorRGBA BORDER(137, 103, 53, 255);
constexpr ColorRGBA BLADE(190, 47, 39, 255);
constexpr ColorRGBA HIGHLIGHT(233, 93, 72, 255);
constexpr ColorRGBA GRIP(184, 133, 62, 255);
constexpr ColorRGBA TEXT(244, 231, 200, 255);

// Native-sized shapes: no scaling, filtering or alpha blending. The empty
// middle row separates the snapped blade from its upper fragment.
constexpr std::array<const char *, 9> SWORD{{
	".....rh", "....rh.", ".......", "...rh..", "..rh...",
	".gggg..", "...g...", "..g....", ".g....."}};
constexpr std::array<const char *, 7> SMALL_SWORD{{
	"....rh", "...rh.", "......", "..rh..", ".ggg..", "..g...", ".g...."}};
constexpr std::array<std::array<uint8_t, 5>, 11> DIGITS{{
	{{7,5,5,5,7}}, {{2,6,2,2,7}}, {{7,1,7,4,7}}, {{7,1,7,1,7}},
	{{5,5,7,1,1}}, {{7,4,7,1,7}}, {{7,4,7,5,7}}, {{7,1,1,1,1}},
	{{7,5,7,5,7}}, {{7,5,7,1,7}}, {{5,1,2,4,5}}}};

template<size_t Height>
void drawSword(Canvas & canvas, Point position, const std::array<const char *, Height> & pixels)
{
	for(size_t y = 0; y < pixels.size(); ++y)
		for(size_t x = 0; pixels[y][x]; ++x)
		{
			const char pixel = pixels[y][x];
			if(pixel != '.')
				canvas.drawColor(Rect(position.x + static_cast<int>(x), position.y + static_cast<int>(y), 1, 1),
					pixel == 'r' ? BLADE : pixel == 'h' ? HIGHLIGHT : GRIP);
		}
}

void drawMiniatureNumber(Canvas & canvas, Point position, const std::string & text)
{
	for(const char character : text)
	{
		const auto & glyph = DIGITS[character == '%' ? 10 : character - '0'];
		for(size_t y = 0; y < glyph.size(); ++y)
			for(int x = 0; x < 3; ++x)
				if(glyph[y] & (4 >> x))
					canvas.drawColor(Rect(position.x + x, position.y + static_cast<int>(y), 1, 1), TEXT);
		position.x += 4;
	}
}

std::array<CreatureID, 7> armyComposition(const CCreatureSet * army)
{
	std::array<CreatureID, 7> result;
	result.fill(CreatureID::NONE);
	if(army)
		for(const auto & [slot, stack] : army->Slots())
			if(slot.validSlot() && stack && stack->getCount() > 0)
				result[slot.getNum()] = stack->getCreatureID();
	return result;
}

int64_t capturedPolicy(const CGHeroInstance * hero)
{
	if(!hero || !hero->getCapabilityRules()["rulesetVersion"].isNumber()) return 0;
	return hero->getCapabilityRules()["rulesetVersion"].Integer();
}
}

void drawBadge(Canvas & canvas, const Point & position, int percent, BadgeRole role)
{
	const auto size = badgeSize(role);
	const Rect bounds(position.x, position.y, size.width, size.height);
	canvas.drawColor(bounds, BACKGROUND);
	canvas.drawColor(Rect(bounds.x, bounds.y, bounds.w, 1), BORDER);
	canvas.drawColor(Rect(bounds.x, bounds.y + bounds.h - 1, bounds.w, 1), BORDER);
	canvas.drawColor(Rect(bounds.x, bounds.y + 1, 1, bounds.h - 2), BORDER);
	canvas.drawColor(Rect(bounds.x + bounds.w - 1, bounds.y + 1, 1, bounds.h - 2), BORDER);
	const auto text = std::to_string(std::clamp(percent, 0, 99)) + "%";
	if(role != BadgeRole::PORTRAIT)
	{
		drawSword(canvas, position + Point(1, 1), SMALL_SWORD);
		drawMiniatureNumber(canvas, position + Point(size.width - 2 - static_cast<int>(text.size()) * 4, 2), text);
	}
	else
	{
		drawSword(canvas, position + Point(1, 1), SWORD);
		canvas.drawText(position + Point(10 + (size.width - 12) / 2, size.height / 2),
			FONT_TINY, TEXT, ETextAlignment::CENTER, text);
	}
}

std::string detailText(const newHorizonsHeroes::LeadershipStackReadout & readout)
{
	auto text = LIBRARY->generaltexth->translate(readout.active
		? "new-horizons.leadership.readout.detail" : "new-horizons.leadership.readout.uncommanded");
	const auto decimal = [](double value)
	{
		std::ostringstream result;
		result << std::fixed << std::setprecision(2) << value;
		return result.str();
	};
	const double percentage = readout.efficiency >= 1.0 ? 100.0
		: std::floor(readout.efficiency * 10000.0) / 100.0;
	boost::replace_all(text, "%EFFICIENCY%", decimal(percentage));
	boost::replace_all(text, "%COUNT%", std::to_string(readout.count));
	boost::replace_all(text, "%CAPACITY%", std::to_string(readout.capacity));
	boost::replace_all(text, "%BASE_LEADERSHIP%", std::to_string(readout.baseLeadership));
	boost::replace_all(text, "%EFFECTIVE_LEADERSHIP%", std::to_string(readout.effectiveLeadership));
	boost::replace_all(text, "%REQUIREMENT%", std::to_string(readout.requirement));
	boost::replace_all(text, "%EFFECTIVE_COUNT%", decimal(readout.effectiveCount()));
	return text;
}

std::string heroDetailText(const CGHeroInstance & hero)
{
	const auto readout = newHorizonsHeroes::leadershipHeroReadout(hero);
	auto text = LIBRARY->generaltexth->translate("new-horizons.leadership.heroHelp");
	boost::replace_all(text, "%BASE%", std::to_string(readout.baseLeadership));
	boost::replace_all(text, "%AUTHORITY%", std::to_string(readout.authorityBonusPercent));
	boost::replace_all(text, "%RANK_EFFECTIVE%", std::to_string(readout.effectiveLeadership));
	return text;
}

bool ReadoutCache::matches(const CGHeroInstance * owner, CreatureID type, int64_t amount,
	const CCreatureSet * army, const void * capture) const
{
	return valid && hero == owner && context == capture && creature == type && count == amount
		&& policyVersion == capturedPolicy(owner)
		&& (!owner || (bonusVersion == owner->getTreeVersion() && level == owner->level
			&& skills == owner->secSkills && perks == owner->getPerkState().selected))
		&& composition == armyComposition(army);
}

void ReadoutCache::capture(const CGHeroInstance * owner, CreatureID type, int64_t amount,
	const CCreatureSet * army, const void * capture)
{
	hero = owner;
	context = capture;
	creature = type;
	count = amount;
	bonusVersion = owner ? owner->getTreeVersion() : -1;
	level = owner ? owner->level : 0;
	policyVersion = capturedPolicy(owner);
	if(owner)
	{
		skills = owner->secSkills;
		perks = owner->getPerkState().selected;
	}
	else
	{
		skills.clear();
		perks.clear();
	}
	composition = armyComposition(army);
	valid = true;
}
}
