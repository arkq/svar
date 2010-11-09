/*
  svar (Simple Voice Activated Recorder) - svar.c
  Copyright (c) 2010 Arkadiusz Bokowy

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  If you want to read full version of the GNU General Public License
  see <http://www.gnu.org/licenses/>.

  ** Note: **
  For contact information and the latest version of this program see
  my webpage <http://arkq.awardspace.us/#svar>.

*/

#include <stdlib.h>
#include <math.h>
#include <getopt.h>
#include <signal.h>
#include <alsa/asoundlib.h>
#include <sndfile.h>
#include <vorbis/vorbisenc.h>

#define APP_NAME "svar"
#define APP_VER "0.0.1"

struct appconfig_t {
	char output_fname_prefix[128];
	int out_fmt;

	int sig_threshold;         //in % of max signal
	int fadeout_lag;           //in ms
	unsigned int split_ftime;  //in s (if -1 "disable" split)

	// data for OGG encoder
	int bitrate_min, bitrate_nom, bitrate_max; //in bit/s
};

#define FRAMES_PER_READ 4410
struct hwconfig_t {
	char device[25];
	snd_pcm_format_t format;
	snd_pcm_access_t access;
	unsigned int rate, channels;
};

#define OUT_FORMATS_NB 2
static char *out_formats[] = {"WAV", "OGG"};

// verbose flag
int verbose;

// Set hardware parameters
int set_hwparams(snd_pcm_t *handle, struct hwconfig_t *hwconf)
{
	snd_pcm_hw_params_t *hw_params;
	int ret_val;

	if((ret_val = snd_pcm_hw_params_malloc(&hw_params)) < 0){
		fprintf(stderr, "error: cannot allocate hw_param struct\n");
		return ret_val;}

	// choose all parameters
	if((ret_val = snd_pcm_hw_params_any(handle, hw_params)) < 0){
		fprintf(stderr, "error: broken configuration (%s)\n", snd_strerror(ret_val));
		return ret_val;}

	// set hardware access
	if((ret_val = snd_pcm_hw_params_set_access(handle, hw_params, hwconf->access)) < 0){
		fprintf(stderr, "error: cannot set access type (%s)\n", snd_strerror(ret_val));
		return ret_val;}

	// set sample format
	if((ret_val = snd_pcm_hw_params_set_format(handle, hw_params, hwconf->format)) < 0){
		fprintf(stderr, "error: cannot set sample format (%s)\n", snd_strerror(ret_val));
		return ret_val;}

	// set the count of channels
	if((ret_val = snd_pcm_hw_params_set_channels(handle, hw_params, hwconf->channels)) < 0){
		fprintf(stderr, "error: cannot set channel count (%s)\n", snd_strerror(ret_val));
		return ret_val;}

	// set the stream rate
	if((ret_val = snd_pcm_hw_params_set_rate_near(handle, hw_params, &hwconf->rate, 0)) < 0){
		fprintf(stderr, "error: cannot set sample rate (%s)\n", snd_strerror(ret_val));
		return ret_val;}

	// write the parameters to device
	if((ret_val = snd_pcm_hw_params(handle, hw_params)) < 0){
		fprintf(stderr, "error: unable to set hw params (%s)\n", snd_strerror(ret_val));
		return ret_val;}

	snd_pcm_hw_params_free(hw_params);
	return 0;
}

// Setup Ctrl-C signal catch for application quit
static int looop_mode;
static void stop_loop_mode(int sig){looop_mode = 0;}
void setup_quit_sigaction()
{
	struct sigaction sigact;

	memset(&sigact, 0, sizeof(sigact));
	sigact.sa_handler = stop_loop_mode;
	if(sigaction(SIGINT, &sigact, NULL) == -1)
		fprintf(stderr, "warning: setting sigaction(SIGINT) failed\n");
}

// Print some information about audio device and its configuration
void print_audio_info(struct hwconfig_t *hwconf, struct appconfig_t *conf)
{
	printf("Capturing audio device: %s\n", hwconf->device);
	printf("Hardware parameters: %iHz, %s, %i channel%c\n", hwconf->rate,
			snd_pcm_format_name(hwconf->format), hwconf->channels,
			hwconf->channels > 1 ? 's' : ' ');
	printf("Output file format: %s\n", out_formats[conf->out_fmt]);

	if(conf->out_fmt == 1) //if OGG print bite rates
		printf("  bitrates: %d, %d, %d kbit/s\n",
				conf->bitrate_min == -1 ? -1 : conf->bitrate_min/1000,
				conf->bitrate_nom/1000,
				conf->bitrate_max == -1 ? -1 : conf->bitrate_max/1000);
}

