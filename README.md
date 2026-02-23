# flutter_open_xr

[![pub package](https://img.shields.io/pub/v/flutter_open_xr.svg)](https://pub.dev/packages/flutter_open_xr)

`flutter_open_xr` は、任意の Flutter プロジェクトを Windows 向け OpenXR ホストで実行するための CLI パッケージです。

`dart run flutter_open_xr build` を実行すると、次を自動実行します。

1. `flutter pub get`
2. `flutter build bundle --debug --target-platform=windows-x64`
3. Flutter `engineRevision` に一致する `windows-x64-embedder.zip` を取得
4. OpenXR-SDK (`release-1.1.57`) を取得（`--openxr-sdk-dir` 未指定時）
5. 同梱ネイティブホスト (`native/windows`) を CMake + MSBuild でビルド

## インストール（pub.dev）

パッケージページ: https://pub.dev/packages/flutter_open_xr

Flutter プロジェクトで:

```powershell
flutter pub add --dev flutter_open_xr
```

または `pubspec.yaml` に直接記載:

```yaml
dev_dependencies:
  flutter_open_xr: ^0.1.1
```

## 使い方

対象 Flutter プロジェクトのルートで実行:

```powershell
dart run flutter_open_xr build
```

主要オプション:

```text
--project-dir <path>      対象 Flutter プロジェクトディレクトリ
--output-dir <path>       最終出力先ディレクトリ
--openxr-sdk-dir <path>   既存の OpenXR-SDK を使う
--cmake <path>            cmake 実行ファイル
--flutter <path>          flutter 実行ファイル
--git <path>              git 実行ファイル
--configuration <name>    Debug / Release / RelWithDebInfo / MinSizeRel
--dry-run                 実行せずコマンドのみ表示
```

ヘルプ表示:

```powershell
dart run flutter_open_xr build --help
```

## 出力先

デフォルト出力:

```text
<project>/build/flutter_open_xr/windows/Release
```

実行ファイル:

```text
flutter_open_xr_runner.exe
```

## 前提

- Windows 10/11
- Visual Studio 2022 Build Tools (C++)
- CMake
- Flutter SDK
- OpenXR Runtime (Quest Link / SteamVR など)

## ローカルサンプル

このリポジトリには `example/` に最小カウンターサンプルがあります。  
`example/pubspec.yaml` では親ディレクトリを `path` 参照しています。

```yaml
dev_dependencies:
  flutter_open_xr:
    path: ..
```

```powershell
cd example
flutter pub get
dart run flutter_open_xr build --dry-run
```
