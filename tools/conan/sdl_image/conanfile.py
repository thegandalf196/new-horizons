# CCI sdl_image/3.4.4#072329d83163c19393337dc3820e3a5c; see LICENSE.recipe.
# Local changes: export/check/apply the maintained two-call interlace patch.
from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import copy, get, patch, rmdir
from conan.tools.scm import Version
import hashlib
import os
from pathlib import Path
import shutil
import types

required_conan_version = ">=2.4"


class SDLImageConan(ConanFile):
    name = "sdl_image"
    description = "SDL_image is an image file loading library"
    topics = ("sdl2", "sdl", "images", "opengl")
    url = "https://github.com/conan-io/conan-center-index"
    homepage = "https://github.com/libsdl-org/SDL_image"
    license = "MIT"
    exports = "LICENSE.recipe", "README.md"
    package_type = "library"
    settings = "os", "arch", "compiler", "build_type"
    options = {
        "shared": [True, False],
        "fPIC": [True, False],
        "with_libjpeg": [True, False],
        "with_libtiff": [True, False],
        "with_libpng": [True, False],
        "with_libwebp": [True, False],
        "with_avif": [True, False],
        "with_jxl": [True, False],
    }
    default_options = {
        "shared": False,
        "fPIC": True,
        "with_libjpeg": True,
        "with_libtiff": True,
        "with_libpng": True,
        "with_libwebp": True,
        "with_avif": True,
        "with_jxl": False,
    }
    implements = ["auto_shared_fpic"]
    languages = "C"

    def export_sources(self):
        # Export only the two maintained files, not a second tracked copy.
        recipe = Path(self.recipe_folder)
        maintained = recipe.parents[1] / "ci"
        if not (maintained / "prepare_sdl3_image_interlace.py").is_file():
            # The source collector reconstructs a standalone recipe with its
            # exact exported sources. Local source flow can use that layout.
            maintained = recipe
        for relative in ("prepare_sdl3_image_interlace.py", "patches/sdl3-image-3.4.4-interlace.patch"):
            source = maintained / relative
            if not source.is_file() or source.is_symlink():
                raise ConanInvalidConfiguration("Missing maintained interlace export")
            destination = Path(self.export_sources_folder) / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            if source.resolve() != destination.resolve():
                shutil.copyfile(source, destination)

    def layout(self):
        cmake_layout(self, src_folder="src")

    def requirements(self):
        self.requires("sdl/[>=3.2.20 <4]", transitive_headers=True)
        if self.options.with_libtiff:
            self.requires("libtiff/[>=4.6.0 <5]")
        if self.options.with_libjpeg:
            self.requires("libjpeg/[>=9e]")
        if self.options.with_libpng:
            self.requires("libpng/[>=1.6 <2]")
        if self.options.with_libwebp:
            self.requires("libwebp/[>=1.3.2 <2]")
        if self.options.with_avif:
            self.requires("libavif/[>=1.0.1 <2]")
        if self.options.with_jxl:
            self.requires("libjxl/0.11.1")

    def validate(self):
        if self.options.shared and not self.dependencies["sdl"].options.shared:
            raise ConanInvalidConfiguration(f"{self.ref} shared requires sdl shared")
        if Version(self.version).major != Version(self.dependencies["sdl"].ref.version).major:
            raise ConanInvalidConfiguration(f"{self.ref} and sdl must have the same major version")

    def build_requirements(self):
        self.tool_requires("cmake/[>=3.16 <5]")

    def source(self):
        if str(self.version) != "3.4.4":
            raise ConanInvalidConfiguration("Interlace patch supports only SDL_image 3.4.4")
        get(self, **self.conan_data["sources"][self.version], strip_root=True)
        exported = Path(self.export_sources_folder)
        checker_path = exported / "prepare_sdl3_image_interlace.py"
        checker = types.ModuleType("nh_sdl_image_interlace")
        checker.__file__ = str(checker_path)
        # Execute the trusted exported source without an import loader writing
        # bytecode into immutable exported build inputs or changing global flags.
        exec(compile(checker_path.read_bytes(), str(checker_path), "exec"), checker.__dict__)
        proof = checker.prepare(Path(self.source_folder))  # Portable check-only.
        patch(self, base_path=self.source_folder,
              patch_file=str(exported / "patches/sdl3-image-3.4.4-interlace.patch"), strip=1)
        result = (Path(self.source_folder) / "src/IMG_libpng.c").read_bytes()
        if hashlib.sha256(result).hexdigest() != proof["patchedSourceSha256"]:
            raise ConanInvalidConfiguration("Patched SDL_image source hash mismatch")
        rmdir(self, os.path.join(self.source_folder, "external"))

    def generate(self):
        tc = CMakeToolchain(self)
        tc.cache_variables["SDLIMAGE_VENDORED"] = False
        tc.cache_variables["SDLIMAGE_DEPS_SHARED"] = False
        tc.cache_variables["SDLIMAGE_SAMPLES"] = False
        tc.cache_variables["SDLIMAGE_STRICT"] = True
        tc.cache_variables["SDLIMAGE_AVIF"] = self.options.with_avif
        tc.cache_variables["SDLIMAGE_JPG"] = self.options.with_libjpeg
        tc.cache_variables["SDLIMAGE_JXL"] = self.options.with_jxl
        tc.cache_variables["SDLIMAGE_PNG"] = self.options.with_libpng
        tc.cache_variables["SDLIMAGE_TIF"] = self.options.with_libtiff
        tc.cache_variables["SDLIMAGE_WEBP"] = self.options.with_libwebp
        tc.cache_variables["SDLIMAGE_BACKEND_IMAGEIO"] = False
        tc.generate()
        cd = CMakeDeps(self)
        cd.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        copy(self, "LICENSE.txt", src=self.source_folder, dst=os.path.join(self.package_folder, "licenses"))
        cmake = CMake(self)
        cmake.install()

        rmdir(self, os.path.join(self.package_folder, "lib", "cmake"))
        rmdir(self, os.path.join(self.package_folder, "lib", "pkgconfig"))
        rmdir(self, os.path.join(self.package_folder, "share"))
        rmdir(self, os.path.join(self.package_folder, "cmake"))

    def package_info(self):
        lib_postfix = ""
        if self.settings.compiler == "msvc" and not self.options.shared:
            lib_postfix += "-static"

        self.cpp_info.set_property("cmake_file_name", "SDL3_image")
        self.cpp_info.set_property("cmake_target_name", "SDL3_image::SDL3_image")
        alias = "shared" if self.options.shared else "static"
        self.cpp_info.set_property("cmake_target_aliases", [f"SDL3_image::SDL3_image-{alias}"])
        self.cpp_info.libs = [f"SDL3_image{lib_postfix}"]
        self.cpp_info.set_property("pkg_config_name", "sdl3-image")
