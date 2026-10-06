/*
 * ImageLocator.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <array>
#include <cstdint>
#include <map>

#include "render/IImage.h"

#include "../../lib/filesystem/ResourcePath.h"
#include "../../lib/constants/EntityIdentifiers.h"

using PaletteRemap = std::map<uint8_t, std::array<uint8_t, 3>>;

struct SharedImageLocator
{
	enum class ShadowMode
	{
		SHADOW_NONE,
		SHADOW_NORMAL,
		SHADOW_SHEAR
	};
	enum class OverlayMode
	{
		OVERLAY_NONE,
		OVERLAY_OUTLINE,
		OVERLAY_FLAG
	};

	std::optional<ImagePath> image;
	std::optional<AnimationPath> defFile;
	int defFrame = -1;
	int defGroup = -1;
	/// Load this exact DEF frame only; do not substitute an animation-layout alias or HD variant.
	bool originalDefFrame = false;
	EImageBlitMode layer = EImageBlitMode::OPAQUE;

	std::optional<ShadowMode> generateShadow;
	std::optional<OverlayMode> generateOverlay;
	PaletteRemap paletteRemap;

	SharedImageLocator() = default;
	SharedImageLocator(const AnimationPath & path, int frame, int group, EImageBlitMode layer);
	SharedImageLocator(const JsonNode & config, EImageBlitMode layer);
	SharedImageLocator(const ImagePath & path, EImageBlitMode layer);

	bool operator < (const SharedImageLocator & other) const;
};

struct ImageLocator : SharedImageLocator
{
	PlayerColor playerColored = PlayerColor::CANNOT_DETERMINE;

	bool verticalFlip = false;
	bool horizontalFlip = false;
	int8_t scalingFactor = 0; // 0 = auto / use default scaling

	using SharedImageLocator::SharedImageLocator;
	 ImageLocator(const JsonNode & config, EImageBlitMode layer);

	bool empty() const;
};
