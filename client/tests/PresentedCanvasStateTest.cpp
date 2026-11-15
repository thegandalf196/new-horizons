/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "../StdInc.h"
#include "../../clientsdl3/render/PresentedCanvasState.h"
#include <iostream>
#include <stdexcept>

static void require(bool condition, const char * message)
{
	if(!condition)
		throw std::runtime_error(message);
}

int main()
{
	try
	{
		require(oneToOnePresentedPixels(Point(800, 600), true, 0, 0, 800, 600), "Native pixels must be eligible");
		require(oneToOnePresentedPixels(Point(800, 600), true, 20, 40, 800, 600), "Integer letterbox must remain eligible");
		require(!oneToOnePresentedPixels(Point(800, 600), false, 0, 0, 800, 600), "Unknown presentation must fall back");
		require(!oneToOnePresentedPixels(Point(800, 600), true, 0.5f, 0, 800, 600), "Half-pixel horizontal offset must fall back");
		require(!oneToOnePresentedPixels(Point(800, 600), true, 0, 0.5f, 800, 600), "Half-pixel vertical offset must fall back");
		require(!oneToOnePresentedPixels(Point(800, 600), true, 0, 0, 799, 600), "Downsampling must retain old layer");
		require(!oneToOnePresentedPixels(Point(800, 600), true, 0, 0, 1200, 900), "Upsampling must retain old layer");
		require(!oneToOnePresentedPixels(Point(), true, 0, 0, 0, 0), "Empty presentation must fall back");
		int oldOwner = 0, newOwner = 0, textureStorage = 0;
		auto * texture = reinterpret_cast<SDL_Texture *>(&textureStorage);
		PresentedCanvasState state;
		require(!state.current(1), "Empty registration must not draw");
		state.owner = &oldOwner;
		state.texture = texture;
		state.generation = 1;
		state.regions.push_back({Rect(0, 0, 32, 32), Rect(3, 4, 16, 16)});
		require(state.current(1), "Live borrowed texture must draw in its generation");
		require(!state.current(2), "Renderer replacement must reject old texture generation");
		// A new map can replace the registration before the old view is released.
		state.owner = &newOwner;
		state.generation = 2;
		require(!state.clear(&oldOwner), "Late old-owner clear must not erase replacement");
		require(state.current(2) && state.regions.size() == 1,
			"Replacement pixels must survive old-owner destruction");
		require(state.clear(&newOwner), "Current owner must be able to clear before texture release");
		require(!state.current(2) && state.regions.empty(), "Cleared registration must not draw");
		state.owner = &newOwner;
		state.texture = texture;
		state.generation = 2;
		state.regions.push_back({Rect(0, 0, 8, 8), Rect(0, 0, 8, 8)});
		// Scene teardown and renderer resize clear even an inactive texture layer.
		require(state.clear(), "Unconditional scene release must clear any owner");
		require(!state.current(2) && state.regions.empty(), "Scene release must discard borrowed regions");
		std::cout << "Presented canvas owner/generation controls passed\n";
		return 0;
	}
	catch(const std::exception & error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
