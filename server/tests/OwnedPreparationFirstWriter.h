/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#ifndef NH_OWNED_PREPARATION_SIMULATION_AUDIT
#error First-writer observations belong only to the dedicated integration executable
#endif
#include <memory>
class CGameState;
class GameConnection;
namespace ownedPreparationAudit
{
/// Copies values at the real LobbyStartGame producer/decode seams; never grants source access.
void observeFirstWriter(bool authoritative, GameConnection &, const std::shared_ptr<CGameState> &);
bool firstWriterVerified();
}
