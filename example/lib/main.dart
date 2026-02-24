import "package:flutter/material.dart";
import "package:flutter_open_xr/background.dart";

void main() {
  runApp(const CounterApp());
}

class CounterApp extends StatelessWidget {
  const CounterApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: "OpenXR Counter Sample",
      theme: ThemeData(colorSchemeSeed: Colors.teal),
      home: const CounterPage(),
    );
  }
}

class CounterPage extends StatefulWidget {
  const CounterPage({super.key});

  @override
  State<CounterPage> createState() => _CounterPageState();
}

class _CounterPageState extends State<CounterPage> {
  int _count = 0;
  _BackgroundSelection _backgroundSelection = _BackgroundSelection.grid;
  final TextEditingController _ddsPathController = TextEditingController();
  final TextEditingController _glbPathController = TextEditingController();
  String _backgroundStatus = "Background: grid (default)";

  @override
  void initState() {
    super.initState();
    _applyBackgroundSelection();
  }

  @override
  void dispose() {
    _ddsPathController.dispose();
    _glbPathController.dispose();
    super.dispose();
  }

  void _increment() {
    setState(() {
      _count += 1;
    });
  }

  Future<void> _applyBackgroundSelection() async {
    final _BackgroundSelection selection = _backgroundSelection;
    final String ddsPath = _ddsPathController.text.trim();
    final String glbPath = _glbPathController.text.trim();

    try {
      switch (selection) {
        case _BackgroundSelection.none:
          await XrBackgroundController.setNone();
          break;
        case _BackgroundSelection.grid:
          await XrBackgroundController.setGroundGrid();
          break;
        case _BackgroundSelection.dds:
          await XrBackgroundController.setDdsFile(ddsPath);
          break;
        case _BackgroundSelection.glb:
          await XrBackgroundController.setGlbFile(glbPath);
          break;
      }
      if (!mounted) {
        return;
      }
      setState(() {
        _backgroundStatus = "Background applied: ${selection.label}";
      });
    } on XrBackgroundCommandException catch (e) {
      if (!mounted) {
        return;
      }
      setState(() {
        _backgroundStatus = "Background error: ${e.message}";
      });
    } catch (e) {
      if (!mounted) {
        return;
      }
      setState(() {
        _backgroundStatus = "Background error: $e";
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text("OpenXR Counter Sample")),
      body: Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: <Widget>[
            const Text("Button pressed:"),
            Text(
              "$_count",
              style: Theme.of(context).textTheme.displaySmall,
            ),
            const SizedBox(height: 24),
            DropdownButton<_BackgroundSelection>(
              value: _backgroundSelection,
              onChanged: (_BackgroundSelection? value) {
                if (value == null) {
                  return;
                }
                setState(() {
                  _backgroundSelection = value;
                });
              },
              items: _BackgroundSelection.values
                  .map(
                    (_BackgroundSelection option) =>
                        DropdownMenuItem<_BackgroundSelection>(
                      value: option,
                      child: Text(option.label),
                    ),
                  )
                  .toList(),
            ),
            const SizedBox(height: 8),
            if (_backgroundSelection == _BackgroundSelection.dds)
              SizedBox(
                width: 520,
                child: TextField(
                  controller: _ddsPathController,
                  decoration: const InputDecoration(
                    border: OutlineInputBorder(),
                    labelText: ".dds path",
                    hintText: r"C:\path\to\background.dds",
                  ),
                ),
              ),
            if (_backgroundSelection == _BackgroundSelection.glb)
              SizedBox(
                width: 520,
                child: TextField(
                  controller: _glbPathController,
                  decoration: const InputDecoration(
                    border: OutlineInputBorder(),
                    labelText: ".glb path",
                    hintText: r"C:\path\to\scene.glb",
                  ),
                ),
              ),
            const SizedBox(height: 8),
            FilledButton(
              onPressed: _applyBackgroundSelection,
              child: const Text("Apply background"),
            ),
            const SizedBox(height: 8),
            Text(_backgroundStatus),
          ],
        ),
      ),
      floatingActionButton: FloatingActionButton(
        onPressed: _increment,
        child: const Icon(Icons.add),
      ),
    );
  }
}

enum _BackgroundSelection {
  none("None"),
  grid("Ground Grid (Default)"),
  dds(".dds"),
  glb(".glb");

  const _BackgroundSelection(this.label);

  final String label;
}
