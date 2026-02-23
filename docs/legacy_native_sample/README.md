# flutter_xr

> 注: この検証プロジェクト本体は `legacy/native_sample/` に移動しています。
> CMake 実行時は `-S legacy/native_sample -B legacy/native_sample/build` のように明示してください。

Windows 上で OpenXR 空間に Flutter UI を表示し、コントローラー入力を Flutter のポインタイベントへ変換するサンプルです。

現在の実装は `flutter_xr` ターゲットを正式ターゲットとして扱います。

## 前提

- Windows 10/11
- Visual Studio 2022 (C++ ツールチェーン)
- Flutter SDK (`flutter` コマンドが PATH で実行可能)
- OpenXR Runtime (Meta Quest Link / SteamVR など)
- `reference/OpenXR-SDK` が存在すること

`reference/OpenXR-SDK` が無い場合:

```powershell
git clone https://github.com/KhronosGroup/OpenXR-SDK.git reference/OpenXR-SDK
```

## プロジェクト構成

- `src/flutter_xr/shared.*`
  OpenXR/D3D 共通ユーティリティ、数値計算、例外補助
- `src/flutter_xr/app_core.cpp`
  OpenXR セッション管理、レンダーループ、終了処理
- `src/flutter_xr/app_input.cpp`
  OpenXR Action 入力、レイと Quad の交差判定、Flutter pointer 送信
- `src/flutter_xr/app_flutter.cpp`
  Flutter embedder 起動、オフスクリーンフレーム受信、D3D テクスチャ更新
- `src/flutter_xr/main.cpp`
  実行エントリポイント

この分割により、スクロール・ドラッグなどの入力拡張は `app_input.cpp`、Flutter 統合変更は `app_flutter.cpp`、OpenXR 表示拡張は `app_core.cpp` に閉じて実装できます。

## 再現性のあるビルド

`CMakePresets.json` を使う手順:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --preset vs2022-x64
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build --preset release
```

`cmake` が PATH にある環境では以下でも同じです:

```powershell
cmake --preset vs2022-x64
cmake --build --preset release
```

ビルド時に以下を自動実行します。

- `flutter --version --machine` から `engineRevision` を取得
- 対応する `windows-x64-embedder.zip` をダウンロードして展開
- `flutter_samples/offscreen_ui` の `flutter pub get`
- `flutter build bundle --debug --target-platform=windows-x64`

## 実行

```powershell
.\build\bin\Release\flutter_xr.exe
```

実行には OpenXR Runtime と HMD 接続が必要です。終了はコンソールで `Esc` または `Q` です。

右手コントローラーのポーズが有効なとき、手元から指している方向へ細い光線（Ray）を描画します。
Quad にヒットしている場合はヒット位置まで、ヒットしていない場合は一定長まで表示します。

## CI / パッケージ化に向けた前提

- ネイティブ処理は `flutter_xr_runtime` 静的ライブラリに集約済み
- 実行バイナリ (`flutter_xr`) は薄いエントリポイントのみ
- Flutter アセット生成・embedder 配置は CMake のカスタムターゲット化済み

このため、GitHub Actions では `--preset` ベースで同一手順を実行しやすく、将来的に Flutter パッケージ側から呼ぶランチャー/プラグイン層を追加しやすい構成です。
