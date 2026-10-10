/* Part of VCMI / New Horizons; GPL-2.0-or-later; see license.txt. */
#include "../../clientsdl3/render/GrayscalePngPalette.h"
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace
{
struct Mask { const char * name; size_t foreground; };
// Independent native-pixel oracles for the selected, hash-verified NHART bytes.
constexpr std::array<Mask, 12> masks{{
	{"archMage", 1791}, {"genie", 1459}, {"giant", 1592}, {"gremlin", 1539},
	{"ironGolem", 1795}, {"mage", 1474}, {"masterGenie", 1444}, {"masterGremlin", 1565},
	{"nagaQueen", 1532}, {"naga", 1352}, {"stoneGolem", 1833}, {"titan", 1842},
}};
void require(bool value, const std::string & message)
{
	if(!value) throw std::runtime_error(message);
}
void verifyPrivateWineEnvironment(const std::string & root)
{
#ifdef _WIN32
	auto check = [](const char * name, const std::string & expected)
	{
		std::array<char, 4096> value{};
		const auto length = GetEnvironmentVariableA(name, value.data(), static_cast<DWORD>(value.size()));
		require(length > 0 && length < value.size() && std::string(value.data()) == expected,
			std::string("Actual Windows environment mismatch: ") + name);
	};
	for(const auto * name : {"SDL_VIDEODRIVER", "SDL_VIDEO_DRIVER", "SDL_AUDIODRIVER", "SDL_AUDIO_DRIVER"})
		check(name, "dummy");
	check("DISPLAY", ":191");
	for(const auto & [name, directory] : std::array<std::pair<const char *, const char *>, 6>{{
		{"HOME", "home"}, {"XDG_CONFIG_HOME", "config"}, {"XDG_DATA_HOME", "data"},
		{"XDG_CACHE_HOME", "cache"}, {"XDG_STATE_HOME", "state"}, {"XDG_RUNTIME_DIR", "runtime"}}})
		check(name, root + "/" + directory);
	for(const auto * name : {"PULSE_SERVER", "PIPEWIRE_REMOTE", "WAYLAND_DISPLAY"})
	{
		SetLastError(ERROR_SUCCESS);
		std::array<char, 4096> value{};
		require(GetEnvironmentVariableA(name, value.data(), static_cast<DWORD>(value.size())) == 0
			&& GetLastError() == ERROR_ENVVAR_NOT_FOUND, std::string("Forbidden Windows environment: ") + name);
	}
	// CREATE_NEW forbids accepting a preexisting proof. This precedes every SDL call.
	const auto path = root + "/pe-environment-proof.txt";
	const auto file = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
	require(file != INVALID_HANDLE_VALUE, "Cannot create exclusive private environment proof");
	constexpr char marker[] = "WINE_PRIVATE_ENV_VERIFIED\n";
	DWORD written = 0;
	const bool saved = WriteFile(file, marker, sizeof(marker) - 1, &written, nullptr)
		&& written == sizeof(marker) - 1;
	CloseHandle(file);
	require(saved, "Cannot write private environment proof");
	std::cout << marker << std::flush;
#else
	throw std::runtime_error("Private Wine environment mode requires the Windows PE");
#endif
}
void check(const std::filesystem::path & directory, const Mask & mask)
{
	const auto name = std::string("NH_academy_") + mask.name + "_portrait_mask.png";
	std::ifstream stream(directory / name, std::ios::binary);
	require(stream.good(), "Missing verified fixture: " + name);
	const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(stream)), {});
	using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
	Surface decoded(IMG_Load_IO(SDL_IOFromConstMem(bytes.data(), bytes.size()), true), SDL_DestroySurface);
	require(decoded != nullptr, "Shipping SDL3_image decode failed: " + name);
	require(decoded->w == 58 && decoded->h == 64, "Native dimensions changed: " + name);
	bool repaired = false;
	if(auto * palette = SDL_GetSurfacePalette(decoded.get()))
	{
		require(palette->ncolors == 256, "Unexpected grayscale palette size: " + name);
		std::array<grayscalePngPalette::Color, 256> colors;
		for(size_t i = 0; i < colors.size(); ++i)
			colors[i] = {palette->colors[i].r, palette->colors[i].g, palette->colors[i].b, palette->colors[i].a};
		repaired = grayscalePngPalette::repair(bytes, colors);
		if(repaired)
		{
			std::array<SDL_Color, 256> corrected;
			for(size_t i = 0; i < colors.size(); ++i)
			{
				require(colors[i][3] == palette->colors[i].a, "Repair changed alpha: " + name);
				corrected[i] = {colors[i][0], colors[i][1], colors[i][2], colors[i][3]};
			}
			require(SDL_SetPaletteColors(palette, corrected.data(), 0, 256), "Palette commit failed: " + name);
		}
	}
	Surface rgba(SDL_ConvertSurface(decoded.get(), SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
	require(rgba != nullptr && SDL_LockSurface(rgba.get()), "Mask read failed: " + name);
	size_t foreground = 0;
	for(int y = 0; y < rgba->h; ++y)
		for(int x = 0; x < rgba->w; ++x)
		{
			const auto * pixel = static_cast<const uint8_t *>(rgba->pixels) + y * rgba->pitch + x * 4;
			require(pixel[0] == pixel[1] && pixel[0] == pixel[2]
				&& (pixel[0] == 0 || pixel[0] == 255) && pixel[3] == 255,
				"Mask is not binary opaque grayscale: " + name);
			foreground += pixel[0] == 255;
		}
	SDL_UnlockSurface(rgba.get());
	require(foreground == mask.foreground, "Foreground oracle differs: " + name);
	std::cout << name << ": " << foreground << " foreground pixels, repaired=" << repaired << '\n';
}
}
int main(int argc, char ** argv)
{
	try
	{
		require(argc == 2 || (argc == 4 && std::string(argv[2]) == "--wine-private-environment"),
			"Usage: nhSdl3GrayscaleMaskRuntimeTest verified-fixture-directory [--wine-private-environment private-root]");
		if(argc == 4) verifyPrivateWineEnvironment(argv[3]);
		// CPU surfaces only: no SDL_Init, window, renderer, audio or event loop.
		for(const auto & mask : masks) check(argv[1], mask);
		std::cout << "PASS: all 12 selected masks via shipping SDL3_image and production repair\n";
		return 0;
	}
	catch(const std::exception & error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
