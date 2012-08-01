#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <dirent.h>
#include <math.h>
#include <config.h>

#include <linux/videodev2.h>
#include <libv4l2.h>
#include <string>

#include "v4l2-ctl.h"

static int tuner_index = 0;
static struct v4l2_tuner tuner;        	/* set_freq/get_freq */
static struct v4l2_modulator modulator;	/* set_freq/get_freq */
static int txsubchans = 0;		/* set_modulator */
static double freq = 0;			/* get/set frequency */
static struct v4l2_frequency vf;	/* get_freq/set_freq */
static struct v4l2_hw_freq_seek freq_seek; /* freq-seek */
static int mode = V4L2_TUNER_MODE_STEREO;  /* set audio mode */

void tuner_usage(void)
{
	printf("\nTuner/Modulator options:\n"
	       "  -F, --get-freq     query the frequency [VIDIOC_G_FREQUENCY]\n"
	       "  -f, --set-freq=<freq>\n"
	       "                     set the frequency to <freq> MHz [VIDIOC_S_FREQUENCY]\n"
	       "  -T, --get-tuner    query the tuner settings [VIDIOC_G_TUNER]\n"
	       "  -t, --set-tuner=<mode>\n"
	       "                     set the audio mode of the tuner [VIDIOC_S_TUNER]\n"
	       "                     Possible values: mono, stereo, lang2, lang1, bilingual\n"
	       "  --tuner-index=<idx> Use idx as tuner idx for tuner/modulator commands\n"
	       "  --get-modulator    query the modulator settings [VIDIOC_G_MODULATOR]\n"
	       "  --set-modulator=<txsubchans>\n"
	       "                     set the sub-carrier modulation [VIDIOC_S_MODULATOR]\n"
	       "                     <txsubchans> is one of:\n"
	       "                     mono:	 Modulate as mono\n"
	       "                     mono-rds:	 Modulate as mono with RDS (radio only)\n"
	       "                     stereo:	 Modulate as stereo\n"
	       "                     stereo-rds: Modulate as stereo with RDS (radio only)\n"
	       "                     bilingual:	 Modulate as bilingual\n"
	       "                     mono-sap:	 Modulate as mono with Second Audio Program\n"
	       "                     stereo-sap: Modulate as stereo with Second Audio Program\n"
	       "  --freq-seek=dir=<0/1>,wrap=<0/1>,spacing=<hz>\n"
	       "                     perform a hardware frequency seek [VIDIOC_S_HW_FREQ_SEEK]\n"
	       "                     dir is 0 (seek downward) or 1 (seek upward)\n"
	       "                     wrap is 0 (do not wrap around) or 1 (wrap around)\n"
	       "                     spacing sets the seek resolution (use 0 for default)\n"
	       );
}

static const char *audmode2s(int audmode)
{
	switch (audmode) {
		case V4L2_TUNER_MODE_STEREO: return "stereo";
		case V4L2_TUNER_MODE_LANG1: return "lang1";
		case V4L2_TUNER_MODE_LANG2: return "lang2";
		case V4L2_TUNER_MODE_LANG1_LANG2: return "bilingual";
		case V4L2_TUNER_MODE_MONO: return "mono";
		default: return "unknown";
	}
}

static std::string rxsubchans2s(int rxsubchans)
{
	std::string s;

	if (rxsubchans & V4L2_TUNER_SUB_MONO)
		s += "mono ";
	if (rxsubchans & V4L2_TUNER_SUB_STEREO)
		s += "stereo ";
	if (rxsubchans & V4L2_TUNER_SUB_LANG1)
		s += "lang1 ";
	if (rxsubchans & V4L2_TUNER_SUB_LANG2)
		s += "lang2 ";
	if (rxsubchans & V4L2_TUNER_SUB_RDS)
		s += "rds ";
	return s;
}

static std::string txsubchans2s(int txsubchans)
{
	std::string s;

	if (txsubchans & V4L2_TUNER_SUB_MONO)
		s += "mono ";
	if (txsubchans & V4L2_TUNER_SUB_STEREO)
		s += "stereo ";
	if (txsubchans & V4L2_TUNER_SUB_LANG1)
		s += "bilingual ";
	if (txsubchans & V4L2_TUNER_SUB_SAP)
		s += "sap ";
	if (txsubchans & V4L2_TUNER_SUB_RDS)
		s += "rds ";
	return s;
}

