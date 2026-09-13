# Native file selection

The glTF import panel offers `Browse and Import...` as well as manual path entry.
Choosing a file imports it immediately. Cancel leaves the scene unchanged; dialog
and import errors appear in the existing glTF error display.

`src/FileDialog.hpp` is the application-facing boundary. `openGltf()` returns a
UTF-8 path, `std::nullopt` for cancellation, or throws for a dialog error. It must
be called on the UI/main thread after GLFW initialization. The implementation
owns native dialog initialization and allocated path memory through RAII.
ImGui and the importer do not include platform-specific dialog headers.

The backend is nativefiledialog-extended, installed through the pinned vcpkg
manifest and linked as `nfd::nfd`. Windows uses the native Windows picker and
macOS uses the native macOS picker. The vcpkg Linux port enables the desktop
portal backend: a desktop session with D-Bus, `xdg-desktop-portal`, and a suitable
desktop backend (such as GTK or KDE) is needed at runtime. A headless session
cannot display a picker; manual path entry remains available.

The picker returns UTF-8 independently of the platform. The glTF entry point
uses `std::filesystem::u8path`; debug labels and cache keys use UTF-8 as well.
This does not change the separate legacy OBJ path handling.

Build with the existing platform presets. Native UI behavior must be checked
on each target OS; a Windows build does not validate macOS or Linux dialogs.
