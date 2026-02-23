# flutter_xr

`PLAN.md` の手順1/2向けに、Windows 上の最小サンプルを用意しています。

## 前提

- Windows 10/11
- Visual Studio 2022 (C++ ツールチェーン)
- CMake 3.21+
- Flutter SDK (`flutter` コマンドが PATH で実行可能)
- OpenXR Runtime (例: Meta Quest Link / SteamVR など) が有効
- `reference/OpenXR-SDK` が存在すること

`reference/OpenXR-SDK` が無い場合:

```powershell
git clone https://github.com/KhronosGroup/OpenXR-SDK.git reference/OpenXR-SDK
```

## ビルド（共通）

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
```

`cmake` が PATH に無い環境では、Visual Studio 付属の `cmake.exe` をフルパス指定してください。

## 手順1サンプル: `xr_quad`

```powershell
cmake --build build --config Release --target xr_quad
```

```powershell
.\build\bin\Release\xr_quad.exe
```

実行すると、`LOCAL` 空間の前方約 1.2m にチェック柄の Quad を表示します。
終了はコンソールで `Esc` または `Q` キーです。

## 手順2サンプル: `flutter_offscreen`

このターゲットは CMake 実行時に検出したインストール済み Flutter バージョンを使います。
ビルド時に以下を自動実行します。

- `flutter --version --machine` から `engineRevision` を取得
- `https://storage.googleapis.com/flutter_infra_release/flutter/<engineRevision>/windows-x64/windows-x64-embedder.zip` を取得して展開
- `flutter_samples/offscreen_ui` の `flutter pub get`
- `flutter build bundle --debug --target-platform=windows-x64`

```powershell
cmake --build build --config Release --target flutter_offscreen
```

```powershell
.\build\bin\Release\flutter_offscreen.exe
```

`flutter_offscreen.exe` は low-level embedder (`flutter_embedder.h`) で Flutter を起動し、オフスクリーン software surface の初回フレームコールバック到達を確認します（画面表示はしません）。

実行に必要な `flutter_engine.dll` / `data/flutter_assets` はビルド時に実行ファイル横へ自動配置されます。`icudtl.dat` はローカル Flutter SDK キャッシュに存在する場合のみ追加で配置します。
