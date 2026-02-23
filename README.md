# flutter_xr

`PLAN.md` の手順1向けに、Windows + D3D11 で `XrCompositionLayerQuad` を表示する最小サンプルを用意しています。

## 前提

- Windows 10/11
- Visual Studio 2022 (C++ ツールチェーン)
- CMake 3.21+
- OpenXR Runtime (例: Meta Quest Link / SteamVR など) が有効
- `reference/OpenXR-SDK` が存在すること

`reference/OpenXR-SDK` が無い場合:

```powershell
git clone https://github.com/KhronosGroup/OpenXR-SDK.git reference/OpenXR-SDK
```

## ビルド

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target xr_quad
```

## 実行

```powershell
.\build\bin\Release\xr_quad.exe
```

実行すると、`LOCAL` 空間の前方約 1.2m にチェック柄の Quad を表示します。
終了はコンソールで `Esc` または `Q` キーです。
