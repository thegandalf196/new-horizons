/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "OwnedPreparationFirstWriter.h"
#include "../../lib/StartInfo.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/bonuses/OwnedBonusCaptureCodec.h"
#include "../../lib/entities/hero/NewHorizonsSpecialtySerialization.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/NewHorizonsNeutralCalibration.h"
#include "../../lib/serializer/GameConnection.h"
#include <array>
#include <cmath>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
namespace ownedPreparationAudit
{
namespace
{
constexpr std::array<const char *, 16> keys{
	"scorchingImpactDamageReductionPercent", "cruelOverseerDefenseReduction",
	"lanceFormationDefensePerHex", "lanceFormationMaximumDefense",
	"stormWingsDefenseIgnorePercent", "stormWingsMinimumFlightDistance",
	"consecratedTargetDefenseIgnorePercent", "featheredFeintPhysicalReductionIgnorePercent",
	"demonicSolidarityReductionPercent", "ironHideReductionPercent",
	"graveDustHealingReductionBasisPoints", "cinderReprisalDamagePercent",
	"emberbrandDamagePercent", "toxicStingChancePercent", "gnollHarriersDamagePercent",
	"prismaticVanguardPhysicalDamagePercent"};
struct Observation
{
	JsonNode values;
	std::weak_ptr<const void> worldOwner;
	std::string session;
	ESerializationVersion epoch;
	ESerializationVersion bonusProfile;
	GameConnectionID endpoint;
};
std::mutex mutex;
std::optional<Observation> serverBeforeWrite;
bool decoded = false;
void require(bool value, const char * reason)
{
	if(!value) throw std::logic_error(reason);
}
JsonNode coefficients(const JsonNode & development, ESerializationVersion bonusProfile)
{
	const auto & policy = development["additionalSpecialties"]["ordinaryCreatureSpecialties"];
	require(policy.isStruct(), "First-writer world/hero has no captured ordinary policy");
	JsonNode result;
	result.Struct();
	for(const auto * key : keys)
		if(const auto found = policy.Struct().find(key); found != policy.Struct().end())
		{
			require(found->second.isNumber() && std::isfinite(found->second.Float())
				&& std::floor(found->second.Float()) == found->second.Float(),
				"First-writer captured coefficient must be an integer");
			result[key] = found->second;
		}
	require(bonusProfile == ESerializationVersion::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES || result.Struct().empty(),
		"First-writer unsupported extended coefficient capture");
	return result;
}
Observation capture(GameConnection & connection, const std::shared_ptr<CGameState> & world)
{
	require(world && world->getInitialStartInfo() && world->getInitialStartInfo()->mode == EStartMode::NEW_GAME,
		"First-writer observation requires the actual ordinary NEW_GAME world");
	ESerializationVersion epoch = ESerializationVersion::NONE;
	const auto context = connection.captureSpecialtySerializationContext(connection.getConnection(), &epoch);
	const auto bonusProfile = OwnedBonusCaptureCodec::fromSourceCapabilities(context).version();
	require(epoch == ESerializationVersion::NEW_HORIZONS_ORDINARY_COMBAT_SPECIALTIES
		|| epoch == ESerializationVersion::NEW_HORIZONS_LEADERSHIP_NEUTRAL_CALIBRATION
		|| epoch == ESerializationVersion::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES
		|| epoch == ESerializationVersion::NEW_HORIZONS_OPTIONAL_QUICK_STUDY,
		"First-writer observation requires an exact supported stable epoch");
	require(!connection.uuid.empty() && connection.connectionID != GameConnectionID::INVALID,
		"First-writer observation requires its established negotiated endpoint");
	Observation result{{}, std::shared_ptr<const void>(world, nullptr), connection.uuid, epoch, bonusProfile, connection.connectionID};
	const CGameState & game = *world;
	result.values["world"] = coefficients(game.getHeroDevelopmentRules(), bonusProfile);
	if(epoch != ESerializationVersion::NEW_HORIZONS_ORDINARY_COMBAT_SPECIALTIES)
	{
		const auto & settings = game.getMap().getSettings();
		newHorizonsNeutralArmies::calibration::validateSerialization(settings, true);
		result.values["neutralCalibration"] = settings.getValue(EGameSettings::CREATURES_NEUTRAL_LEADERSHIP_CALIBRATION);
	}
	if(epoch == ESerializationVersion::NEW_HORIZONS_OPTIONAL_QUICK_STUDY)
		result.values["quickStudy"]["world"] = game.getHeroPerkRules()["quickStudyPolicy"];
	auto & heroes = result.values["heroes"].Struct();
	for(const auto id : game.getMap().getHeroesOnMap())
	{
		const auto * hero = game.getHero(id);
		require(hero != nullptr, "First-writer observation found an unresolved map hero");
		heroes[std::to_string(id.getNum())] = coefficients(hero->getPrimaryGrowthRules(), bonusProfile);
		if(epoch == ESerializationVersion::NEW_HORIZONS_OPTIONAL_QUICK_STUDY)
			result.values["quickStudy"]["heroes"][std::to_string(id.getNum())] = hero->getPerkState().rules["quickStudyPolicy"];
	}
	require(!heroes.empty(), "First-writer observation requires independently initialized heroes");
	return result;
}
void report(const Observation & value, bool authoritative)
{
	auto row = value.values;
	row["epoch"].Integer() = static_cast<int32_t>(value.epoch);
	if(value.bonusProfile != value.epoch) row["bonusProfile"].Integer() = static_cast<int32_t>(value.bonusProfile);
	row["endpointId"].Integer() = static_cast<int>(value.endpoint);
	row["phase"].String() = authoritative ? "beforeLobbyStartGameWrite" : "afterLobbyStartGameDecode";
	row["independentDecodedWorld"].Bool() = !authoritative;
	// No session UUID, pointer, source object or private path escapes this value-only record.
	std::cout << "NH_FIRST_WRITER " << row.toCompactString() << '\n';
}
}
void observeFirstWriter(bool authoritative, GameConnection & connection, const std::shared_ptr<CGameState> & world)
{
	auto value = capture(connection, world); // Connection lock retires before the local observation mutex.
	std::lock_guard lock(mutex);
	if(authoritative)
	{
		require(!serverBeforeWrite && !decoded, "First-writer producer must occur exactly once");
		serverBeforeWrite = std::move(value);
		report(*serverBeforeWrite, true);
		return;
	}
	require(serverBeforeWrite && !decoded, "First-writer client decode has no unique producer observation");
	const auto & server = *serverBeforeWrite;
	require(server.session == value.session && server.epoch == value.epoch
		&& server.bonusProfile == value.bonusProfile,
		"First-writer peer negotiation/session mismatch");
	require(!server.worldOwner.expired()
		&& (server.worldOwner.owner_before(value.worldOwner) || value.worldOwner.owner_before(server.worldOwner)),
		"First-writer client must own its independently decoded world");
	require(server.values == value.values, "First-writer world/hero coefficients changed across real transport");
	decoded = true;
	report(value, false);
}
bool firstWriterVerified()
{
	std::lock_guard lock(mutex);
	return decoded;
}
}
