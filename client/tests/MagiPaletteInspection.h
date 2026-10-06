/*
 * MagiPaletteInspection.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 *
 * Explicitly opt-in, local-only diagnostic export for original indexed DEF
 * resources. This header is intended for a native test fixture only.
 */
#pragma once

#include "../render/CDefFile.h"
#include "../render/IImageLoader.h"
#include "../../lib/Point.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(VCMI_SDL3)
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>
#else
#include <SDL_pixels.h>
#include <SDL_surface.h>
#endif

namespace magiPaletteInspection
{
namespace detail
{
struct Rgb
{
	uint8_t r = 0;
	uint8_t g = 0;
	uint8_t b = 0;
};

struct Frame
{
	int width = 0;
	int height = 0;
	int dataX = 0;
	int dataY = 0;
	int dataWidth = 0;
	int dataHeight = 0;
	std::array<Rgb, 256> palette{};
	std::vector<uint8_t> indices;
};

class FrameCapture final : public IImageLoader
{
	Frame frame;
	int line = 0;
	int column = 0;

	void append(size_t count, uint8_t index)
	{
		if(line >= frame.dataHeight || column < 0 || column > frame.dataWidth
			|| count > static_cast<size_t>(frame.dataWidth - column))
			throw std::runtime_error("DEF palette inspection frame data exceeds stored rectangle");

		for(size_t i = 0; i < count; ++i)
		{
			const size_t x = static_cast<size_t>(frame.dataX + column++);
			const size_t y = static_cast<size_t>(frame.dataY + line);
			frame.indices[y * static_cast<size_t>(frame.width) + x] = index;
		}
	}

public:
	void init(Point storedSize, Point margins, Point canvasSize, SDL_Color * colors) override
	{
		if(storedSize.x <= 0 || storedSize.y <= 0 || canvasSize.x <= 0 || canvasSize.y <= 0
			|| margins.x < 0 || margins.y < 0
			|| margins.x + storedSize.x > canvasSize.x || margins.y + storedSize.y > canvasSize.y
			|| colors == nullptr)
			throw std::runtime_error("Invalid geometry in DEF palette inspection frame");

		frame = {};
		frame.width = canvasSize.x;
		frame.height = canvasSize.y;
		frame.dataX = margins.x;
		frame.dataY = margins.y;
		frame.dataWidth = storedSize.x;
		frame.dataHeight = storedSize.y;
		frame.indices.assign(static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height), 0);
		for(size_t i = 0; i < frame.palette.size(); ++i)
			frame.palette[i] = {colors[i].r, colors[i].g, colors[i].b};
		line = 0;
		column = 0;
	}

	void load(size_t count, const ui8 * data) override
	{
		if(data == nullptr && count != 0)
			throw std::runtime_error("Null data in DEF palette inspection frame");
		for(size_t i = 0; i < count; ++i)
			append(1, data[i]);
	}

	void load(size_t count, ui8 color = 0) override
	{
		append(count, color);
	}

	void endLine() override
	{
		if(line >= frame.dataHeight || column != frame.dataWidth)
			throw std::runtime_error("Incomplete scanline in DEF palette inspection frame");
		++line;
		column = 0;
	}

	Frame take()
	{
		if(line != frame.dataHeight || column != 0)
			throw std::runtime_error("Incomplete DEF palette inspection frame");
		return std::move(frame);
	}
};

struct IndexStats
{
	uint64_t population = 0;
	int minX = std::numeric_limits<int>::max();
	int minY = std::numeric_limits<int>::max();
	int maxX = -1;
	int maxY = -1;

