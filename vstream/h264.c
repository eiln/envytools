/*
 * Copyright (C) 2011 Marcin Kościelnicki <koriakin@0x04.net>
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "h264.h"
#include "h264_cabac.h"
#include <stdio.h>
#include <stdlib.h>

void h264_del_seqparm(struct h264_seqparm *seqparm) {
	free(seqparm->vui);
	free(seqparm);
}

int h264_scaling_list(struct bitstream *str, uint32_t *scaling_list, int size, uint32_t *use_default_flag) {
	uint32_t lastScale = 8;
	uint32_t nextScale = 8;
	int32_t delta_scale;
	int i;
	if (str->dir == VS_DECODE) {
		for (i = 0; i < size; i++) {
			if (nextScale != 0) {
				if (vs_se(str, &delta_scale)) return 1;
				nextScale = (lastScale + delta_scale + 256)%256;
				*use_default_flag = (i == 0 && nextScale == 0);
			}
			lastScale = scaling_list[i] = (nextScale?nextScale:lastScale);
		}
	} else {
		int haltidx = size;
		if (*use_default_flag)
			haltidx = 0;
		while (haltidx >= 2 && scaling_list[haltidx-1] == scaling_list[haltidx-2])
			haltidx--;
		for (i = 0; i < haltidx; i++) {
			delta_scale = (scaling_list[i] - lastScale + 256)%256;
			if (delta_scale >= 128)
				delta_scale -= 256;
			if (vs_se(str, &delta_scale)) return 1;
			lastScale = scaling_list[i];
		}
		if (i != size) {
			delta_scale = (0 - lastScale + 256)%256;
			if (delta_scale >= 128)
				delta_scale -= 256;
			if (vs_se(str, &delta_scale)) return 1;
		}
	}
	return 0;
}

int h264_seqparm(struct bitstream *str, struct h264_seqparm *seqparm) {
	int i;
	if (vs_u(str, &seqparm->profile_idc, 8)) return 1;
	if (vs_u(str, &seqparm->constraint_set, 8)) return 1;
	if (vs_u(str, &seqparm->level_idc, 8)) return 1;
	if (vs_ue(str, &seqparm->seq_parameter_set_id)) return 1;
	switch (seqparm->profile_idc) {
		case H264_PROFILE_BASELINE:
		case H264_PROFILE_MAIN:
		case H264_PROFILE_EXTENDED:
			if (vs_infer(str, &seqparm->chroma_format_idc, 1)) return 1;
			if (vs_infer(str, &seqparm->separate_colour_plane_flag, 0)) return 1;
			if (vs_infer(str, &seqparm->bit_depth_luma_minus8, 0)) return 1;
			if (vs_infer(str, &seqparm->bit_depth_chroma_minus8, 0)) return 1;
			if (vs_infer(str, &seqparm->qpprime_y_zero_transform_bypass_flag, 0)) return 1;
			if (vs_infer(str, &seqparm->seq_scaling_matrix_present_flag, 0)) return 1;
			break;
		case H264_PROFILE_HIGH:
		case H264_PROFILE_HIGH_10:
		case H264_PROFILE_HIGH_422:
		case H264_PROFILE_HIGH_444_PRED:
		case H264_PROFILE_CAVLC_444:
		case H264_PROFILE_SCALABLE_BASELINE:
		case H264_PROFILE_SCALABLE_HIGH:
		case H264_PROFILE_MULTIVIEW_HIGH:
		case H264_PROFILE_STEREO_HIGH:
			if (vs_ue(str, &seqparm->chroma_format_idc)) return 1;
			if (seqparm->chroma_format_idc == 3) {
				if (vs_u(str, &seqparm->separate_colour_plane_flag, 1)) return 1;
			} else {
				if (vs_infer(str, &seqparm->separate_colour_plane_flag, 0)) return 1;
			}
			if (vs_ue(str, &seqparm->bit_depth_luma_minus8)) return 1;
			if (vs_ue(str, &seqparm->bit_depth_chroma_minus8)) return 1;
			if (vs_u(str, &seqparm->qpprime_y_zero_transform_bypass_flag, 1)) return 1;
			if (vs_u(str, &seqparm->seq_scaling_matrix_present_flag, 1)) return 1;
			if (seqparm->seq_scaling_matrix_present_flag) {
				for (i = 0; i < (seqparm->chroma_format_idc == 3 ? 12 : 8); i++) {
					if (vs_u(str, &seqparm->seq_scaling_list_present_flag[i], 1)) return 1;
					if (seqparm->seq_scaling_list_present_flag[i]) {
						if (i < 6) {
							if (h264_scaling_list(str, seqparm->seq_scaling_list_4x4[i], 16, &seqparm->use_default_scaling_matrix_flag[i])) return 1;;
						} else {
							if (h264_scaling_list(str, seqparm->seq_scaling_list_8x8[i-6], 64, &seqparm->use_default_scaling_matrix_flag[i])) return 1;;
						}
					}
				}
			}
			break;
		default:
			fprintf (stderr, "Unknown profile\n");
			return 1;
	}
	if (vs_ue(str, &seqparm->log2_max_frame_num_minus4)) return 1;
	if (vs_ue(str, &seqparm->pic_order_cnt_type)) return 1;
	switch (seqparm->pic_order_cnt_type) {
		case 0:
			if (vs_ue(str, &seqparm->log2_max_pic_order_cnt_lsb_minus4)) return 1;
			break;
		case 1:
			if (vs_u(str, &seqparm->delta_pic_order_always_zero_flag, 1)) return 1;
			if (vs_se(str, &seqparm->offset_for_non_ref_pic)) return 1;
			if (vs_se(str, &seqparm->offset_for_top_to_bottom_field)) return 1;
			if (vs_ue(str, &seqparm->num_ref_frames_in_pic_order_cnt_cycle)) return 1;
			for (i = 0; i < seqparm->num_ref_frames_in_pic_order_cnt_cycle; i++) {
				if (vs_se(str, &seqparm->offset_for_ref_frame[i])) return 1;
			}
			break;
	}
	if (vs_ue(str, &seqparm->max_num_ref_frames)) return 1;
	if (vs_u(str, &seqparm->gaps_in_frame_num_value_allowed_flag, 1)) return 1;
	if (vs_ue(str, &seqparm->pic_width_in_mbs_minus1)) return 1;
	if (vs_ue(str, &seqparm->pic_height_in_map_units_minus1)) return 1;
	if (vs_u(str, &seqparm->frame_mbs_only_flag, 1)) return 1;
	if (!seqparm->frame_mbs_only_flag) {
		if (vs_u(str, &seqparm->mb_adaptive_frame_field_flag, 1)) return 1;
	} else {
		if (vs_infer(str, &seqparm->mb_adaptive_frame_field_flag, 0)) return 1;
	}
	if (vs_u(str, &seqparm->direct_8x8_inference_flag, 1)) return 1;
	if (vs_u(str, &seqparm->frame_cropping_flag, 1)) return 1;
	if (seqparm->frame_cropping_flag) {
		if (vs_ue(str, &seqparm->frame_crop_left_offset)) return 1;
		if (vs_ue(str, &seqparm->frame_crop_right_offset)) return 1;
		if (vs_ue(str, &seqparm->frame_crop_top_offset)) return 1;
		if (vs_ue(str, &seqparm->frame_crop_bottom_offset)) return 1;
	} else {
		if (vs_infer(str, &seqparm->frame_crop_left_offset, 0)) return 1;
		if (vs_infer(str, &seqparm->frame_crop_right_offset, 0)) return 1;
		if (vs_infer(str, &seqparm->frame_crop_top_offset, 0)) return 1;
		if (vs_infer(str, &seqparm->frame_crop_bottom_offset, 0)) return 1;
	}
	uint32_t vui_parameters_present_flag = !!(seqparm->vui);
	if (vs_u(str, &vui_parameters_present_flag, 1)) return 1;
	if (vui_parameters_present_flag) {
		if (str->dir == VS_DECODE) {
			seqparm->vui = calloc (sizeof *seqparm->vui, 1);
		}
		/* XXX: implement me */
		abort();
	} else {
		seqparm->vui = 0;
	}
	return 0;
}

