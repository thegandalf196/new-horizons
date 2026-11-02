/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <memory>
class HypotheticBattle;
namespace battle { class OwnedBattleFrameBatch; }
namespace ownedPreparationIntegration
{
/// Optional real-client assertion observer. No source admission, native pointer,
/// success return, gameplay preference or bypass of the normal decision path.
/// Receives a private assertion child; mutations cannot alter the decision root.
class Observer
{
public:
	virtual ~Observer() = default;
	virtual void onOwnedRoot(const std::shared_ptr<HypotheticBattle> & current,
		const std::shared_ptr<const battle::OwnedBattleFrameBatch> & source) = 0;
};
}
