# flutter_open_xr example

`example/` は `flutter_open_xr` を親ディレクトリから `path` 参照する最小カウンターアプリです。

## 使い方

PowerShell:

```powershell
cd example
flutter pub get
dart run flutter_open_xr build --dry-run
```

実ビルド:

```powershell
dart run flutter_open_xr build
```

既存の OpenXR-SDK を使う場合:

```powershell
dart run flutter_open_xr build --openxr-sdk-dir ..\reference\OpenXR-SDK
```