// Allocate memory for ALSA read buffer
int alloc_buffer_S16_LE(int16_t **buff, int channels)
{
	*buff = malloc(sizeof(int16_t)*FRAMES_PER_READ*channels);
	if(*buff != NULL) return 0;

	fprintf(stderr, "error: failed to allocate memory for read buffer\n");
	return 1;
}

// Calculate max peak and amplitude RMSD (based on all channels)
void peak_check_S16_LE(int16_t *buff, int frames, int channels,
		int16_t *peak, int16_t *rms)
{
	int x;
	int16_t abslvl;
	int64_t sum2;

	*peak = 0;
	for(x = sum2 = 0; x < frames*channels; x++) {
		abslvl = abs(((int16_t*)buff)[x]);
		if(*peak < abslvl) *peak = abslvl;

		sum2 += abslvl*abslvl;
	}
	*rms = sqrt(sum2/frames);
}

// Main recording loop with WAV format writing engine
void main_loop_WAV(snd_pcm_t *handle, struct hwconfig_t *hwconf,
		struct appconfig_t *conf)
{
	SNDFILE *fp;
	SF_INFO sfinfo;

	char ofname[128];
	time_t cur_t;
	struct tm *ofname_tm;
	int16_t *buffer, max_peak, amp_rmsd;
	int rd_len, wr_len, create_file;
	struct timespec peak_time, cur_time;
	unsigned int time_diff; //in [ms]

	memset(&peak_time, 0, sizeof(peak_time));

	memset(&sfinfo, 0, sizeof(sfinfo));
	sfinfo.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
	sfinfo.samplerate = hwconf->rate;
	sfinfo.channels = hwconf->channels;

	looop_mode = 1;
	setup_quit_sigaction();

	if(alloc_buffer_S16_LE(&buffer, hwconf->channels)) return;

	create_file = 1;
	while(looop_mode) { //main recording loop
		rd_len = snd_pcm_readi(handle, buffer, FRAMES_PER_READ);

		if(rd_len == -EPIPE) { //buffer overrun
			snd_pcm_prepare(handle);
			if(verbose){
				printf("warning: pcm_readi buffer overrun\n");
				fflush(stdout);}
			continue;
		}

		peak_check_S16_LE(buffer, rd_len, hwconf->channels, &max_peak, &amp_rmsd);

		// if max peak in buffer is greater then threshold then
		// update last peak time
		if(((int)max_peak)*100/0x7ffe > conf->sig_threshold)
			clock_gettime(CLOCK_MONOTONIC, &peak_time);

		clock_gettime(CLOCK_MONOTONIC, &cur_time);
		time_diff = (cur_time.tv_sec - peak_time.tv_sec)*1000 +
				(cur_time.tv_nsec - peak_time.tv_nsec)/1000000;

		// if diff time is lower then fadeout lag write buffer to file		
		if(time_diff < conf->fadeout_lag) {

			if(create_file) { //create new output file if needed
				time(&cur_t); ofname_tm = localtime(&cur_t);
				sprintf(ofname, "%s%02d%02d%02d%02d.wav", conf->output_fname_prefix,
						ofname_tm->tm_mday, ofname_tm->tm_hour, ofname_tm->tm_min,
						ofname_tm->tm_sec);

				create_file = 0;
				if((fp = sf_open(ofname, SFM_WRITE, &sfinfo)) == NULL){
					fprintf(stderr, "error: unable to create output file\n");
					return;}
			}

			wr_len = sf_writef_short(fp, buffer, FRAMES_PER_READ);
		}
		// split file time reached (close opened file and indicate to open new one)
		else if(create_file == 0 && cur_time.tv_sec - peak_time.tv_sec
				> conf->split_ftime){
			create_file = 1;
			sf_close(fp);}
	}

	// close only if there is opened file already
	if(create_file == 0) sf_close(fp);

	free(buffer);
}

