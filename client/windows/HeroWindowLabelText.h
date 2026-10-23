/*
 * HeroWindowLabelText.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "../GameEngine.h"
#include "../render/IFont.h"
#include "../render/IRenderHandler.h"
#include "../widgets/TextControls.h"
#include "../../lib/texts/TextOperations.h"

#include <cassert>
#include <initializer_list>
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

inline void setSpecialtyName(CLabel & label, const std::string & text, bool compact)
{
	setFittingText(label, text, compact ? 88 : 104, {FONT_SMALL, FONT_TINY});
}
}
