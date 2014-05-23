svar
====

SVAR (Simple Voice Activated Recorder)

It is a simple console application (low memory footprint and CPU usage) designed for recording
audio when a specified audio level is exceeded. It is commonly known solution called Voice
Activated Recording (VAR). Capturing the audio signal is based on the ALSA technology. Currently
this application supports two output formats: WAV and OGG. However, for low CPU consumption WAV is
recommended. To the name of the output file, the time stamp is appended in the DDHHMMSS format.
When split-time parameter is different then -1, marking time mode is activated. It means, that
when incoming audio signal level is lower then the sig-level for the time longer then this
parameter, it will be created new audio output file.

There is one known bug in this application. When using more then one audio channel on old
computers (CPU < 1GHz) frequently audio buffer overrun occurs, which cause loss of samples (time
of ms).


Instalation
-----------

	$ autoreconf --install
	$ mkdir build && cd build
	$ ../configure
	$ make && make install
