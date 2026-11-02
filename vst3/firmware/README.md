# Firmware

`tiracon_mod.bin` (8 KB) is the firmware that runs inside the plugin: the original program of the Tiracon 6V's
digital board (Z80/U880), modified. The sequencer is removed; Unison, Velocity, Pitch Bend, Detune, MIDI CC for all
parameters and MIDI receive after reset are added. CMake embeds this file into the plugin at build time.

The factory programs in `src/core/gen/factory_ram.inc` come from the original instrument as well.

**License:** Both are **not** covered by the MIT license of this repository. They are included so that the plugin
can be built and works like the instrument. The manufacturer no longer exists; if you hold rights to the original
firmware and object to its inclusion, please open an issue.
