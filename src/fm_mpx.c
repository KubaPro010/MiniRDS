/*
 * mpxgen - FM multiplex encoder with Stereo and RDS
 * Copyright (C) 2019 Anthony96922
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "common.h"

#include "rds.h"
#ifdef RDS2
#include "rds2.h"
#endif
#include "fm_mpx.h"
#include "osc.h"

/*
 * Local oscillator objects
 * this is where the MPX waveforms are stored
 *
 */
static struct osc_t osc_19k;
static struct osc_t osc_57k;
#ifdef RDS2
static struct osc_t osc_67k;
static struct osc_t osc_71k;
static struct osc_t osc_76k;
#endif

static float mpx_vol;

static uint8_t rdsgen;

void set_output_volume(float vol) {
	if (vol > 100.0f) vol = 100.0f;
	mpx_vol = vol / 100.0f;
}

void set_rdsgen(uint8_t gen) {
	#ifdef RDS2
	if (gen > 2) gen = 2;
	#else
	if (gen > 1) gen = 1;
	#endif
	rdsgen = gen;
}

/* subcarrier volumes */
static float volumes[MPX_SUBCARRIER_END] = {
	0.0f, /* pilot tone: 0% */
	0.075f, /* RDS: 7.5% modulation */
#ifdef RDS2
	/* RDS2 */
	0.05f, //66.5  khz 5%
	0.05f, //71.25 khz 5%
	0.0f  //76     khz 0%
#endif
};

void set_carrier_volume(uint8_t carrier, float new_volume) {
	/* check for valid index */
	if (carrier >= MPX_SUBCARRIER_END) return;

	/* don't allow levels over 15% */
	if (new_volume >= 15.0f) new_volume = 15.0f;

	volumes[carrier] = new_volume / 100.0f;
}

void fm_mpx_init(uint32_t sample_rate) {
	/* initialize the subcarrier oscillators */
	osc_init(&osc_19k, sample_rate, 19000.0f);
	osc_init(&osc_57k, sample_rate, 57000.0f);
	rdsgen = 1;
#ifdef RDS2
	osc_init(&osc_67k, sample_rate, 66500.0f);
	osc_init(&osc_71k, sample_rate, 71250.0f);
	osc_init(&osc_76k, sample_rate, 76000.0f);
	rdsgen = 2;
#endif
}

void fm_rds_get_frames(float *outbuf, size_t num_frames) {
	size_t j = 0;
	float out;

	for (size_t i = 0; i < num_frames; i++) {
		out = 0.0f;

		/* Pilot tone for calibration */
		out += osc_get_cos(&osc_19k)
			* volumes[MPX_SUBCARRIER_ST_PILOT];

		#ifdef RDS2
		out += osc_get_cos(&osc_57k)
			* get_rds_sample(0, 0)
			* volumes[MPX_SUBCARRIER_RDS_STREAM_0];
		#else
		out += osc_get_cos(&osc_57k)
			* get_rds_sample(0)
			* volumes[MPX_SUBCARRIER_RDS_STREAM_0];
		#endif
#ifdef RDS2
#ifdef RDS2_QUADRATURE_CARRIER
		/* RDS2 is quadrature phase */

		/* 90 degree shift */
		if(rdsgen == 2 && volumes[MPX_SUBCARRIER_RDS2_STREAM_1] != 0) {
			out += osc_get_sin(&osc_67k)
				* get_rds_sample(1, 0)
				* volumes[MPX_SUBCARRIER_RDS2_STREAM_1];
		}

		/* 180 degree shift */
		if(rdsgen == 2 && volumes[MPX_SUBCARRIER_RDS2_STREAM_2] != 0) {
			out += -osc_get_cos(&osc_71k)
				* get_rds_sample(2, 0)
				* volumes[MPX_SUBCARRIER_RDS2_STREAM_2];
		}

		/* 270 degree shift */
		if(rdsgen == 2 && volumes[MPX_SUBCARRIER_RDS2_STREAM_3] != 0) {
			out += -osc_get_sin(&osc_76k)
				* get_rds_sample(3, 0)
				* volumes[MPX_SUBCARRIER_RDS2_STREAM_3];
		}
#else
		if(rdsgen == 2 && volumes[MPX_SUBCARRIER_RDS2_STREAM_1] != 0) {
			out += osc_get_cos(&osc_67k)
				* get_rds_sample(1, 0)
				* volumes[MPX_SUBCARRIER_RDS2_STREAM_1];
		}

		if(rdsgen == 2 && volumes[MPX_SUBCARRIER_RDS2_STREAM_1] != 0) {
			out += osc_get_cos(&osc_71k)
				* get_rds_sample(2, 0)
				* volumes[MPX_SUBCARRIER_RDS2_STREAM_2];
		}

		if(rdsgen == 2 && volumes[MPX_SUBCARRIER_RDS2_STREAM_3] != 0) {
			out += osc_get_cos(&osc_76k)
				* get_rds_sample(3, 0)
				* volumes[MPX_SUBCARRIER_RDS2_STREAM_3];
		}
#endif
#endif

		/* update oscillator */
		osc_update_pos(&osc_19k);
		osc_update_pos(&osc_57k);
#ifdef RDS2
		osc_update_pos(&osc_67k);
		osc_update_pos(&osc_71k);
		osc_update_pos(&osc_76k);
#endif

		/* declipper */
		out = fminf(+1.0f, out);
		out = fmaxf(-1.0f, out);

		/* adjust volume and put into channel */
		if(rdsgen != 0) outbuf[j] = out * mpx_vol;
		j++;

	}
}

void fm_mpx_exit() {
	osc_exit(&osc_19k);
	osc_exit(&osc_57k);
#ifdef RDS2
	osc_exit(&osc_67k);
	osc_exit(&osc_71k);
	osc_exit(&osc_76k);
#endif
}
