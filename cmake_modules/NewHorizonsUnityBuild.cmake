# SPDX-License-Identifier: GPL-2.0-or-later
# Opt-in batching for the three large first-party compile targets only.
option(ENABLE_NH_UNITY_BUILD "Experiment with batched New Horizons C++ compilation" OFF)
set(NH_UNITY_BUILD_BATCH_SIZE 8 CACHE STRING "Maximum C++ sources per experimental Unity object (8-16)")
set_property(CACHE NH_UNITY_BUILD_BATCH_SIZE PROPERTY STRINGS 8 12 16)

if(ENABLE_NH_UNITY_BUILD AND NOT NH_UNITY_BUILD_BATCH_SIZE MATCHES "^(8|9|1[0-6])$")
	message(FATAL_ERROR "NH_UNITY_BUILD_BATCH_SIZE must be an integer from 8 through 16")
endif()

function(nh_enable_unity_build target)
	if(NOT ENABLE_NH_UNITY_BUILD)
		return()
	endif()

	if(target STREQUAL "vcmiMain")
		# These implementations share private helper names with other source
		# files. CBonusTypeHandler also leaves an INSTANTIATE macro defined.
		set(standalone
			CBonusTypeHandler.cpp
			entities/hero/NewHorizonsCommandRules.cpp
			entities/hero/NewHorizonsCapabilityRules.cpp
			entities/hero/NewHorizonsMasteryRules.cpp
			entities/hero/NewHorizonsMasteryState.cpp
			modding/CModHandler.cpp
			rewardable/VisitRewardPresentation.cpp
			mapObjects/Quest.cpp
			spells/TimedMagnitudeSpellEffectPreview.cpp
			nativeRules/MechanicsAuthoring.cpp
			json/JsonUtils.cpp
			battle/HeroCommandCoefficient.cpp
			entities/hero/LeadershipRules.cpp
			spells/CSpellHandler.cpp)
	elseif(target STREQUAL "BattleAI")
		# Windows CI compiles these publication-guard objects by their original
		# Ninja object paths before building the rest of the client.
		set(standalone
			ForecastRoundIntegration.cpp
			ForecastActivationIntegration.cpp
			ForecastNativePreparation.cpp
			ForecastUnitUpdateIntegration.cpp
			ForecastEffectIntegration.cpp
			ForecastSelectedReceiptIntegration.cpp
			ForecastAcceptedAttackIntegration.cpp
			StackPreparedEffectState.cpp
			OwnedFrameEffectPublication.cpp
			# The Windows AI flag audit requires original compile database rows.
			AttackPossibility.cpp
			BattleExchangeVariant.cpp
			# Its anonymous require(bool) collides with ForecastExchangeIntegration.
			ForecastFocusIntegration.cpp
			ForecastNativeReadView.cpp
			ForecastEffectHealth.cpp
			ForecastAcceptedSnapshotAdmission.cpp
			ForecastExchangeIntegration.cpp)
	elseif(target STREQUAL "vcmiclientcommon")
		# Local category/settings/icon helpers have identical signatures in the
		# companion source files. Keep their original translation-unit scopes.
		set(standalone
			windows/CCastleInterface.cpp
			windows/CRecruitmentWindow.cpp
			windows/wiki/WikiModContent.cpp
			windows/settings/GeneralOptionsTab.cpp
			windows/settings/OtherOptionsTab.cpp
			battle/PurifyWindow.cpp
			battle/ShadowGiftWindow.cpp
			battle/SelectiveDispelWindow.cpp
			battle/TemporalFieldWindow.cpp
			mainmenu/ReleaseUpdateTransport.cpp
			windows/wiki/WikiHeroContent.cpp
			windows/wiki/WikiArtifactContent.cpp)
	else()
		message(FATAL_ERROR "Experimental Unity is not admitted for target ${target}")
	endif()

	set_target_properties(${target} PROPERTIES
		UNITY_BUILD ON
		UNITY_BUILD_BATCH_SIZE ${NH_UNITY_BUILD_BATCH_SIZE})
	foreach(source IN LISTS standalone)
		set_source_files_properties(${source} PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
	endforeach()
	# CMake also keeps sources with individual compile flags/definitions outside
	# batches, preserving the existing strict-FP and forecast-specific settings.
endfunction()
