/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "../../Global.h"
#include <cstdint>
#include <memory>
#include <span>
class HypotheticBattle;
class StackWithBonuses;
namespace forecast
{
/// Source/unit permission and known work must be preflighted before construction.
/// Holds a before-image of lazy native views and the friendly-observation token.
/// Existing target objects remain read-only throughout preparation; only newly
/// created map entries may be rolled back. No gameplay resources are consumed.
class DLL_LINKAGE NativePreparation final
{
	struct Saved;
	HypotheticBattle & branch;
	std::unique_ptr<Saved> saved;
	bool accepted = false;
public:
	explicit NativePreparation(HypotheticBattle &);
	~NativePreparation() noexcept;
	NativePreparation(const NativePreparation &) = delete;
	NativePreparation & operator=(const NativePreparation &) = delete;
	std::shared_ptr<StackWithBonuses> unit(uint32_t id);
	/// Call only after ALL fallible checks, immediately before no-throw commit.
	void accept(std::span<const uint32_t> affected) noexcept;
};
}
