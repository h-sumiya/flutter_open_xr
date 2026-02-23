import "dart:convert";
import "dart:io";
import "dart:isolate";

import "package:path/path.dart" as p;

import "build_options.dart";

class FlutterOpenXrBuilder {
  static const String _openXrSdkRepository =
      "https://github.com/KhronosGroup/OpenXR-SDK.git";

  Future<int> build(
    BuildOptions options, {
    required IOSink out,
    required IOSink err,
  }) async {
    if (!Platform.isWindows) {
      err.writeln("flutter_open_xr currently supports Windows only.");
      return 2;
    }

    if (!options.projectDir.existsSync()) {
      err.writeln(
          "Project directory does not exist: ${options.projectDir.path}");
      return 2;
    }

    final pubspecPath = p.join(options.projectDir.path, "pubspec.yaml");
    if (!File(pubspecPath).existsSync()) {
      err.writeln("pubspec.yaml was not found: $pubspecPath");
      err.writeln(
          "Run this command from a Flutter project or pass --project-dir.");
      return 2;
    }

    final packageRoot = await _resolvePackageRoot();
    if (packageRoot == null) {
      err.writeln("Could not resolve flutter_open_xr package location.");
      return 2;
    }

    final nativeSourceDir = Directory(
      p.join(packageRoot.path, "native", "windows"),
    );
    if (!nativeSourceDir.existsSync()) {
      err.writeln("Native host source is missing: ${nativeSourceDir.path}");
      return 2;
    }

    final workspaceDir = Directory(
      p.join(options.projectDir.path, ".dart_tool", "flutter_open_xr"),
    );
    final assetsDir = Directory(p.join(workspaceDir.path, "flutter_assets"));
    final downloadsDir = Directory(p.join(workspaceDir.path, "downloads"));
    final embedderRootDir = Directory(p.join(workspaceDir.path, "embedder"));
    final nativeBuildDir = Directory(p.join(workspaceDir.path, "native_build"));
    final outputDir = options.outputDir ??
        Directory(
          p.join(
            options.projectDir.path,
            "build",
            "flutter_open_xr",
            "windows",
            options.configuration,
          ),
        );

    final needGit = options.openXrSdkDir == null;
    final toolCheck = await _checkRequiredTools(
      options,
      out: out,
      err: err,
      needGit: needGit,
    );
    if (toolCheck != 0) {
      return toolCheck;
    }

    final flutterInfo = await _readFlutterInfo(
      options.flutterExecutable,
      options.projectDir.path,
      err,
    );
    if (flutterInfo == null) {
      return 2;
    }

    final embedderDir = Directory(
      p.join(embedderRootDir.path, flutterInfo.engineRevision),
    );
    final openXrSdkDir = options.openXrSdkDir ??
        Directory(
          p.join(
            workspaceDir.path,
            "deps",
            "OpenXR-SDK-${options.openXrSdkTag}",
          ),
        );
    final flutterIcuDataPath = p.join(
      flutterInfo.flutterRoot,
      "bin",
      "cache",
      "artifacts",
      "engine",
      "windows-x64",
      "icudtl.dat",
    );

    out.writeln("Project: ${options.projectDir.path}");
    out.writeln("Flutter SDK: ${flutterInfo.flutterVersion}");
    out.writeln("Flutter engine revision: ${flutterInfo.engineRevision}");
    out.writeln("Workspace: ${workspaceDir.path}");
    out.writeln("Output: ${outputDir.path}");

    if (!options.dryRun) {
      workspaceDir.createSync(recursive: true);
      downloadsDir.createSync(recursive: true);
      embedderRootDir.createSync(recursive: true);
      nativeBuildDir.createSync(recursive: true);
      outputDir.createSync(recursive: true);
    }

    final pubGetCode = await _runCommand(
      options.flutterExecutable,
      const ["pub", "get"],
      workingDirectory: options.projectDir.path,
      dryRun: options.dryRun,
      out: out,
      err: err,
    );
    if (pubGetCode != 0) {
      return pubGetCode;
    }

    final bundleCode = await _runCommand(
      options.flutterExecutable,
      [
        "build",
        "bundle",
        "--debug",
        "--target-platform=windows-x64",
        "--asset-dir",
        assetsDir.path,
      ],
      workingDirectory: options.projectDir.path,
      dryRun: options.dryRun,
      out: out,
      err: err,
    );
    if (bundleCode != 0) {
      return bundleCode;
    }

    final embedderCode = await _ensureFlutterEmbedder(
      engineRevision: flutterInfo.engineRevision,
      cmakeExecutable: options.cmakeExecutable,
      downloadsDir: downloadsDir,
      embedderDir: embedderDir,
      dryRun: options.dryRun,
      out: out,
      err: err,
    );
    if (embedderCode != 0) {
      return embedderCode;
    }

    final openXrCode = await _ensureOpenXrSdk(
      options: options,
      openXrSdkDir: openXrSdkDir,
      dryRun: options.dryRun,
      out: out,
      err: err,
    );
    if (openXrCode != 0) {
      return openXrCode;
    }

    final configureCode = await _runCommand(
      options.cmakeExecutable,
      [
        "-S",
        nativeSourceDir.path,
        "-B",
        nativeBuildDir.path,
        "-G",
        "Visual Studio 17 2022",
        "-A",
        "x64",
        "-DOPENXR_SDK_DIR=${openXrSdkDir.path}",
        "-DFLUTTER_EMBEDDER_DIR=${embedderDir.path}",
        "-DFLUTTER_ASSETS_DIR=${assetsDir.path}",
        "-DFLUTTER_ICUDTL_PATH=$flutterIcuDataPath",
      ],
      dryRun: options.dryRun,
      out: out,
      err: err,
    );
    if (configureCode != 0) {
      return configureCode;
    }

    final nativeBuildCode = await _runCommand(
      options.cmakeExecutable,
      [
        "--build",
        nativeBuildDir.path,
        "--config",
        options.configuration,
        "--target",
        "flutter_open_xr_runner",
      ],
      dryRun: options.dryRun,
      out: out,
      err: err,
    );
    if (nativeBuildCode != 0) {
      return nativeBuildCode;
    }

    final runtimeDir = Directory(
      p.join(nativeBuildDir.path, "bin", options.configuration),
    );
    final outputExe =
        File(p.join(outputDir.path, "flutter_open_xr_runner.exe"));

    if (options.dryRun) {
      out.writeln("[dry-run] copy ${runtimeDir.path} -> ${outputDir.path}");
      out.writeln("[dry-run] expected executable: ${outputExe.path}");
      return 0;
    }

    if (!runtimeDir.existsSync()) {
      err.writeln("Native output directory was not found: ${runtimeDir.path}");
      return 2;
    }

    _copyDirectory(runtimeDir, outputDir);

    if (!outputExe.existsSync()) {
      err.writeln(
        "Build finished but flutter_open_xr_runner.exe was not found at ${outputExe.path}",
      );
      return 2;
    }

    out.writeln("Build completed.");
    out.writeln("Executable: ${outputExe.path}");
    return 0;
  }

