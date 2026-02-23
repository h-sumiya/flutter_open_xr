import 'dart:math' as math;

import 'package:flutter/material.dart';

void main() {
  runApp(const OffscreenDemoApp());
}

class OffscreenDemoApp extends StatefulWidget {
  const OffscreenDemoApp({super.key});

  @override
  State<OffscreenDemoApp> createState() => _OffscreenDemoAppState();
}

class _OffscreenDemoAppState extends State<OffscreenDemoApp>
    with SingleTickerProviderStateMixin {
  late final AnimationController _controller;

  @override
  void initState() {
    super.initState();
    _controller = AnimationController(
      duration: const Duration(seconds: 4),
      vsync: this,
    )..repeat();
  }

  @override
  void dispose() {
    _controller.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      debugShowCheckedModeBanner: false,
      home: AnimatedBuilder(
        animation: _controller,
        builder: (context, _) {
          final t = _controller.value;
          final phase = t * math.pi * 2.0;
          final hue = (t * 360.0) % 360.0;
          final titleColor =
              HSLColor.fromAHSL(1.0, hue, 0.85, 0.65).toColor();

          return Container(
            decoration: const BoxDecoration(
              gradient: LinearGradient(
                begin: Alignment.topLeft,
                end: Alignment.bottomRight,
                colors: <Color>[Color(0xFF0D1B2A), Color(0xFF2A6F97)],
              ),
            ),
            child: Center(
              child: Transform.rotate(
                angle: math.sin(phase) * 0.03,
                child: Container(
                  width: 520,
                  padding: const EdgeInsets.all(28),
                  decoration: BoxDecoration(
                    color: Colors.black.withOpacity(0.3),
                    borderRadius: BorderRadius.circular(24),
                    border: Border.all(color: Colors.white24, width: 2),
                  ),
                  child: DefaultTextStyle(
                    style: const TextStyle(color: Colors.white),
                    child: Column(
                      mainAxisSize: MainAxisSize.min,
                      children: <Widget>[
                        Text(
                          'Flutter Offscreen Sample',
                          style: TextStyle(
                            fontSize: 36,
                            fontWeight: FontWeight.w700,
                            color: titleColor,
                          ),
                        ),
                        const SizedBox(height: 16),
                        LinearProgressIndicator(
                          value: t,
                          minHeight: 10,
                          borderRadius: BorderRadius.circular(10),
                        ),
                        const SizedBox(height: 18),
                        Text(
                          'phase: ${phase.toStringAsFixed(2)} rad',
                          style: const TextStyle(fontSize: 22),
                        ),
                        const SizedBox(height: 8),
                        const Text(
                          'Rendered by Flutter software callback',
                          style: TextStyle(fontSize: 16, color: Colors.white70),
                        ),
                      ],
                    ),
                  ),
                ),
              ),
            ),
          );
        },
      ),
    );
  }
}
