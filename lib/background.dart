import "package:flutter/services.dart";

enum XrBackgroundKind {
  none,
  groundGrid,
  dds,
  glb,
}

class XrBackgroundCommandException implements Exception {
  const XrBackgroundCommandException(this.message);

  final String message;

  @override
  String toString() => "XrBackgroundCommandException: $message";
}

class XrBackgroundController {
  XrBackgroundController._();

  static const BasicMessageChannel<String?> _channel =
      BasicMessageChannel<String?>(
    "flutter_open_xr/background",
    StringCodec(),
  );

  static Future<void> setNone() {
    return _send("none");
  }

  static Future<void> setGroundGrid() {
    return _send("grid");
  }

  static Future<void> setDdsFile(String path) {
    final String normalized = path.trim();
    if (normalized.isEmpty) {
      throw const XrBackgroundCommandException(
        "DDS file path is empty.",
      );
    }
    return _send("dds|$normalized");
  }

  static Future<void> setGlbFile(String path) {
    final String normalized = path.trim();
    if (normalized.isEmpty) {
      throw const XrBackgroundCommandException(
        "GLB file path is empty.",
      );
    }
    return _send("glb|$normalized");
  }

  static Future<void> set(
    XrBackgroundKind kind, {
    String? path,
  }) {
    switch (kind) {
      case XrBackgroundKind.none:
        return setNone();
      case XrBackgroundKind.groundGrid:
        return setGroundGrid();
      case XrBackgroundKind.dds:
        return setDdsFile(path ?? "");
      case XrBackgroundKind.glb:
        return setGlbFile(path ?? "");
    }
  }

  static Future<void> _send(String command) async {
    final String? response = await _channel.send(command);
    final String normalized = (response ?? "").trim();
    if (normalized.isEmpty || normalized == "ok") {
      return;
    }
    if (normalized.startsWith("error:")) {
      throw XrBackgroundCommandException(
        normalized.substring("error:".length).trim(),
      );
    }
    throw XrBackgroundCommandException(
      "Unexpected response from host: $normalized",
    );
  }
}