  Future<int> _checkRequiredTools(
    BuildOptions options, {
    required IOSink out,
    required IOSink err,
    required bool needGit,
  }) async {
    final cmakeResult = await _checkTool(
      executable: options.cmakeExecutable,
      args: const ["--version"],
      label: "CMake",
      err: err,
    );
    if (cmakeResult != 0) {
      return cmakeResult;
    }

    final flutterResult = await _checkTool(
      executable: options.flutterExecutable,
      args: const ["--version"],
      label: "Flutter",
      err: err,
    );
    if (flutterResult != 0) {
      return flutterResult;
    }

    if (needGit) {
      final gitResult = await _checkTool(
        executable: options.gitExecutable,
        args: const ["--version"],
        label: "Git",
        err: err,
      );
      if (gitResult != 0) {
        return gitResult;
      }
    } else {
      out.writeln(
        "OpenXR SDK clone step is skipped because --openxr-sdk-dir is provided.",
      );
    }

    return 0;
  }

  Future<_FlutterInfo?> _readFlutterInfo(
    String flutterExecutable,
    String workingDirectory,
    IOSink err,
  ) async {
    late ProcessResult result;
    try {
      result = await Process.run(
        flutterExecutable,
        const ["--version", "--machine"],
        workingDirectory: workingDirectory,
        runInShell: Platform.isWindows,
      );
    } on ProcessException catch (e) {
      err.writeln("Failed to run flutter: ${e.message}");
      return null;
    }

    if (result.exitCode != 0) {
      err.writeln("flutter --version --machine failed.");
      final stderrText = result.stderr.toString().trim();
      if (stderrText.isNotEmpty) {
        err.writeln(stderrText);
      }
      return null;
    }

    final stdoutText = result.stdout.toString();
    final dynamic parsed;
    try {
      parsed = jsonDecode(stdoutText);
    } on FormatException catch (e) {
      err.writeln("Failed to parse flutter --version --machine output: $e");
      return null;
    }

    if (parsed is! Map<String, dynamic>) {
      err.writeln("Unexpected flutter --version output format.");
      return null;
    }

    final engineRevision = parsed["engineRevision"]?.toString();
    final flutterRoot = parsed["flutterRoot"]?.toString();
    final flutterVersion = parsed["flutterVersion"]?.toString() ?? "unknown";

    if (engineRevision == null || engineRevision.isEmpty) {
      err.writeln(
          "engineRevision was not found in flutter --version --machine.");
      return null;
    }
    if (flutterRoot == null || flutterRoot.isEmpty) {
      err.writeln("flutterRoot was not found in flutter --version --machine.");
      return null;
    }

    return _FlutterInfo(
      flutterVersion: flutterVersion,
      engineRevision: engineRevision,
      flutterRoot: flutterRoot,
    );
  }

