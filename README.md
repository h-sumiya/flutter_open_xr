# flutter_open_xr

[English](README.md) | [日本語](README.ja.md)

[![pub package](https://img.shields.io/pub/v/flutter_open_xr.svg)](https://pub.dev/packages/flutter_open_xr)

![flutter_open_xr counter sample](screenshot/counter.png)

Build Flutter UI into a Windows OpenXR runtime host with one command.

> [!WARNING]
> This project is in active development and still experimental.
> APIs, output structure, and build behavior may change between releases.

## Why this project

`flutter_open_xr` is a CLI package for teams that want to:

- Keep Flutter as the app UI layer.
- Run inside a native Windows OpenXR host.
- Reproduce builds from a predictable, scripted pipeline.

## What the build command does

`dart run flutter_open_xr build` runs these steps:

1. `flutter pub get`
2. `flutter build bundle --debug --target-platform=windows-x64`
3. Download the Flutter embedder ZIP pinned to your local Flutter `engineRevision`
4. Use OpenXR-SDK `release-1.1.57` (or your own checkout via `--openxr-sdk-dir`)
5. Build `native/windows` via CMake + MSBuild and copy runtime files to output

## Install

```powershell
flutter pub add --dev flutter_open_xr
```

Or in `pubspec.yaml`:

```yaml
dev_dependencies:
  flutter_open_xr: ^0.1.1
```

## Quick start

From your Flutter project root:

```powershell
dart run flutter_open_xr build
```

Show all options:

```powershell
dart run flutter_open_xr build --help
```

Dry-run to verify commands without executing:

```powershell
dart run flutter_open_xr build --dry-run
```

## Build options

```text
--project-dir <path>      Flutter project directory (default: current directory)
--output-dir <path>       Output directory for runtime artifacts
--openxr-sdk-dir <path>   Existing OpenXR-SDK directory to use (skip clone)
--cmake <path>            CMake executable path (default: cmake)
--flutter <path>          Flutter executable path (default: flutter)
--git <path>              Git executable path (default: git)
--configuration <name>    Debug / Release / RelWithDebInfo / MinSizeRel (default: Release)
--dry-run                 Print commands only
```

## Requirements

- Windows 10/11
- Visual Studio 2022 Build Tools (C++ toolchain)
- CMake
- Flutter SDK
- OpenXR Runtime (for example Quest Link or SteamVR)

## Output

Default output directory:

```text
<project>/build/flutter_open_xr/windows/Release
```

Main executable:

```text
flutter_open_xr_runner.exe
```

## Local example

This repository includes a sample app in `example/`.
It uses a local path dependency:

```yaml
dev_dependencies:
  flutter_open_xr:
    path: ..
```

Run:

```powershell
cd example
flutter pub get
dart run flutter_open_xr build --dry-run
```
