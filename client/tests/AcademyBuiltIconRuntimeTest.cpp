/*
 * AcademyBuiltIconRuntimeTest.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 *
 * Loads the runtime-generated Academy town-list icons at each SDL2 image
 * scale. An opt-in path also checks the detached Cabir animation descriptors
 * and authored icon bindings. SDL is constrained to its dummy video/audio
 * drivers and software renderer. ScreenHandler's existing constructor
 * clear/present stays on that dummy backend; the fixture has no event, input,
 * or presentation loop.
 */
#include "../StdInc.h"

#include "../GameEngine.h"
#include "../CMT.h"
#include "../battle/BattleConstants.h"
#include "../battle/CreatureAnimation.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/CConfigHandler.h"
#include "../../lib/AsyncRunner.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/VCMIDirs.h"
#include "../../lib/filesystem/Filesystem.h"
#include "../../lib/logging/CBasicLogConfigurator.h"
#include "../../lib/modding/IdentifierStorage.h"
#include "../../lib/modding/ModScope.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/mapObjectConstructors/AObjectTypeHandler.h"
#include "../../lib/mapObjectConstructors/CObjectClassesHandler.h"
#include "../../lib/mapObjects/ObjectTemplate.h"

#include "render/Canvas.h"
#include "render/CAnimation.h"
#include "render/IRenderHandler.h"
#include "render/IScreenHandler.h"
#include "MagiPaletteInspection.h"

#include "../../lib/json/JsonNode.h"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

[[noreturn]] void handleFatalError(const std::string & message, bool)
{
	throw std::runtime_error("Unexpected VCMI fatal error in Academy/Cabir resource fixture: " + message);
}

namespace
{
struct BuiltIcon
{
	const char * normal;
	const char * built;
	int nativeWidth;
	int nativeHeight;
};

struct AcademyPortrait
{
	const char * identifier;
	const char * image;
	const char * mask;
	int internalId;
	int defFrame;
	const char * smallImage = nullptr;
	const char * sourceAnimation = nullptr;
	int sourceFrame = -1;
};

constexpr std::array<BuiltIcon, 4> academyBuiltIcons{{
	{"NH_academy_fort_large_normal.png", "NH_academy_fort_large_built.png", 58, 64},
	{"NH_academy_fort_small_normal.png", "NH_academy_fort_small_built.png", 48, 32},
	{"NH_academy_village_large_normal.png", "NH_academy_village_large_built.png", 58, 64},
	{"NH_academy_village_small_normal.png", "NH_academy_village_small_built.png", 48, 32},
}};

constexpr std::array<AcademyPortrait, 12> academyPortraits{{
	{"gremlin", "NH_cabir_icon_large.png", "", 28, 30, "NH_cabir_icon_small.png"},
	{"masterGremlin", "NH_cabirMaster_icon_large.png", "", 29, 31, "NH_cabirMaster_icon_small.png"},
	{"ironGolem", "NH_academy_ironGolem_icon_large.png", "NH_academy_ironGolem_portrait_mask.png", 32, 34},
	{"stoneGolem", "NH_academy_stoneGolem_icon_large.png", "NH_academy_stoneGolem_portrait_mask.png", 33, 35},
	{"mage", "NH_academy_mage_icon_large.png", "NH_academy_mage_portrait_mask.png", 34, 36},
	{"archMage", "NH_academy_archMage_icon_large.png", "NH_academy_archMage_portrait_mask.png", 35, 37,
		nullptr, "NH_ArchMageGreyPortrait", 0},
	{"genie", "NH_academy_genie_icon_large.png", "NH_academy_genie_portrait_mask.png", 36, 38},
	{"masterGenie", "NH_academy_masterGenie_icon_large.png", "NH_academy_masterGenie_portrait_mask.png", 37, 39},
	{"naga", "NH_academy_naga_icon_large.png", "NH_academy_naga_portrait_mask.png", 38, 40},
	{"nagaQueen", "NH_academy_nagaQueen_icon_large.png", "NH_academy_nagaQueen_portrait_mask.png", 39, 41},
	{"giant", "NH_academy_giant_icon_large.png", "NH_academy_giant_portrait_mask.png", 40, 42},
	{"titan", "NH_academy_titan_icon_large.png", "NH_academy_titan_portrait_mask.png", 41, 43},
}};

void require(bool condition, const std::string & message)
{
	if(!condition)
		throw std::runtime_error(message);
}

void setRequiredEnvironment(const char * name, const std::filesystem::path & value)
{
	if(setenv(name, value.c_str(), 1) != 0)
		throw std::runtime_error(std::string("Could not set ") + name);
}

class IsolatedProfile
{
	std::filesystem::path root;

public:
	IsolatedProfile(const std::filesystem::path & originalData)
	{
		char pathTemplate[] = "/tmp/vcmi-academy-built-icons.XXXXXX";
		char * createdPath = mkdtemp(pathTemplate);
		require(createdPath != nullptr, "Could not create isolated runtime-test profile");
		root = createdPath;

		setRequiredEnvironment("XDG_DATA_HOME", root / "data");
		setRequiredEnvironment("XDG_CONFIG_HOME", root / "config");
		setRequiredEnvironment("XDG_CACHE_HOME", root / "cache");
		setRequiredEnvironment("SDL_VIDEODRIVER", "dummy");
		setRequiredEnvironment("SDL_AUDIODRIVER", "dummy");
		setRequiredEnvironment("SDL_RENDER_DRIVER", "software");

		const auto userConfig = root / "config" / "vcmi";
		std::filesystem::create_directories(userConfig);
		std::filesystem::create_directories(root / "data" / "vcmi");
		std::filesystem::create_directories(root / "cache" / "vcmi");
		std::filesystem::create_directory_symlink(originalData, root / "data" / "vcmi" / "Data");

		// This profile activates only the built-in content and New Horizons. It
		// prevents the renderer exercise from reading or rewriting the user's
		// normal mod configuration.
		std::ofstream mods(userConfig / "modSettings.json");
		require(static_cast<bool>(mods), "Could not create isolated New Horizons mod settings");
		mods << R"({"activePreset":"academy-icon-runtime","presets":{"academy-icon-runtime":{"mods":["vcmi","new-horizons"],"settings":{}}}})";
		require(static_cast<bool>(mods), "Could not write isolated New Horizons mod settings");
	}

	~IsolatedProfile()
	{
		std::error_code ignored;
		std::filesystem::remove_all(root, ignored);
	}

