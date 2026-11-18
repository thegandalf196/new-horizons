/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include <functional>
#include <string>

class CGTownInstance;

namespace townEstateUI
{
void showAdministration(const CGTownInstance & town);
void showIncomeMenu(const CGTownInstance & town, std::function<void()> marketplace);
std::string incomeHelp(const CGTownInstance & town);
std::string capitolHelp(const CGTownInstance & town, bool blocked);
std::string constructionHelp(const CGTownInstance & town);
}
