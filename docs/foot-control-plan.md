# Prism Patch foot control plan

Written 4 November 2026 from a cross-plugin audit. Build it in Prism Patch's own thread.

## What happens today

Prism Patch is the reference for every other plugin. Footswitches on CC 20 to 23, expression on CC 11, both on channel 16, Learn, heel and toe calibration, curve, the Pedal page and the original Chroma Console map behind an opt-in channel all work as the house rules ask. It works with the new pedals today.

## The one change

C+D currently hands the footswitches to the next Prism Patch the moment both go down. Keeper needs hold C+D for 1 second in every plugin, so:

1. Add `ComboHoldAB` and `ComboHoldCD` events to `FootswitchLogic` in `PedalControl`, fired when both switches of a combo stay down for 1 second. Do this here first, because every other plugin copies this module.
2. C+D tapped: hand the footswitches to the next Prism Patch, as now.
3. C+D held 1 second: Keeper. If Keeper isn't built yet, reserve it and stay silent.
4. Add the combo hold to the tests.

## Footswitch table after the change

| Footswitch | Tap | Hold |
| --- | --- | --- |
| A | Bypass (or the bypass group) | Bypass everything |
| B | Tap tempo, or clear Capture | Capture |
| C | Gesture record on or off | Erase Gestures |
| D | Next preset | Previous preset |
| A+B | Show or hide the Deep page | |
| C+D | Hand the footswitches to the next Prism Patch | Keeper (1 second) |