// Display audio signal level
void signal_level_loop(snd_pcm_t *handle, struct hwconfig_t *hwconf)
{
	int rd_len;
	int16_t *buffer, max_peak, amp_rmsd;

	looop_mode = 1;
	setup_quit_sigaction();

	if(alloc_buffer_S16_LE(&buffer, hwconf->channels)) return;

	while(looop_mode) { //main recording loop
		rd_len = snd_pcm_readi(handle, buffer, FRAMES_PER_READ);

		if(rd_len == -EPIPE) { //buffer overrun
			snd_pcm_prepare(handle);
			if(verbose){
				printf("warning: pcm_readi buffer overrun\n");
				fflush(stdout);}
			continue;
		}

		peak_check_S16_LE(buffer, rd_len, hwconf->channels, &max_peak, &amp_rmsd);

		// print values "on fly"
		printf("\rsignal peak [%%]: %3u, siganl RMS [%%]: %3u",
				max_peak*100/0x7ffe, amp_rmsd*100/0x7ffe);
		fflush(stdout);
	}

	printf("\n");
	free(buffer);
}

// Do vorbis compression and write stream to OGG file
int do_analysis_and_write_ogg(vorbis_dsp_state *vd, vorbis_block *vb,
		ogg_stream_state *os, FILE *fp)
{
	int wr_len, sum_wr_len;
	ogg_packet o_pack;
	ogg_page o_page;

	sum_wr_len = 0;

	// do the main analysis and creating packets
	while(vorbis_analysis_blockout(vd, vb) == 1) {
		vorbis_analysis(vb, NULL);
		vorbis_bitrate_addblock(vb);

		while(vorbis_bitrate_flushpacket(vd, &o_pack)) {
			ogg_stream_packetin(os, &o_pack);

			// form OGG pages and write it to output file
			while(ogg_stream_pageout(os, &o_page)){
				wr_len = fwrite(o_page.header, 1, o_page.header_len, fp);
				wr_len += fwrite(o_page.body, 1, o_page.body_len, fp);
				sum_wr_len += wr_len;}
		}
	}
	return sum_wr_len;
}

// Main recording loop with OGG format writing engine
void main_loop_OGG(snd_pcm_t *handle, struct hwconfig_t *hwconf,
		struct appconfig_t *conf)
{
	FILE *fp;
	ogg_stream_state os;
	ogg_packet o_pack_main, o_pack_comm, o_pack_code;
	float **vorb_buffer;
	vorbis_info vi;
	vorbis_dsp_state vd;
	vorbis_block vb;
	vorbis_comment vc;
	int fx, cx;

	char ofname[128];
	time_t cur_t;
	struct tm *ofname_tm;
	int16_t *buffer, max_peak, amp_rmsd;
	int rd_len, wr_len, create_file;
	struct timespec peak_time, cur_time;
	unsigned int time_diff; //in [ms]

	memset(&peak_time, 0, sizeof(peak_time));

	// initialize vorbis encoder
	vorbis_info_init(&vi);
	if(vorbis_encode_init(&vi, hwconf->channels, hwconf->rate,
			conf->bitrate_max, conf->bitrate_nom, conf->bitrate_min) < 0){
		fprintf(stderr, "error: invalid parameters for vorbis bitrate\n");
		vorbis_info_clear(&vi);
		return;}

	vorbis_comment_init(&vc);
//	vorbis_comment_add(&vc, "SVAR");

	looop_mode = 1;
	setup_quit_sigaction();

	if(alloc_buffer_S16_LE(&buffer, hwconf->channels)) return;

