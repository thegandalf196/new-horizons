/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <memory>
class CBattleInfoCallback;
namespace battle
{
class OwnedBattleFrameBatch;
class OwnedBattleFrameCapture;
struct BattleAccessibilityState;
/// INTERNAL construction belongs only to the real factory under actual Session.
/// No public importer, source pointer or deferred source callable is returned.
class OwnedBattleFrameSubject final
{
	friend class OwnedBattleFrameCapture;
	static std::shared_ptr<const BattleAccessibilityState> captureScene(const CBattleInfoCallback & source, const OwnedBattleFrameBatch & batch);
	static std::shared_ptr<CBattleInfoCallback> seal(const CBattleInfoCallback & source, const OwnedBattleFrameBatch & batch);
};
}
