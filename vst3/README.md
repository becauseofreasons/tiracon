# Tiracon – Quellcode des VST3-Plugins

Was das Plugin kann und wie man es installiert: [README im Hauptordner](../README.md).

Emulation der Digitalplatine (Z80 + Peripherie) mit Klangmodell der Stimmkarten und der Analogplatine, gebaut direkt
mit dem Steinberg VST3 SDK (ohne JUCE, ohne JavaScript). Oberfläche mit VSTGUI aus dem SDK, alles im Code gezeichnet,
keine Bitmaps. Die Firmware `firmware/tiracon_mod.bin` wird beim Bauen eingebettet (Lizenzhinweis:
[firmware/README.md](firmware/README.md)).

Einzelheiten zum Verhalten:
* MIDI: Noten, Velocity und Pitch Bend gehen über den emulierten MIDI-Eingang (31 250 Bd) an die Firmware, immer
  auf ihrem Kanal (P82, 0 = Omni → Kanal 1). CC-Nummer = Parameternummer.
* Parameter P11–P76 gehen intern als CC an die Firmware, höchstens alle 45 ms je Parameter (Pitch Bend 10 ms).
  Die Firmware-Werte gehen zurück an die DAW (150 ms Sperre nach eigenem Senden).
* Programmwechsel: schaltet bei Bedarf P84 PROGR-CHG per CC ein.
* Zustand im Projekt: RAM, Seriennummer, Volume, Anzahl Geräte. Neue Instanz: Werksprogramme, Gerät 1266.
  Nach dem Laden (RAM, Projekt, neues Gerät) bootet die Firmware in Programm 11; 1,2 s später schickt die Engine
  einen Programmwechsel auf das Programm aus dem RAM und danach die Werte des Editierpuffers, die davon abweichen
  (`Engine::powerOnRestoring`). Ein einfacher Reset bleibt in Programm 11.
* Mehrere Geräte (12/24/48 Stimmen): Noten an das Gerät mit den wenigsten gehaltenen Noten (bei Gleichstand reihum,
  Unison P37 berücksichtigt), Note-Off an dasselbe. Parameter, Pitch Bend, Programme und Tasten an alle; alle 150 ms
  werden Abweichungen angeglichen. Summe mit tanh(x/n^¼), mono (einzelne Töne −1,5/−3/−4,5 dB bei 2/4/8 Geräten).
* Einstellungen je Benutzer: `%APPDATA%\Tiracon\settings.txt` bzw. `~/.config/Tiracon/settings.txt`.

## Bauen (Linux)

Voraussetzungen: [VST3 SDK](https://github.com/steinbergmedia/vst3sdk) (mit Submodulen) in `~/SDKs/vst3sdk`
(oder `-DVST3_SDK_ROOT=…`), cmake ≥ 3.25, ninja, g++, die X11/xcb/cairo-Entwicklungspakete für VSTGUI; für
Windows `mingw-w64`. Befehle im Hauptordner des Repositorys ausführen.

```bash
cmake -S vst3 -B ~/build/tiracon -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build ~/build/tiracon
```

Windows 64 und 32 Bit (Cross-Build mit MinGW):

```bash
cmake -S vst3 -B ~/build/tiracon-win -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=$PWD/vst3/cmake/mingw-w64.cmake
cmake --build ~/build/tiracon-win
cmake -S vst3 -B ~/build/tiracon-win32 -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=$PWD/vst3/cmake/mingw-w64-i686.cmake
cmake --build ~/build/tiracon-win32
```

Ein Windows-Bundle enthält beide Varianten (`Contents/x86_64-win` und `Contents/x86-win`); jeder Host nimmt sich die
passende. Der 32-Bit-Build rechnet mit SSE2 (`-mfpmath=sse`), damit er bitgleich zum 64-Bit-Build bleibt.

Besonderheiten der MinGW-Builds (das SDK bleibt unverändert): VSTGUI wird ohne seinen Standalone-Teil
eingebunden, `cmake/mingw-compat/` ergänzt zwei Kleinigkeiten, die MinGW gegenüber MSVC fehlen, und
`dataexchange.cpp` des SDK wird nicht mitgebaut (braucht `std::aligned_alloc`, vom Plugin nicht benutzt).

## Tests

```bash
node vst3/test/compare.js ~/build/tiracon/tiracon_compare vst3/test/scenarios/basic.txt   # C++ gegen WebApp
~/build/tiracon/tiracon_multitest                                                         # 12/24/48 Stimmen
~/build/tiracon/bin/Release/tiracon_hosttest ~/build/tiracon/VST3/Release/Tiracon.vst3     # Plugin wie in einer DAW
~/build/tiracon/bin/Release/validator -e ~/build/tiracon/VST3/Release/Tiracon.vst3         # Steinberg-Validator
```

Oberfläche ohne Fenster als PNG (mit echter Emulation): `~/build/tiracon/bin/Release/tiracon_uishot <Ordner>`.
Mit `-DTIRACON_HOSTING_TOOLS=ON` baut das Projekt zusätzlich den `editorhost` des SDK (öffnet das Plugin-Fenster).

`compare.js` lässt dasselbe Szenario im JS-Kern der WebApp und im C++-Kern laufen und vergleicht alle 50 ms
CPU-Zustand, RAM, Steuerspannungen und Audio.

## Aufbau

| Ordner | Inhalt |
|---|---|
| `src/core/` | Emulation ohne SDK: `z80`, `devices`, `board`, `synth`, `device` (Toleranzen), `firmware`, `engine` (Plugin-Logik), `multiengine` (mehrere Tiracons), `uimodel` (Anzeige, Hüllkurvenmodell) |
| `src/core/gen/` | aus der WebApp erzeugte Daten (`node vst3/tools/gen_data.js`) |
| `src/gui/` | Oberfläche (VSTGUI): Anzeige, Tastenfeld, Stimmen, Parameterliste, EG-Editor |
| `src/plugin/` | VST3: Processor, Controller, Editor, Factory, Brücke Processor ↔ Oberfläche |
| `test/` | Vergleich mit der WebApp, End-to-End-Test |
| `cmake/` | SDK- und VSTGUI-Einbindung, MinGW-Toolchain und -Anpassungen |
| `firmware/` | eingebettete Firmware (nicht MIT, siehe dort) |

Referenz für den Kern ist die Browser-WebApp des Emulators (nicht Teil dieses Repositorys). `compare.js` und
`tools/gen_data.js` erwarten sie in `../emulator` neben `vst3/` oder in `TIRACON_WEBAPP=<emulator-Ordner>`.
Änderungen an `z80.js`, `devices.js`, `tiracon.js`, `synth-worklet.js` oder `tolerance.js` in `src/core/` nachziehen
und mit `compare.js` prüfen. Ohne WebApp lässt sich alles bauen; nur dieser Vergleich fehlt dann.
