# Prism Patch

A VST3 for Ableton Live, recreating the Hologram Electronics Chroma Console (firmware v1.04) in the Flip Fella look.

Four modules, each with five effects, chained in any order:

| Module | Effects | Knobs |
| --- | --- | --- |
| Character | Drive, Sweeten, Fuzz, Howl, Swell | Tilt, Amount |
| Movement | Doubler, Vibrato, Phaser, Tremolo, Pitch | Rate, Amount |
| Diffusion | Cascade, Reels, Space, Collage, Reverse | Time, Amount |
| Texture | Filter, Squash, Cassette, Broken, Interference | Amount |

## Install

1. Open the **builds** branch of this repo and download `PrismPatch-Windows.zip`.
2. Unzip it and copy the `Prism Patch.vst3` folder to `C:\Program Files\Common Files\VST3`.
3. In Ableton, rescan plugins (Preferences, Plug-Ins, Rescan). Prism Patch appears under Audio Effects.

## The panel

- **Main** shows each module's effect, its knobs and its on switch. Tap the flip card to step through effects, or pick one from the grid. The arrows swap a module's place in the chain.
- **Deep** shows everything the pedal hides behind A+B: Sensitivity, both Drifts, each Effect Vol and Output. It also holds input headroom (with Auto Calibrate), Gesture quantise and speed, Capture routing and overdub, trails, the Spare slot and the Mix at zero choice.
- **Pedal** shows what each footswitch does, what it listens for, and Learn.

Along the bottom: Gesture, Capture, Mix, presets, the four footswitches as on-screen buttons, the expression pedal, and bypass.

## Gesture and Capture

- **Gesture**: tap it, move any of the eight main knobs, tap again. Each moved knob loops its movement in time with your tempo. Grab a looping knob to erase its loop, or right-click Gesture to erase them all. Ableton automation still works on top.
- **Capture**: hold to record up to 30 seconds, release to play. A short hold makes a soft pad, a longer one a loop. Tap to clear. Capture loops save with your Ableton set.

## Footswitches and expression (optional)

Everything works from the screen, so you don't need any pedals. If you have a MIDI footswitch:

1. Set its first four switches to send **momentary CC 20, 21, 22 and 23 on MIDI channel 16** (127 when pressed, 0 when released). Or use **Learn** on the Pedal page to use whatever yours sends.
2. Set an expression pedal input to **CC 11 on channel 16**, or click the expression bar and choose Learn.
3. Click the expression bar, choose **Calibrate heel and toe**, then rock the pedal from heel to toe once.

These settings are shared by every Flip Fella plugin on your computer, so you set them up once.

| Footswitch | Tap | Hold |
| --- | --- | --- |
| A | Bypass (or the bypass group) | Bypass everything |
| B | Tap tempo, or clear Capture | Capture |
| C | Gesture record on or off | Erase Gestures |
| D | Next preset | Previous preset |
| A+B | Show or hide the Deep page | |
| C+D | Hand the footswitches to the next Prism Patch | |

### Getting footswitch MIDI to the plugin in Ableton

1. In Preferences, Link Tempo MIDI, turn on **Track** for your footswitch's MIDI input.
2. Make a MIDI track. Set **MIDI From** to your footswitch and **MIDI To** to the track holding Prism Patch, choosing Prism Patch in the second box.
3. Arm the MIDI track, or set its monitor to In.

### Kevin's setup (M-Vave Chocolate Plus and M-Audio EX-P)

In CubeSuite: channel 16, footswitches A to D as momentary CC 20 to 23, the pedal jack set to expression input on CC 11. Put the Chocolate Plus switch on **U** and connect with the USB cable, not Bluetooth. The EX-P needs a 3.5 mm to 6.35 mm TRS adapter and reads backwards on the Chocolate Plus, which Calibrate fixes.

## Original pedal MIDI map

Click the expression bar, open **Original pedal MIDI map** and pick a channel. Prism Patch then answers the Chroma Console's own CC chart and program changes on that channel, so templates made for the pedal work here. It is off by default, because the pedal uses CC 64 (the sustain pedal number) for Tilt.

## Building

GitHub Actions builds the Windows VST3 on every push to `main` and publishes it to the `builds` branch. To build locally: `cmake -S . -B build && cmake --build build --config Release`.
