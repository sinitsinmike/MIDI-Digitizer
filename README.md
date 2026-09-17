# MIDI Digitizer

Arduino-based touch MIDI controller. A resistive touch digitizer is used
as the playing surface: the X coordinate selects a MIDI note or controls
a MIDI CC pair, while Y controls velocity or the second CC value.

Current firmware target: **Arduino Nano (ATmega328P) / LGT8F328** with a
**SH1106 128×64 I²C display** and **5-pin DIN MIDI OUT**.

## Main features

-   Touch-controlled MIDI notes.
-   MIDI CC control mode with 10 configurable CC pairs.
-   Musical scale quantization.
-   Major, Minor, Harmonic Major, Harmonic Minor, Phrygian, Lydian,
    Mixolydian, Dorian and Pentatonic scales.
-   Adjustable MIDI note range.
-   Chord mode.
-   Arpeggiator with multiple patterns.
-   MIDI clock input for synchronized arpeggiation.
-   Selectable arpeggiator divisions.
-   Tempo control.
-   Y-axis inversion.
-   Touch-surface calibration.
-   10 EEPROM profiles.
-   OLED status display and menu.
-   Rotary encoder with push button.
-   EEPROM persistence with CRC validation.

## Hardware

### Controller

The current firmware defines:

  Function               Pin
  -------------------- -----
  Touch X+                A0
  Touch Y+                A1
  Touch X−                D5
  Touch Y−                D6
  Encoder A               D2
  Encoder B               D3
  Encoder button          D8
  Menu Next               D9
  Save                   D10
  Chord button            D7
  Arpeggiator button      D4

The touch panel is read through the four touch-panel connections. The
firmware averages five ADC samples and uses calibration values to
convert the measured coordinates to a normalized 0--127 range.

### Display

The current build uses:

-   SH1106
-   128×64 pixels
-   I²C
-   U8x8 library
-   I²C address `0x3C` by default

If the display module uses `0x3D`, the address can be changed in the
firmware.

## MIDI

MIDI is implemented through the Arduino `MIDI.h` library.

The current firmware uses:

-   Hardware `Serial`
-   MIDI baud rate: **31250**
-   MIDI channel setting: **1**
-   MIDI Thru disabled

The physical MIDI output is intended for connection to a synthesizer,
sound module, MIDI interface or computer MIDI input.

``` text
MIDI Digitizer
      │
      │ MIDI OUT
      ▼
 MIDI IN
      │
      ├── synthesizer
      ├── sound module
      ├── MIDI interface
      └── computer
```

The exact DIN connector wiring must follow the hardware schematic of the
particular device revision.

## Power

The firmware itself does not define the required external supply
voltage. Use the power arrangement specified by the hardware revision of
the controller.

Before powering the device, verify:

-   supply voltage;
-   polarity;
-   connector type;
-   regulator/board requirements.

Do not connect an unverified power source.

## Playing surface

The touch panel has two operating modes.

### MIDI Notes

In **MIDI Notes** mode:

-   X selects the note position.
-   Y controls velocity.
-   The configured note range defines the lowest and highest MIDI notes.
-   The selected scale quantizes the continuous X position to notes
    belonging to that scale.

The firmware first maps X continuously between `Note Min` and
`Note Max`, then quantizes the resulting note to the selected scale.

This means the physical X coordinate is continuous, while the resulting
MIDI note is discrete.

### MIDI CC

In **MIDI CC** mode:

-   X controls the first CC of the selected pair.
-   Y controls the second CC.
-   One of 10 CC pairs can be selected.

The default CC pairs in the current firmware are:

    Pair   X CC   Y CC Default label
  ------ ------ ------ ---------------
       0      1     11 Mod/Ex
       1      7     10 Vol/Pn
       2     74     71 Cut/Res
       3     73     72 Atk/Rel
       4     91     93 Rev/Cho
       5      4      2 FootBr
       6      5     65 PortOn
       7     70     74 Var/Cu
       8     92     95 TremPh
       9     94     93 DetCho

