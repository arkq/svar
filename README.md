svar
====

SVAR - Simple Voice Activated Recorder

It is a simple console application (low memory footprint and CPU usage) designed for recording
audio when a specified signal level is exceeded. It is commonly known solution called Voice
Activated Recording (VAR). When the signal level is low for longer than the fadeout time, audio
recording is paused. Capturing the audio signal is based on the
[ALSA](http://www.alsa-project.org/) technology, so it should work on all modern Linux systems.
Currently this application supports three output formats:
RAW (PCM 16bit interlaced),
WAV ([libsndfile](http://www.mega-nerd.com/libsndfile/)) and
variable bitrate OGG ([libvorbis](http://www.xiph.org/vorbis/)).
However, for low CPU consumption WAV is recommended - it is the default selection.

There is also possible to split output file into chunks containing continuous recording. New
output file is generated every time a new signal appears (after the split time period). In such a
case, the time of signal appearance can be determined by the timestap attached to the output file
name. The timestamp is in the following format: DD-HH:MM:SS.

For the fine adjustment of the activation condition (the signal level), one can run svar with the
`--sig-meter` parameter. This activates the signal meter mode, in which the maximal peak value and
the RMS is displayed. Activation threshold is based on the maximal peak value in the signal
packed (time of tenth of the second).

Installation
------------

	$ autoreconf --install
	$ mkdir build && cd build
	$ ../configure --enable-sndfile --enable-vorbis
	$ make && make install
