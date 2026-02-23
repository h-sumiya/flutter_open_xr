import "dart:io";

import "package:flutter_open_xr/flutter_open_xr.dart";

Future<void> main(List<String> args) async {
  final cli = FlutterOpenXrCli();
  final code = await cli.run(args, out: stdout, err: stderr);
  exit(code);
}
