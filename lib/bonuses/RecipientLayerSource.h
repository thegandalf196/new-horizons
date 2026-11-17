/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <memory>
#include <cstdint>
#include <utility>
namespace bonusDependencies
{
class CapturedModifiers;
class OwnedBonusQueryFrame;
/// Explicit bounded recipient SOURCE lease. The source owner must install this
/// exact immutable frame as its fixed origBearer view under its native receipt.
/// This value object grants no native-world admission and no export capability.
class DLL_LINKAGE RecipientLayerSource final
{
	friend class CapturedModifiers;
	std::shared_ptr<const uint8_t> baseIdentity;
	std::shared_ptr<const OwnedBonusQueryFrame> frame;
	RecipientLayerSource(std::shared_ptr<const uint8_t> base,
		std::shared_ptr<const OwnedBonusQueryFrame> frame) : baseIdentity(std::move(base)), frame(std::move(frame)) {}
public:
	const std::shared_ptr<const OwnedBonusQueryFrame> & sourceFrame() const noexcept { return frame; }
};
}