	const std::filesystem::path & path() const { return root; }
};

void verifyDifferentPixels(const std::shared_ptr<IImage> & normal, const std::shared_ptr<IImage> & built,
		const BuiltIcon & icon)
{
	require(normal != nullptr, std::string("Could not load normal icon ") + icon.normal);
	require(built != nullptr, std::string("Could not generate built icon ") + icon.built);
	const Point size = normal->dimensions();
	require(size == built->dimensions(), std::string("Normal/built icon dimensions differ: ") + icon.built);
	require(size == Point(icon.nativeWidth, icon.nativeHeight), std::string("Unexpected native icon dimensions: ") + icon.built);

	Canvas normalCanvas(size, CanvasScalingPolicy::IGNORE);
	Canvas builtCanvas(size, CanvasScalingPolicy::IGNORE);
	normalCanvas.drawColor(Rect(Point(0, 0), size), ColorRGBA(0, 0, 0, 0));
	builtCanvas.drawColor(Rect(Point(0, 0), size), ColorRGBA(0, 0, 0, 0));
	normalCanvas.draw(normal, Point(0, 0));
	builtCanvas.draw(built, Point(0, 0));

	size_t changedPixels = 0;
	int minX = size.x;
	int minY = size.y;
	int maxX = -1;
	int maxY = -1;
	for(int y = 0; y < size.y; ++y)
	{
		for(int x = 0; x < size.x; ++x)
		{
			const Point pixel(x, y);
			if(normalCanvas.getPixel(pixel) != builtCanvas.getPixel(pixel))
			{
				++changedPixels;
				minX = std::min(minX, x);
				minY = std::min(minY, y);
				maxX = std::max(maxX, x);
				maxY = std::max(maxY, y);
			}
		}
	}
	require(changedPixels > 0, std::string("Built badge did not change pixels for ") + icon.built);
	require(minX >= size.x - 17 && minY >= size.y - 16,
		std::string("Built badge escaped its expected lower-right region: ") + icon.built);
	require(changedPixels * 10 < static_cast<size_t>(size.x * size.y),
		std::string("Built icon changed more than ten percent of its source pixels: ") + icon.built);
	std::cout << "  " << icon.built << ": " << changedPixels << " badge-different pixels in "
		<< minX << ',' << minY << ".." << maxX << ',' << maxY << '\n';
}

void drawImage(Canvas & canvas, const std::shared_ptr<IImage> & image, const Point & size)
{
	require(image != nullptr, "Could not load image needed for Academy portrait composition check");
	require(image->dimensions() == size, "Academy portrait input has unexpected native geometry");
	canvas.drawColor(Rect(Point(0, 0), size), ColorRGBA(0, 0, 0, 0));
	canvas.draw(image, Point(0, 0));
}

ImagePath mountedSpritePath(const ImagePath & image)
{
	const std::string name = image.getOriginalName();
	if(name.rfind("SPRITES/", 0) == 0)
		return image;
	return ImagePath::builtin("SPRITES/" + name);
}

std::vector<ColorRGBA> captureImagePixels(const std::shared_ptr<IImage> & image, const Point & expectedSize,
		const std::string & description)
{
	require(image != nullptr, "Could not load " + description);
	require(image->dimensions() == expectedSize, description + " has unexpected native dimensions");
	Canvas canvas(expectedSize, CanvasScalingPolicy::IGNORE);
	canvas.drawColor(Rect(Point(0, 0), expectedSize), ColorRGBA(0, 0, 0, 0));
	canvas.draw(image, Point(0, 0));

	std::vector<ColorRGBA> pixels;
	pixels.reserve(static_cast<size_t>(expectedSize.x * expectedSize.y));
	for(int y = 0; y < expectedSize.y; ++y)
		for(int x = 0; x < expectedSize.x; ++x)
			pixels.push_back(canvas.getPixel(Point(x, y)));
	return pixels;
}

std::vector<ColorRGBA> captureScaledImagePixels(const std::shared_ptr<IImage> & image, const Point & nativeSize,
		int scale, const std::string & description)
{
	require(image != nullptr, "Could not load " + description);
	require(image->dimensions() == nativeSize, description + " has unexpected native dimensions");
	const Point renderedSize = nativeSize * scale;
	Canvas canvas(nativeSize, CanvasScalingPolicy::AUTO);
	canvas.drawColor(Rect(Point(0, 0), nativeSize), ColorRGBA(0, 0, 0, 0));
	canvas.draw(image, Point(0, 0));

	std::vector<ColorRGBA> pixels;
	pixels.reserve(static_cast<size_t>(renderedSize.x * renderedSize.y));
	for(int y = 0; y < renderedSize.y; ++y)
		for(int x = 0; x < renderedSize.x; ++x)
			pixels.push_back(canvas.getPixel(Point(x, y)));
	return pixels;
}

using IndexedFrame = magiPaletteInspection::detail::Frame;

struct PaletteRemapFixture
{
	IndexedFrame source;
	PaletteRemap red;
	PaletteRemap blue;
};

bool isGreenPaletteColor(const magiPaletteInspection::detail::Rgb & color)
{
	return color.g > color.r + 12 && color.g > color.b + 12;
}

PaletteRemapFixture makeMagiPaletteRemapFixture()
{
	CDefFile definition(AnimationPath::builtin("SPRITES/PMAGEX.DEF"));
	require(definition.hasFrame(0, 0), "PMAGEX must have a first projectile frame for palette-remap checks");
	magiPaletteInspection::detail::FrameCapture loader;
	definition.loadFrame(0, 0, loader);
	PaletteRemapFixture fixture;
	fixture.source = loader.take();
	require(fixture.source.width > 0 && fixture.source.height > 0
		&& fixture.source.indices.size() == static_cast<size_t>(fixture.source.width * fixture.source.height),
		"PMAGEX indexed frame capture returned invalid source geometry");

	std::array<size_t, 256> populations{};
	for(int y = fixture.source.dataY; y < fixture.source.dataY + fixture.source.dataHeight; ++y)
	{
		for(int x = fixture.source.dataX; x < fixture.source.dataX + fixture.source.dataWidth; ++x)
		{
			const size_t offset = static_cast<size_t>(y * fixture.source.width + x);
			++populations[fixture.source.indices[offset]];
		}
	}

	for(size_t index = 8; index < populations.size(); ++index)
	{
		const auto & original = fixture.source.palette[index];
		if(populations[index] == 0 || !isGreenPaletteColor(original))
			continue;

		// Use only in-memory test mappings. These are conspicuously different so
		// alias-cache contamination or a late/no-op remap is easy to detect.
		fixture.red.emplace(static_cast<uint8_t>(index), std::array<uint8_t, 3>{{255, 24, 24}});
		fixture.blue.emplace(static_cast<uint8_t>(index), std::array<uint8_t, 3>{{24, 64, 255}});
	}
	require(!fixture.red.empty(), "PMAGEX frame 0 must contain used green palette entries to exercise recoloring");
	return fixture;
}

std::string paletteRemapJson(const PaletteRemap & paletteRemap)
{
	std::ostringstream json;
	json << R"({"defFile":"SPRITES/PMAGEX.DEF","defFrame":0,"defGroup":0,"paletteRemap":{)";
	bool first = true;
	for(const auto & [index, color] : paletteRemap)
	{
		if(!first)
			json << ',';
		first = false;
		json << '"' << static_cast<unsigned>(index) << "\":["
			<< static_cast<unsigned>(color[0]) << ','
			<< static_cast<unsigned>(color[1]) << ','
			<< static_cast<unsigned>(color[2]) << ']';
	}
	json << "}}";
	return json.str();
}

ImageLocator parsePaletteRemapLocator(const std::string & jsonText)
{
	JsonNode json(jsonText.c_str(), jsonText.size(), "palette-remap-runtime-test.json");
	return ImageLocator(json, EImageBlitMode::COLORKEY);
}

void requirePaletteRemapJsonRejected(const std::string & mapText, const std::string & description)
{
	const std::string jsonText = R"({"defFile":"SPRITES/PMAGEX.DEF","defFrame":0,"defGroup":0,"paletteRemap":)"
		+ mapText + '}';
	bool rejected = false;
	try
	{
		(void)parsePaletteRemapLocator(jsonText);
	}
	catch(const std::exception &)
	{
		rejected = true;
	}
	require(rejected, "Invalid palette remap JSON was accepted: " + description);
}

void verifyPaletteRemapJsonValidation()
{
	requirePaletteRemapJsonRejected(R"({"7":[1,2,3]})", "reserved palette index 7");
	requirePaletteRemapJsonRejected(R"({"256":[1,2,3]})", "palette index above 255");
	requirePaletteRemapJsonRejected(R"({"not-an-index":[1,2,3]})", "noninteger palette index");
	requirePaletteRemapJsonRejected(R"({"32":[1.5,2,3]})", "noninteger RGB channel");
	requirePaletteRemapJsonRejected(R"({"32":[1,2,256]})", "RGB channel above 255");
	requirePaletteRemapJsonRejected(R"({"32":[1,2]})", "non-RGB tuple");
}

struct VisiblePixelBounds
{
	int left = std::numeric_limits<int>::max();
	int top = std::numeric_limits<int>::max();
	int right = -1;
	int bottom = -1;

