/*
 * ImageLocator.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "ImageLocator.h"

#include "../GameEngine.h"
#include "render/IScreenHandler.h"

#include "../../lib/json/JsonNode.h"

namespace
{

uint8_t parsePaletteIndex(const std::string & key)
{
	if(key.empty() || key.front() == '0')
		throw std::invalid_argument("Palette remap index must be a canonical decimal value from 8 to 255");

	unsigned int index = 0;
	for(char digit : key)
	{
		if(digit < '0' || digit > '9')
			throw std::invalid_argument("Palette remap index must be a canonical decimal value from 8 to 255");

		index = index * 10 + static_cast<unsigned int>(digit - '0');
		if(index > 255)
			throw std::invalid_argument("Palette remap index is outside the palette range");
	}

	if(index < 8)
		throw std::invalid_argument("Palette remap cannot replace reserved DEF palette indices 0 to 7");

	return static_cast<uint8_t>(index);
}

std::array<uint8_t, 3> parsePaletteColor(const JsonNode & node)
{
	if(!node.isVector() || node.Vector().size() != 3)
		throw std::invalid_argument("Palette remap color must be an RGB triple");

	std::array<uint8_t, 3> color{};
	for(size_t component = 0; component < color.size(); ++component)
	{
		const auto & value = node.Vector()[component];
		if(value.getType() != JsonNode::JsonType::DATA_INTEGER)
			throw std::invalid_argument("Palette remap RGB components must be integers");

		auto channel = value.Integer();
		if(channel < 0 || channel > 255)
			throw std::invalid_argument("Palette remap RGB components must be from 0 to 255");

		color[component] = static_cast<uint8_t>(channel);
	}

	return color;
}

PaletteRemap parsePaletteRemap(const JsonNode & node)
{
	if(!node.isStruct())
		throw std::invalid_argument("Palette remap must be an object of palette-index to RGB entries");

	PaletteRemap result;
	for(const auto & [key, color] : node.Struct())
		result.emplace(parsePaletteIndex(key), parsePaletteColor(color));

	return result;
}

}

SharedImageLocator::SharedImageLocator(const JsonNode & config, EImageBlitMode mode)
	: defFrame(config["defFrame"].Integer())
	, defGroup(config["defGroup"].Integer())
	, layer(mode)
{
	if(!config["file"].isNull())
		image = ImagePath::fromJson(config["file"]);

	if(!config["defFile"].isNull())
		defFile = AnimationPath::fromJson(config["defFile"]);

	if(!config["generateShadow"].isNull())
		generateShadow = static_cast<SharedImageLocator::ShadowMode>(config["generateShadow"].Integer());

	if(!config["generateOverlay"].isNull())
		generateOverlay = static_cast<SharedImageLocator::OverlayMode>(config["generateOverlay"].Integer());

	if(!config["overlayAlphaThreshold"].isNull())
	{
		const auto & value = config["overlayAlphaThreshold"];
		if(value.getType() != JsonNode::JsonType::DATA_INTEGER || value.Integer() < 0 || value.Integer() > 255)
			throw std::invalid_argument("Overlay alpha threshold must be an integer from 0 to 255");
		overlayAlphaThreshold = static_cast<uint8_t>(value.Integer());
	}

	if(!config["paletteRemap"].isNull())
		paletteRemap = parsePaletteRemap(config["paletteRemap"]);

	if(!paletteRemap.empty())
	{
		if(!defFile || image)
			throw std::invalid_argument("Palette remap requires a DEF frame source");

		originalDefFrame = true;
	}
}

SharedImageLocator::SharedImageLocator(const ImagePath & path, EImageBlitMode mode)
	: image(path)
	, layer(mode)
{
}

SharedImageLocator::SharedImageLocator(const AnimationPath & path, int frame, int group, EImageBlitMode mode)
	: defFile(path)
	, defFrame(frame)
	, defGroup(group)
	, layer(mode)
{
}

ImageLocator::ImageLocator(const JsonNode & config, EImageBlitMode mode)
	: SharedImageLocator(config, mode)
	, verticalFlip(config["verticalFlip"].Bool())
	, horizontalFlip(config["horizontalFlip"].Bool())
{
}

bool SharedImageLocator::operator < (const SharedImageLocator & other) const
{
	if(image != other.image)
		return image < other.image;
	if(defFile != other.defFile)
		return defFile < other.defFile;
	if(defGroup != other.defGroup)
		return defGroup < other.defGroup;
	if(defFrame != other.defFrame)
		return defFrame < other.defFrame;
	if(originalDefFrame != other.originalDefFrame)
		return originalDefFrame < other.originalDefFrame;
	if(layer != other.layer)
		return layer < other.layer;
	if(generateShadow != other.generateShadow)
		return generateShadow < other.generateShadow;
	if(generateOverlay != other.generateOverlay)
		return generateOverlay < other.generateOverlay;
	if(overlayAlphaThreshold != other.overlayAlphaThreshold)
		return overlayAlphaThreshold < other.overlayAlphaThreshold;
	if(paletteRemap != other.paletteRemap)
		return paletteRemap < other.paletteRemap;

	return false;
}

bool ImageLocator::empty() const
{
	return !image.has_value() && !defFile.has_value();
}
