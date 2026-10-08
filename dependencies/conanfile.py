from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.apple import is_apple_os
from conan.tools.microsoft import is_msvc

from os import getenv
from pathlib import Path

# sdl_mixer recipe requires >=2.25
required_conan_version = ">=2.25.0"

class VCMI(ConanFile):
    settings = "os", "compiler", "build_type", "arch"

    _libRequires = [
        "minizip/[^1.2.12]",
        "zlib/[^1.2.12]",
        "libiconv/1.17",
    ]
    _clientRequires = [
        "libsquish/[^1.15]",
        "onetbb/[^2021.7]",
        "sdl_image/[^3.4.4]",
        "sdl_mixer/[^3.2.0]",
        "sdl_ttf/[^3.2.2]",
    ]
    _launcherRequires = [
        "xz_utils/[^5.2.5]", # innoextract
    ]
    requires = _libRequires + _clientRequires + _launcherRequires

    options = {
        "target_pre_windows10": [True, False],
        "with_ffmpeg": [True, False],
        "with_onnxruntime": [True, False],
        "with_discord_presence": [True, False],
        "lua_lib": ["luajit", "lua"],
    }
    default_options = {
        "target_pre_windows10": False,
        "with_ffmpeg": True,
        "with_onnxruntime": True,
        "with_discord_presence": True,
        "lua_lib": "luajit",
    }

    def config_options(self):
        isMobile = self.settings.os == "iOS" or self.settings.os == "Android"

        # shared options should be synced with conan_profiles/base/common (and possibly others)
        self.options["boost"].shared = True
        self.options["bzip2"].shared = True
        self.options["libiconv"].shared = True
        self.options["libpng"].shared = True
        self.options["minizip"].shared = True
        self.options["ogg"].shared = True
        self.options["opus"].shared = True
        self.options["qt"].shared = self.settings.os != "iOS"
        self.options["xz_utils"].shared = True
        self.options["zlib"].shared = True

        # static on "single app" platforms
        isSdlShared = not isMobile
        self.options["sdl"].shared = isSdlShared
        self.options["sdl_image"].shared = isSdlShared
        self.options["sdl_mixer"].shared = isSdlShared
        self.options["sdl_ttf"].shared = isSdlShared

        self.options["qt"].openssl = not is_apple_os(self)
        self.options["qt"].qtsvg = True
        if self.settings.os == "Android":
            self.options["qt"].android_sdk = getenv("ANDROID_HOME")
        if isMobile:
            self.options["qt"].opengl = "es2"

        if self.settings.os != "Windows":
            del self.options.target_pre_windows10

        if isMobile:
            del self.options.with_discord_presence

    # hard requirements on dependencies' options
    def configure(self):
        self.options["qt"].qttools = True
        self.options["qt"].with_md4c = True
        if self.settings.os == "Android":
            # TODO: in Qt 6 this option doesn't exist
            self.options["qt"].qtandroidextras = True

        if self.options.lua_lib == "lua":
            self.options["lua"].compile_as_cpp = True

        if is_msvc(self):
            # required because VCMI uses dynamic runtime
            self.options["boost"].shared = True
            self.options["ffmpeg"].shared = True

    def requirements(self):
        # onnxruntime depends on exact boost version
        # placing it before our boost requirement ensures that this version will be in the graph to prevent conflicts like:
        # Conflict between boost/1.83.0 and boost/1.90.0 in the graph.
        # see https://docs.conan.io/2/knowledge/faq.html#getting-version-conflicts-even-when-using-version-ranges
        if self.options.with_onnxruntime:
            self.requires("onnxruntime/1.18.1")

        # lib
        # boost::filesystem removed support for Windows < 10 in v1.87
        boostMinVersion = "1.74"
        if self.options.get_safe("target_pre_windows10", False):
            self.requires(f"boost/[>={boostMinVersion} <1.87]")
        else:
            self.requires(f"boost/[^{boostMinVersion}]")

        luajitVersionFile = Path(__spec__.origin).resolve().parent / "luajit_version"
        luaLib = str(self.options.lua_lib)
        luaLibVersion = {
            "lua": "[^5.4.7]",
            "luajit": luajitVersionFile.read_text().rstrip(),
        }.get(luaLib)
        self.requires(f"{luaLib}/{luaLibVersion}")

        # client
        if self.options.with_ffmpeg:
            self.requires("opus/[<1.6]") # 1.6 doesn't build for Windows ARM: https://github.com/xiph/opus/pull/478
            self.requires("ffmpeg/[>=4.4]")

        if self.options.get_safe("with_discord_presence", False):
            self.requires("fmt/[>=12.1.0]")
            self.requires("glaze/[>=5.5.4]")

        # sdl_mixer requires SDL 3.4+
        self.requires("sdl/[^3.4.0]")

        # launcher
        if self.settings.os == "Android":
            self.requires("qt/[~5.15.14]") # earlier versions have serious bugs
        else:
            self.requires("qt/[~5.15.2]")

    def validate(self):
        if not is_apple_os(self) and self.dependencies["qt"].options.openssl != True:
            self.output.warning("qt:openssl option for non-Apple OS should be set to True, otherwise mods can't be downloaded")