	void add(int x, int y)
	{
		++population;
		minX = std::min(minX, x);
		minY = std::min(minY, y);
		maxX = std::max(maxX, x + 1);
		maxY = std::max(maxY, y + 1);
	}
};

using GroupStats = std::array<IndexStats, 256>;
using ResourceStats = std::map<size_t, GroupStats>;

inline SDL_Surface * createIndexedSurface(int width, int height, const std::array<Rgb, 256> & colors)
{
#if defined(VCMI_SDL3)
	SDL_Surface * surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_INDEX8);
	if(surface == nullptr)
		throw std::runtime_error("Could not create indexed SDL diagnostic surface");
	SDL_Palette * palette = SDL_CreateSurfacePalette(surface);
	if(palette == nullptr)
	{
		SDL_DestroySurface(surface);
		throw std::runtime_error("Could not create SDL diagnostic palette");
	}
	for(size_t i = 0; i < colors.size(); ++i)
		palette->colors[i] = SDL_Color{colors[i].r, colors[i].g, colors[i].b, SDL_ALPHA_OPAQUE};
	if(!SDL_SetSurfaceColorKey(surface, true, 0))
	{
		SDL_DestroySurface(surface);
		throw std::runtime_error("Could not set SDL diagnostic transparency key");
	}
#else
	SDL_Surface * surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 8, SDL_PIXELFORMAT_INDEX8);
	if(surface == nullptr)
		throw std::runtime_error("Could not create indexed SDL diagnostic surface");
	SDL_Palette * palette = SDL_AllocPalette(static_cast<int>(colors.size()));
	if(palette == nullptr)
	{
		SDL_FreeSurface(surface);
		throw std::runtime_error("Could not create SDL diagnostic palette");
	}
	std::array<SDL_Color, 256> sdlColors{};
	for(size_t i = 0; i < colors.size(); ++i)
		sdlColors[i] = SDL_Color{colors[i].r, colors[i].g, colors[i].b, SDL_ALPHA_OPAQUE};
	const int paletteResult = SDL_SetPaletteColors(palette, sdlColors.data(), 0, static_cast<int>(sdlColors.size()));
	const int surfaceResult = paletteResult == 0 ? SDL_SetSurfacePalette(surface, palette) : -1;
	SDL_FreePalette(palette);
	if(paletteResult != 0 || surfaceResult != 0 || SDL_SetColorKey(surface, SDL_TRUE, 0) != 0)
	{
		SDL_FreeSurface(surface);
		throw std::runtime_error("Could not configure SDL diagnostic palette");
	}
#endif
	return surface;
}

struct SurfaceDeleter
{
	void operator()(SDL_Surface * surface) const
	{
#if defined(VCMI_SDL3)
		SDL_DestroySurface(surface);
#else
		SDL_FreeSurface(surface);
#endif
	}
};

using SurfacePtr = std::unique_ptr<SDL_Surface, SurfaceDeleter>;

inline void saveBmp(const std::filesystem::path & path, const Frame & frame)
{
	SurfacePtr surface(createIndexedSurface(frame.width, frame.height, frame.palette));
	for(int y = 0; y < frame.height; ++y)
	{
		const auto * source = frame.indices.data() + static_cast<size_t>(y) * static_cast<size_t>(frame.width);
		auto * target = static_cast<uint8_t *>(surface->pixels) + static_cast<size_t>(y) * static_cast<size_t>(surface->pitch);
		std::copy_n(source, frame.width, target);
	}
#if defined(VCMI_SDL3)
	if(!SDL_SaveBMP(surface.get(), path.string().c_str()))
		throw std::runtime_error("Could not save DEF diagnostic BMP");
#else
	if(SDL_SaveBMP(surface.get(), path.string().c_str()) != 0)
		throw std::runtime_error("Could not save DEF diagnostic BMP");
#endif
}

inline Frame cropOpaque(const Frame & source)
{
	int minX = source.width;
	int minY = source.height;
	int maxX = -1;
	int maxY = -1;
	for(int y = 0; y < source.height; ++y)
		for(int x = 0; x < source.width; ++x)
			if(source.indices[static_cast<size_t>(y) * static_cast<size_t>(source.width) + static_cast<size_t>(x)] != 0)
			{
				minX = std::min(minX, x);
				minY = std::min(minY, y);
				maxX = std::max(maxX, x);
				maxY = std::max(maxY, y);
			}
	if(maxX < minX || maxY < minY)
		throw std::runtime_error("DEF diagnostic frame contains no opaque pixels");

	Frame result;
	result.width = maxX - minX + 1;
	result.height = maxY - minY + 1;
	result.dataWidth = result.width;
	result.dataHeight = result.height;
	result.palette = source.palette;
	result.indices.resize(static_cast<size_t>(result.width) * static_cast<size_t>(result.height));
	for(int y = 0; y < result.height; ++y)
	{
		const auto * begin = source.indices.data() + static_cast<size_t>(minY + y) * static_cast<size_t>(source.width) + static_cast<size_t>(minX);
		auto * target = result.indices.data() + static_cast<size_t>(y) * static_cast<size_t>(result.width);
		std::copy_n(begin, result.width, target);
	}
	return result;
}

