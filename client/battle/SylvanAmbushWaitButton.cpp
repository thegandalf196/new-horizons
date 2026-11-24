/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "SylvanAmbushWaitButton.h"
#include "../widgets/Buttons.h"
#include "../../lib/CStack.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/filesystem/Filesystem.h"
#include "../../lib/texts/CGeneralTextHandler.h"

namespace
{
/// The Ambush art is a JSON frame descriptor in the NHART pack, not a DEF, so
/// look for the descriptor under SPRITES/ where the renderer resolves it.
bool sylvanAmbushWaitArtAvailable()
{
	const auto path = AnimationPath::builtin(sylvanAmbushWaitButton::AMBUSH_ANIMATION);
	const auto * resources = CResourceHandler::get();
	return resources->existsResource(path.addPrefix("SPRITES/").toType<EResType::JSON>())
		|| resources->existsResource(path);
}
}

namespace sylvanAmbushWaitButton
{
bool refresh(CButton & wait, bool & ambushArtShown, const CPlayerBattleCallback & battle, const CStack * active)
{
	// Only the local player's own stack: never read an opponent's perk state.
	const bool own = active && battle.getPlayerID() && battle.battleGetOwner(active) == *battle.getPlayerID();
	const auto offer = own ? newHorizonsSylvanAmbush::concealmentOffer(battle, *active)
		: newHorizonsSylvanAmbush::ConcealmentOffer{};
	const auto shown = present(offer, [](const std::string & key) { return LIBRARY->generaltexth->translate(key); });
	const bool art = shown.ambush && sylvanAmbushWaitArtAvailable();
	if(art != ambushArtShown)
	{
		wait.setImage(AnimationPath::builtin(art ? AMBUSH_ANIMATION : DEFAULT_ANIMATION));
		ambushArtShown = art;
	}
	if(shown.ambush)
		wait.setHelp(CButton::tooltip(shown.title, shown.help));
	return shown.ambush;
}
}