The CC values can be edited in the menu.

## Scales

The current firmware contains these scales:

  Menu name   Scale intervals
  ----------- ----------------------
  Major       0, 2, 4, 5, 7, 9, 11
  Minor       0, 2, 3, 5, 7, 8, 10
  HarmMaj     0, 2, 4, 5, 7, 8, 11
  HarmMin     0, 2, 3, 5, 7, 8, 11
  Phryg       0, 1, 3, 5, 7, 8, 10
  Lydian      0, 2, 4, 6, 7, 9, 11
  Mixol       0, 2, 4, 5, 7, 9, 10
  Dorian      0, 2, 3, 5, 7, 9, 10
  Pent        0, 2, 4, 7, 9

The tonic is derived from `Note Min` (`Note Min % 12`).

## Controls

### Rotary encoder

In the main screen:

-   **Notes mode:** rotate to shift the current note range.
-   **CC mode:** rotate to select the active CC pair.
-   Hold the encoder button while rotating to make note-range changes in
    semitone-octave steps where supported.

In the menu:

-   rotate to change the selected parameter.

### Encoder button

-   Hold for approximately **1 second** from the main screen to enter
    the menu.
-   Hold for approximately **1 second** in the menu to save persistent
    settings and return to the main screen.
-   In profile Load/Save items, press the encoder to execute the
    selected operation.

If the encoder is rotated while its button is held, the firmware
prevents that rotation from being interpreted as a long-press menu
command.

### D9 --- Next

In the menu, D9 moves to the next menu item.

### D10 --- Save

In the menu, D10 writes the current persistent settings to EEPROM.

### D7 --- Chord

In Notes mode, holding D7 enables chord generation.

### D4 --- Arpeggiator

In Notes mode, holding D4 enables the arpeggiator.

## Display

### Main screen --- MIDI Notes

The display shows:

-   `MIDI Notes`
-   current note range
-   currently sounding note and velocity
-   D7 chord indication
-   D4 arpeggiator indication

### Main screen --- MIDI CC

The display shows:

-   `MIDI CC`
-   selected CC pair
-   current X/Y CC values

### Menu

The menu contains the following parameters in the current firmware:

1.  Mode
2.  CC Pair
3.  CC X
4.  CC Y
5.  Note Min
6.  Note Max
7.  Scale
8.  Arp Style
9.  Division
10. Tempo
11. Clock In
12. Chord Len
13. Profile \#
14. Load
15. Save
16. Invert Y
17. Calibrate

Some entries are controlled by compile-time feature switches and
therefore can differ between firmware builds.

## Menu parameters

### Mode

Switches between:

-   `CC`
-   `NOTES`

### CC Pair

Selects one of the 10 CC pairs.

### CC X / CC Y

Changes the MIDI CC numbers assigned to the X and Y axes.

Range: `0–127`.

### Note Min / Note Max

Sets the MIDI note range.

Range: `0–127`.

The minimum cannot exceed the maximum.

### Scale

Selects the musical scale used for note quantization.

### Arp Style

Available patterns:

-   Up
-   Down
-   Ping-Pong
-   Up/Down
-   Outside-In
-   Random
-   Random Walk

### Division

Available divisions:

-   1/4
-   1/8
-   1/16
-   1/3
-   1/6
-   1/7
-   1/9

### Tempo

Tempo range:

**40--240 BPM**

### Clock In

Enables MIDI clock input for synchronized timing where supported by the
arpeggiator.

### Chord Len

Current range:

**3--4 notes**

The chord is generated from degrees of the selected scale.

### Invert Y

Reverses the normalized Y-axis direction.

### Calibrate

Starts a 10-second touch-panel calibration.

The display instructs the user to move across the touch surface:

``` text
LB -> RB
RT -> LT
```

The calibration records X/Y minimum and maximum values and stores them
in EEPROM.

## Profiles

The firmware provides **10 EEPROM profiles**, numbered:

``` text
Profile 0
Profile 1
...
Profile 9
```

### Important distinction

