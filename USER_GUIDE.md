# MIDI Digitizer — User Guide

A practical guide to playing, configuring, and connecting the MIDI Digitizer.

> **Firmware:** Arduino Nano / LGT8F328  
> **Display:** SH1106 128×64 OLED  
> **MIDI:** 5-pin DIN MIDI OUT, 31250 baud

---

## 1. What is the MIDI Digitizer?

The MIDI Digitizer is a touch-controlled MIDI instrument.

The resistive touch surface is the main playing area:

- **X axis** selects notes or controls MIDI CC X.
- **Y axis** controls velocity or MIDI CC Y.
- The OLED shows the current mode and settings.
- The rotary encoder controls parameters.
- Dedicated buttons provide menu navigation, saving, chords, and arpeggiation.

The instrument can work in two main modes:

1. **MIDI Notes** — play notes from a selected musical scale.
2. **MIDI CC** — use the touch surface as a two-dimensional MIDI controller.

---

# 2. Quick Start

## 2.1 Connect MIDI

Connect the Digitizer's **MIDI OUT** to:

- a synthesizer;
- a sound module;
- a MIDI interface;
- a computer with a MIDI interface.

```text
MIDI Digitizer
      │
      │ MIDI OUT
      ▼
    MIDI IN
      │
      ├── Synthesizer
      ├── Sound module
      ├── MIDI interface
      └── Computer
```

The MIDI connection uses the standard MIDI data rate:

**31250 baud**

## 2.2 Power on

Use the power arrangement specified for your hardware revision.

Before powering the device, verify:

- supply voltage;
- polarity;
- connector;
- regulator/board requirements.

## 2.3 Play

In **MIDI Notes** mode:

1. Touch the surface.
2. Move left/right to change the note.
3. Move up/down to change velocity.
4. Lift your finger to release the note.

The X coordinate is converted into the selected note range and then
quantized to the selected scale.

---

# 3. Touch Surface

The touch surface is continuous, but MIDI notes are discrete.

The signal path is:

```text
Finger position
      ↓
Touch-panel measurement
      ↓
Calibration
      ↓
Normalized X = 0..127
      ↓
Note range
      ↓
Scale quantization
      ↓
MIDI note
```

This distinction is important:

**The physical X positions are not automatically the same as equally
spaced semitone positions.**

When a scale is selected, the firmware maps the continuous X position
to the configured note range and then quantizes it to notes belonging
to that scale.

For example, a major scale contains:

```text
Whole step → Whole step → Half step → Whole step → Whole step → Whole step → Half step
```

Therefore, the musical distance between adjacent scale notes can be
different even though the touch coordinate itself is continuous.

---

# 4. MIDI Notes Mode

The main screen shows the current note-playing state.

### X axis

X selects the pitch.

The usable pitch range is defined by:

- **Note Min**
- **Note Max**
- **Scale**

### Y axis

Y controls MIDI velocity.

The Y direction can be reversed with:

**Invert Y**

### Note range

The note range is expressed as MIDI note numbers:

```text
Note Min ───────────── Note Max
```

Valid range:

**0–127**

The minimum value cannot be higher than the maximum value.

---

# 5. Musical Scales

The firmware currently provides:

| Menu | Scale |
|---|---|
| `Major` | Major |
| `Minor` | Natural minor |
| `HarmMaj` | Harmonic major |
| `HarmMin` | Harmonic minor |
| `Phryg` | Phrygian |
| `Lydian` | Lydian |
| `Mixol` | Mixolydian |
| `Dorian` | Dorian |
| `Pent` | Pentatonic |

The tonic is derived from:

```text
Note Min % 12
```

The scale is applied after the continuous X position has been mapped
into the selected MIDI note range.

---

# 6. MIDI CC Mode

Switch the instrument to:

**Mode → CC**

The touch surface becomes a two-dimensional MIDI controller.

```text
X → CC X
Y → CC Y
```

The firmware provides 10 CC pairs.

| Pair | X CC | Y CC | Default |
|---:|---:|---:|---|
| 0 | 1 | 11 | Mod / Expression |
| 1 | 7 | 10 | Volume / Pan |
| 2 | 74 | 71 | Cutoff / Resonance |
| 3 | 73 | 72 | Attack / Release |
| 4 | 91 | 93 | Reverb / Chorus |
| 5 | 4 | 2 | Foot / Breath |
| 6 | 5 | 65 | Portamento |
| 7 | 70 | 74 | Variation / Cutoff |
| 8 | 92 | 95 | Tremolo / Phaser |
| 9 | 94 | 93 | Detune / Chorus |

The CC numbers can be edited from the menu.

Each CC value is in the standard MIDI range:

**0–127**

---

# 7. Controls

## Rotary Encoder

### On the main screen

In **MIDI Notes** mode:

- rotate to shift the current note range;
- hold the encoder button while rotating for larger range changes
  where supported.

In **MIDI CC** mode:

- rotate to select the active CC pair.

### In the menu

Rotate the encoder to change the selected parameter.

---

## Encoder Button

### From the main screen

Hold for approximately **1 second** to enter the menu.

