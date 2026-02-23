import "dart:io";

import "package:args/args.dart";
import "package:path/path.dart" as p;

import "build_options.dart";
import "builder.dart";

class FlutterOpenXrCli {
  Future<int> run(
    List<String> args, {
    required IOSink out,
    required IOSink err,
  }) async {
    if (args.isEmpty ||
        args.first == "-h" ||
        args.first == "--help" ||
        args.first == "help") {
      _printGeneralHelp(out);
      return 0;
    }

    final command = args.first;
    switch (command) {
      case "build":
        return _runBuild(args.sublist(1), out: out, err: err);
      default:
        err.writeln("Unknown command: $command");
        _printGeneralHelp(err);
        return 64;
    }
  }

  Future<int> _runBuild(
    List<String> args, {
    required IOSink out,
    required IOSink err,
  }) async {
    final parser = ArgParser(allowTrailingOptions: true)
      ..addOption("project-dir", help: "Flutter project directory.")
      ..addOption("output-dir", help: "Directory for final runtime output.")
      ..addOption(
        "openxr-sdk-dir",
        help: "Use an existing OpenXR-SDK checkout instead of cloning.",
      )
      ..addOption(
        "cmake",
        defaultsTo: "cmake",
        help: "CMake executable path.",
      )
      ..addOption(
        "flutter",
        defaultsTo: "flutter",
        help: "Flutter executable path.",
      )
      ..addOption(
        "git",
        defaultsTo: "git",
        help: "Git executable path.",
      )
      ..addOption(
        "configuration",
        defaultsTo: "Release",
        allowed: const ["Debug", "Release", "RelWithDebInfo", "MinSizeRel"],
        help: "Native build configuration.",
      )
      ..addFlag(
        "dry-run",
        negatable: false,
        help: "Print planned commands without executing build steps.",
      )
      ..addFlag("help", abbr: "h", negatable: false, help: "Show this help.");

    late ArgResults parsed;
    try {
      parsed = parser.parse(args);
    } on ArgParserException catch (e) {
      err.writeln(e.message);
      _printBuildHelp(err, parser);
      return 64;
    }

    if (parsed["help"] == true) {
      _printBuildHelp(out, parser);
      return 0;
    }

    final projectDir = Directory(
      p.normalize(
        Directory(
          parsed["project-dir"] == null
              ? Directory.current.path
              : parsed["project-dir"] as String,
        ).absolute.path,
      ),
    );
    final outputDir = parsed["output-dir"] == null
        ? null
        : Directory(
            p.normalize(
              Directory(parsed["output-dir"] as String).absolute.path,
            ),
          );
    final openXrSdkDir = parsed["openxr-sdk-dir"] == null
        ? null
        : Directory(
            p.normalize(
              Directory(parsed["openxr-sdk-dir"] as String).absolute.path,
            ),
          );

    final options = BuildOptions(
      projectDir: projectDir,
      outputDir: outputDir,
      openXrSdkDir: openXrSdkDir,
      cmakeExecutable: parsed["cmake"] as String,
      flutterExecutable: parsed["flutter"] as String,
      gitExecutable: parsed["git"] as String,
      configuration: parsed["configuration"] as String,
      dryRun: parsed["dry-run"] as bool,
    );

    return FlutterOpenXrBuilder().build(options, out: out, err: err);
  }

  void _printGeneralHelp(IOSink out) {
    out.writeln("flutter_open_xr command line interface");
    out.writeln();
    out.writeln("Usage:");
    out.writeln("  dart run flutter_open_xr <command> [options]");
    out.writeln();
    out.writeln("Commands:");
    out.writeln(
        "  build    Build a Flutter project into a Windows OpenXR host runtime.");
    out.writeln();
    out.writeln(
        "Run 'dart run flutter_open_xr build --help' for build options.");
  }

  void _printBuildHelp(IOSink out, ArgParser parser) {
    out.writeln("Usage:");
    out.writeln("  dart run flutter_open_xr build [options]");
    out.writeln();
    out.writeln(parser.usage);
  }
}