static std::string tcap2s(unsigned cap)
{
	std::string s;

	if (cap & V4L2_TUNER_CAP_LOW)
		s += "62.5 Hz ";
	else
		s += "62.5 kHz ";
	if (cap & V4L2_TUNER_CAP_NORM)
		s += "multi-standard ";
	if (cap & V4L2_TUNER_CAP_HWSEEK_BOUNDED)
		s += "hwseek-bounded ";
	if (cap & V4L2_TUNER_CAP_HWSEEK_WRAP)
		s += "hwseek-wrap ";
	if (cap & V4L2_TUNER_CAP_STEREO)
		s += "stereo ";
	if (cap & V4L2_TUNER_CAP_LANG1)
		s += "lang1 ";
	if (cap & V4L2_TUNER_CAP_LANG2)
		s += "lang2 ";
	if (cap & V4L2_TUNER_CAP_RDS)
		s += "rds ";
	return s;
}

static void parse_freq_seek(char *optarg, struct v4l2_hw_freq_seek &seek)
{
	char *value;
	char *subs = optarg;

	while (*subs != '\0') {
		static const char *const subopts[] = {
			"dir",
			"wrap",
			"spacing",
			NULL
		};

		switch (parse_subopt(&subs, subopts, &value)) {
		case 0:
			seek.seek_upward = strtol(value, 0L, 0);
			break;
		case 1:
			seek.wrap_around = strtol(value, 0L, 0);
			break;
		case 2:
			seek.spacing = strtol(value, 0L, 0);
			break;
		default:
			tuner_usage();
			exit(1);
		}
	}
}

void tuner_cmd(int ch, char *optarg)
{
	switch (ch) {
	case OptSetFreq:
		freq = strtod(optarg, NULL);
		break;
	case OptSetTuner:
		if (!strcmp(optarg, "stereo"))
			mode = V4L2_TUNER_MODE_STEREO;
		else if (!strcmp(optarg, "lang1"))
			mode = V4L2_TUNER_MODE_LANG1;
		else if (!strcmp(optarg, "lang2"))
			mode = V4L2_TUNER_MODE_LANG2;
		else if (!strcmp(optarg, "bilingual"))
			mode = V4L2_TUNER_MODE_LANG1_LANG2;
		else if (!strcmp(optarg, "mono"))
			mode = V4L2_TUNER_MODE_MONO;
		else {
			fprintf(stderr, "Unknown audio mode\n");
			tuner_usage();
			exit(1);
		}
		break;
	case OptSetModulator:
		txsubchans = strtol(optarg, 0L, 0);
		if (!strcmp(optarg, "stereo"))
			txsubchans = V4L2_TUNER_SUB_STEREO;
		else if (!strcmp(optarg, "stereo-sap"))
			txsubchans = V4L2_TUNER_SUB_STEREO | V4L2_TUNER_SUB_SAP;
		else if (!strcmp(optarg, "bilingual"))
			txsubchans = V4L2_TUNER_SUB_LANG1;
		else if (!strcmp(optarg, "mono"))
			txsubchans = V4L2_TUNER_SUB_MONO;
		else if (!strcmp(optarg, "mono-sap"))
			txsubchans = V4L2_TUNER_SUB_MONO | V4L2_TUNER_SUB_SAP;
		else if (!strcmp(optarg, "stereo-rds"))
			txsubchans = V4L2_TUNER_SUB_STEREO | V4L2_TUNER_SUB_RDS;
		else if (!strcmp(optarg, "mono-rds"))
			txsubchans = V4L2_TUNER_SUB_MONO | V4L2_TUNER_SUB_RDS;
		else {
			fprintf(stderr, "Unknown txsubchans value\n");
			tuner_usage();
			exit(1);
		}
		break;
	case OptFreqSeek:
		parse_freq_seek(optarg, freq_seek);
		break;
	case OptTunerIndex:
		tuner_index = strtoul(optarg, NULL, 0);
		break;
	}
}

