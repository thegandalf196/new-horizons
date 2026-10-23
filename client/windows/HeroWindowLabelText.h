/*
 * HeroWindowLabelText.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "../GameEngine.h"
#include "CMessage.h"
#include "../render/IFont.h"
#include "../render/IRenderHandler.h"
#include "../widgets/TextControls.h"
#include "../../lib/texts/TextOperations.h"

#include <cassert>
#include <initializer_list>
#include <memory>
#include <string>

namespace heroWindowLabelText
{
/// Keep the preferred font when possible; return whether the text was elided.
inline bool setFittingText(CLabel & label, const std::string & fullText, int width,
                          std::initializer_list<EFonts> fonts)
{
	assert(fonts.size() > 0);
	for(const auto font : fonts)
	{
		label.font = font;
		if(ENGINE->renderHandler().loadFont(font)->getStringWidth(fullText) <= width)
		{
			label.setText(fullText);
			return false;
		}
	}

	const auto font = ENGINE->renderHandler().loadFont(label.font);
	const std::string ellipsis = "...";
	std::string text = fullText;
	while(!text.empty() && font->getStringWidth(text + ellipsis) > width)
		TextOperations::trimRightUnicode(text);
	label.setText(font->getStringWidth(ellipsis) <= width ? text + ellipsis : "");
	return true;
}

inline bool setTitle(CLabel & label, const std::string & text, bool compact)
{
	if(compact)
		return setFittingText(label, text, 140, {FONT_MEDIUM, FONT_SMALL, FONT_TINY});

	// Preserve the original layout's existing width, including unlimited labels.
	label.font = FONT_MEDIUM;
	label.setText(text);
	return false;
}

/// Explicit capture keeps replacements attached to the hero window even when
/// the caller has no active construction scope. Dropped labels detach on destruction.
inline std::shared_ptr<CLabel> makeSkillName(CIntObject & owner, Point position, int width, bool compact)
{
	OBJECT_CONSTRUCTION_TARGETED(&owner);
	if(compact)
		return std::make_shared<CMultiLineLabel>(Rect(position, Point(width, 22)),
			FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE);
	return std::make_shared<CLabel>(position.x, position.y, FONT_SMALL,
		ETextAlignment::TOPLEFT, Colors::WHITE, "", width);
}

inline void setSkillName(CLabel & label, const std::string & text, bool compact)
{
	if(compact)
	{
		// The learned-name box has room for two Tiny lines, not a wider row.
		assert(dynamic_cast<CMultiLineLabel *>(&label));
		for(const auto font : {FONT_SMALL, FONT_TINY})
		{
			label.font = font;
			const auto lines = CMessage::breakText(text, 74, font);
			const auto height = ENGINE->renderHandler().loadFont(font)->getLineHeight();
			if(lines.size() * height <= 22)
			{
				label.setText(text);
				return;
			}
		}
		setFittingText(label, text, 74, {FONT_TINY});
	}
	else
	{
		label.font = FONT_SMALL;
		label.setText(text);
	}
}

inline void setSpecialtyName(CLabel & label, const std::string & text, bool compact)
{
	setFittingText(label, text, compact ? 88 : 104, {FONT_SMALL, FONT_TINY});
}
}
