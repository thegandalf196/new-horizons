/*
 * ClientReplaySessionReadiness.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

struct BattleUnitsChanged;
class ClientPacketReceiveSession;

/// Borrowed native-session preflight, not a readiness permit or callback.
/// Only the real receive-stack owner can construct a provider. Its existing
/// destruction order keeps this base alive until the prepared child retires.
/// The pure replay storage path does not link the native Engine implementation.
class ClientReplaySessionReadiness
{
	friend class ClientPacketReceiveSession;
	ClientReplaySessionReadiness() = default;
	ClientReplaySessionReadiness(const ClientReplaySessionReadiness &) = delete;
	ClientReplaySessionReadiness & operator=(const ClientReplaySessionReadiness &) = delete;
protected:
	virtual ~ClientReplaySessionReadiness() = default;
public:
	virtual bool ready(const BattleUnitsChanged & packet) const noexcept = 0;
};