inline Frame scaleNearest(const Frame & source, int factor)
{
	Frame result;
	result.width = source.width * factor;
	result.height = source.height * factor;
	result.dataWidth = result.width;
	result.dataHeight = result.height;
	result.palette = source.palette;
	result.indices.resize(static_cast<size_t>(result.width) * static_cast<size_t>(result.height));
	for(int y = 0; y < result.height; ++y)
		for(int x = 0; x < result.width; ++x)
			result.indices[static_cast<size_t>(y) * static_cast<size_t>(result.width) + static_cast<size_t>(x)] =
				source.indices[static_cast<size_t>(y / factor) * static_cast<size_t>(source.width) + static_cast<size_t>(x / factor)];
	return result;
}

inline void addToCensus(ResourceStats & resource, size_t group, const Frame & frame)
{
	GroupStats & stats = resource[group];
	for(int y = frame.dataY; y < frame.dataY + frame.dataHeight; ++y)
		for(int x = frame.dataX; x < frame.dataX + frame.dataWidth; ++x)
			stats[frame.indices[static_cast<size_t>(y) * static_cast<size_t>(frame.width) + static_cast<size_t>(x)]].add(x, y);
}

inline void writeCensus(const std::filesystem::path & path,
	const std::map<std::string, ResourceStats> & resources,
	const std::map<std::string, std::array<Rgb, 256>> & palettes)
{
	std::ofstream output(path);
	if(!output)
		throw std::runtime_error("Could not create DEF palette census CSV");
	output << "resource,group,index,r,g,b,population,min_x,min_y,max_x_exclusive,max_y_exclusive\n";
	for(const auto & [resource, groups] : resources)
		for(const auto & [group, indices] : groups)
			for(size_t index = 0; index < indices.size(); ++index)
			{
				const auto & stats = indices[index];
				const auto & color = palettes.at(resource)[index];
				output << resource << ',' << group << ',' << index << ',' << static_cast<unsigned>(color.r) << ','
					<< static_cast<unsigned>(color.g) << ',' << static_cast<unsigned>(color.b) << ',' << stats.population << ',';
				if(stats.population == 0)
					output << ",,,\n";
				else
					output << stats.minX << ',' << stats.minY << ',' << stats.maxX << ',' << stats.maxY << '\n';
			}
	if(!output)
		throw std::runtime_error("Failed writing DEF palette census CSV");
}

inline std::filesystem::path validateDestination()
{
	const char * value = std::getenv("VCMI_MAGI_PALETTE_INSPECTION_DIR");
	if(value == nullptr || *value == '\0')
		throw std::runtime_error("VCMI_MAGI_PALETTE_INSPECTION_DIR must name an existing empty /tmp directory");
	const std::filesystem::path requested(value);
	if(!requested.is_absolute() || requested.lexically_normal() != requested)
		throw std::runtime_error("DEF palette inspection path must be absolute and normalized");
	for(auto component = requested; !component.empty(); component = component.parent_path())
	{
		std::error_code error;
		const auto status = std::filesystem::symlink_status(component, error);
		if(!error && std::filesystem::is_symlink(status))
			throw std::runtime_error("DEF palette inspection path cannot contain symlinks");
		if(component == component.root_path())
			break;
	}
	const auto canonical = std::filesystem::canonical(requested);
	const auto root = std::filesystem::canonical("/tmp");
	const auto relative = canonical.lexically_relative(root);
	if(relative.empty() || relative.is_absolute() || *relative.begin() == ".." || canonical == root)
		throw std::runtime_error("DEF palette inspection may write only beneath /tmp");
	if(!std::filesystem::is_directory(canonical) || !std::filesystem::is_empty(canonical))
		throw std::runtime_error("DEF palette inspection destination must be an existing empty directory");
	return canonical;
}