int h264_mb_avail(struct h264_slice *slice, uint32_t mbaddr) {
	/* XXX: wrong with FMO */
	if (mbaddr < slice->first_mb_in_slice
		|| mbaddr > slice->curr_mb_addr)
		return 0;
	return 1;
}

uint32_t h264_next_mb_addr(struct h264_slice *slice, uint32_t mbaddr) {
	/* XXX: wrong with FMO */
	return mbaddr+1;
}

uint32_t h264_mb_nb(struct h264_slice *slice, enum h264_mb_pos pos) {
	uint32_t mbaddr = slice->curr_mb_addr;
	if (slice->mbaff_frame_flag)
		mbaddr /= 2;
	switch (pos) {
		case H264_MB_THIS:
			break;
		case H264_MB_A:
			if ((mbaddr % slice->pic_width_in_mbs) == 0)
				return -1;
			mbaddr--;
			break;
		case H264_MB_B:
			mbaddr -= slice->pic_width_in_mbs;
			break;
		case H264_MB_C:
			if (((mbaddr+1) % slice->pic_width_in_mbs) == 0)
				return -1;
			mbaddr -= slice->pic_width_in_mbs - 1;
			break;
		case H264_MB_D:
			if ((mbaddr % slice->pic_width_in_mbs) == 0)
				return -1;
			mbaddr -= slice->pic_width_in_mbs + 1;
			break;
	}
	if (slice->mbaff_frame_flag)
		mbaddr *= 2;
	if (!h264_mb_avail(slice, mbaddr))
		return -1;
	return mbaddr;
}

