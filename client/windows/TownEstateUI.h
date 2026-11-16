/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include <memory>
#include <string>

class CButton;
class CGTownInstance;

namespace townEstateUI
{
std::shared_ptr<CButton> createStewardButton(const CGTownInstance & town);
std::string incomeHelp(const CGTownInstance & town);
std::string capitolHelp(const CGTownInstance & town, bool blocked);
std::string constructionHelp(const CGTownInstance & town);
}
