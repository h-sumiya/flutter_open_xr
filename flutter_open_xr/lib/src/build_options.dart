import "dart:io";

class BuildOptions {
  const BuildOptions({
    required this.projectDir,
    this.outputDir,
    this.openXrSdkDir,
    this.cmakeExecutable = "cmake",
    this.flutterExecutable = "flutter",
    this.gitExecutable = "git",
    this.configuration = "Release",
    this.openXrSdkTag = "release-1.1.57",
    this.dryRun = false,
  });

  final Directory projectDir;
  final Directory? outputDir;
  final Directory? openXrSdkDir;
  final String cmakeExecutable;
  final String flutterExecutable;
  final String gitExecutable;
  final String configuration;
  final String openXrSdkTag;
  final bool dryRun;
}