	create_file = 1;
	while(looop_mode) { //main recording loop
		rd_len = snd_pcm_readi(handle, buffer, FRAMES_PER_READ);

		if(rd_len == -EPIPE) { //buffer overrun
			snd_pcm_prepare(handle);
			if(verbose){
				printf("warning: pcm_readi buffer overrun\n");
				fflush(stdout);}
			continue;
		}

		peak_check_S16_LE(buffer, rd_len, hwconf->channels, &max_peak, &amp_rmsd);

		// if max peak in buffer is greater then threshold then
		// update last peak time
		if(((int)max_peak)*100/0x7ffe > conf->sig_threshold)
			clock_gettime(CLOCK_MONOTONIC, &peak_time);

		clock_gettime(CLOCK_MONOTONIC, &cur_time);
		time_diff = (cur_time.tv_sec - peak_time.tv_sec)*1000 +
				(cur_time.tv_nsec - peak_time.tv_nsec)/1000000;

		// if diff time is lower then fadeout lag write buffer to file		
		if(time_diff < conf->fadeout_lag) {

			if(create_file) { //create new output file if needed
				time(&cur_t); ofname_tm = localtime(&cur_t);
				sprintf(ofname, "%s%02d%02d%02d%02d.ogg", conf->output_fname_prefix,
						ofname_tm->tm_mday, ofname_tm->tm_hour, ofname_tm->tm_min,
						ofname_tm->tm_sec);

				create_file = 0;
				if((fp = fopen(ofname, "w")) == NULL){
					fprintf(stderr, "error: unable to create output file\n");
					goto fopenerr_exit;}

				// this stuff needs to be initialized every new OGG file
				// that's why it is here...
				vorbis_analysis_init(&vd, &vi);
				vorbis_block_init(&vd, &vb);
				ogg_stream_init(&os, (ofname_tm->tm_mday << 24) + (ofname_tm->tm_hour << 16)
						+ (ofname_tm->tm_min << 8) + ofname_tm->tm_sec);

				// write three header packets to OGG stream
				vorbis_analysis_headerout(&vd, &vc, &o_pack_main, &o_pack_comm, &o_pack_code);
				ogg_stream_packetin(&os, &o_pack_main);
				ogg_stream_packetin(&os, &o_pack_comm);
				ogg_stream_packetin(&os, &o_pack_code);
			}

			vorb_buffer = vorbis_analysis_buffer(&vd, rd_len);

			// convert ALSA buffer into vorbis buffer
			for(fx = 0; fx < rd_len; fx++) for(cx = 0; cx < hwconf->channels; cx++)
				vorb_buffer[cx][fx] = buffer[fx*hwconf->channels + cx]/(float)0x7ffe;

			vorbis_analysis_wrote(&vd, rd_len);
			wr_len = do_analysis_and_write_ogg(&vd, &vb, &os, fp);
		}
		// split file time reached (close opened file and indicate to open new one)
		else if(create_file == 0 && cur_time.tv_sec - peak_time.tv_sec
				> conf->split_ftime){
			create_file = 1;

			// indicate end of data
			vorbis_analysis_wrote(&vd, 0);
			do_analysis_and_write_ogg(&vd, &vb, &os, fp);

			fclose(fp);
			ogg_stream_clear(&os);
			vorbis_block_clear(&vb);
			vorbis_dsp_clear(&vd);
		}
	}

	// close only if there is opened file already
	if(create_file == 0) {
		// indicate end of data
		vorbis_analysis_wrote(&vd, 0);
		do_analysis_and_write_ogg(&vd, &vb, &os, fp);

		fclose(fp);
		ogg_stream_clear(&os);
		vorbis_block_clear(&vb);
		vorbis_dsp_clear(&vd);
	}

fopenerr_exit:
	vorbis_comment_clear(&vc);
	vorbis_info_clear(&vi);
	free(buffer);
}