	bool operator==(const VisiblePixelBounds &) const = default;
};

VisiblePixelBounds visiblePixelBounds(const std::vector<ColorRGBA> & pixels, const Point & size)
{
	VisiblePixelBounds bounds;
	for(int y = 0; y < size.y; ++y)
	{
		for(int x = 0; x < size.x; ++x)
		{
			if(pixels[static_cast<size_t>(y * size.x + x)].a == 0)
				continue;
			bounds.left = std::min(bounds.left, x);
			bounds.top = std::min(bounds.top, y);
			bounds.right = std::max(bounds.right, x);
			bounds.bottom = std::max(bounds.bottom, y);
		}
	}
	return bounds;
}

int channelValue(const ColorRGBA & pixel, uint8_t channel)
{
	switch(channel)
	{
		case 0: return pixel.r;
		case 1: return pixel.g;
		case 2: return pixel.b;
		default: throw std::logic_error("Invalid RGB channel in palette-remap check");
	}
}

int64_t channelDifference(const std::vector<ColorRGBA> & pixels, uint8_t positive, uint8_t negative)
{
	int64_t total = 0;
	for(const ColorRGBA & pixel : pixels)
	{
		if(pixel.a != 0)
			total += channelValue(pixel, positive) - channelValue(pixel, negative);
	}
	return total;
}

size_t dominantPixelCount(const std::vector<ColorRGBA> & pixels, uint8_t positive, uint8_t negative)
{
	return static_cast<size_t>(std::count_if(pixels.begin(), pixels.end(), [positive, negative](const ColorRGBA & pixel)
	{
		return pixel.a != 0 && channelValue(pixel, positive) > channelValue(pixel, negative) + 16;
	}));
}

void verifyMagiPaletteRemap(IRenderHandler & renderer, const PaletteRemapFixture & fixture, int scale)
{
	ImageLocator originalLocator(AnimationPath::builtin("SPRITES/PMAGEX.DEF"), 0, 0, EImageBlitMode::COLORKEY);
	originalLocator.originalDefFrame = true;
	ImageLocator redLocator = parsePaletteRemapLocator(paletteRemapJson(fixture.red));
	ImageLocator blueLocator = parsePaletteRemapLocator(paletteRemapJson(fixture.blue));
	require(redLocator.originalDefFrame && blueLocator.originalDefFrame,
		"A mapped DEF frame must force exact-original-frame loading");
	require(redLocator.paletteRemap == fixture.red && blueLocator.paletteRemap == fixture.blue,
		"Palette remap JSON did not preserve its authored mappings");

	std::set<SharedImageLocator> cacheKeys;
	cacheKeys.insert(originalLocator);
	cacheKeys.insert(redLocator);
	cacheKeys.insert(blueLocator);
	require(cacheKeys.size() == 3,
		"Unmapped and distinct palette-remapped DEF frames must have independent image-cache keys");

	const Point nativeSize(fixture.source.width, fixture.source.height);
	const Point renderedSize = nativeSize * scale;
	auto originalBefore = renderer.loadImage(originalLocator);
	ENGINE->async().wait();
	const auto originalPixels = captureScaledImagePixels(originalBefore, nativeSize, scale, "original PMAGEX frame before maps");
	auto red = renderer.loadImage(redLocator);
	auto blue = renderer.loadImage(blueLocator);

	std::map<size_t, std::vector<ImageLocator>> layout;
	layout[0].push_back(redLocator);
	CAnimation flipped(AnimationPath::builtin("SPRITES/PMAGEX.DEF"), std::move(layout), EImageBlitMode::COLORKEY);
	flipped.createFlippedGroup(0, 1);
	const ImageLocator flippedLocator = flipped.getImageLocator(0, 1);
	require(flippedLocator.paletteRemap == fixture.red && flippedLocator.verticalFlip,
		"A flipped animation group must retain its frame palette map and flip flag");
	auto flippedRed = flipped.getImage(0, 1, true);
	auto originalAfter = renderer.loadImage(originalLocator);
	ENGINE->async().wait();

	const auto redPixels = captureScaledImagePixels(red, nativeSize, scale, "red-remapped PMAGEX frame");
	const auto bluePixels = captureScaledImagePixels(blue, nativeSize, scale, "blue-remapped PMAGEX frame");
	const auto originalAfterPixels = captureScaledImagePixels(originalAfter, nativeSize, scale, "original PMAGEX frame after maps");
	const auto flippedPixels = captureScaledImagePixels(flippedRed, nativeSize, scale, "flipped red-remapped PMAGEX frame");
	require(originalPixels == originalAfterPixels,
		"Loading palette-remapped aliases must not mutate or contaminate the original PMAGEX frame");
	require(visiblePixelBounds(originalPixels, renderedSize) == visiblePixelBounds(redPixels, renderedSize)
		&& visiblePixelBounds(originalPixels, renderedSize) == visiblePixelBounds(bluePixels, renderedSize),
		"Palette remapping must preserve the rendered projectile's visible bounds");

	for(int y = 0; y < renderedSize.y; ++y)
	{
		for(int x = 0; x < renderedSize.x; ++x)
		{
			const size_t offset = static_cast<size_t>(y * renderedSize.x + x);
			const size_t flipOffset = static_cast<size_t>(y * renderedSize.x + (renderedSize.x - 1 - x));
			require(flippedPixels[offset] == redPixels[flipOffset],
				"Flipped animation group must render the same mapped pixels with left/right orientation reversed");
		}
	}

	if(scale == 1)
	{
		std::array<bool, 256> mapped{};
		for(const auto & [index, color] : fixture.red)
			mapped[index] = true;

		size_t mappedSourcePixels = 0;
		size_t unchangedSourcePixels = 0;
		for(int y = 0; y < fixture.source.height; ++y)
		{
			for(int x = 0; x < fixture.source.width; ++x)
			{
				const size_t offset = static_cast<size_t>(y * fixture.source.width + x);
				const uint8_t index = fixture.source.indices[offset];
				if(mapped[index])
				{
					++mappedSourcePixels;
					const auto & redColor = fixture.red.at(index);
					const auto & blueColor = fixture.blue.at(index);
					const ColorRGBA redExpected(redColor[0], redColor[1], redColor[2], originalPixels[offset].a);
					const ColorRGBA blueExpected(blueColor[0], blueColor[1], blueColor[2], originalPixels[offset].a);
					require(redPixels[offset] == redExpected && bluePixels[offset] == blueExpected,
						"Mapped PMAGEX palette pixels must use the requested RGB while preserving source alpha");
				}
				else
				{
					++unchangedSourcePixels;
					require(redPixels[offset] == originalPixels[offset] && bluePixels[offset] == originalPixels[offset],
						"Unmapped PMAGEX pixels, including structural palette indices 0–7, must remain unchanged at 1x");
				}
			}
		}
		require(mappedSourcePixels > 0 && unchangedSourcePixels > 0,
			"Palette-remap check must cover both mapped and unchanged source pixels");
	}
	else
	{
		// xBRZ interpolates color at enlarged edges, so compare color trends and
		// geometry at output scale instead of assuming source-index-to-output-pixel
		// identity. Green source entries should now produce an observable red/blue
		// signal without changing the projectile footprint.
		require(channelDifference(redPixels, 0, 1) > channelDifference(originalPixels, 0, 1)
			&& dominantPixelCount(redPixels, 0, 1) > 0,
			"Red palette mapping must remain visible after xBRZ upscaling");
		require(channelDifference(bluePixels, 2, 1) > channelDifference(originalPixels, 2, 1)
			&& dominantPixelCount(bluePixels, 2, 1) > 0,
			"Blue palette mapping must remain visible after xBRZ upscaling");
	}
	std::cout << "  PMAGEX palette map/cache/flip at " << scale << "x preserves source and geometry\n";
}

void verifyCabirApproachImage(const ImagePath & configuredPath, const char * expectedImageName, const char * side)
{
	const ImagePath expectedPath = ImagePath::builtin(expectedImageName);
	require(configuredPath == expectedPath,
		std::string("Unexpected Cabir map-encounter image binding from ") + side + " approach");
	const ImagePath mountedPath = mountedSpritePath(configuredPath);
	require(CResourceHandler::get()->existsResource(mountedPath),
		std::string("Cabir map-encounter image resource is missing: ") + expectedImageName);
	const auto image = ENGINE->renderHandler().loadImage(mountedPath, EImageBlitMode::SIMPLE);
	const std::vector<ColorRGBA> pixels = captureImagePixels(image, Point(64, 64), expectedImageName);
	require(std::any_of(pixels.begin(), pixels.end(), [](const ColorRGBA & pixel) { return pixel.a != 0; }),
		std::string("Cabir map-encounter image is blank: ") + expectedImageName);
}

void verifyCabirAdventureMap(const CCreature & creature, const char * mapDescriptor,
		const char * leftEncounterImage, const char * rightEncounterImage)
{
	const auto handler = LIBRARY->objtypeh->getHandlerFor(Obj::MONSTER, creature.getId().num);
	require(handler != nullptr, "Cabir monster object handler is missing");
	const auto templates = handler->getTemplates();
	require(templates.size() == 1, "Cabir monster must resolve to one custom adventure-map template");
	require(templates.front() != nullptr, "Cabir monster template is null");
	const auto & objectTemplate = *templates.front();
	require(objectTemplate.id == Obj::MONSTER && objectTemplate.subid == creature.getId().num,
		"Cabir adventure-map template is not bound to its creature identity");
	require(objectTemplate.animationFile == AnimationPath::builtin(mapDescriptor),
		std::string("Cabir monster template does not use map descriptor ") + mapDescriptor);
	require(objectTemplate.getWidth() == 2 && objectTemplate.getHeight() == 2,
		"Cabir monster template must preserve its 2x2 adventure-map footprint");
	require(objectTemplate.isVisitable(), "Cabir monster template must remain visitable");

	for(int y = 0; y < 2; ++y)
	{
		for(int x = 0; x < 2; ++x)
		{
			const bool anchor = x == 0 && y == 0; // readJson reverses the declared mask into bottom-right-relative coordinates
			require(objectTemplate.isVisibleAt(x, y), "Cabir mapMask must keep every footprint tile visible");
			require(objectTemplate.isBlockedAt(x, y) == anchor
				&& objectTemplate.isVisitableAt(x, y) == anchor,
				"Cabir mapMask [VV, VA] must block and mark only the A anchor visitable");
		}
	}

	for(int dy = -1; dy <= 1; ++dy)
	{
		for(int dx = -1; dx <= 1; ++dx)
		{
			if(dx != 0 || dy != 0)
				require(objectTemplate.isVisitableFrom(dx, dy),
					"Cabir inherited visitableFrom must allow all eight surrounding approach tiles");
		}
	}

	auto & renderer = ENGINE->renderHandler();
	const auto mapAnimation = renderer.loadAnimation(objectTemplate.animationFile, EImageBlitMode::WITH_SHADOW);
	require(mapAnimation != nullptr && mapAnimation->size(0) >= 4,
		std::string("Cabir map descriptor must have at least four group-0 frames: ") + mapDescriptor);

	std::vector<std::vector<ColorRGBA>> distinctFrames;
	std::array<ImagePath, 4> mapFramePaths;
	for(size_t frame = 0; frame < 4; ++frame)
	{
		const auto locator = mapAnimation->getImageLocator(frame, 0);
		require(locator.image.has_value()
			&& CResourceHandler::get()->existsResource(mountedSpritePath(*locator.image)),
			std::string("Cabir map descriptor frame resource is missing: ") + mapDescriptor
			+ " frame " + std::to_string(frame));
		mapFramePaths[frame] = *locator.image;
		const auto pixels = captureImagePixels(mapAnimation->getImage(frame, 0, true), Point(64, 64),
			std::string(mapDescriptor) + " group-0 frame " + std::to_string(frame));
		require(std::any_of(pixels.begin(), pixels.end(), [](const ColorRGBA & pixel) { return pixel.a != 0; }),
			std::string("Cabir map frame is blank: ") + mapDescriptor + " frame " + std::to_string(frame));
		for(const auto & prior : distinctFrames)
			require(pixels != prior,
				std::string("Cabir map animation repeats a placeholder frame: ") + mapDescriptor);
		distinctFrames.push_back(pixels);
	}

	const char * compatibilityDescriptor = creature.getIndex() == 28 ? "AVWgrem0" : "AVWgrex0";
	const auto compatibilityAnimation = renderer.loadAnimation(
		AnimationPath::builtin(compatibilityDescriptor), EImageBlitMode::WITH_SHADOW);
	require(compatibilityAnimation && compatibilityAnimation->size(0) == 8,
		std::string("Cabir compatibility descriptor must preserve the legacy eight-frame group 0: ")
			+ compatibilityDescriptor);
	for(size_t frame = 0; frame < 8; ++frame)
	{
		const size_t sourceFrame = frame % mapFramePaths.size();
		const auto locator = compatibilityAnimation->getImageLocator(frame, 0);
		require(locator.image.has_value() && *locator.image == mapFramePaths[sourceFrame]
			&& CResourceHandler::get()->existsResource(mountedSpritePath(*locator.image)),
			std::string("Cabir compatibility frame does not reuse the unique map resource: ")
				+ compatibilityDescriptor + " frame " + std::to_string(frame));
		const auto pixels = captureImagePixels(compatibilityAnimation->getImage(frame, 0, true), Point(64, 64),
			std::string(compatibilityDescriptor) + " group-0 frame " + std::to_string(frame));
		require(pixels == distinctFrames[sourceFrame],
			std::string("Cabir compatibility alias changed its source map pixels: ") + compatibilityDescriptor
			+ " frame " + std::to_string(frame));
	}

	verifyCabirApproachImage(creature.mapAttackFromLeft, leftEncounterImage, "left");
	verifyCabirApproachImage(creature.mapAttackFromRight, rightEncounterImage, "right");
	require(creature.mapAttackFromLeft != creature.mapAttackFromRight,
		"Cabir left/right encounter images must be separately bound");
	std::cout << "  " << mapDescriptor << ": 2x2 visitable template, eight approaches, four distinct map frames, legacy alias and left/right encounter images\n";
}

void verifyAcademyPortrait(const AcademyPortrait & portrait)
{
	const auto creatureId = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "creature", std::string(portrait.identifier));
	require(creatureId.has_value(), std::string("Could not resolve core:") + portrait.identifier);
	const auto * creature = LIBRARY->creh->objects.at(static_cast<size_t>(*creatureId)).get();
	require(creature != nullptr, std::string("Missing creature core:") + portrait.identifier);
	require(creature->getIndex() == portrait.internalId,
		std::string("Creature ID ordering changed unexpectedly for core:") + portrait.identifier);
	require(creature->largeIconName == portrait.image,
		std::string("Creature large icon is not bound to authored route: core:") + portrait.identifier);