  Future<int> _ensureFlutterEmbedder({
    required String engineRevision,
    required String cmakeExecutable,
    required Directory downloadsDir,
    required Directory embedderDir,
    required bool dryRun,
    required IOSink out,
    required IOSink err,
  }) async {
    final embedderHeader = File(p.join(embedderDir.path, "flutter_embedder.h"));
    final engineDll = File(p.join(embedderDir.path, "flutter_engine.dll"));
    final engineImportLib = File(
      p.join(embedderDir.path, "flutter_engine.dll.lib"),
    );
    if (embedderHeader.existsSync() &&
        engineDll.existsSync() &&
        engineImportLib.existsSync()) {
      out.writeln("Using cached Flutter embedder: ${embedderDir.path}");
      return 0;
    }

    final embedderUrl = Uri.parse(
      "https://storage.googleapis.com/flutter_infra_release/flutter/"
      "$engineRevision/windows-x64/windows-x64-embedder.zip",
    );
    final embedderZip = File(
      p.join(downloadsDir.path, "windows-x64-embedder-$engineRevision.zip"),
    );

    if (dryRun) {
      out.writeln("[dry-run] download $embedderUrl -> ${embedderZip.path}");
      out.writeln(
          "[dry-run] extract ${embedderZip.path} -> ${embedderDir.path}");
      return 0;
    }

    if (!embedderZip.existsSync()) {
      out.writeln("Downloading Flutter embedder: $embedderUrl");
      final downloadOk = await _downloadFile(
        embedderUrl,
        embedderZip,
        out: out,
        err: err,
      );
      if (!downloadOk) {
        return 2;
      }
    } else {
      out.writeln("Using cached embedder archive: ${embedderZip.path}");
    }

    embedderDir.createSync(recursive: true);
    final extractCode = await _runCommand(
      cmakeExecutable,
      ["-E", "tar", "xvf", embedderZip.path, "--format=zip"],
      workingDirectory: embedderDir.path,
      dryRun: false,
      out: out,
      err: err,
    );
    if (extractCode != 0) {
      return extractCode;
    }

    if (!embedderHeader.existsSync() ||
        !engineDll.existsSync() ||
        !engineImportLib.existsSync()) {
      err.writeln(
        "Embedder extraction completed, but expected files are missing in ${embedderDir.path}.",
      );
      return 2;
    }

    return 0;
  }

  Future<int> _ensureOpenXrSdk({
    required BuildOptions options,
    required Directory openXrSdkDir,
    required bool dryRun,
    required IOSink out,
    required IOSink err,
  }) async {
    final cmakeFile = File(p.join(openXrSdkDir.path, "CMakeLists.txt"));
    if (cmakeFile.existsSync()) {
      out.writeln("Using OpenXR SDK: ${openXrSdkDir.path}");
      return 0;
    }

    if (options.openXrSdkDir != null) {
      err.writeln(
        "OpenXR SDK directory is invalid: ${options.openXrSdkDir!.path}\n"
        "Expected file: ${cmakeFile.path}",
      );
      return 2;
    }

    final parent = openXrSdkDir.parent;
    if (dryRun) {
      out.writeln(
        "[dry-run] clone OpenXR-SDK ${options.openXrSdkTag} -> ${openXrSdkDir.path}",
      );
      return 0;
    }

    parent.createSync(recursive: true);
    final cloneCode = await _runCommand(
      options.gitExecutable,
      [
        "clone",
        "--branch",
        options.openXrSdkTag,
        "--depth",
        "1",
        _openXrSdkRepository,
        openXrSdkDir.path,
      ],
      dryRun: false,
      out: out,
      err: err,
    );
    if (cloneCode != 0) {
      return cloneCode;
    }

    if (!cmakeFile.existsSync()) {
      err.writeln("Cloned OpenXR SDK but CMakeLists.txt was not found.");
      return 2;
    }
    return 0;
  }

