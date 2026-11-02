# Tiracon

VST3-Instrument von **BecauseOfReasons**.

![Tiracon](img/tiracon@2x.png)

Tiracon ist eine Emulation des **Tiracon 6V**, eines sechsstimmigen Analogsynthesizers aus der DDR. Im Plugin läuft
die echte Firmware des Geräts auf einem emulierten Z80 (U880) mit allen Bausteinen der Digitalplatine: Timer,
Ein-/Ausgabe, MIDI-Schnittstelle und dem einen DAC, über den die Firmware alle Steuerspannungen ausgibt. Dahinter
sitzt ein Klangmodell der sechs Stimmkarten (VCO, Teiler, Wellenformen, 24-dB-Tiefpass, VCA) und der Analogplatine.
Das Plugin verhält sich deshalb wie das Gerät: Es bootet, es ist ohne AUTO TUNE leicht verstimmt, und Hüllkurven,
LFO und Programme arbeiten genau so, wie die Firmware sie berechnet.

### Firmware

Eingebaut ist eine erweiterte Fassung der Originalfirmware: Der Sequencer ist entfernt, dafür gibt es
**Unison** (P37, bis zu 6 Stimmen pro Note), **Velocity** (P38, auf VCA und/oder Filter), **Pitch Bend** (±1 Halbton),
**Detune** (P45–P48, je Stimmkarte oder schwebend) und **MIDI-CC für alle Parameter**.

### Spielen

* Noten, Velocity und Pitch Bend gehen über den emulierten MIDI-Eingang an die Firmware. Noten unter MIDI 36 spielt
  sie nicht.
* **CC-Nummer = Parameternummer** wie am Gerät (z. B. CC 13 → P13).
* Alle Klangparameter P11–P76 sind in der DAW automatisierbar. Was am Bedienfeld passiert, geht an die DAW zurück.
* 32 Programmplätze 11–48 als Programmliste der DAW.

### Bedienfeld

* **Anzeige** wie am Gerät (Programm, Parameter, Wert, Wertebereich) mit kurzer Aufschlüsselung des Werts.
* **Tastenfeld** wie das Original, dazu ein **Regler** für den angezeigten Parameter.
* **Stimmen und Steuerspannungen** live.
* **Parameterliste** im Stil der Parametertafel des Geräts: Ein Klick wählt den Parameter.
* **EG-Editor**: Die Hüllkurve wird so gezeigt, wie die Firmware sie abarbeitet. Die Punkte lassen sich ziehen.
* **RAM**: Das batteriegepufferte RAM (alle Programme, Stimmung) laden und speichern. Eine Standard-Datei für neue
  Instanzen, auf Wunsch schreibt WRITE automatisch dorthin zurück.
* **Neues Gerät**: würfelt eine andere Bauteilstreuung. Jedes „Gerät“ klingt und stimmt leicht anders.

### 12 / 24 / 48 Stimmen

Über das Menü „Stimmen“ in der Kopfzeile laufen 1, 2, 4 oder 8 komplette Geräte nebeneinander, also 6, 12, 24 oder 48 Stimmen.
Jedes hat die unveränderte Firmware und eigene Bauteile. Noten werden verteilt, Parameter und Programme gelten für
alle. 48 Stimmen brauchen etwa einen halben CPU-Kern.

### Speichern

RAM, Gerät, Stimmenzahl und Lautstärke stehen im DAW-Projekt. Beim Laden bootet die Firmware wie das Gerät in
Programm 11; danach stellt das Plugin das gespeicherte Programm wieder ein, samt Änderungen, die noch nicht mit WRITE
gesichert waren. Eine neue Instanz startet mit den Werksprogrammen oder der Standard-Datei.

### Download und Installation

Fertige Plugins für Windows (32 und 64 Bit) und Linux gibt es unter **[Releases](../../releases)**.
* **Windows:** Ordner `Tiracon.vst3` nach `C:\Program Files\Common Files\VST3` kopieren.
* **Linux:** Ordner `Tiracon.vst3` nach `~/.vst3` kopieren. Gebraucht werden die üblichen Desktop-Bibliotheken (xcb, cairo,
  pango); unter Ubuntu/Debian fehlt manchmal `libxcb-cursor0`.

Nach dem Laden bootet die Firmware etwa eine Sekunde. Danach einmal **AUTO TUNE** drücken, wie am Gerät.

Bauen aus dem Quellcode und Tests: [vst3/README.md](vst3/README.md)

### Lizenz

Der Quellcode steht unter der MIT-Lizenz, siehe [LICENSE](LICENSE). **Ausgenommen** sind die Firmware und die
Werksprogramme des Originalgeräts, siehe [vst3/firmware/README.md](vst3/firmware/README.md). Verwendeter Fremdcode
(Steinberg VST3 SDK, VSTGUI) und dessen Lizenzen: [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

Tiracon ist ein eigenständiges Projekt und steht in keiner Verbindung zum Hersteller des Originalinstruments.
VST is a registered trademark of Steinberg Media Technologies GmbH.