`Profile #` in the menu is the **profile slot selected for the next Load
or Save operation**.

It is not, by itself, an immediate profile switch.

The workflow is:

``` text
Profile #
    ↓
select slot 0–9
    ↓
Load or Save
```

### Load

Select the required `Profile #`, move to `Load`, and press the encoder.

If the profile contains valid data, its stored parameters are copied
into the current working settings.

### Save

Select the required `Profile #`, move to `Save`, and press the encoder.

The current working parameters are written to that profile slot.

### Startup

At startup the firmware:

1.  loads the global persistent settings from EEPROM;
2.  reads the stored `lastProfile` number;
3.  attempts to load that profile;
4.  starts the normal operating screen.

Therefore the profile used at startup is the profile recorded as the
last successfully loaded or saved profile.

### What is stored in a profile

The profile structure contains the main performance settings:

-   Notes/CC mode
-   selected CC pair
-   note range
-   scale
-   tempo
-   MIDI clock input state
-   arpeggiator division
-   arpeggiator style
-   chord length
-   Y inversion
-   CC pairs when profile CC storage is enabled

Global calibration data and the global `lastProfile` selector are stored
separately from the profile data.

## Persistent settings

The device uses EEPROM for persistent storage.

Global settings include:

-   touch calibration;
-   current mode;
-   CC pair;
-   note range;
-   scale;
-   tempo;
-   clock input;
-   arpeggiator settings;
-   chord length;
-   Y inversion;
-   profile selector.

The stored data uses a magic value, version number and CRC16 validation.
If the stored global data is invalid, the firmware restores defaults.

## Default settings

Important firmware defaults include:

  Parameter      Default
  -------------- ---------
  Mode           CC
  Note Min       48
  Note Max       72
  Scale          Major
  Tempo          120 BPM
  Clock In       Off
  Arp Division   1/8
  Arp Style      Up
  Chord Length   4
  Invert Y       On
  Profile        0

Default touch calibration is approximately:

``` text
X: 100–900
Y: 100–900
```

A real calibration should be performed after hardware assembly.

## Touch calibration and note geometry

The touch surface is calibrated to a normalized 0--127 coordinate
system.

For Notes mode, the firmware performs:

``` text
physical X
    ↓
calibrated X = 0..127
    ↓
linear Note Min..Note Max
    ↓
scale quantization
    ↓
MIDI note
```

The scale quantizer selects the closest note belonging to the selected
scale.

Consequently, the **musical intervals between successive notes are not
necessarily equal in semitones**. For example, a major scale contains
both whole-tone and semitone intervals.

This is separate from the physical digitizer calibration: calibration
determines the mapping of the touch surface to the normalized X range.

## Arpeggiator and chords

The arpeggiator can operate on the selected root/chord and supports
several ordering patterns.

When chord mode is active, the firmware builds a chord from scale
degrees of the selected scale.

The arpeggiator can also use MIDI clock input when enabled.

## Project development

The project contains several development/release-candidate firmware
revisions.

When modifying the firmware, test these areas independently:

-   touch calibration;
-   physical note positioning;
-   scale quantization;
-   note range;
-   MIDI Note On/Off;
-   CC output;
-   encoder operation;
-   menu navigation;
-   profile Load;
-   profile Save;
-   startup profile loading;
-   arpeggiator timing;
-   chord generation.

A known-good firmware revision should be retained before testing changes
to the touch-to-note mapping.

## Build

Open the required `.ino` file in Arduino IDE and select the target
Arduino Nano/LGT8F328 board and serial port.

The current source uses libraries including:

-   Arduino core
-   Wire
-   EEPROM
-   MIDI Library
-   U8x8lib

The exact library versions depend on the development environment.

## Repository

GitHub:

https://github.com/sinitsinmike/MIDI-Digitizer

## Status

**Development / Experimental**

The firmware is under active development. Hardware revisions and
firmware builds may change the available menu items, controls and
electrical connections.

For reproducible testing, record the exact firmware filename/version
used for each hardware test.
