/*
 * mpxgen - FM multiplex encoder with Stereo and RDS
 * Copyright (C) 2021 Anthony96922
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
#include "waveforms.h"
#include "modulator.h"

static struct rds_t **rds_ctx;
static float **waveform;

/*
 * Create the RDS objects
 *
 */
void init_rds_objects() {
	rds_ctx = malloc(NUM_STREAMS * sizeof(struct rds_t));

	for (uint8_t i = 0; i < NUM_STREAMS; i++) {
		rds_ctx[i] = malloc(sizeof(struct rds_t));
		rds_ctx[i]->bit_buffer = malloc(BITS_PER_GROUP);
		rds_ctx[i]->sample_buffer =
			malloc(SAMPLE_BUFFER_SIZE * sizeof(float));

#ifdef RDS2_SYMBOL_SHIFTING
		/*
		 * symbol shifting to reduce total power of aggregate carriers
		 *
		 * see:
		 * https://ietresearch.onlinelibrary.wiley.com/doi/pdf/10.1049/el.2019.0292
		 * for more information
		 */
		switch (i) {
		case 1:
			/* time offset of 1/2 */
			rds_ctx[i]->symbol_shift = SAMPLES_PER_BIT / 2;
			rds_ctx[i]->symbol_shift_buf = malloc(
				rds_ctx[i]->symbol_shift * sizeof(float));
			memset(rds_ctx[i]->symbol_shift_buf, 0,
				rds_ctx[i]->symbol_shift * sizeof(float));
			break;
		case 2:
			/* time offset of 1/4 */
			rds_ctx[i]->symbol_shift = SAMPLES_PER_BIT / 4;
			rds_ctx[i]->symbol_shift_buf = malloc(
				rds_ctx[i]->symbol_shift * sizeof(float));
			memset(rds_ctx[i]->symbol_shift_buf, 0,
				rds_ctx[i]->symbol_shift * sizeof(float));
			break;
		case 3:
			/* time offset of 3/4 */
			rds_ctx[i]->symbol_shift = (SAMPLES_PER_BIT / 4) * 3;
			rds_ctx[i]->symbol_shift_buf = malloc(
				rds_ctx[i]->symbol_shift * sizeof(float));
			memset(rds_ctx[i]->symbol_shift_buf, 0,
				rds_ctx[i]->symbol_shift * sizeof(float));
			break;
		default: /* stream 0 */
			/* no time offset */
			rds_ctx[i]->symbol_shift = 0;
			break;
		}
#endif
		rds_ctx[i]->symbol_shift_buf_idx = 0;

	}

	waveform = malloc(2 * sizeof(float));

	for (uint8_t i = 0; i < 2; i++) {
		waveform[i] = malloc(FILTER_SIZE * sizeof(float));
		for (uint16_t j = 0; j < FILTER_SIZE; j++) {
			waveform[i][j] = i ?
				+waveform_biphase[j] : -waveform_biphase[j];
		}
	}
}

void exit_rds_objects() {
	for (uint8_t i = 0; i < NUM_STREAMS; i++) {
		free(rds_ctx[i]->sample_buffer);
		free(rds_ctx[i]->bit_buffer);
		free(rds_ctx[i]);
		if (rds_ctx[i]->symbol_shift) {
			free(rds_ctx[i]->symbol_shift_buf);
		}
	}

	free(rds_ctx);

	for (uint8_t i = 0; i < 2; i++) {
		free(waveform[i]);
	}

	free(waveform);
}

/* Get an RDS sample. This generates the envelope of the waveform using
 * pre-generated elementary waveform samples.
 */
#ifdef RDS2
float get_rds_sample(uint8_t stream_num, uint8_t rds2tunneling) {
#else
float get_rds_sample(uint8_t stream_num) {
#endif
	struct rds_t *rds;
	uint16_t idx;
	float *cur_waveform;
	float sample;

	/* select context */
	rds = rds_ctx[stream_num];

	if (rds->sample_count == SAMPLES_PER_BIT) {
		if (rds->bit_pos == BITS_PER_GROUP) {
#ifdef RDS2
			if(!rds2tunneling) {
				if (stream_num > 0) {
					get_rds2_bits(stream_num, rds->bit_buffer);
				} else {
					get_rds_bits(rds->bit_buffer);
				}
			} else {
				get_rds_bits(rds->bit_buffer);
			}
#else
			get_rds_bits(rds->bit_buffer);
#endif
			rds->bit_pos = 0;
		}

		/* do differential encoding */
		rds->cur_bit = rds->bit_buffer[rds->bit_pos++];
		rds->prev_output = rds->cur_output;
		rds->cur_output = rds->prev_output ^ rds->cur_bit;

		idx = rds->in_sample_index;
		cur_waveform = waveform[rds->cur_output];

		for (uint16_t i = 0; i < FILTER_SIZE; i++) {
			rds->sample_buffer[idx++] += *cur_waveform++;
			if (idx == SAMPLE_BUFFER_SIZE) idx = 0;
		}

		rds->in_sample_index += SAMPLES_PER_BIT;
		if (rds->in_sample_index == SAMPLE_BUFFER_SIZE)
			rds->in_sample_index = 0;

		rds->sample_count = 0;
	}
	rds->sample_count++;

	if (rds->symbol_shift) {
		rds->symbol_shift_buf[rds->symbol_shift_buf_idx++] =
			rds->sample_buffer[rds->out_sample_index];

		if (rds->symbol_shift_buf_idx == rds->symbol_shift)
			rds->symbol_shift_buf_idx = 0;

		sample = rds->symbol_shift_buf[rds->symbol_shift_buf_idx];

		goto done;
	}

	sample = rds->sample_buffer[rds->out_sample_index];
done:
	rds->sample_buffer[rds->out_sample_index++] = 0;
	if (rds->out_sample_index == SAMPLE_BUFFER_SIZE)
		rds->out_sample_index = 0;
	return sample;
}
