#include "flutter_xr/app.h"

#include <exception>
#include <iostream>

int main() {
    try {
        flutter_xr::ScopedComInitializer com;
        flutter_xr::FlutterXrApp app;
        app.Initialize();
        app.Run();
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "[fatal] " << ex.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "[fatal] Unknown exception\n";
        return 1;
    }
}
