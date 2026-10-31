/*
 * BonusSerializationContext.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

/// Target-codec facts, propagated unchanged through serialized Bonus owners.
struct BonusSerializationContext
{
	bool adjacencyFiltersSupported;
	bool combatEventParametersSerialized;
	bool ordinaryRangedSpecialtiesSupported = false;
	bool ordinaryMaintenanceSpecialtiesSupported = false;
	bool ordinaryCombatSpecialtiesSupported = false;
	bool extendedOrdinarySpecialtiesSupported = false;

	template<typename Handler>
	static BonusSerializationContext fromHandler(const Handler & handler)
	{
		return {handler.hasFeature(Handler::Version::NEW_HORIZONS_ADJACENCY_FILTERS),
			handler.hasFeature(Handler::Version::BONUS_TRIGGER),
			handler.hasFeature(Handler::Version::NEW_HORIZONS_ORDINARY_RANGED_SPECIALTIES),
			handler.hasFeature(Handler::Version::NEW_HORIZONS_ORDINARY_MAINTENANCE_SPECIALTIES),
			handler.hasFeature(Handler::Version::NEW_HORIZONS_ORDINARY_COMBAT_SPECIALTIES),
			handler.hasFeature(Handler::Version::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES)};
	}
};