	auto & renderer = ENGINE->renderHandler();
	if(portrait.smallImage)
	{
		require(creature->smallIconName == portrait.smallImage,
			std::string("Creature small icon is not bound to authored route: core:") + portrait.identifier);

		const auto verifyBinding = [&](const char * imageName, const char * aliasName, const Point & expectedSize,
				EImageBlitMode mode)
		{
			const ImagePath iconPath = mountedSpritePath(ImagePath::builtin(imageName));
			require(CResourceHandler::get()->existsResource(iconPath),
				std::string("Authored creature icon resource is missing: ") + imageName);
			const auto authored = renderer.loadImage(iconPath, mode);
			const auto alias = renderer.loadImage(AnimationPath::builtin(aliasName), creature->getIconIndex(), 0, mode);
			require(authored && alias, std::string("Could not load authored creature icon: ") + imageName);
			require(authored->dimensions() == expectedSize && alias->dimensions() == expectedSize,
				std::string("Authored creature icon has unexpected native geometry: ") + imageName);

			Canvas authoredCanvas(expectedSize, CanvasScalingPolicy::IGNORE);
			Canvas aliasCanvas(expectedSize, CanvasScalingPolicy::IGNORE);
			authoredCanvas.drawColor(Rect(Point(0, 0), expectedSize), ColorRGBA(0, 0, 0, 0));
			aliasCanvas.drawColor(Rect(Point(0, 0), expectedSize), ColorRGBA(0, 0, 0, 0));
			authoredCanvas.draw(authored, Point(0, 0));
			aliasCanvas.draw(alias, Point(0, 0));

			const ColorRGBA firstPixel = authoredCanvas.getPixel(Point(0, 0));
			size_t nonuniformPixels = 0;
			for(int y = 0; y < expectedSize.y; ++y)
			{
				for(int x = 0; x < expectedSize.x; ++x)
				{
					const Point pixel(x, y);
					const ColorRGBA authoredPixel = authoredCanvas.getPixel(pixel);
					require(aliasCanvas.getPixel(pixel) == authoredPixel,
						std::string("Creature icon alias differs from authored pixels: ") + imageName);
					if(authoredPixel != firstPixel)
						++nonuniformPixels;
				}
			}
			require(nonuniformPixels > 0, std::string("Authored creature icon is blank: ") + imageName);
		};

		verifyBinding(portrait.image, "TWCRPORT", Point(58, 64), EImageBlitMode::OPAQUE);
		verifyBinding(portrait.smallImage, "CPRSMALL", Point(32, 32), EImageBlitMode::COLORKEY);
		std::cout << "  " << portrait.identifier << ": authored large/small icons bound at 58x64 and 32x32\n";
		return;
	}

	if(std::string_view(portrait.identifier) == "archMage")
		require(creature->smallIconName == "NH_ArchMageGreySmall:0:0",
			"Arch Mage must route its small icon through the palette-mapped CPRSMALL frame reference");
	else
		require(creature->smallIconName.empty(),
			std::string("Creature small icon must remain on its original CPRSMALL frame: core:") + portrait.identifier);

	constexpr Point size(58, 64);
	const auto generated = renderer.loadImage(ImagePath::builtin(portrait.image), EImageBlitMode::SIMPLE);
	const auto backdrop = renderer.loadImage(
		ImagePath::builtin("NH_academy_creature_portrait_backdrop.png"), EImageBlitMode::SIMPLE);
	const auto matte = renderer.loadImage(ImagePath::builtin(portrait.mask), EImageBlitMode::SIMPLE);
	const AnimationPath portraitSource = portrait.sourceAnimation
		? AnimationPath::builtin(portrait.sourceAnimation)
		: AnimationPath::builtin("TWCRPORT");
	const int portraitSourceFrame = portrait.sourceFrame >= 0 ? portrait.sourceFrame : portrait.defFrame;
	if(portrait.sourceAnimation)
	{
		const JsonPath sourceDescriptor = portraitSource.addPrefix("SPRITES/").toType<EResType::JSON>();
		require(CResourceHandler::get()->existsResource(sourceDescriptor),
			std::string("Mapped portrait animation descriptor is missing: ") + portrait.sourceAnimation);
	}
	ImageLocator sourceLocator;
	if(portrait.sourceAnimation)
	{
		const auto sourceAnimation = renderer.loadAnimation(portraitSource, EImageBlitMode::OPAQUE);
		require(sourceAnimation && sourceAnimation->size(0) > static_cast<size_t>(portraitSourceFrame),
			std::string("Portrait source animation/frame is missing: ") + portraitSource.getOriginalName());
		sourceLocator = sourceAnimation->getImageLocator(portraitSourceFrame, 0);
		require(sourceLocator.defFile.has_value()
			&& sourceLocator.defFile->getOriginalName().find("TWCRPORT") != std::string::npos
			&& sourceLocator.defFrame == portrait.defFrame && sourceLocator.defGroup == 0
			&& !sourceLocator.paletteRemap.empty(),
			"Arch Mage portrait alias must remap its authored TWCRPORT source frame through its palette map");
	}
	else
	{
		sourceLocator = ImageLocator(portraitSource, portraitSourceFrame, 0, EImageBlitMode::OPAQUE);
	}
	sourceLocator.scalingFactor = 1;
	sourceLocator.originalDefFrame = true;
	const auto original = renderer.loadImage(sourceLocator);

	Canvas generatedCanvas(size, CanvasScalingPolicy::IGNORE);
	Canvas backdropCanvas(size, CanvasScalingPolicy::IGNORE);
	Canvas matteCanvas(size, CanvasScalingPolicy::IGNORE);
	Canvas originalCanvas(size, CanvasScalingPolicy::IGNORE);
	drawImage(generatedCanvas, generated, size);
	drawImage(backdropCanvas, backdrop, size);
	drawImage(matteCanvas, matte, size);
	drawImage(originalCanvas, original, size);

	size_t foregroundPixels = 0;
	size_t backdropPixels = 0;
	for(int y = 0; y < size.y; ++y)
	{
		for(int x = 0; x < size.x; ++x)
		{
			const Point pixel(x, y);
			const ColorRGBA mask = matteCanvas.getPixel(pixel);
			require(mask.r == mask.g && mask.r == mask.b && (mask.r == 0 || mask.r == 255),
				std::string("Portrait matte is not binary grayscale: ") + portrait.mask);
			if(mask.r == 255)
			{
				++foregroundPixels;
				require(generatedCanvas.getPixel(pixel) == originalCanvas.getPixel(pixel),
					std::string("Generated portrait changed an original subject pixel: ") + portrait.image);
			}
			else
			{
				++backdropPixels;
				require(generatedCanvas.getPixel(pixel) == backdropCanvas.getPixel(pixel),
					std::string("Generated portrait has a non-authored background pixel: ") + portrait.image);
			}
		}
	}
	require(foregroundPixels > 0 && backdropPixels > 0, "Portrait matte must select both source and authored background");

	const auto remappedLarge = renderer.loadImage(AnimationPath::builtin("TWCRPORT"), creature->getIconIndex(), 0,
		EImageBlitMode::OPAQUE);
	Canvas remappedCanvas(size, CanvasScalingPolicy::IGNORE);
	drawImage(remappedCanvas, remappedLarge, size);
	for(int y = 0; y < size.y; ++y)
		for(int x = 0; x < size.x; ++x)
			require(remappedCanvas.getPixel(Point(x, y)) == generatedCanvas.getPixel(Point(x, y)),
				std::string("TWCRPORT creature alias did not route to generated portrait: ") + portrait.identifier);

	ImageLocator originalSmallLocator(AnimationPath::builtin("CPRSMALL"), creature->getIconIndex(), 0,
		EImageBlitMode::COLORKEY);
	originalSmallLocator.scalingFactor = 1;
	originalSmallLocator.originalDefFrame = true;
	const auto originalSmall = renderer.loadImage(originalSmallLocator);
	const auto mappedSmall = renderer.loadImage(AnimationPath::builtin("CPRSMALL"), creature->getIconIndex(), 0,
		EImageBlitMode::COLORKEY);
	require(originalSmall && mappedSmall && originalSmall->dimensions() == Point(32, 32)
		&& mappedSmall->dimensions() == Point(32, 32), "Original CPRSMALL creature icon is missing or changed geometry");
	std::shared_ptr<IImage> expectedSmall = originalSmall;
	if(std::string_view(portrait.identifier) == "archMage")
	{
		expectedSmall = renderer.loadImage(ImagePath::builtin(creature->smallIconName), EImageBlitMode::COLORKEY);
		require(expectedSmall && expectedSmall->dimensions() == Point(32, 32),
			"Arch Mage palette-mapped CPRSMALL reference is missing or changed geometry");
	}
	Canvas originalSmallCanvas(Point(32, 32), CanvasScalingPolicy::IGNORE);
	Canvas mappedSmallCanvas(Point(32, 32), CanvasScalingPolicy::IGNORE);
	Canvas expectedSmallCanvas(Point(32, 32), CanvasScalingPolicy::IGNORE);
	drawImage(originalSmallCanvas, originalSmall, Point(32, 32));
	drawImage(mappedSmallCanvas, mappedSmall, Point(32, 32));
	drawImage(expectedSmallCanvas, expectedSmall, Point(32, 32));
	size_t transparentSmallPixels = 0;
	for(int y = 0; y < 32; ++y)
	{
		for(int x = 0; x < 32; ++x)
		{
			const Point pixel(x, y);
			const ColorRGBA sourcePixel = originalSmallCanvas.getPixel(pixel);
			const ColorRGBA expected = expectedSmallCanvas.getPixel(pixel);
			if(sourcePixel.a < 255)
				++transparentSmallPixels;
			require(mappedSmallCanvas.getPixel(pixel) == expected,
				std::string("CPRSMALL registered icon did not match its source or authored alias for core:")
				+ portrait.identifier);
		}
	}
	require(transparentSmallPixels > 0, "CPRSMALL source is expected to remain a transparent cutout");
	std::cout << "  " << portrait.identifier << ": " << foregroundPixels << " preserved subject pixels, "
		<< backdropPixels << " authored backdrop pixels; CPRSMALL registration verified\n";
}

bool cabirAnimationValidationRequested()
{
	const char * value = std::getenv("NH_VALIDATE_CABIR_ANIMATIONS");
	return value && std::string_view(value) == "1";
}

