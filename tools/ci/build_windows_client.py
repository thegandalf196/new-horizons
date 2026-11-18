#!/usr/bin/env python3
"""Configure or build the Windows client and CI regression targets with Ninja."""
# SPDX-License-Identifier: GPL-2.0-or-later

import argparse
import subprocess


TARGETS = (
    'vcmiclient',
    'nhWindowsBootstrap',
    'nhWindowsSetup',
    'nhSdl3GrayscaleMaskRuntimeTest',
    'nhArtStartupRuntimeTest',
    'nhImageOptimizerRuntimeTest',
    'nhCreaturePreviewSurfaceRuntimeTest',
    'nhSdl3OutlineRuntimeTest',
)


# Compile publication-guard users first so MSVC storage errors fail before the
# rest of the client. The normal Ninja build reuses these exact object outputs.
GUARD_OBJECT_TARGETS = tuple(
    'AI/BattleAI/CMakeFiles/BattleAI.dir/' + source + '.obj'
    for source in (
        'ForecastRoundIntegration.cpp',
        'ForecastActivationIntegration.cpp',
        'ForecastNativePreparation.cpp',
        'ForecastUnitUpdateIntegration.cpp',
        'ForecastEffectIntegration.cpp',
        'ForecastSelectedReceiptIntegration.cpp',
        'ForecastAcceptedAttackIntegration.cpp',
        'StackPreparedEffectState.cpp',
        'OwnedFrameEffectPublication.cpp',
    )
)


def configure(args):
    command = [
        args.cmake, '--preset', 'new-horizons-windows-x64',
        '-DENABLE_PCH=' + args.use_precompiled_headers,
        '-DENABLE_CCACHE=ON',
        '-DENABLE_NH_SDL3_MASK_REGRESSION=ON',
        '-DENABLE_NH_ART_CACHE_REGRESSION=ON',
        '-DENABLE_NH_PREVIEW_SURFACE_REGRESSION=ON',
        '-DENABLE_NH_SDL3_OUTLINE_REGRESSION=ON',
        '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
        '-DCMAKE_C_COMPILER_LAUNCHER=sccache',
        '-DCMAKE_CXX_COMPILER_LAUNCHER=sccache',
    ]
    return subprocess.run(command, check=False).returncode


def build(args):
    for targets, failure_options in (
        (GUARD_OBJECT_TARGETS, ('-k', '0')),
        (TARGETS, ()),
    ):
        command = [
            args.cmake, '--build', args.build_root, '--target', *targets,
            '--parallel', str(args.jobs), '--', '-d', 'keeprsp', *failure_options,
        ]
        status = subprocess.run(command, check=False).returncode
        if status:
            return status
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(required=True)
    configure_parser = commands.add_parser('configure', help='Configure the existing CI preset')
    configure_parser.add_argument('--use-precompiled-headers', choices=('ON', 'OFF'), default='ON')
    configure_parser.set_defaults(operation=configure)
    build_parser = commands.add_parser('build', help='Build the client, portable entry point and regression targets')
    build_parser.add_argument('--build-root', required=True, help='Configured Windows Ninja build directory')
    build_parser.add_argument('--jobs', type=int, choices=range(1, 11), default=4,
                              help='Maximum concurrent build jobs (default: 4)')
    build_parser.set_defaults(operation=build)
    for command in (configure_parser, build_parser):
        command.add_argument('--cmake', default='cmake', help='CMake executable for the configured toolchain')
    args = parser.parse_args(argv)
    return args.operation(args)


if __name__ == '__main__':
    raise SystemExit(main())
