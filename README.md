# Tuner & Metronome for 3DS

## How to Use
The metronome is used by setting a tempo in beats per minute, the number of beats per measure, then enabling the metronome. By default a visual flash will apear on the top screen with every beat, though this can be disabled.

The tone generator can be used by setting a waveform and pitch for each tone channel, then enabling the tone channel. Pitch can either be changed directly as the frequency, or by changing the note, which will calculate the frequency using equal temperament.

A BPM calculator can be used to determine what tempo something is. This will not auto reset.

### Controls
Use up and down to move between options, and right and left to change them. When modifying tone generators, use R and L to move between options within a tone generator.

Use X to tap to the beat for the BPM calculator. This can then be reset with Y.

## Installing on 3DS
I recommend copying the CIA file from the latest release to your 3DS' SD card, then using a title manager such as FBI to install it.

Alternatively, you can download the 3DSX/SMDH file from the latest release and install it to your SD card's `/3ds/` directory for use with the Homebrew launcher.

## Building
Use make with the makefile, and devkitPro. Nothing else special should be required. A batch file to build it has been included as well as one to also build a CIA using bannertool and makerom.
