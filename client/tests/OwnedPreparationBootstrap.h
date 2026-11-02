/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#if !defined(NH_OWNED_PREPARATION_INTEGRATION) || !NH_OWNED_PREPARATION_INTEGRATION
#error Owned bootstrap belongs only to the dedicated integration executable
#endif
#include <string>
#include <cstddef>
class CClient;
namespace ownedPreparationIntegration
{
class Bootstrap final
{
	bool pending = true, requested = false, validated = false;
	std::string digest;
	std::size_t bytes;
public:
	Bootstrap();
	void update();
	bool loaded() const { return validated; }
};
}