void verifyMagiProjectileColors()
{
	constexpr std::array<std::array<uint8_t, 3>, 5> archMageRgb{{
		{{192, 32, 24}},
		{{232, 48, 40}},
		{{255, 64, 48}},
		{{232, 48, 40}},
		{{192, 32, 24}},
	}};
	constexpr std::array<std::array<uint8_t, 2>, 5> archMageAlpha{{
		{{255, 64}},
		{{255, 128}},
		{{255, 255}},
		{{255, 128}},
		{{255, 64}},
	}};
	const auto getCreature = [](const char * identifier) -> const CCreature *
	{
		const auto creatureId = LIBRARY->identifiers()->getIdentifier(
			ModScope::scopeGame(), "creature", std::string(identifier));
		require(creatureId.has_value(), std::string("Could not resolve core:") + identifier);
		const size_t index = static_cast<size_t>(*creatureId);
		require(index < LIBRARY->creh->objects.size(),
			std::string("Creature identifier is out of range: core:") + identifier);
		return LIBRARY->creh->objects[index].get();
	};
	const auto * mage = getCreature("mage");
	const auto * archMage = getCreature("archMage");
	require(mage != nullptr && archMage != nullptr, "Loaded Tower Mage definitions are missing");
	require(mage->animDefName == AnimationPath::builtin("CMAGE.DEF"),
		"Mage must retain its ordinary CMAGE.DEF animation");
	require(mage->animation.projectileImageName == AnimationPath::builtin("NH_MageRedProjectile.def"),
		"Mage must use its palette-remapped PMAGEX projectile alias");
	require(archMage->animDefName == AnimationPath::builtin("NH_ArchMageGrey.def"),
		"Arch Mage must use its palette-remapped CAMAGE animation alias");
	require(mage->animation.projectileRay.empty(),
		"Mage must not inherit the Arch Mage procedural ray colour override");
	require(archMage->animation.attackClimaxFrame == 8,
		"Arch Mage procedural rays must retain the inherited attack climax frame 8");
	require(archMage->animation.projectileRay.size() == archMageRgb.size(),
		"Loaded Arch Mage must have exactly five procedural rays");

	for(size_t index = 0; index < archMageRgb.size(); ++index)
	{
		const RayColor & ray = archMage->animation.projectileRay[index];
		const auto & rgb = archMageRgb[index];
		const auto & alpha = archMageAlpha[index];
		require(ray.start == ColorRGBA(rgb[0], rgb[1], rgb[2], alpha[0]),
			"Loaded Arch Mage ray start color/alpha differs at ray " + std::to_string(index));
		require(ray.end == ColorRGBA(rgb[0], rgb[1], rgb[2], alpha[1]),
			"Loaded Arch Mage ray end color/alpha differs at ray " + std::to_string(index));
	}

	const std::array<const CCreature *, 2> magi{{mage, archMage}};
	const std::array<const char *, 2> identifiers{{"core:mage", "core:archMage"}};
	for(size_t index = 0; index < magi.size(); ++index)
	{
		const auto * creature = magi[index];
		require(!creature->hasBonusOfType(BonusType::NO_MELEE_PENALTY),
			std::string("Magi must retain the ordinary shooter melee penalty: ")
				+ identifiers[index]);
		require(creature->hasBonusOfType(BonusType::SHOOTER),
			"Magi must retain shooter status");
		require(creature->hasBonusOfType(BonusType::NO_DISTANCE_PENALTY),
			"Magi must retain their ranged-distance capability");
	}
	std::cout << "  Mage/Arch Mage: loaded palette-mapped aliases and five red Arch Mage rays\n";
}

AnimationPath normalizedAnimationSource(AnimationPath path)
{
	constexpr std::string_view spritePrefix = "SPRITES/";
	std::string name = path.getName();
	if(name.starts_with(spritePrefix))
		name.erase(0, spritePrefix.size());
	return AnimationPath::builtin(name);
}

AnimationPath animationPathInSprites(AnimationPath path)
{
	if(!path.getName().starts_with("SPRITES/"))
		path = path.addPrefix("SPRITES/");
	return path;
}

void verifyPaletteMap(const ImageLocator & locator, const std::string & description)
{
	require(locator.defFile.has_value() && locator.originalDefFrame && !locator.paletteRemap.empty(),
		description + " must use a palette-remapped original DEF frame locator");
	for(const auto & [index, color] : locator.paletteRemap)
	{
		(void)color;
		require(index >= 8, description + " must not remap reserved palette indices 0-7");
	}
}

std::shared_ptr<CAnimation> verifyCompletePaletteAlias(IRenderHandler & renderer,
	const char * aliasName, const char * sourceName)
{
	const AnimationPath aliasPath = AnimationPath::builtin(aliasName);
	const AnimationPath sourcePath = AnimationPath::builtin(sourceName);
	const JsonPath descriptorPath = aliasPath.addPrefix("SPRITES/").toType<EResType::JSON>();
	require(CResourceHandler::get()->existsResource(descriptorPath),
		std::string("Palette-remapped animation alias descriptor is missing: ") + aliasName);
	const auto alias = renderer.loadAnimation(aliasPath, EImageBlitMode::OPAQUE);
	const auto source = renderer.loadAnimation(sourcePath, EImageBlitMode::OPAQUE);
	require(alias && source, std::string("Could not load mapped animation alias or source: ") + aliasName);

	size_t totalFrames = 0;
	for(size_t group = 0; group < 64; ++group)
	{
		const size_t aliasFrames = alias->size(group);
		const size_t sourceFrames = source->size(group);
		require(aliasFrames == sourceFrames,
			std::string("Animation alias changed source group length: ") + aliasName
			+ " group " + std::to_string(group));
		for(size_t frame = 0; frame < aliasFrames; ++frame)
		{
			const ImageLocator locator = alias->getImageLocator(frame, group);
			require(locator.defFile.has_value()
				&& normalizedAnimationSource(*locator.defFile) == normalizedAnimationSource(sourcePath)
				&& locator.defGroup == static_cast<int>(group)
				&& locator.defFrame == static_cast<int>(frame),
				std::string("Animation alias changed source frame order: ") + aliasName
				+ " group " + std::to_string(group) + " frame " + std::to_string(frame));
			verifyPaletteMap(locator, std::string(aliasName) + " frame locator");
			++totalFrames;
		}
	}
	require(totalFrames > 0, std::string("Palette-remapped animation alias has no frames: ") + aliasName);
	std::cout << "  " << aliasName << ": " << totalFrames
		<< " source-matched frames across every group; all preserve reserved palette entries\n";
	return alias;
}

std::shared_ptr<CAnimation> verifyArchMagePortraitAlias(IRenderHandler & renderer)
{
	const AnimationPath aliasPath = AnimationPath::builtin("NH_ArchMageGreyPortrait");
	const JsonPath descriptorPath = aliasPath.addPrefix("SPRITES/").toType<EResType::JSON>();
	require(CResourceHandler::get()->existsResource(descriptorPath),
		"Arch Mage portrait alias JSON descriptor is missing");
	const auto alias = renderer.loadAnimation(aliasPath, EImageBlitMode::OPAQUE);
	require(alias && alias->size(0) == 1,
		"Arch Mage portrait alias must provide exactly its one authored frame");
	for(size_t group = 1; group < 64; ++group)
		require(alias->size(group) == 0, "Arch Mage portrait alias must not expose extra animation groups");

	const ImageLocator locator = alias->getImageLocator(0, 0);
	require(locator.defFile.has_value()
		&& normalizedAnimationSource(*locator.defFile) == AnimationPath::builtin("TWCRPORT.DEF")
		&& locator.defFrame == 37 && locator.defGroup == 0,
		"Arch Mage portrait alias must resolve to TWCRPORT frame 37");
	verifyPaletteMap(locator, "Arch Mage portrait alias");
	return alias;
}

void verifyMappedPalettePixels(IRenderHandler & renderer, const CAnimation & alias,
	size_t group, size_t frame, EImageBlitMode mode, const std::string & description, bool requireChangedPixel)
{
	ImageLocator mappedLocator = alias.getImageLocator(frame, group);
	verifyPaletteMap(mappedLocator, description);
	mappedLocator.layer = mode;
	mappedLocator.scalingFactor = 1;
	mappedLocator.originalDefFrame = true;
	ImageLocator sourceLocator(*mappedLocator.defFile, mappedLocator.defFrame, mappedLocator.defGroup, mode);
	sourceLocator.scalingFactor = 1;
	sourceLocator.originalDefFrame = true;

	const auto mappedImage = renderer.loadImage(mappedLocator);
	const auto sourceImage = renderer.loadImage(sourceLocator);
	require(mappedImage && sourceImage, description + " did not load both mapped and original source pixels");
	const Point nativeSize = sourceImage->dimensions();
	require(mappedImage->dimensions() == nativeSize, description + " changed source canvas dimensions");

	CDefFile sourceDef(animationPathInSprites(*mappedLocator.defFile));
	magiPaletteInspection::detail::FrameCapture capture;
	sourceDef.loadFrame(static_cast<size_t>(mappedLocator.defFrame), static_cast<size_t>(mappedLocator.defGroup), capture);
	const auto indexedFrame = capture.take();
	require(nativeSize == Point(indexedFrame.width, indexedFrame.height),
		description + " changed original DEF canvas geometry");
	const auto sourcePixels = captureImagePixels(sourceImage, nativeSize, description + " original");
	const auto mappedPixels = captureImagePixels(mappedImage, nativeSize, description + " mapped");
	bool changedPixel = false;
	size_t usedMappedEntries = 0;
	for(size_t offset = 0; offset < indexedFrame.indices.size(); ++offset)
	{
		ColorRGBA expected = sourcePixels[offset];
		const auto remap = mappedLocator.paletteRemap.find(indexedFrame.indices[offset]);
		if(remap != mappedLocator.paletteRemap.end())
		{
			++usedMappedEntries;
			expected = ColorRGBA(remap->second[0], remap->second[1], remap->second[2], expected.a);
			changedPixel = changedPixel || expected != sourcePixels[offset];
		}
		require(mappedPixels[offset] == expected,
			description + " runtime output did not apply only the authored palette map while preserving alpha");
	}
	if(requireChangedPixel)
		require(usedMappedEntries > 0 && changedPixel,
			description + " palette map did not change any used source color");
}