### In the menu

Hold for approximately **1 second** to save persistent settings and
return to the main screen.

### Profile Load / Save

When the menu is on a profile operation:

- select the desired profile number;
- select `Load` or `Save`;
- press the encoder to execute the operation.

---

## D9 — Next

In the menu:

**D9 → next menu item**

---

## D10 — Save

In the menu:

**D10 → write current persistent settings to EEPROM**

---

## D7 — Chord

In Notes mode, hold **D7** to enable chord generation.

---

## D4 — Arpeggiator

In Notes mode, hold **D4** to enable the arpeggiator.

---

# 8. OLED Display

## MIDI Notes screen

The main screen provides information about:

- MIDI Notes mode;
- current note range;
- currently sounding note;
- velocity;
- chord state;
- arpeggiator state.

## MIDI CC screen

The main screen shows:

- MIDI CC mode;
- selected CC pair;
- current X value;
- current Y value.

## Menu

The current menu contains:

1. Mode
2. CC Pair
3. CC X
4. CC Y
5. Note Min
6. Note Max
7. Scale
8. Arp Style
9. Division
10. Tempo
11. Clock In
12. Chord Len
13. Profile #
14. Load
15. Save
16. Invert Y
17. Calibrate

Some entries may differ between firmware builds.

---

# 9. Menu Settings

## Mode

Select:

- `CC`
- `NOTES`

---

## CC Pair

Select one of the 10 available CC pairs.

---

## CC X / CC Y

Set the MIDI CC number used by the X or Y axis.

Range:

**0–127**

---

## Note Min / Note Max

Set the lowest and highest MIDI note.

Range:

**0–127**

---

## Scale

Select the musical scale used for note quantization.

---

## Arp Style

Available patterns:

- Up
- Down
- Ping-Pong
- Up/Down
- Outside-In
- Random
- Random Walk

---

## Division

Available arpeggiator divisions:

- 1/4
- 1/8
- 1/16
- 1/3
- 1/6
- 1/7
- 1/9

---

## Tempo

Tempo range:

**40–240 BPM**

Tempo is used by the internal arpeggiator timing.

---

## Clock In

Enables MIDI clock synchronization where supported by the
arpeggiator.

---

## Chord Len

Select the chord length.

Current firmware range:

**3–4 notes**

Chords are generated from degrees of the selected scale.

---

## Invert Y

Reverses the direction of the Y axis.

Use this if the physical direction of velocity feels backwards.

---

# 10. Chord Mode

Hold the **Chord button (D7)** while playing in Notes mode.

The firmware generates a chord from the selected scale.

The number of chord notes is controlled by:

**Chord Len**

Current range:

**3–4 notes**

The resulting chord depends on the selected root and scale.

---

# 11. Arpeggiator

Hold the **Arpeggiator button (D4)** in Notes mode.

The arpeggiator can use several patterns:

```text
Up
Down
Ping-Pong
Up/Down
Outside-In
Random
Random Walk
```

The timing can be controlled by:

- internal tempo;
- selected division;
- MIDI Clock In, when enabled.

Available divisions:

```text
1/4
1/8
1/16
1/3
1/6
1/7
1/9
```

---

# 12. Profiles

The Digitizer has **10 EEPROM profile slots**:

```text
Profile 0
Profile 1
Profile 2
...
Profile 9
```

## Important: Profile # is a slot selector

The `Profile #` item in the menu does **not** by itself immediately
change the active playing configuration.

It selects the EEPROM slot that will be used by the next:

- `Load`
- `Save`

Think of it as:

```text
Profile #
    │
    ├── choose slot
    │
    ├── Load → copy slot into current settings
    │
    └── Save → copy current settings into slot
```

This distinction prevents accidentally assuming that simply changing
the number has already loaded another profile.

---

## Loading a profile

To load a profile:

1. Enter the menu.
2. Select `Profile #`.
3. Choose profile `0–9`.
4. Move to `Load`.
5. Press the encoder.

If the stored profile is valid, its settings become the current working
settings.

---

## Saving a profile

To save the current configuration:

1. Configure the instrument.
2. Enter the menu.
3. Select `Profile #`.
4. Choose profile `0–9`.
5. Move to `Save`.
6. Press the encoder.

The current working settings are written to that profile slot.

---

## Startup profile

The firmware stores a `lastProfile` selector separately from the
profile data.

At startup it:

1. loads global persistent data;
2. reads the stored profile number;
3. attempts to load that profile;
4. starts the instrument.

The profile associated with the stored `lastProfile` value is therefore
the profile the firmware attempts to use at startup.

---

# 13. Calibration

Calibration compensates for differences between touch panels and
hardware assemblies.

Open:

**Menu → Calibrate**

The calibration process lasts approximately **10 seconds**.

Follow the display instructions and move across the touch surface:

```text
LB → RB
RT → LT
```

The firmware records the measured X/Y limits and stores them in EEPROM.

## When should calibration be performed?

Perform calibration:

- after assembling the hardware;
- after replacing the touch panel;
- if the active area feels shifted;
- if the full touch range is not reachable.

