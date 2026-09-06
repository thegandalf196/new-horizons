"""Client-only dependency consumer for local Linux -> MinGW x64 builds.

Uses the pinned VCMI dependency versions/profile options without pulling Qt,
ONNX or Discord. Lua, oneTBB (adventure AI), combat AI and FFmpeg stay enabled.
This is not an MSVC-binary conversion and does not modify the pinned submodule.
"""

from pathlib import Path
from conan import ConanFile
from conan.tools.cmake import CMakeDeps, CMakeToolchain
from conan.tools.files import save


class NewHorizonsMinGWClient(ConanFile):
    settings = "os", "arch", "compiler", "build_type"
    generators = "VirtualBuildEnv"
    requires = (
        "boost/[>=1.74 <1.87]", "minizip/[^1.2.12]", "zlib/[^1.2.12]",
        "libiconv/1.17", "libsquish/[^1.15]", "onetbb/[^2021.7]",
        "sdl/[^3.4.0]", "sdl_image/[^3.4.4]", "sdl_mixer/[^3.2.0]",
        "sdl_ttf/[^3.2.2]", "lua/[>=5.4.7 <5.5]", "opus/[<1.6]", "ffmpeg/[>=4.4]",
    )

    def generate(self):
        # The pinned oneTBB recipe declares both tbb and tbb12 on Windows,
        # but its MinGW package contains only libtbb12.dll.a. Adjust this
        # consumer's generated metadata, never rename/mutate cached libraries.
        if self.settings.os == "Windows" and self.settings.compiler == "gcc":
            tbb = self.dependencies.host["onetbb"]
            component = tbb.cpp_info.components["libtbb"]
            libraries = list(component.libs)
            if libraries == ["tbb", "tbb12"]:
                folders = [Path(directory) for directory in tbb.cpp_info.libdirs]
                generic = any((folder / name).is_file() for folder in folders
                              for name in ("libtbb.dll.a", "libtbb.a", "tbb.lib"))
                versioned = any((folder / "libtbb12.dll.a").is_file() for folder in folders)
                if not generic and versioned:
                    component.libs = ["tbb12"]
                    self.output.info("MinGW oneTBB: use existing versioned import library tbb12")
        CMakeDeps(self).generate()
        toolchain = CMakeToolchain(self)
        toolchain.variables["USING_CONAN"] = True
        runtime = []
        for dependency in self.dependencies.host.values():
            for directory in dependency.cpp_info.bindirs:
                runtime.extend(str(path).replace("\\", "/") for path in Path(directory).glob("*.dll"))
        manifest = str(Path(self.generators_folder) / "_runtime_libs.txt")
        save(self, manifest, "\n".join(sorted(set(runtime))))
        toolchain.variables["CONAN_RUNTIME_LIBS_FILE"] = manifest
        toolchain.variables["ENABLE_SDL3"] = True
        toolchain.generate()
