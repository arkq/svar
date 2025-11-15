# SVAR - Simple Voice Activated Recorder

[![Build Status](https://github.com/arkq/svar/actions/workflows/build-and-test.yaml/badge.svg)](https://github.com/arkq/svar/actions/workflows/build-and-test.yaml)

It is a simple console application (low memory footprint and CPU usage) designed for recording
audio when a specified signal level is exceeded. It is commonly known solution called Voice
Operated Recording (VOR). When the signal level is low for longer than the fadeout time, audio
recording is paused.

Supported audio backends:

- [ALSA](http://www.alsa-project.org/) - Advanced Linux Sound Architecture (Linux only)
- [PipeWire](https://pipewire.org) - Multimedia Processing Framework (Linux only)
- [PortAudio](http://www.portaudio.com/) - Cross-platform Audio I/O Library (all platforms)

Supported output formats:

- RAW (PCM 16bit interleaved)
- WAV, RF64 ([libsndfile](http://www.mega-nerd.com/libsndfile/))
- MP3 ([mp3lame](http://lame.sourceforge.net/))
- OGG/OPUS ([libopusenc](https://opus-codec.org/))
- OGG/VORBIS ([libvorbis](http://www.xiph.org/vorbis/))

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

### Dependencies (Debian/Ubuntu)

```sh
# Basic build tools and CMake
sudo apt install build-essential cmake
# Audio backends
sudo apt install libasound2-dev libpipewire-0.3-dev portaudio19-dev
# Output formats
sudo apt install libmp3lame-dev libogg-dev libsndfile1-dev libopusenc-dev libvorbis-dev
```

### Building

```sh
cmake -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_ALSA=ON -DENABLE_PIPEWIRE=ON -DENABLE_PORTAUDIO=ON \
  -DENABLE_SNDFILE=ON -DENABLE_MP3LAME=ON -DENABLE_OPUS=ON -DENABLE_VORBIS=ON
cmake --build build
```

### Installing

```sh
sudo cmake --install build
```
