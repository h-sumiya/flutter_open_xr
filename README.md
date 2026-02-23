# flutter_open_xr

`flutter_open_xr` は、任意の Flutter プロジェクトを Windows 向け OpenXR ホストで実行するための CLI パッケージです。
このリポジトリのルートがそのまま Dart パッケージになっています。

`dev_dependencies` に追加して、対象プロジェクト側で次を実行します。

```powershell
dart run flutter_open_xr build
```

このコマンドは次を自動実行します。

1. `flutter pub get`
2. `flutter build bundle --debug --target-platform=windows-x64`
3. Flutter `engineRevision` に一致する `windows-x64-embedder.zip` を取得
4. OpenXR-SDK (`release-1.1.57`) を取得（未指定時）
5. 同梱ネイティブホスト (`native/windows`) を CMake + MSBuild でビルド

## インストール例

```yaml
dev_dependencies:
  flutter_open_xr:
    git:
      url: https://example.invalid/your-repo.git
```

## 使い方

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

## 旧構成のメモ

以前のネイティブ検証プロジェクトは `legacy/native_sample/` に移動し、
関連メモは `docs/legacy_native_sample/` に移動しています。