inline void inspect(const std::filesystem::path & destination, const std::string & resource,
	const std::vector<std::pair<size_t, size_t>> & selectedFrames,
	std::map<std::string, ResourceStats> & census,
	std::map<std::string, std::array<Rgb, 256>> & palettes)
{
	CDefFile definition(AnimationPath::builtin("SPRITES/" + resource));
	const std::set<std::pair<size_t, size_t>> selected(selectedFrames.begin(), selectedFrames.end());
	std::map<std::pair<size_t, size_t>, Frame> previews;
	auto & stats = census[resource];
	bool havePalette = false;

	for(const auto & [group, count] : definition.getEntries())
		for(size_t frameIndex = 0; frameIndex < count; ++frameIndex)
		{
			FrameCapture capture;
			definition.loadFrame(frameIndex, group, capture);
			Frame frame = capture.take();
			if(!havePalette)
			{
				palettes[resource] = frame.palette;
				havePalette = true;
			}
			addToCensus(stats, group, frame);
			const auto key = std::pair{group, frameIndex};
			if(selected.contains(key))
				previews.emplace(key, std::move(frame));
		}

	for(const auto & key : selected)
	{
		const auto found = previews.find(key);
		if(found == previews.end())
			throw std::runtime_error("Requested frame absent from DEF palette inspection resource");
		const std::string stem = resource.substr(0, resource.find('.')) + "_g" + std::to_string(key.first)
			+ "_f" + std::to_string(key.second);
		saveBmp(destination / (stem + "_full.bmp"), found->second);
		const Frame crop = cropOpaque(found->second);
		saveBmp(destination / (stem + "_crop.bmp"), crop);
		saveBmp(destination / (stem + "_crop_4x.bmp"), scaleNearest(crop, 4));
	}
}

inline void exportDiagnostic(const std::filesystem::path & destination)
{
	std::map<std::string, ResourceStats> census;
	std::map<std::string, std::array<Rgb, 256>> palettes;
	inspect(destination, "CMAGE.DEF", {{2, 0}, {14, 0}, {14, 12}}, census, palettes);
	inspect(destination, "CAMAGE.DEF", {{2, 0}, {14, 0}, {14, 12}, {15, 0}, {15, 12}, {16, 0}, {16, 12}}, census, palettes);
	inspect(destination, "PMAGEX.DEF", {{0, 0}, {0, 1}, {0, 2}, {0, 3}, {0, 4}, {0, 5}, {0, 6}, {0, 7}, {0, 8}}, census, palettes);
	inspect(destination, "TWCRPORT.DEF", {{0, 37}}, census, palettes);
	inspect(destination, "CPRSMALL.DEF", {{0, 36}, {0, 37}}, census, palettes);
	inspect(destination, "AvWattak.DEF", {{0, 68}, {0, 69}, {0, 70}, {0, 71}}, census, palettes);
	writeCensus(destination / "palette_census.csv", census, palettes);
	std::ofstream notes(destination / "README.txt");
	if(!notes)
		throw std::runtime_error("Could not create DEF palette inspection note");
	notes << "Local-only diagnostic of original purchaser DEF resources. Do not commit or redistribute.\n"
		<< "Selected frames have indexed full-canvas, exact opaque-crop, and nearest-neighbor 4x BMPs.\n"
		<< "palette_census.csv counts indices in stored frame rectangles across all groups; bounds are half-open.\n";
}
} // namespace detail

/// Does nothing unless explicitly opted in with VCMI_MAGI_PALETTE_INSPECTION=1
/// and VCMI_MAGI_PALETTE_INSPECTION_DIR naming an empty existing directory under /tmp.
inline bool exportIfOptedIn()
{
	const char * enabled = std::getenv("VCMI_MAGI_PALETTE_INSPECTION");
	if(enabled == nullptr || std::string_view(enabled) != "1")
		return false;
	detail::exportDiagnostic(detail::validateDestination());
	return true;
}
} // namespace magiPaletteInspection