std::shared_ptr<CAnimation> verifyMappedFrameAlias(IRenderHandler & renderer, const char * aliasName,
	const char * sourceName, const std::vector<size_t> & sourceFrames, EImageBlitMode mode)
{
	const AnimationPath aliasPath = AnimationPath::builtin(aliasName);
	const AnimationPath sourcePath = AnimationPath::builtin(sourceName);
	const JsonPath descriptorPath = aliasPath.addPrefix("SPRITES/").toType<EResType::JSON>();
	require(CResourceHandler::get()->existsResource(descriptorPath),
		std::string("Mapped image alias descriptor is missing: ") + aliasName);
	const auto alias = renderer.loadAnimation(aliasPath, mode);
	require(alias && alias->size(0) == sourceFrames.size(),
		std::string("Mapped image alias has an unexpected frame count: ") + aliasName);
	for(size_t group = 1; group < 64; ++group)
		require(alias->size(group) == 0, std::string("Mapped image alias has an unexpected group: ") + aliasName);

	for(size_t frame = 0; frame < sourceFrames.size(); ++frame)
	{
		const ImageLocator locator = alias->getImageLocator(frame, 0);
		require(locator.defFile.has_value()
			&& normalizedAnimationSource(*locator.defFile) == normalizedAnimationSource(sourcePath)
			&& locator.defGroup == 0
			&& locator.defFrame == static_cast<int>(sourceFrames[frame]),
			std::string("Mapped image alias changed its original frame binding: ") + aliasName
			+ " frame " + std::to_string(frame));
		verifyPaletteMap(locator, std::string(aliasName) + " frame locator");
	}
	return alias;
}

std::shared_ptr<IImage> loadExactOriginalFrame(IRenderHandler & renderer, const AnimationPath & source,
	size_t frame, size_t group, EImageBlitMode mode)
{
	ImageLocator locator(source, static_cast<int>(frame), static_cast<int>(group), mode);
	locator.scalingFactor = 1;
	locator.originalDefFrame = true;
	return renderer.loadImage(locator);
}

void verifyOrdinaryFrameRemainsUnmapped(IRenderHandler & renderer, const AnimationPath & source,
	size_t frame, EImageBlitMode mode, const std::string & description)
{
	const auto exact = loadExactOriginalFrame(renderer, source, frame, 0, mode);
	const auto ordinary = renderer.loadImage(source, static_cast<int>(frame), 0, mode);
	require(exact && ordinary && exact->dimensions() == ordinary->dimensions(),
		description + " did not retain its original native canvas");
	require(captureImagePixels(exact, exact->dimensions(), description + " exact source")
		== captureImagePixels(ordinary, exact->dimensions(), description + " ordinary route"),
		description + " was affected by another creature's palette alias");
}

void verifyOrdinaryAnimationGroupRemainsUnmapped(IRenderHandler & renderer, const AnimationPath & source,
	size_t group, EImageBlitMode mode, const std::string & description)
{
	const auto animation = renderer.loadAnimation(source, mode);
	require(animation && animation->size(group) > 0, description + " has no source frames");
	for(size_t frame = 0; frame < animation->size(group); ++frame)
	{
		const auto exact = loadExactOriginalFrame(renderer, source, frame, group, mode);
		const auto ordinary = animation->getImage(frame, group, true);
		require(exact && ordinary && exact->dimensions() == ordinary->dimensions(),
			description + " changed native frame geometry at frame " + std::to_string(frame));
		require(captureImagePixels(exact, exact->dimensions(), description + " exact source")
			== captureImagePixels(ordinary, exact->dimensions(), description + " ordinary route"),
			description + " changed source pixels at frame " + std::to_string(frame));
	}
}

void verifyGeneratedImageMatchesAlias(IRenderHandler & renderer, const ImagePath & generatedPath,
	const CAnimation & alias, size_t frame, EImageBlitMode mode, const Point & expectedSize,
	const std::string & description)
{
	ImageLocator aliasLocator = alias.getImageLocator(frame, 0);
	aliasLocator.layer = mode;
	aliasLocator.scalingFactor = 1;
	aliasLocator.originalDefFrame = true;
	const auto expected = renderer.loadImage(aliasLocator);
	const auto generated = renderer.loadImage(generatedPath, mode);
	require(expected && generated && expected->dimensions() == expectedSize
		&& generated->dimensions() == expectedSize,
		description + " did not preserve its source frame geometry");
	const auto expectedPixels = captureImagePixels(expected, expectedSize, description + " mapped source");
	const auto generatedPixels = captureImagePixels(generated, expectedSize, description + " generated route");
	require(generatedPixels == expectedPixels,
		description + " generated image differs from the exact palette-mapped source frame");
}

void verifyArchMageSmallAndEncounterImages(IRenderHandler & renderer)
{
	const auto getCreature = [](const char * identifier) -> const CCreature *
	{
		const auto creatureId = LIBRARY->identifiers()->getIdentifier(
			ModScope::scopeGame(), "creature", std::string(identifier));
		require(creatureId.has_value(), std::string("Could not resolve core:") + identifier);
		return LIBRARY->creh->objects.at(static_cast<size_t>(*creatureId)).get();
	};
	const auto * mage = getCreature("mage");
	const auto * archMage = getCreature("archMage");
	require(mage && archMage, "Loaded Mage/Arch Mage definitions are missing for map/icon palette checks");
	require(mage->smallIconName.empty(), "Mage must retain its original CPRSMALL frame route");
	require(archMage->smallIconName == "NH_ArchMageGreySmall:0:0",
		"Arch Mage must use its palette-mapped small-icon frame reference");
	require(archMage->mapAttackFromRight == ImagePath::builtin("NH_ArchMageGreyEncounter:0:0")
		&& archMage->mapAttackFromLeft == ImagePath::builtin("NH_ArchMageGreyEncounter:0:1"),
		"Arch Mage must use palette-mapped right/left encounter frame references");

	const auto smallIconAlias = verifyMappedFrameAlias(renderer, "NH_ArchMageGreySmall", "CPRSMALL", {37},
		EImageBlitMode::COLORKEY);
	verifyMappedPalettePixels(renderer, *smallIconAlias, 0, 0, EImageBlitMode::COLORKEY,
		"Arch Mage small icon source frame", true);
	const auto archMageSmallSource = loadExactOriginalFrame(renderer, AnimationPath::builtin("CPRSMALL"), 37, 0,
		EImageBlitMode::COLORKEY);
	require(archMageSmallSource != nullptr, "Arch Mage original CPRSMALL frame 37 is missing");
	verifyGeneratedImageMatchesAlias(renderer, ImagePath::builtin("CPRSMALL:0:37"),
		*smallIconAlias, 0, EImageBlitMode::COLORKEY, archMageSmallSource->dimensions(),
		"Arch Mage registered CPRSMALL icon");

	const auto encounterAlias = verifyMappedFrameAlias(renderer, "NH_ArchMageGreyEncounter", "AvWattak", {70, 71},
		EImageBlitMode::SIMPLE);
	verifyMappedPalettePixels(renderer, *encounterAlias, 0, 0, EImageBlitMode::SIMPLE,
		"Arch Mage right encounter source frame", true);
	verifyMappedPalettePixels(renderer, *encounterAlias, 0, 1, EImageBlitMode::SIMPLE,
		"Arch Mage left encounter source frame", true);
	const auto rightSource = loadExactOriginalFrame(renderer, AnimationPath::builtin("AvWattak"), 70, 0,
		EImageBlitMode::SIMPLE);
	const auto leftSource = loadExactOriginalFrame(renderer, AnimationPath::builtin("AvWattak"), 71, 0,
		EImageBlitMode::SIMPLE);
	require(rightSource && leftSource, "Arch Mage original encounter source frames are missing");
	verifyGeneratedImageMatchesAlias(renderer, archMage->mapAttackFromRight, *encounterAlias, 0,
		EImageBlitMode::SIMPLE, rightSource->dimensions(), "Arch Mage right encounter image");
	verifyGeneratedImageMatchesAlias(renderer, archMage->mapAttackFromLeft, *encounterAlias, 1,
		EImageBlitMode::SIMPLE, leftSource->dimensions(), "Arch Mage left encounter image");

	verifyOrdinaryFrameRemainsUnmapped(renderer, AnimationPath::builtin("CPRSMALL"), 36,
		EImageBlitMode::COLORKEY, "Mage CPRSMALL frame 36");
	verifyOrdinaryFrameRemainsUnmapped(renderer, AnimationPath::builtin("AvWattak"), 68,
		EImageBlitMode::SIMPLE, "Mage left AvWattak frame 68");
	verifyOrdinaryFrameRemainsUnmapped(renderer, AnimationPath::builtin("AvWattak"), 69,
		EImageBlitMode::SIMPLE, "Mage right AvWattak frame 69");
	std::cout << "  Arch Mage small icon and encounter images: exact mapped source frames, native geometry/alpha, Mage frames unchanged\n";
}