Calibration should be performed with the panel in its normal installed
position.

---

# 14. MIDI Connection

The firmware uses the standard MIDI serial data rate:

**31250 baud**

The device provides MIDI OUT.

Typical connection:

```text
Digitizer MIDI OUT
        │
        ▼
Synthesizer MIDI IN
```

or:

```text
Digitizer MIDI OUT
        │
        ▼
MIDI Interface
        │
        ▼
Computer / DAW
```

The exact DIN connector wiring depends on the hardware revision.
Follow the schematic for the physical unit.

---

# 15. Power

The firmware does not define a universal external supply voltage.

Use the power specification for the particular hardware revision.

Before connecting power, check:

- voltage;
- polarity;
- connector;
- regulator requirements.

Do not use an unverified power supply.

---

# 16. Default Settings

Typical firmware defaults are:

| Setting | Default |
|---|---|
| Mode | CC |
| Note Min | 48 |
| Note Max | 72 |
| Scale | Major |
| Tempo | 120 BPM |
| Clock In | Off |
| Arp Division | 1/8 |
| Arp Style | Up |
| Chord Length | 4 |
| Invert Y | On |
| Profile | 0 |

Default touch calibration is approximately:

```text
X: 100–900
Y: 100–900
```

A real calibration is recommended for the assembled instrument.

---

# 17. EEPROM and Persistent Settings

The Digitizer stores settings in EEPROM so they can survive power-off.

Persistent data includes settings such as:

- touch calibration;
- mode;
- CC pair;
- note range;
- scale;
- tempo;
- MIDI clock state;
- arpeggiator settings;
- chord length;
- Y inversion;
- profile selector.

The stored data uses validation information including a magic value,
version, and CRC16.

If stored global data is invalid, the firmware restores defaults.

---

# 18. Troubleshooting

## No MIDI sound

Check:

1. MIDI cable direction.
2. Digitizer **MIDI OUT → device MIDI IN**.
3. Synthesizer volume.
4. MIDI channel.
5. Touch input.
6. Note range.
7. Current mode.

---

## Notes feel incorrectly positioned

First check:

1. Touch calibration.
2. `Note Min`.
3. `Note Max`.
4. Selected scale.
5. Whether the behavior is caused by scale quantization rather than
   the physical touch mapping.

Remember that scale notes do not necessarily have equal semitone
intervals.

---

## Y direction feels reversed

Use:

**Menu → Invert Y**

---

## Profile does not appear to change

Remember:

**Profile # selects a slot.**

It does not by itself load that slot.

Use:

```text
Profile # → choose slot → Load
```

To store the current settings:

```text
Profile # → choose slot → Save
```

If testing profiles, change one obvious parameter first, save it to a
known slot, then load that slot and verify the parameter.

---

## Settings disappear after power-off

Verify that the settings were actually written to EEPROM.

For menu operation, use the dedicated `Save` function or the documented
long encoder-button action as appropriate for the firmware build.

---

## Touch range is too small or shifted

Run:

**Menu → Calibrate**

and repeat the calibration across the usable physical area.

---

# 19. Hardware Reference

The current firmware defines these connections:

| Function | Pin |
|---|---:|
| Touch X+ | A0 |
| Touch Y+ | A1 |
| Touch X− | D5 |
| Touch Y− | D6 |
| Encoder A | D2 |
| Encoder B | D3 |
| Encoder button | D8 |
| Menu Next | D9 |
| Save | D10 |
| Chord button | D7 |
| Arpeggiator button | D4 |

Display:

```text
SH1106
128 × 64
I²C
Default address: 0x3C
```

---

# 20. Recommended First-Time Setup

For a new instrument, use this sequence:

```text
Power on
   ↓
Calibrate touch surface
   ↓
Select MIDI Notes
   ↓
Set Note Min / Note Max
   ↓
Select Scale
   ↓
Touch and verify notes
   ↓
Adjust Invert Y if necessary
   ↓
Test Chord mode
   ↓
Test Arpeggiator
   ↓
Save a working configuration to a profile
```

For a MIDI CC controller:

```text
Power on
   ↓
Select MIDI CC
   ↓
Select CC Pair
   ↓
Set CC X / CC Y if required
   ↓
Move across the touch surface
   ↓
Verify MIDI CC values
   ↓
Save the configuration if required
```

---

# 21. Firmware and Hardware Revisions

The MIDI Digitizer is an active development project.

Different firmware revisions may change:

- menu items;
- profile behavior;
- touch mapping;
- arpeggiator behavior;
- chord behavior;
- hardware pin assignments;
- display behavior.

When reporting a problem, record:

- firmware filename/version;
- hardware revision;
- selected mode;
- relevant menu settings;
- profile number;
- whether the problem occurs after a restart.

This makes firmware behavior much easier to reproduce.

---

## Project

**GitHub:**  
https://github.com/sinitsinmike/MIDI-Digitizer

---

## Status

**Development / Experimental**

This guide describes the current firmware behavior and hardware
interface documented for the project. Always verify the exact firmware
revision and hardware revision used by your instrument.