int h264_pred_weight_table_entry(struct bitstream *str, struct h264_pred_weight_table *table, struct h264_pred_weight_table_entry *entry) {
	int ret = 0;
	ret |= vs_u(str, &entry->luma_weight_flag, 1);
	if (entry->luma_weight_flag) {
		ret |= vs_se(str, &entry->luma_weight);
		ret |= vs_se(str, &entry->luma_offset);
	} else {
		ret |= vs_infer(str, &entry->luma_weight, 1 << table->luma_log2_weight_denom);
		ret |= vs_infer(str, &entry->luma_offset, 0);
	}
	ret |= vs_u(str, &entry->chroma_weight_flag, 1);
	if (entry->chroma_weight_flag) {
		ret |= vs_se(str, &entry->chroma_weight[0]);
		ret |= vs_se(str, &entry->chroma_offset[0]);
		ret |= vs_se(str, &entry->chroma_weight[1]);
		ret |= vs_se(str, &entry->chroma_offset[1]);
	} else {
		ret |= vs_infer(str, &entry->chroma_weight[0], 1 << table->chroma_log2_weight_denom);
		ret |= vs_infer(str, &entry->chroma_offset[0], 0);
		ret |= vs_infer(str, &entry->chroma_weight[1], 1 << table->chroma_log2_weight_denom);
		ret |= vs_infer(str, &entry->chroma_offset[1], 0);
	}
	return ret;
}

int h264_pred_weight_table(struct bitstream *str, struct h264_slice *slice, struct h264_pred_weight_table *table) {
	int ret = 0;
	ret |= vs_ue(str, &table->luma_log2_weight_denom);
	ret |= vs_ue(str, &table->chroma_log2_weight_denom);
	int i;
	for (i = 0; i <= slice->num_ref_idx_l0_active_minus1; i++)
		ret |= h264_pred_weight_table_entry(str, table, &table->l0[i]);
	if (slice->slice_type % 5 == 1)
		for (i = 0; i <= slice->num_ref_idx_l1_active_minus1; i++)
			ret |= h264_pred_weight_table_entry(str, table, &table->l1[i]);
	return ret;
}

