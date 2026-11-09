# Changelog

## 1.0.1 (2026-11-09)

**Deutsch**

- Behoben: Absturz beim Plugin-Scan in manchen DAWs (z. B. Studio One), wodurch das Plugin auf der Sperrliste landete. Gab ein Host das Plugin ohne vorheriges `terminate()` frei oder rief `initialize()` zweimal auf, lief der Hintergrund-Thread für die Standard-RAM-Datei noch und beendete den ganzen Prozess. Der Thread wird jetzt in jedem Fall sauber beendet.
- Klang, Parameter und gespeicherte Projekte sind unverändert, 1.0.1 ersetzt 1.0.0 direkt.
- Wurde Tiracon schon gesperrt: in der DAW die Sperrliste zurücksetzen bzw. das Plugin neu scannen lassen (Studio One: Plug-In-Manager).

**English**

- Fixed: crash during the plugin scan in some DAWs (e.g. Studio One), which put the plugin on the block list. If a host released the plugin without calling `terminate()` first, or called `initialize()` twice, the background thread for the standard RAM file was still running and ended the whole process. The thread is now always shut down cleanly.
- Sound, parameters and saved projects are unchanged; 1.0.1 is a drop-in replacement for 1.0.0.
- If Tiracon was already blocked: reset the DAW's block list or rescan the plugin (Studio One: Plug-In Manager).

## 1.0.0 (2026-11-02)

- First release: VST3 instrument for Windows (32 and 64 bit) and Linux.
