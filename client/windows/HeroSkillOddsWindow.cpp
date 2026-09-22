/*
 * HeroSkillOddsWindow.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HeroSkillOddsWindow.h"

#include "../GameInstance.h"
#include "../gui/Shortcut.h"
#include "../gui/TextAlignment.h"
#include "../render/Colors.h"
#include "../widgets/Buttons.h"
#include "../widgets/GraphicalPrimitiveCanvas.h"
#include "../widgets/Images.h"
#include "../widgets/TextControls.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/entities/hero/CHeroClass.h"
#include "../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../lib/mapObjects/CGHeroInstance.h"

namespace
{
struct SkillWeight
{
	SecondarySkill skill = SecondarySkill::NONE;
	std::string key;
	std::string name;
	int weight = 0;
};

std::string skillName(SecondarySkill skill, const std::string & fallback)
{
	try
	{
		return skill.toSkill()->getNameTranslated();
	}
	catch(const std::exception &)
	{
		return fallback;
	}
}

int skillIconFrame(SecondarySkill skill)
{
	try
	{
		// A class-odds pane describes a prospective Basic offer.  The normal
		// CSkill icon family still carries the school/faction identity, while
		// the rank marker makes the preview distinct from a learned rank.
		return skill.toSkill()->getIconIndex(1);
	}
	catch(const std::exception &)
	{
		return -1;
	}
}
}

HeroSkillOddsWindow::HeroSkillOddsWindow(const CGHeroInstance & hero)
	: CWindowObject(BORDERED)
{
	OBJECT_CONSTRUCTION;
	const auto & rules = hero.getPrimaryGrowthRules();
	const bool canonical = newHorizonsHeroes::usesSkillOfferWeights(rules);
	const auto ownFactionSkill = canonical
		? newHorizonsHeroes::factionSkill(rules, hero.getFactionID())
		: std::optional<SecondarySkill>();
	const auto isForeignFactionSkill = [&](SecondarySkill skill)
	{
		// Legacy classes have no New Horizons faction-skill registry.  Keep the
		// old pane's exact roster there; this filter is deliberately scoped to a
		// saved canonical New Horizons rules snapshot.
		return canonical && newHorizonsHeroes::isFactionSkill(rules, skill)
			&& (!ownFactionSkill || !newHorizonsHeroes::isFactionSkillForFaction(
				rules, hero.getFactionID(), skill));
	};

	std::vector<SkillWeight> weights;
	if(canonical)
	{
		// Read this hero's saved class row, not the current module's defaults.
		for(const auto & [id, weight] : rules["skillOfferWeights"].Struct())
		{
			const int decoded = SecondarySkill::decode(id);
			if(decoded < 0)
				continue;
			const SecondarySkill skill(decoded);
			if(isForeignFactionSkill(skill))
				continue;
			weights.push_back({skill, id, skillName(skill, id), std::max<int>(0, weight.Integer())});
		}
	}
	else
	{
		for(const auto & [skill, weight] : hero.getHeroClass()->secSkillProbability)
			weights.push_back({skill, SecondarySkill::encode(skill.getNum()), skillName(skill, "Unknown"), std::max(1, weight)});
	}
	std::stable_sort(weights.begin(), weights.end(), [](const SkillWeight & left, const SkillWeight & right)
	{
		return left.name < right.name;
	});

	int total = 0;
	for(const auto & entry : weights)
		total += entry.weight;

	// A compact card grid keeps the complete class table inspectable without
	// falling back to a names-only scrolling list.  Five columns also leaves
	// enough room for long translated skill names at a readable small font.
	constexpr int columns = 5;
	constexpr int cardWidth = 132;
	constexpr int cardHeight = 46;
	constexpr int cardGap = 6;
	constexpr int margin = 18;
	constexpr int cardTop = 126;
	const int rows = std::max(1, static_cast<int>((weights.size() + columns - 1) / columns));
	const int footerTop = cardTop + rows * (cardHeight + cardGap) + 8;
	pos = Rect(0, 0, margin * 2 + columns * cardWidth + (columns - 1) * cardGap, footerTop + 84);
	elements.push_back(std::make_shared<TransparentFilledRectangle>(Rect(0, 0, pos.w, pos.h),
		ColorRGBA(39, 35, 41), ColorRGBA(180, 154, 98), 2));
	elements.push_back(std::make_shared<CLabel>(pos.w / 2, 24, FONT_BIG, ETextAlignment::CENTER, Colors::YELLOW, "Class skill odds"));
	elements.push_back(std::make_shared<CLabel>(pos.w / 2, 53, FONT_MEDIUM, ETextAlignment::CENTER, Colors::WHITE,
		GAME->translator().translate(hero.getClassNameTextID()), pos.w - 36));
	elements.push_back(std::make_shared<CMultiLineLabel>(Rect(18, 73, pos.w - 36, 40), FONT_SMALL,
		ETextAlignment::TOPLEFT, Colors::WHITE, canonical
			? "Saved class weights. Base share = weight / total class weight; zero means no ordinary offer. Foreign faction Skills are hidden."
			: "Legacy class weights (minimum 1). Base share = weight / total class weight; special offer rules may differ."));

	for(size_t i = 0; i < weights.size(); ++i)
	{
		const auto & entry = weights[i];
		const int column = static_cast<int>(i % columns);
		const int row = static_cast<int>(i / columns);
		const int x = margin + column * (cardWidth + cardGap);
		const int y = cardTop + row * (cardHeight + cardGap);
		const auto card = std::make_shared<TransparentFilledRectangle>(Rect(x, y, cardWidth, cardHeight),
			ColorRGBA(52, 46, 43), ColorRGBA(117, 96, 64));
		elements.push_back(card);
		if(const int frame = skillIconFrame(entry.skill); frame >= 0)
			elements.push_back(std::make_shared<CAnimImage>(AnimationPath::builtin("SECSKILL"), frame,
				Rect(x + 4, y + 6, 32, 32)));
		const double share = total > 0 ? 100.0 * entry.weight / total : 0.0;
		const std::string caption = entry.name + "\n" + std::to_string(entry.weight) + " / "
			+ (boost::format("%.1f%%") % share).str();
		elements.push_back(std::make_shared<CMultiLineLabel>(Rect(x + 40, y + 3, cardWidth - 44, cardHeight - 6), FONT_TINY,
			ETextAlignment::CENTERLEFT, Colors::WHITE, caption));
	}
	if(weights.empty())
		elements.push_back(std::make_shared<CLabel>(pos.w / 2, cardTop + 20, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE,
		"No class skill weights are available.", pos.w - 36));

	// Keep the established wording in a compact footer, but make the primary
	// surface above an icon pane rather than a names-only text box.
	elements.push_back(std::make_shared<CTextBox>(
		"Not exact next-level probabilities. Base share = weight / total class weight. Learned skills, eligibility and available slots change the candidate pool.",
		Rect(18, footerTop, pos.w - 126, 56), 0, FONT_TINY, ETextAlignment::TOPLEFT, Colors::WHITE));
	elements.push_back(std::make_shared<CButton>(Point(pos.w - 90, footerTop + 12), AnimationPath::builtin("IOKAY"),
		CButton::tooltip("Close", "Return to the hero screen."), [this]() { close(); }, EShortcut::GLOBAL_CANCEL));
	updateShadow();
	center();
}
