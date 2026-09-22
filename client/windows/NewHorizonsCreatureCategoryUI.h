/*
 * NewHorizonsCreatureCategoryUI.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 *
 * Presentation helpers for the saved Core / Elite / Champion classification.
 * The caller supplies the optional view from its actual game callback; an
 * absent view deliberately produces no text so legacy windows retain their
 * original presentation.
 */
#pragma once

#include "../../lib/entities/creature/NewHorizonsCreatureCategoryRules.h"
#include "../../lib/texts/ITranslator.h"

#include <optional>
#include <string>

namespace newHorizonsCreatureCategoryUI
{
inline std::string name(const std::optional<newHorizonsCreatures::CreatureCategoryView> & category,
	const ITranslator * translator)
{
	if(!category || category->nameTextId.empty() || !translator)
		return {};
	return translator->translate(category->nameTextId);
}

inline std::string prefix(const std::optional<newHorizonsCreatures::CreatureCategoryView> & category,
	const ITranslator * translator, const std::string & text)
{
	const auto categoryName = name(category, translator);
	return categoryName.empty() ? text : "[" + categoryName + "] " + text;
}
}