void verifyArchMageAdventureMap()
{
	const auto archMageId = LIBRARY->identifiers()->getIdentifier(
		ModScope::scopeGame(), "creature", std::string("archMage"));
	require(archMageId.has_value(), "Could not resolve core:archMage for its adventure-map template");
	const auto * archMage = LIBRARY->creh->objects.at(static_cast<size_t>(*archMageId)).get();
	require(archMage != nullptr && archMage->getIndex() == 35, "Unexpected Arch Mage creature definition");

	const auto handler = LIBRARY->objtypeh->getHandlerFor(Obj::MONSTER, archMage->getId().num);
	require(handler != nullptr, "Arch Mage monster object handler is missing");
	const auto templates = handler->getTemplates();
	require(templates.size() == 1 && templates.front() != nullptr,
		"Arch Mage must resolve to one dedicated adventure-map template");
	const auto & objectTemplate = *templates.front();
	require(objectTemplate.id == Obj::MONSTER && objectTemplate.subid == archMage->getId().num,
		"Arch Mage adventure-map template is not bound to its creature identity");
	const AnimationPath mapPath = AnimationPath::builtin("NH_ArchMageGreyMap.def");
	require(objectTemplate.animationFile == mapPath,
		"Arch Mage adventure-map template must use NH_ArchMageGreyMap.def");
	require(objectTemplate.getWidth() == 2 && objectTemplate.getHeight() == 2 && objectTemplate.isVisitable(),
		"Arch Mage must preserve the 2x2 visitable monster footprint");
	for(int y = 0; y < 2; ++y)
	{
		for(int x = 0; x < 2; ++x)
		{
			const bool anchor = x == 0 && y == 0; // readJson reverses the mask into bottom-right-relative coordinates
			require(objectTemplate.isVisibleAt(x, y), "Arch Mage mapMask must keep every footprint tile visible");
			require(objectTemplate.isBlockedAt(x, y) == anchor && objectTemplate.isVisitableAt(x, y) == anchor,
				"Arch Mage mapMask [VV, VA] must block and mark only its A anchor visitable");
		}
	}
	for(int dy = -1; dy <= 1; ++dy)
	{
		for(int dx = -1; dx <= 1; ++dx)
		{
			if(dx != 0 || dy != 0)
				require(objectTemplate.isVisitableFrom(dx, dy),
					"Arch Mage must allow all eight surrounding visit approaches");
		}
	}

	auto & renderer = ENGINE->renderHandler();
	const auto mapAnimation = renderer.loadAnimation(mapPath, EImageBlitMode::WITH_SHADOW);
	require(mapAnimation && mapAnimation->size(0) == 30,
		"Arch Mage map alias must provide exactly 30 group-0 frames");
	for(size_t group = 1; group < 64; ++group)
		require(mapAnimation->size(group) == 0, "Arch Mage map alias must not add unrelated animation groups");
	for(size_t frame = 0; frame < 30; ++frame)
	{
		ImageLocator locator = mapAnimation->getImageLocator(frame, 0);
		require(locator.defFile.has_value()
			&& normalizedAnimationSource(*locator.defFile) == AnimationPath::builtin("AVWmagx0.DEF")
			&& locator.defGroup == 0 && locator.defFrame == static_cast<int>(frame),
			"Arch Mage map alias must preserve original AVWmagx0 group-0 frame order at frame "
				+ std::to_string(frame));
		verifyPaletteMap(locator, "Arch Mage map alias frame " + std::to_string(frame));
		locator.layer = EImageBlitMode::WITH_SHADOW;
		locator.scalingFactor = 1;
		locator.originalDefFrame = true;
		const auto sourceFrame = renderer.loadImage(locator);
		require(sourceFrame && sourceFrame->dimensions() == Point(64, 64),
			"Arch Mage map alias must retain the original 64x64 canvas at frame " + std::to_string(frame));
	}
	for(const size_t frame : {size_t(0), size_t(15), size_t(29)})
		verifyMappedPalettePixels(renderer, *mapAnimation, 0, frame, EImageBlitMode::WITH_SHADOW,
			"Arch Mage map frame " + std::to_string(frame), true);

	const auto mageId = LIBRARY->identifiers()->getIdentifier(
		ModScope::scopeGame(), "creature", std::string("mage"));
	require(mageId.has_value(), "Could not resolve core:mage for map-animation preservation");
	const auto * mage = LIBRARY->creh->objects.at(static_cast<size_t>(*mageId)).get();
	require(mage != nullptr, "Loaded Mage definition is missing for map-animation preservation");
	const auto mageHandler = LIBRARY->objtypeh->getHandlerFor(Obj::MONSTER, mage->getId().num);
	require(mageHandler != nullptr, "Mage monster object handler is missing");
	const auto mageTemplates = mageHandler->getTemplates();
	require(std::any_of(mageTemplates.begin(), mageTemplates.end(), [](const auto & entry)
		{ return entry && entry->animationFile == AnimationPath::builtin("AVWmage0"); }),
		"Base Mage must retain its original AVWmage0 adventure-map animation");
	verifyOrdinaryAnimationGroupRemainsUnmapped(renderer, AnimationPath::builtin("AVWmage0"), 0,
		EImageBlitMode::WITH_SHADOW, "Base Mage AVWmage0 group 0");
	std::cout << "  Arch Mage map template: 2x2 anchor footprint, eight approaches, 30 exact 64x64 AVWmagx0 frames; AVWmage0 unchanged\n";
}

void verifyMagiPaletteAliases(IRenderHandler & renderer)
{
	const auto archMage = verifyCompletePaletteAlias(renderer, "NH_ArchMageGrey.def", "CAMAGE.DEF");
	const auto mageProjectile = verifyCompletePaletteAlias(renderer, "NH_MageRedProjectile.def", "PMAGEX.DEF");
	const auto portrait = verifyArchMagePortraitAlias(renderer);

	verifyMappedPalettePixels(renderer, *archMage, 2, 0, EImageBlitMode::WITH_SHADOW_AND_SELECTION,
		"Arch Mage holding frame", true);
	verifyMappedPalettePixels(renderer, *archMage, 14, 8, EImageBlitMode::WITH_SHADOW_AND_SELECTION,
		"Arch Mage shooting frame", true);
	verifyMappedPalettePixels(renderer, *mageProjectile, 0, 0, EImageBlitMode::COLORKEY,
		"Mage projectile frame", true);
	verifyMappedPalettePixels(renderer, *portrait, 0, 0, EImageBlitMode::OPAQUE,
		"Arch Mage portrait source frame", true);
	verifyArchMageSmallAndEncounterImages(renderer);
	verifyArchMageAdventureMap();
}

void exportMappedMagiPreviews(IRenderHandler & renderer, const std::filesystem::path & destination)
{
	const auto previewDirectory = destination / "mapped-runtime-aliases";
	require(std::filesystem::create_directory(previewDirectory),
		"Mapped alias preview subdirectory must be new under the explicitly opted-in /tmp destination");
	const auto exportFrame = [&](const AnimationPath & animationPath, size_t group, size_t frame,
		EImageBlitMode mode, const std::string & filename)
	{
		const auto animation = renderer.loadAnimation(animationPath, mode);
		require(animation && animation->size(group) > frame,
			"Opt-in mapped alias preview frame is unavailable: " + filename);
		const auto image = animation->getImage(frame, group, true);
		require(image != nullptr, "Could not render opt-in mapped alias preview: " + filename);
		image->exportBitmap(boost::filesystem::path((previewDirectory / filename).string()));
	};
	exportFrame(AnimationPath::builtin("NH_ArchMageGrey.def"), 2, 0,
		EImageBlitMode::WITH_SHADOW_AND_SELECTION, "archmage_standing_g2_f0.png");
	exportFrame(AnimationPath::builtin("NH_ArchMageGrey.def"), 14, 8,
		EImageBlitMode::WITH_SHADOW_AND_SELECTION, "archmage_shooting_g14_f8.png");
	exportFrame(AnimationPath::builtin("NH_ArchMageGreyPortrait"), 0, 0,
		EImageBlitMode::OPAQUE, "archmage_portrait_source.png");
	exportFrame(AnimationPath::builtin("NH_ArchMageGreySmall"), 0, 0,
		EImageBlitMode::COLORKEY, "archmage_small_icon.png");
	exportFrame(AnimationPath::builtin("NH_ArchMageGreyEncounter"), 0, 0,
		EImageBlitMode::SIMPLE, "archmage_encounter_right.png");
	exportFrame(AnimationPath::builtin("NH_ArchMageGreyEncounter"), 0, 1,
		EImageBlitMode::SIMPLE, "archmage_encounter_left.png");
	for(const size_t frame : {size_t(0), size_t(15), size_t(29)})
		exportFrame(AnimationPath::builtin("NH_ArchMageGreyMap"), 0, frame,
			EImageBlitMode::WITH_SHADOW, "archmage_map_f" + std::to_string(frame) + ".png");
	for(size_t frame = 0; frame < 9; ++frame)
		exportFrame(AnimationPath::builtin("NH_MageRedProjectile.def"), 0, frame,
			EImageBlitMode::COLORKEY, "mage_projectile_f" + std::to_string(frame) + ".png");
}