int main(int argc, char *argv[])
{
	int opt, ret_val, siglevel;
	struct option longopts[] = {
		{"help", no_argument, NULL, 'h'},
		{"verbose", no_argument, NULL, 'v'},
		{"device", required_argument, NULL, 'D'},
		{"channels", required_argument, NULL, 'C'},
		{"rate", required_argument, NULL, 'R'},
		{"sig-level", required_argument, NULL, 'l'},
		{"fadeout-lag", required_argument, NULL, 'f'},
		{"out-format", required_argument, NULL, 'o'},
		{"split-time", required_argument, NULL, 's'},
		{"sig-meter", no_argument, NULL, 'm'},
		{0, 0, 0, 0}
	};
	struct hwconfig_t hwconf;
	snd_pcm_t *snd_handle;
	struct appconfig_t conf;

	// set compiled in defaults
	strcpy(hwconf.device, "hw:0,0");
	hwconf.format = SND_PCM_FORMAT_S16_LE;         //modifying this is not recommended
	hwconf.access = SND_PCM_ACCESS_RW_INTERLEAVED; //whole audio code is based on it...
	hwconf.rate = 44100;
	hwconf.channels = 1;

	memset(&conf, 0, sizeof(conf));
	strcpy(conf.output_fname_prefix, "rec-");
	conf.sig_threshold = 2;
	conf.fadeout_lag = 500;
	conf.split_ftime = -1;

	// default OGG compression
	conf.bitrate_min = 32000;
	conf.bitrate_nom = 64000;
	conf.bitrate_max = 128000;

	verbose = 0;
	siglevel = 0;

	// print app banner
	printf("SVAR (Simple Voice Activated Recorder) ver. " APP_VER "\n");

	// arguments parser
	while((opt = getopt_long(argc, argv, "hvmD:R:C:l:f:o:s:", longopts, NULL)) != -1)
		switch(opt) {
		case 'h':
			printf("Usage: " APP_NAME " [options]\n"
"  -h, --help\t\t\tprint this help text and exit\n"
"  -D DEV, --device=DEV\t\tselect audio input device (current: %s)\n"
"  -R NN, --rate=NN\t\tset sample rate (current: %u)\n"
"  -C NN, --channels=NN\t\tspecify number of channels (current: %u)\n"
"  -l NN, --sig-level=NN\t\tactivation signal threshold (current: %u)\n"
"  -f NN, --fadeout-lag=NN\tfadeout time lag in ms (current: %u)\n"
"  -s NN, --split-time=NN\tsplit output file time in s (current: %d)\n"
"  -o FMT, --out-format=FMT\toutput file format (current: %s)\n"
"  -m, --sig-meter\t\taudio signal level meter\n"
"  -v, --verbose\t\t\tprint some extra information\n",
					hwconf.device, hwconf.rate, hwconf.channels, conf.sig_threshold,
					conf.fadeout_lag, conf.split_ftime, out_formats[conf.out_fmt]);
			exit(EXIT_SUCCESS);
		case 'v': verbose = 1; break;
		case 'D':
			memset(hwconf.device, 0, sizeof(hwconf.device));
			strncpy(hwconf.device, optarg, sizeof(hwconf.device) - 1);
			break;
		case 'R':
			ret_val = atoi(optarg);
			hwconf.rate = ret_val;
			break;
		case 'C':
			ret_val = atoi(optarg);
			hwconf.channels = ret_val;
			break;
		case 'm': siglevel = 1; break;
		case 'l':
			ret_val = atoi(optarg);
			if(ret_val < 0 || ret_val > 100)
				printf("warning: sig-level out of range [0, 100] (%d)"
						", leaving default\n", ret_val);
			else conf.sig_threshold = ret_val;
			break;
		case 'f':
			ret_val = atoi(optarg);
			if(ret_val < 100 || ret_val > 1000000)
				printf("warning: fadeout-lag out of range [100, 1000000] (%d)"
						", leaving default\n", ret_val);
			else conf.fadeout_lag = ret_val;
			break;
		case 'o':
			conf.out_fmt = -1;
			for(ret_val = 0; ret_val < OUT_FORMATS_NB; ret_val++)
				if(strcasecmp(out_formats[ret_val], optarg) == 0) conf.out_fmt = ret_val;
			if(conf.out_fmt == -1){
				printf("warning: format not available, leaving default\n");
				conf.out_fmt = 0;}
			break;
		case 's':
			ret_val = atoi(optarg);
			if(ret_val < -1 || ret_val > 1000000)
				printf("warning: split-time out of range [-1, 1000000] (%d)"
						", leaving default\n", ret_val);
			else conf.split_ftime = ret_val;
			break;
		default:
			printf("Try '" APP_NAME " --help' for more information.\n");
			exit(EXIT_FAILURE);
		}

	// open audio device
	ret_val = snd_pcm_open(&snd_handle, hwconf.device, SND_PCM_STREAM_CAPTURE, 0);
	if(ret_val < 0){
		fprintf(stderr, "error: cannot open audio device (%s)\n", hwconf.device);
		exit(EXIT_FAILURE);}

	// set hardware parameters
	if(set_hwparams(snd_handle, &hwconf) < 0){
		snd_pcm_close(snd_handle); exit(EXIT_FAILURE);}

	if((ret_val = snd_pcm_prepare(snd_handle)) < 0){
		fprintf(stderr, "error: cannot prepare audio interface to use (%s)\n",
				snd_strerror(ret_val));
		snd_pcm_close(snd_handle); exit(EXIT_FAILURE);}

	if(verbose) print_audio_info(&hwconf, &conf);

	// audio loops
	if(siglevel) signal_level_loop(snd_handle, &hwconf);
	else
		switch(conf.out_fmt){
		case 0:	main_loop_WAV(snd_handle, &hwconf, &conf); break;
		case 1: main_loop_OGG(snd_handle, &hwconf, &conf);
		}

	snd_pcm_close(snd_handle);
	exit(EXIT_SUCCESS);
}
