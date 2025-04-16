# SVAR - Simple Voice Activated Recorder

[![Build Status](https://github.com/arkq/svar/actions/workflows/build-and-test.yaml/badge.svg)](https://github.com/arkq/svar/actions/workflows/build-and-test.yaml)

It is a simple console application (low memory footprint and CPU usage) designed for recording
audio when a specified signal level is exceeded. It is commonly known solution called Voice
Operated Recording (VOR). When the signal level is low for longer than the fadeout time, audio
recording is paused.

On Linux systems, capturing the audio signal is based on the [ALSA](http://www.alsa-project.org/)
technology. For all other systems, [PortAudio](http://www.portaudio.com/) library will be used.
Alternatively, it is possible to force PortAudio back-end on Linux systems by adding
`-DENABLE_PORTAUDIO=ON` to the CMake configuration step.

Currently this application supports four output formats:

- RAW (PCM 16bit interleaved)
- WAV ([libsndfile](http://www.mega-nerd.com/libsndfile/))
- MP3 ([mp3lame](http://lame.sourceforge.net/))
- OGG ([libvorbis](http://www.xiph.org/vorbis/))

For low CPU consumption WAV is recommended - it is the default selection.

There is also possible to split output file into chunks containing continuous recording. New
output file is generated every time a new signal appears (after the split time period). In such a
case, the time of signal appearance can be determined by the output file name, which by default is
in the format of "rec-DD-HH:MM:SS". It is possible to customize it with a [`strftime(3)` format
string](https://man7.org/linux/man-pages/man3/strftime.3.html).

For the fine adjustment of the activation threshold level, one can run `svar`
with the `--sig-meter` option which activates the signal meter mode. This mode
shows root mean square (RMS) value of the live signal. The RMS is calculated
from the last 100 ms of captured audio.

## Installation

```sh
mkdir build && cd build
cmake .. -DENABLE_SNDFILE=ON -DENABLE_MP3LAME=ON -DENABLE_VORBIS=ON
make && make install
```