void verifyCabirAnimation(const char * descriptorName, bool master)
{
	const char * creatureIdentifier = master ? "masterGremlin" : "gremlin";
	const int expectedCreatureId = master ? 29 : 28;
	const auto creatureId = LIBRARY->identifiers()->getIdentifier(
		ModScope::scopeGame(), "creature", std::string(creatureIdentifier));
	require(creatureId.has_value(), std::string("Could not resolve core:") + creatureIdentifier);
	const auto * creature = LIBRARY->creh->objects.at(static_cast<size_t>(*creatureId)).get();
	require(creature != nullptr && creature->getIndex() == expectedCreatureId,
		std::string("Unexpected Cabir creature binding for core:") + creatureIdentifier);

	auto & renderer = ENGINE->renderHandler();
	const AnimationPath descriptorPath = AnimationPath::builtin(descriptorName);
	require(creature->animDefName == descriptorPath,
		std::string("Cabir creature animation does not use its dedicated descriptor: core:")
			+ creatureIdentifier);
	const auto animation = renderer.loadAnimation(descriptorPath, EImageBlitMode::WITH_SHADOW_AND_SELECTION);
	require(animation != nullptr, std::string("Could not load creature animation: ") + descriptorName);
	const auto standingGroup = static_cast<size_t>(ECreatureAnimType::HOLDING);
	const auto standing = animation->getImage(0, standingGroup, true);
	require(standing != nullptr, "Cabir standing frame is missing");
	standing->setOverlayColor(ColorRGBA(0, 0, 0, 0));
	const auto unselected = captureImagePixels(standing, standing->dimensions(), "Cabir unselected contour");
	standing->setOverlayColor(ColorRGBA(255, 255, 0, 255));
	const auto selected = captureImagePixels(standing, standing->dimensions(), "Cabir selected contour");
	standing->setOverlayColor(ColorRGBA(0, 0, 0, 0));
	require(unselected != selected, "Cabir selection must draw an actual silhouette outline");

	std::vector<ECreatureAnimType> requiredGroups{
		ECreatureAnimType::MOVING,
		ECreatureAnimType::HOLDING,
		ECreatureAnimType::HITTED,
		ECreatureAnimType::DEFENCE,
		ECreatureAnimType::DEATH,
		ECreatureAnimType::ATTACK_UP,
		ECreatureAnimType::ATTACK_FRONT,
		ECreatureAnimType::ATTACK_DOWN,
	};
	if(master)
	{
		requiredGroups.insert(requiredGroups.end(), {
			ECreatureAnimType::SHOOT_UP,
			ECreatureAnimType::SHOOT_FRONT,
			ECreatureAnimType::SHOOT_DOWN,
			ECreatureAnimType::CAST_FRONT,
		});
	}

	for(const auto group : requiredGroups)
	{
		const size_t groupId = static_cast<size_t>(group);
		const size_t frameCount = animation->size(groupId);
		require(frameCount > 0, std::string("Required Cabir animation group is empty: ")
			+ descriptorName + " group " + std::to_string(groupId));

		for(size_t frame = 0; frame < frameCount; ++frame)
		{
			const auto locator = animation->getImageLocator(frame, groupId);
			require(locator.image.has_value()
				&& CResourceHandler::get()->existsResource(mountedSpritePath(*locator.image)),
				std::string("Cabir descriptor frame resource is missing: ") + descriptorName
				+ " group " + std::to_string(groupId) + " frame " + std::to_string(frame));
			require(animation->getImage(frame, groupId, true) != nullptr,
				std::string("Could not load Cabir animation frame: ") + descriptorName
				+ " group " + std::to_string(groupId) + " frame " + std::to_string(frame));
		}
	}

	const std::vector<ECreatureAnimType> multiFrameGroups{
		ECreatureAnimType::MOVING,
		ECreatureAnimType::ATTACK_UP,
		ECreatureAnimType::ATTACK_FRONT,
		ECreatureAnimType::ATTACK_DOWN,
		ECreatureAnimType::DEATH,
	};
	for(const auto group : multiFrameGroups)
		require(animation->size(static_cast<size_t>(group)) > 1,
			std::string("Cabir motion group must contain multiple frames: ") + descriptorName
			+ " group " + std::to_string(static_cast<size_t>(group)));

	if(master)
	{
		for(const auto group : {ECreatureAnimType::SHOOT_UP, ECreatureAnimType::SHOOT_FRONT,
			ECreatureAnimType::SHOOT_DOWN})
			require(animation->size(static_cast<size_t>(group)) >= 3,
				std::string("Cabir Master shooting group must contain the release frame: ") + descriptorName
				+ " group " + std::to_string(static_cast<size_t>(group)));
		require(animation->size(static_cast<size_t>(ECreatureAnimType::CAST_FRONT)) > 1,
			std::string("Cabir Master cast group must contain multiple frames: ") + descriptorName);

		require(creature->animation.projectileImageName == AnimationPath::builtin("CPRGOGX.DEF"),
			"Cabir Master must use the original Gog missile animation resource");
		require(creature->animation.attackClimaxFrame == 3,
			"Cabir Master must release its projectile at the authored attack climax frame 3");

		const AnimationPath projectilePath = AnimationPath::builtin("SPRITES/CPRGOGX.DEF");
		const auto projectile = renderer.loadAnimation(projectilePath, EImageBlitMode::COLORKEY);
		require(projectile && projectile->size(0) > 0, "Original CPRGOGX.DEF projectile must load forward frames");
		if(projectile->size(1) == 0)
			projectile->createFlippedGroup(0, 1);
		require(projectile->size(1) == projectile->size(0)
			&& projectile->getImage(0, 0, true) && projectile->getImage(0, 1, true),
			"CPRGOGX projectile must provide a reverse group through the runtime flip fallback");
		const auto describeDefReference = [](const ImageLocator & locator)
		{
			if(!locator.defFile)
				return std::string("<no DEF reference>");
			return locator.defFile->getOriginalName() + " (canonical " + locator.defFile->getName() + ")";
		};
		const std::string expectedDefReference = projectilePath.getOriginalName()
			+ " (canonical " + projectilePath.getName() + ")";
		const auto projectileFrame = projectile->getImageLocator(0, 0);
		require(projectileFrame.defFile.has_value() && *projectileFrame.defFile == projectilePath,
			"Cabir Master projectile should resolve to original CPRGOGX DEF " + expectedDefReference
			+ "; actual locator is " + describeDefReference(projectileFrame));
		const auto reverseProjectileFrame = projectile->getImageLocator(0, 1);
			require(reverseProjectileFrame.defFile.has_value() && *reverseProjectileFrame.defFile == projectilePath,
			"Cabir Master reverse projectile should retain original CPRGOGX DEF " + expectedDefReference
			+ "; actual locator is " + describeDefReference(reverseProjectileFrame));
	}
	verifyCabirAdventureMap(*creature,
		master ? "NH_CabirMasterMap.def" : "NH_CabirMap.def",
		master ? "NH_CabirMasterEncounterLeft.png" : "NH_CabirEncounterLeft.png",
		master ? "NH_CabirMasterEncounterRight.png" : "NH_CabirEncounterRight.png");

	const auto battleAnimation = std::make_shared<CreatureAnimation>(descriptorPath,
		[](CreatureAnimation *, ECreatureAnimType) { return 1.0f; });
	require(battleAnimation->framesInGroup(ECreatureAnimType::DEAD) > 0,
		std::string("Runtime death pose fallback is missing for ") + descriptorName);
	std::cout << "  " << descriptorName << ": loaded distinct descriptor groups/frames"
		<< (master ? " and original CPRGOGX projectile" : "") << '\n';
}

void verifyCabirAnimations()
{
	verifyCabirAnimation("NH_Cabir", false);
	verifyCabirAnimation("NH_CabirMaster", true);
}

void setUpscalingFilter(const char * name)
{
	Settings filter = settings.write["video"]["upscalingFilter"];
	filter->String() = name;
	Settings renderer = settings.write["video"]["driver"];
	renderer->String() = "software";
}

void runRuntimeRegression()
{
	const char * originalDataValue = std::getenv("NH_H3_DATA_DIR");
	require(originalDataValue && *originalDataValue,
		"Set NH_H3_DATA_DIR to the installed Heroes III Data directory before running this fixture");
	const auto originalData = std::filesystem::canonical(originalDataValue);
	require(std::filesystem::is_directory(originalData)
		&& std::filesystem::is_regular_file(originalData / "H3bitmap.lod")
		&& std::filesystem::is_regular_file(originalData / "H3sprite.lod"),
		"NH_H3_DATA_DIR must contain the original H3bitmap.lod and H3sprite.lod archives");

	IsolatedProfile profile(originalData);
	require(VCMIDirs::get().userDataPath() == profile.path() / "data" / "vcmi",
		"Runtime test did not select its isolated XDG data profile");
	require(VCMIDirs::get().userConfigPath() == profile.path() / "config" / "vcmi",
		"Runtime test did not select its isolated XDG config profile");

	CBasicLogConfigurator logging(VCMIDirs::get().userLogsPath() / "academy-built-icon-runtime.log", nullptr);
	logging.configureDefault();
	struct RuntimeCleanup
	{
		CBasicLogConfigurator & logging;
		~RuntimeCleanup()
		{
			// SDL image scaling workers may read the global ENGINE; drain them before reset clears it.
			if(ENGINE)
				ENGINE->async().wait();
			ENGINE.reset();
			delete LIBRARY;
			LIBRARY = nullptr;
			CResourceHandler::destroy();
			logging.deconfigure();
		}
	} cleanup{logging};

	LIBRARY = new GameLibrary;
	LIBRARY->initializeFilesystem(false);
	LIBRARY->initializeLibrary();
	const auto & activeMods = LIBRARY->modh->getActiveMods();
	require(std::find(activeMods.begin(), activeMods.end(), "new-horizons") != activeMods.end(),
		"Isolated renderer test did not activate New Horizons");
	verifyMagiProjectileColors();
	verifyPaletteRemapJsonValidation();
	const PaletteRemapFixture magiPaletteFixture = makeMagiPaletteRemapFixture();

	constexpr std::array<const char *, 4> upscalingFilters{{"none", "xbrz2", "xbrz3", "xbrz4"}};
	for(size_t factorIndex = 0; factorIndex < upscalingFilters.size(); ++factorIndex)
	{
		if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0)
			throw std::runtime_error(std::string("SDL dummy initialization failed: ") + SDL_GetError());
		const char * activeVideoDriver = SDL_GetCurrentVideoDriver();
		require(activeVideoDriver && std::string(activeVideoDriver) == "dummy",
			"SDL did not use the dummy video driver before GameEngine construction");

		setUpscalingFilter(upscalingFilters[factorIndex]);
		ENGINE = std::make_unique<GameEngine>();
		const int expectedScale = factorIndex == 0 ? 1 : static_cast<int>(factorIndex + 1);
		require(ENGINE->screenHandler().getScalingFactor() == expectedScale,
			"ScreenHandler did not honor requested test scaling factor");

		auto & renderer = ENGINE->renderHandler();
		renderer.onLibraryLoadingFinished(LIBRARY);
		std::cout << "SDL dummy runtime scale " << expectedScale << "x\n";
		if(factorIndex == 0)
		{
			verifyMagiPaletteAliases(renderer);
			const char * inspectionEnabled = std::getenv("VCMI_MAGI_PALETTE_INSPECTION");
			if(inspectionEnabled && std::string_view(inspectionEnabled) == "1")
			{
				const auto inspectionDirectory = magiPaletteInspection::detail::validateDestination();
				require(magiPaletteInspection::exportIfOptedIn(),
					"Opt-in original palette diagnostics unexpectedly declined export");
				exportMappedMagiPreviews(renderer, inspectionDirectory);
			}
			else
				(void)magiPaletteInspection::exportIfOptedIn();
		}
		verifyMagiPaletteRemap(renderer, magiPaletteFixture, expectedScale);

		for(const BuiltIcon & icon : academyBuiltIcons)
		{
			auto normal = renderer.loadImage(ImagePath::builtin(icon.normal), EImageBlitMode::SIMPLE);
			auto built = renderer.loadImage(ImagePath::builtin(icon.built), EImageBlitMode::SIMPLE);
			verifyDifferentPixels(normal, built, icon);
		}
		for(const AcademyPortrait & portrait : academyPortraits)
			verifyAcademyPortrait(portrait);
		if(factorIndex == 0 && cabirAnimationValidationRequested())
			verifyCabirAnimations();

		// Destroying the backend releases SDL. The next iteration starts it again
		// under the same dummy environment and verifies the selected driver anew.
		// Drain image scaling before reset because worker tasks read the global ENGINE.
		ENGINE->async().wait();
		ENGINE.reset();
	}

	std::cout << "Academy built-icon and creature-portrait runtime regression PASS\n";
}
}

int main()
{
	const char * originalData = std::getenv("NH_H3_DATA_DIR");
	if(!originalData || !*originalData)
	{
		std::cout << "SKIP: set NH_H3_DATA_DIR to the installed Heroes III Data directory\n";
		return 77;
	}

	try
	{
		runRuntimeRegression();
	}
	catch(const std::exception & error)
	{
		std::cerr << "Academy built-icon runtime regression FAIL: " << error.what() << '\n';
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