  Future<bool> _downloadFile(
    Uri url,
    File output, {
    required IOSink out,
    required IOSink err,
  }) async {
    output.parent.createSync(recursive: true);

    final client = HttpClient();
    try {
      final request = await client.getUrl(url);
      final response = await request.close();
      if (response.statusCode != HttpStatus.ok) {
        err.writeln("Download failed (${response.statusCode}): $url");
        return false;
      }

      final sink = output.openWrite();
      await response.pipe(sink);
      await sink.close();
      out.writeln("Saved: ${output.path}");
      return true;
    } on SocketException catch (e) {
      err.writeln("Network error while downloading $url: ${e.message}");
      return false;
    } on FileSystemException catch (e) {
      err.writeln("Failed to write ${output.path}: ${e.message}");
      return false;
    } finally {
      client.close(force: true);
    }
  }

  Future<int> _checkTool({
    required String executable,
    required List<String> args,
    required String label,
    required IOSink err,
  }) async {
    try {
      final result = await Process.run(
        executable,
        args,
        runInShell: Platform.isWindows,
      );
      if (result.exitCode == 0) {
        return 0;
      }
      err.writeln("$label check failed: ${_formatCommand(executable, args)}");
      final stderrText = result.stderr.toString().trim();
      if (stderrText.isNotEmpty) {
        err.writeln(stderrText);
      }
      return result.exitCode;
    } on ProcessException catch (e) {
      err.writeln("$label was not found: ${e.message}");
      return 2;
    }
  }

  Future<int> _runCommand(
    String executable,
    List<String> args, {
    String? workingDirectory,
    required bool dryRun,
    required IOSink out,
    required IOSink err,
  }) async {
    out.writeln("> ${_formatCommand(executable, args)}");
    if (workingDirectory != null) {
      out.writeln("  cwd: $workingDirectory");
    }

    if (dryRun) {
      return 0;
    }

    try {
      final process = await Process.start(
        executable,
        args,
        workingDirectory: workingDirectory,
        runInShell: Platform.isWindows,
        mode: ProcessStartMode.inheritStdio,
      );
      final code = await process.exitCode;
      if (code != 0) {
        err.writeln("Command failed with exit code $code.");
      }
      return code;
    } on ProcessException catch (e) {
      err.writeln("Failed to start command: ${e.message}");
      return 2;
    }
  }

  String _formatCommand(String executable, List<String> args) {
    final all = <String>[executable, ...args];
    return all.map(_quoteIfNeeded).join(" ");
  }

  String _quoteIfNeeded(String value) {
    if (!value.contains(" ")) {
      return value;
    }
    return "\"$value\"";
  }

  void _copyDirectory(Directory source, Directory destination) {
    destination.createSync(recursive: true);
    for (final entity in source.listSync(followLinks: false)) {
      final name = p.basename(entity.path);
      final targetPath = p.join(destination.path, name);
      if (entity is Directory) {
        _copyDirectory(entity, Directory(targetPath));
      } else if (entity is File) {
        entity.copySync(targetPath);
      }
    }
  }

  Future<Directory?> _resolvePackageRoot() async {
    final uri = await Isolate.resolvePackageUri(
      Uri.parse("package:flutter_open_xr/flutter_open_xr.dart"),
    );
    if (uri == null || uri.scheme != "file") {
      return null;
    }
    final libFile = File.fromUri(uri);
    return libFile.parent.parent;
  }
}

class _FlutterInfo {
  const _FlutterInfo({
    required this.flutterVersion,
    required this.engineRevision,
    required this.flutterRoot,
  });

  final String flutterVersion;
  final String engineRevision;
  final String flutterRoot;
}