int h264_mb_pred(struct bitstream *str, struct h264_cabac_context *cabac, struct h264_slice *slice, struct h264_macroblock *mb) {
	if (mb->mb_type < H264_MB_TYPE_P_BASE) {
		if (!h264_is_intra_16x16_mb_type(mb->mb_type)) {
		}

		/* XXX */
		abort();
	} else if (mb->mb_type != H264_MB_TYPE_B_DIRECT_16X16) {
		/* XXX */
		abort();
	}
}

int h264_sub_mb_pred(struct bitstream *str, struct h264_cabac_context *cabac, struct h264_slice *slice, struct h264_macroblock *mb) {
	/* XXX */
	abort();
}

int h264_residual(struct bitstream *str, struct h264_cabac_context *cabac, struct h264_slice *slice, struct h264_macroblock *mb, int a, int b) {
	/* XXX */
	abort();
}

int h264_macroblock_layer(struct bitstream *str, struct h264_cabac_context *cabac, struct h264_slice *slice, struct h264_macroblock *mb) {
	struct h264_picparm *picparm = slice->picparm;
	struct h264_seqparm *seqparm = picparm->seqparm;
	if (h264_mb_type(str, cabac, slice->slice_type, &mb->mb_type)) return 1;
	if (mb->mb_type == H264_MB_TYPE_I_PCM) {
		if (vs_align_byte(str, VS_ALIGN_0)) return 1;
		int i;
		for (i = 0; i < 256; i++)
			vs_u(str, &mb->pcm_sample_luma[i], seqparm->bit_depth_luma_minus8 + 8);
		for (i = 0; i < 2 * slice->mbwidthc * slice->mbheightc; i++)
			vs_u(str, &mb->pcm_sample_chroma[i], seqparm->bit_depth_chroma_minus8 + 8);
		if (cabac)
			if (h264_cabac_init_arith(str, cabac)) return 1;
		if (vs_infer(str, &mb->mb_qp_delta, 0)) return 1;
		if (vs_infer(str, &mb->transform_size_8x8_flag, 0)) return 1;
		if (vs_infer(str, &mb->coded_block_pattern, 0x2f)) return 1;
		if (vs_infer(str, &mb->intra_chroma_pred_mode, 0)) return 1;
	} else {
		int noSubMbPartSizeLessThan8x8Flag = 1;
		if (h264_is_submb_mb_type(mb->mb_type)) {
			if (h264_sub_mb_pred(str, cabac, slice, mb)) return 1;
			int i;
			for (i = 0; i < 4; i++) {
				if (mb->sub_mb_type[i] != H264_SUB_MB_TYPE_B_DIRECT_8X8) {
					if (h264_sub_mb_type_split_mode(mb->sub_mb_type[i]))
						noSubMbPartSizeLessThan8x8Flag = 0;
				} else {
					if (!seqparm->direct_8x8_inference_flag)
						noSubMbPartSizeLessThan8x8Flag = 0;
				}
			}
		} else {
			if (mb->mb_type == H264_MB_TYPE_I_NXN) {
				if (picparm->transform_8x8_mode_flag) {
					if (h264_transform_size_8x8_flag(str, cabac, &mb->transform_size_8x8_flag)) return 1;
				} else {
					if (vs_infer(str, &mb->transform_size_8x8_flag, 0)) return 1;
				}
			}
			if (h264_mb_pred(str, cabac, slice, mb)) return 1;
		}
		if (mb->mb_type == H264_MB_TYPE_I_NXN || mb->mb_type >= H264_MB_TYPE_I_END) {
			int has_chroma = seqparm->chroma_format_idc < 3 && seqparm->chroma_format_idc != 0;
			if (h264_coded_block_pattern(str, cabac, mb->mb_type, has_chroma, &mb->coded_block_pattern)) return 1;
			if (mb->mb_type != H264_MB_TYPE_I_NXN) {
				if ((mb->coded_block_pattern & 0xf) && picparm->transform_8x8_mode_flag && noSubMbPartSizeLessThan8x8Flag && (mb->mb_type != H264_MB_TYPE_B_DIRECT_16X16 || seqparm->direct_8x8_inference_flag)) {
					if (h264_transform_size_8x8_flag(str, cabac, &mb->transform_size_8x8_flag)) return 1;
				} else {
					if (vs_infer(str, &mb->transform_size_8x8_flag, 0)) return 1;
				}
			}
		} else {
			int infer_cbp = (((mb->mb_type - H264_MB_TYPE_I_16X16_0_0_0) >> 2) % 3) << 4;
			if (mb->mb_type >= H264_MB_TYPE_I_16X16_0_0_1)
				infer_cbp = 0xf;
			if (vs_infer(str, &mb->coded_block_pattern, infer_cbp)) return 1;
			if (vs_infer(str, &mb->transform_size_8x8_flag, 0)) return 1;
		}
		if (mb->coded_block_pattern || h264_is_intra_16x16_mb_type(mb->mb_type)) {
			if (h264_mb_qp_delta(str, cabac, &mb->mb_qp_delta))
				return 1;
			if (h264_residual(str, cabac, slice, mb, 0, 15)) return 1;
		}
	}
	return 0;
}

