# Changelog

## 0.1.0

- Initial release.
- Add `dart run flutter_open_xr build` command for arbitrary Flutter projects.
- Build flow now uses:
  - `flutter build bundle` from target project
  - Flutter engine-matched embedder download
  - bundled OpenXR native host build via CMake/MSBuild
