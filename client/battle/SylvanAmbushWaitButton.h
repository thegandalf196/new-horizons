/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/battle/NewHorizonsSylvanAmbush.h"
#include <boost/algorithm/string/replace.hpp>
#include <functional>
#include <string>

class CButton;
class CStack;
class CPlayerBattleCallback;

/// The Ambush variant of the battle Wait button. It reads the same
/// concealment offer the server grants on Wait, so it never promises more.
namespace sylvanAmbushWaitButton
{
/// Original button frame and states with the Ambush symbol; optional resource.
inline constexpr const char * AMBUSH_ANIMATION = "NH_wait_ambush_button";
inline constexpr const char * DEFAULT_ANIMATION = "icm006";

struct Presentation
{
	bool ambush = false;
	std::string title;
	std::string help;
};

using Translate = std::function<std::string(const std::string &)>;

/// Pure text choice for an offer; translated keys, no widget or callback access.
inline Presentation present(const newHorizonsSylvanAmbush::ConcealmentOffer & offer, const Translate & translate)
{
	using Source = battle::SylvanAmbushState::Source;
	Presentation result;
	if(!offer.slot)
		return result;
	result.ambush = true;
	result.title = translate("new-horizons.combat.sylvanAmbush.wait.title");
	if(*offer.slot == Source::FOREST_COVER)
		result.help = translate("new-horizons.combat.sylvanAmbush.wait.forestCover");
	else
	{
		result.help = translate("new-horizons.combat.sylvanAmbush.wait.rank");
		boost::replace_all(result.help, "%LEFT%", std::to_string(offer.rankSlots - offer.rankUsed));
		boost::replace_all(result.help, "%TOTAL%", std::to_string(offer.rankSlots));
	}
	return result;
}

/// Switch art and help for the active stack. `ambushArtShown` remembers the
/// current image so the button reloads only on a change. Without the art
/// resource the hourglass stays and only the help text changes.
/// Returns true when it applied the Ambush help (the caller keeps its own otherwise).
bool refresh(CButton & wait, bool & ambushArtShown, const CPlayerBattleCallback & battle, const CStack * active);
}