int infer_skip(struct bitstream *str, struct h264_slice *slice, struct h264_macroblock *mb) {
	uint32_t skip_type = (slice->slice_type == H264_SLICE_TYPE_B ? H264_MB_TYPE_B_SKIP : H264_MB_TYPE_P_SKIP);
	if ((slice->curr_mb_addr & 1) && h264_is_skip_mb_type(slice->mbs[slice->curr_mb_addr & ~1].mb_type))
		if (vs_infer(str, &mb->mb_field_decoding_flag, 0)) return 1;
	if (vs_infer(str, &mb->mb_type, skip_type)) return 1;
	if (vs_infer(str, &mb->mb_qp_delta, 0)) return 1;
	if (vs_infer(str, &mb->transform_size_8x8_flag, 0)) return 1;
	if (vs_infer(str, &mb->coded_block_pattern, 0)) return 1;
	/* XXX: more stuff? */
	return 0;
}

int h264_slice_data(struct bitstream *str, struct h264_slice *slice) {
	slice->prev_mb_addr = -1;
	slice->curr_mb_addr = slice->first_mb_in_slice * (1 + slice->mbaff_frame_flag);
	uint32_t skip_type = (slice->slice_type == H264_SLICE_TYPE_B ? H264_MB_TYPE_B_SKIP : H264_MB_TYPE_P_SKIP);
	if (slice->picparm->entropy_coding_mode_flag) {
		if (vs_align_byte(str, VS_ALIGN_1)) return 1;
		struct h264_cabac_context *cabac = h264_cabac_new(slice);
		if (h264_cabac_init_arith(str, cabac)) { h264_cabac_destroy(cabac); return 1; }
		while (1) {
			uint32_t mb_skip_flag = 0;
			if (slice->slice_type != H264_SLICE_TYPE_I && slice->slice_type != H264_SLICE_TYPE_SI) {
				if (str->dir == VS_ENCODE) {
					mb_skip_flag = slice->mbs[slice->curr_mb_addr].mb_type == skip_type;
				}
				if (h264_mb_skip_flag(str, cabac, &mb_skip_flag)) { h264_cabac_destroy(cabac); return 1; }
				if (mb_skip_flag) {
					if (infer_skip(str, slice, &slice->mbs[slice->curr_mb_addr])) { h264_cabac_destroy(cabac); return 1; }
				}
			}
			if (!mb_skip_flag) {
				if (slice->mbaff_frame_flag) {
					uint32_t first_addr = slice->curr_mb_addr & ~1;
					if (slice->curr_mb_addr == first_addr) {
						if (h264_mb_field_decoding_flag(str, cabac, &slice->mbs[first_addr].mb_field_decoding_flag)) { h264_cabac_destroy(cabac); return 1; }
					} else {
						if (slice->mbs[first_addr].mb_type == skip_type) {
							if (h264_mb_field_decoding_flag(str, cabac, &slice->mbs[first_addr].mb_field_decoding_flag)) { h264_cabac_destroy(cabac); return 1; }
						}
						if (vs_infer(str, &slice->mbs[first_addr + 1].mb_field_decoding_flag, slice->mbs[first_addr].mb_field_decoding_flag)) { h264_cabac_destroy(cabac); return 1; }
					}
				}
				if (h264_macroblock_layer(str, cabac, slice, &slice->mbs[slice->curr_mb_addr])) { h264_cabac_destroy(cabac); return 1; }
			}
			if (!slice->mbaff_frame_flag || (slice->curr_mb_addr & 1)) {
				uint32_t end_of_slice_flag = slice->last_mb_in_slice == slice->curr_mb_addr;
				if (h264_cabac_terminate(str, cabac, &end_of_slice_flag)) { h264_cabac_destroy(cabac); return 1; }
				if (end_of_slice_flag) {
					slice->last_mb_in_slice = slice->curr_mb_addr;
					h264_cabac_destroy(cabac);
					return 0;
				}
			}
			slice->curr_mb_addr = h264_next_mb_addr(slice, slice->curr_mb_addr);
		}
	} else {
		while (1) {
			int end = 0;
			uint32_t mb_skip_run = 0;
			if (slice->slice_type != H264_SLICE_TYPE_I && slice->slice_type != H264_SLICE_TYPE_SI) {
				if (str->dir == VS_ENCODE) {
					mb_skip_run = 0;
					while (slice->mbs[slice->curr_mb_addr].mb_type == skip_type) {
						mb_skip_run++;
						if (infer_skip(str, slice, &slice->mbs[slice->curr_mb_addr])) return 1;
						if (slice->curr_mb_addr == slice->last_mb_in_slice) {
							end = 1;
							break;
						}
						slice->curr_mb_addr = h264_next_mb_addr(slice, slice->curr_mb_addr);
					}
					if (vs_ue(str, &mb_skip_run)) return 1;
					if (end)
						return 0;
				} else {
					if (vs_ue(str, &mb_skip_run)) return 1;
					while (mb_skip_run--) {
						if (slice->curr_mb_addr == (uint32_t)-1) {
							fprintf(stderr, "MB index out of bounds\n");
							return 1;
						}
						slice->last_mb_in_slice = slice->curr_mb_addr;
						slice->mbs[slice->curr_mb_addr].mb_type = skip_type;
						if (infer_skip(str, slice, &slice->mbs[slice->curr_mb_addr])) return 1;
						slice->curr_mb_addr = h264_next_mb_addr(slice, slice->curr_mb_addr);
					}
					int more = vs_has_more_data(str);
					if (more == -1)
						return 1;
					if (more == 0)
						return 0;
				}
			}
			if (slice->mbaff_frame_flag) {
				uint32_t first_addr = slice->curr_mb_addr & ~1;
				if (slice->curr_mb_addr == first_addr) {
					if (h264_mb_field_decoding_flag(str, 0, &slice->mbs[first_addr].mb_field_decoding_flag)) return 1;
				} else {
					if (slice->mbs[first_addr].mb_type == skip_type)
						if (h264_mb_field_decoding_flag(str, 0, &slice->mbs[first_addr].mb_field_decoding_flag)) return 1;
					if (vs_infer(str, &slice->mbs[first_addr + 1].mb_field_decoding_flag, slice->mbs[first_addr].mb_field_decoding_flag)) return 1;
				}
			}
			if (h264_macroblock_layer(str, 0, slice, &slice->mbs[slice->curr_mb_addr])) return 1;
			slice->curr_mb_addr = h264_next_mb_addr(slice, slice->curr_mb_addr);
			if(str->dir == VS_ENCODE) {
				if (slice->last_mb_in_slice == slice->curr_mb_addr)
					return 0;
			} else {
				slice->last_mb_in_slice = slice->curr_mb_addr;
				int more = vs_has_more_data(str);
				if (more == -1)
					return 1;
				if (more == 0)
					return 0;
			}
		}
	}
}