void tuner_set(int fd)
{
	if (options[OptSetFreq]) {
		double fac = 16;

		if (capabilities & V4L2_CAP_MODULATOR) {
			vf.type = V4L2_TUNER_RADIO;
			modulator.index = tuner_index;
			if (doioctl(fd, VIDIOC_G_MODULATOR, &modulator) == 0) {
				fac = (modulator.capability & V4L2_TUNER_CAP_LOW) ? 16000 : 16;
			}
		} else {
			vf.type = V4L2_TUNER_ANALOG_TV;
			tuner.index = tuner_index;
			if (doioctl(fd, VIDIOC_G_TUNER, &tuner) == 0) {
				fac = (tuner.capability & V4L2_TUNER_CAP_LOW) ? 16000 : 16;
				vf.type = tuner.type;
			}
		}
		vf.tuner = tuner_index;
		vf.frequency = __u32(freq * fac);
		if (doioctl(fd, VIDIOC_S_FREQUENCY, &vf) == 0)
			printf("Frequency for tuner %d set to %d (%f MHz)\n",
			       vf.tuner, vf.frequency, vf.frequency / fac);
	}

	if (options[OptSetTuner]) {
		struct v4l2_tuner vt;

		memset(&vt, 0, sizeof(struct v4l2_tuner));
		vt.index = tuner_index;
		if (doioctl(fd, VIDIOC_G_TUNER, &vt) == 0) {
			vt.audmode = mode;
			doioctl(fd, VIDIOC_S_TUNER, &vt);
		}
	}

	if (options[OptSetModulator]) {
		struct v4l2_modulator mt;

		memset(&mt, 0, sizeof(struct v4l2_modulator));
		mt.index = tuner_index;
		if (doioctl(fd, VIDIOC_G_MODULATOR, &mt) == 0) {
			mt.txsubchans = txsubchans;
			doioctl(fd, VIDIOC_S_MODULATOR, &mt);
		}
	}

	if (options[OptFreqSeek]) {
		freq_seek.tuner = tuner_index;
		freq_seek.type = V4L2_TUNER_RADIO;
		doioctl(fd, VIDIOC_S_HW_FREQ_SEEK, &freq_seek);
	}
}

void tuner_get(int fd)
{
	if (options[OptGetFreq]) {
		double fac = 16;

		if (capabilities & V4L2_CAP_MODULATOR) {
			vf.type = V4L2_TUNER_RADIO;
			modulator.index = tuner_index;
			if (doioctl(fd, VIDIOC_G_MODULATOR, &modulator) == 0)
				fac = (modulator.capability & V4L2_TUNER_CAP_LOW) ? 16000 : 16;
		} else {
			vf.type = V4L2_TUNER_ANALOG_TV;
			tuner.index = tuner_index;
			if (doioctl(fd, VIDIOC_G_TUNER, &tuner) == 0) {
				fac = (tuner.capability & V4L2_TUNER_CAP_LOW) ? 16000 : 16;
				vf.type = tuner.type;
			}
		}
		vf.tuner = tuner_index;
		if (doioctl(fd, VIDIOC_G_FREQUENCY, &vf) == 0)
			printf("Frequency for tuner %d: %d (%f MHz)\n",
			       vf.tuner, vf.frequency, vf.frequency / fac);
	}

	if (options[OptGetTuner]) {
		struct v4l2_tuner vt;

		memset(&vt, 0, sizeof(struct v4l2_tuner));
		vt.index = tuner_index;
		if (doioctl(fd, VIDIOC_G_TUNER, &vt) == 0) {
			printf("Tuner %d:\n", vt.index);
			printf("\tName                 : %s\n", vt.name);
			printf("\tCapabilities         : %s\n", tcap2s(vt.capability).c_str());
			if (vt.capability & V4L2_TUNER_CAP_LOW)
				printf("\tFrequency range      : %.1f MHz - %.1f MHz\n",
				     vt.rangelow / 16000.0, vt.rangehigh / 16000.0);
			else
				printf("\tFrequency range      : %.1f MHz - %.1f MHz\n",
				     vt.rangelow / 16.0, vt.rangehigh / 16.0);
			printf("\tSignal strength/AFC  : %d%%/%d\n", (int)((vt.signal / 655.35)+0.5), vt.afc);
			printf("\tCurrent audio mode   : %s\n", audmode2s(vt.audmode));
			printf("\tAvailable subchannels: %s\n",
					rxsubchans2s(vt.rxsubchans).c_str());
		}
	}

	if (options[OptGetModulator]) {
		struct v4l2_modulator mt;

		memset(&mt, 0, sizeof(struct v4l2_modulator));
		modulator.index = tuner_index;
		if (doioctl(fd, VIDIOC_G_MODULATOR, &mt) == 0) {
			printf("Modulator %d:\n", modulator.index);
			printf("\tName                 : %s\n", mt.name);
			printf("\tCapabilities         : %s\n", tcap2s(mt.capability).c_str());
			if (mt.capability & V4L2_TUNER_CAP_LOW)
				printf("\tFrequency range      : %.1f MHz - %.1f MHz\n",
				     mt.rangelow / 16000.0, mt.rangehigh / 16000.0);
			else
				printf("\tFrequency range      : %.1f MHz - %.1f MHz\n",
				     mt.rangelow / 16.0, mt.rangehigh / 16.0);
			printf("\tSubchannel modulation: %s\n",
					txsubchans2s(mt.txsubchans).c_str());
		}
	}
}

void tuner_list(int fd)
{
}