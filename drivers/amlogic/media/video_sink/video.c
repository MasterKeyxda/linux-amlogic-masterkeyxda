/*
 * drivers/amlogic/media/video_sink/video.c
 *
 * Copyright (C) 2017 Amlogic, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/amlogic/major.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/ctype.h>
#include <linux/amlogic/media/frame_sync/ptsserv.h>
#include <linux/amlogic/media/frame_sync/timestamp.h>
#include <linux/amlogic/media/frame_sync/tsync.h>
#include <linux/amlogic/media/vfm/vframe.h>
#include <linux/amlogic/media/vfm/vframe_provider.h>
#include <linux/amlogic/media/vfm/vframe_receiver.h>
#include <linux/amlogic/media/utils/amstream.h>
#include <linux/amlogic/media/vout/vout_notify.h>
#include <linux/amlogic/media/video_sink/video_signal_notify.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/amlogic/media/canvas/canvas.h>
#include <linux/amlogic/media/canvas/canvas_mgr.h>
#include <linux/dma-mapping.h>
#include <linux/dma-contiguous.h>
#include <linux/sched.h>
#include <linux/amlogic/media/video_sink/video_keeper.h>
#include "video_priv.h"
#include "video_reg.h"
#define KERNEL_ATRACE_TAG KERNEL_ATRACE_TAG_VIDEO
#include <trace/events/meson_atrace.h>

#if defined(CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_VECM)
#include "vpp_pq.h"
#include <linux/amlogic/media/amvecm/amvecm.h>
#endif
#include <linux/amlogic/media/utils/vdec_reg.h>

#ifdef CONFIG_PM
#include <linux/delay.h>
#include <linux/pm.h>
#endif

#include <linux/amlogic/media/registers/register.h>
#include <linux/uaccess.h>
#include <linux/amlogic/media/utils/amports_config.h>
#include <linux/amlogic/media/vpu/vpu.h>
#include "videolog.h"
#ifdef CONFIG_AM_VIDEO_LOG
#define AMLOG
#endif
#include <linux/amlogic/media/utils/amlog.h>
MODULE_AMLOG(LOG_LEVEL_ERROR, 0, LOG_DEFAULT_LEVEL_DESC, LOG_MASK_DESC);

#include <linux/amlogic/media/video_sink/vpp.h>
#include "linux/amlogic/media/frame_provider/tvin/tvin_v4l2.h"
#ifdef CONFIG_AMLOGIC_MEDIA_VSYNC_RDMA
#include "../common/rdma/rdma.h"
#endif
#include <linux/amlogic/media/video_sink/video.h>
#include <linux/amlogic/media/codec_mm/configs.h>
#include <linux/amlogic/media/codec_mm/codec_mm.h>

#include "../common/vfm/vfm.h"
#include <linux/amlogic/media/amdolbyvision/dolby_vision.h>

#include <linux/amlogic/media/di/di.h>

#ifdef CONFIG_AMLOGIC_LEGACY_EARLY_SUSPEND
#include <linux/amlogic/pm.h>
#endif
#ifdef CONFIG_AMLOGIC_MEDIA_SECURITY
#include <linux/amlogic/media/vpu_secure/vpu_secure.h>
#endif
#include <linux/math64.h>
#include <linux/fence.h>
#include "video_receiver.h"
#define CREATE_TRACE_POINTS
#include "video_trace.h"
#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE video_trace
#include <trace/define_trace.h>
#include <linux/cpufreq.h>
#ifdef CONFIG_AMLOGIC_MEDIA_MSYNC
#include <uapi/linux/amlogic/msync.h>
#endif

static int get_count;
static int get_di_count;
static int put_di_count;
static int di_release_count;
static int get_count_pip;

#define DEBUG_TMP 0

u32 osd_vpp_misc;
u32 osd_vpp_misc_mask;
bool update_osd_vpp_misc;
u32 osd_preblend_en;
int video_vsync = -ENXIO;
int video_vsync_viu2 = -ENXIO;

static struct ai_scenes_pq vpp_scenes[AI_SCENES_MAX];
static u32 cur_omx_index;

struct video_frame_detect_s {
	u32 interrupt_count;
	u32 start_receive_count;
};

static int debugflags;
static int output_fps;
static u32 omx_pts;
static u32 omx_pts_set_index;
static bool omx_run;
static u32 omx_version = 3;
static u32 omx_continusdrop_cnt;
#define OMX_PTS_DV_DEFAULT_UPPER 2500
#define OMX_PTS_DV_DEFAULT_LOWER -1600
static int omx_pts_interval_upper = 5500;
static int omx_pts_interval_lower = -5500;
static int omx_pts_dv_upper = OMX_PTS_DV_DEFAULT_UPPER;
static int omx_pts_dv_lower = OMX_PTS_DV_DEFAULT_LOWER;
static int omx_pts_set_from_hwc_count;
static int omx_pts_set_from_hwc_count_begin;
static bool omx_check_previous_session;
u32 omx_cur_session = 0xffffffff;
static int drop_frame_count;
static int OMX_MAX_COUNT_RESET_SYSTEMTIME = 5;
#define OMX_MAX_COUNT_RESET_SYSTEMTIME_BEGIN 10
static int receive_frame_count;
static int display_frame_count;
static int omx_need_drop_frame_num;
static bool omx_drop_done;
static bool video_start_post;
static bool videopeek;
static bool nopostvideostart;
static bool nopostvideostop;
static int hold_property_changed;
static struct video_frame_detect_s video_frame_detect;
static long long time_setomxpts;
static long long time_setomxpts_last;
struct nn_value_t nn_scenes_value[AI_PQ_TOP];

/*----omx_info  bit0: keep_last_frame, bit1~31: unused----*/
static u32 omx_info = 0x1;

#define ENABLE_UPDATE_HDR_FROM_USER 0
#if ENABLE_UPDATE_HDR_FROM_USER
static struct vframe_master_display_colour_s vf_hdr;
static bool has_hdr_info;
#endif
static DEFINE_MUTEX(omx_mutex);

#define DURATION_GCD 750

static bool bypass_pps = true;

/* for bit depth setting. */
int bit_depth_flag = 8;

bool omx_secret_mode;
EXPORT_SYMBOL(omx_secret_mode);
static int omx_continuous_drop_count;
static bool omx_continuous_drop_flag;
static u32 cur_disp_omx_index;
#define OMX_CONTINUOUS_DROP_LEVEL 5
#define DEBUG_FLAG_FFPLAY	(1<<0)
#define DEBUG_FLAG_CALC_PTS_INC	(1<<1)

static bool dovi_drop_flag;
static int dovi_drop_frame_num;

#define RECEIVER_NAME "amvideo"
#ifdef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
static char dv_provider[32] = "dvbldec";
#endif

static s32 amvideo_poll_major;
/*static s8 dolby_first_delay;*/ /* for bug 145902 */

static struct time_info_t time_info[20];
static long long  min_dis_hwc;
static long long  max_dis_hwc;

#define PARSE_MD_IN_ADVANCE 1

static int video_receiver_event_fun(int type, void *data, void *);

static const struct vframe_receiver_op_s video_vf_receiver = {
	.event_cb = video_receiver_event_fun
};

static struct vframe_receiver_s video_vf_recv;

#define RECEIVERPIP_NAME "videopip"
static int pip_receiver_event_fun(int type, void *data, void *);

static const struct vframe_receiver_op_s videopip_vf_receiver = {
	.event_cb = pip_receiver_event_fun
};

static struct vframe_receiver_s videopip_vf_recv;

static struct vpp_frame_par_s *curpip_frame_par;
struct vframe_s *cur_pipbuf;
struct vframe_s local_pip;
struct vframe_s local_pip_ext;

static struct device *amvideo_dev;
static struct device *amvideo_poll_dev;

static u32 cur_width;
static u32 cur_height;

#define DRIVER_NAME "amvideo"
#define MODULE_NAME "amvideo"
#define DEVICE_NAME "amvideo"

#ifdef CONFIG_AML_VSYNC_FIQ_ENABLE
#define FIQ_VSYNC
#endif

/* #define SLOW_SYNC_REPEAT */
/* #define INTERLACE_FIELD_MATCH_PROCESS */
bool disable_slow_sync;

#ifdef INTERLACE_FIELD_MATCH_PROCESS
#define FIELD_MATCH_THRESHOLD  10
static int field_matching_count;
#endif

#define M_PTS_SMOOTH_MAX 45000
#define M_PTS_SMOOTH_MIN 2250
#define M_PTS_SMOOTH_ADJUST 900
static u32 underflow;
static u32 next_peek_underflow;

static u32 frame_skip_check_cnt;

/*frame_detect_flag: 1 enable, 0 disable */
/*frame_detect_time: */
/*	How often "frame_detect_receive_count" and */
/*		"frame_detect_drop_count" are updated, suggested set 1(s) */
/*frame_detect_fps: Set fps based on the video file, */
/*					If the FPS is 60, set it to 60000. */
/*frame_detect_receive_count: */
/*	The number of frame that should be obtained during the test time. */
/*frame_detect_drop_count: */
/*	The number of frame lost during test time. */


static u32 frame_detect_flag;
static u32 frame_detect_time = 1;
static u32 frame_detect_fps = 60000;
static u32 frame_detect_receive_count;
static u32 frame_detect_drop_count;

static u32 vpp_hold_setting_cnt;

#ifdef FIQ_VSYNC
#define BRIDGE_IRQ INT_TIMER_C
#define BRIDGE_IRQ_SET() WRITE_CBUS_REG(ISA_TIMERC, 1)
#endif

/*********************************************************/

static DEFINE_MUTEX(video_layer_mutex);

static u32 layer_cap;

/* default value 20 30 */
static s32 black_threshold_width = 20;
static s32 black_threshold_height = 48;

static struct vframe_s hist_test_vf;
static bool hist_test_flag;
static unsigned long hist_buffer_addr;
static u32 hist_print_count;

static atomic_t gafbc_request = ATOMIC_INIT(0);

#define DUR2PTS(x) ((x) - ((x) >> 4))
#define DUR2PTS_RM(x) ((x) & 0xf)
#define PTS2DUR(x) (((x) << 4) / 15)

#ifdef VIDEO_PTS_CHASE
static int vpts_chase;
static int av_sync_flag;
static int vpts_chase_counter;
static int vpts_chase_pts_diff;
#endif

static int step_enable;
static int step_flag;

static u32 ipt_enable;
static u32 pip_ipt_enable;

struct video_recv_s *gvideo_recv[2] = {NULL, NULL};

/*seek values on.video_define.h*/
static int debug_flag;
int get_video_debug_flags(void)
{
	return debug_flag;
}

static int vsync_enter_line_max;
static int vsync_exit_line_max;

static u32 performance_debug;
static bool over_sync;
static u32 over_sync_count;

#ifdef CONFIG_AMLOGIC_MEDIA_VSYNC_RDMA
static int vsync_rdma_line_max;
#endif
u32 framepacking_support;
static unsigned int framepacking_width = 1920;
static unsigned int framepacking_height = 2205;
unsigned int framepacking_blank = 45;
unsigned int process_3d_type;
static unsigned int last_process_3d_type;
#ifdef TV_3D_FUNCTION_OPEN
/* toggle_3d_fa_frame is for checking the vpts_expire  in 2 vsnyc */
int toggle_3d_fa_frame = 1;
/*the pause_one_3d_fl_frame is for close*/
/*the A/B register switch in every sync at pause mode. */
static int pause_one_3d_fl_frame;
MODULE_PARM_DESC(pause_one_3d_fl_frame, "\n pause_one_3d_fl_frame\n");
module_param(pause_one_3d_fl_frame, uint, 0664);

/*debug info control for skip & repeate vframe case*/
static unsigned int video_dbg_vf;
MODULE_PARM_DESC(video_dbg_vf, "\n video_dbg_vf\n");
module_param(video_dbg_vf, uint, 0664);

static unsigned int video_get_vf_cnt;
static unsigned int videopip_get_vf_cnt;
static unsigned int video_drop_vf_cnt;
MODULE_PARM_DESC(video_drop_vf_cnt, "\n video_drop_vf_cnt\n");
module_param(video_drop_vf_cnt, uint, 0664);
static unsigned int videopip_drop_vf_cnt;
MODULE_PARM_DESC(videopip_drop_vf_cnt, "\n videopip_drop_vf_cnt\n");
module_param(videopip_drop_vf_cnt, uint, 0664);

static unsigned int disable_dv_drop;
MODULE_PARM_DESC(disable_dv_drop, "\n disable_dv_drop\n");
module_param(disable_dv_drop, uint, 0664);

static u32 vdin_frame_skip_cnt;
MODULE_PARM_DESC(vdin_frame_skip_cnt, "\n vdin_frame_skip_cnt\n");
module_param(vdin_frame_skip_cnt, uint, 0664);

static u32 vdin_err_crc_cnt;
MODULE_PARM_DESC(vdin_err_crc_cnt, "\n vdin_err_crc_cnt\n");
module_param(vdin_err_crc_cnt, uint, 0664);
#define ERR_CRC_COUNT 6

#ifdef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
static bool dv_mute_vpp_flag;
#endif
static unsigned int video_3d_format;
static unsigned int mvc_flag;
unsigned int force_3d_scaler = 3;
static int mode_3d_changed;
static int last_mode_3d;
#endif

bool reverse;
u32  mirror;
bool vd1_vd2_mux;
bool get_video_reverse(void)
{
	return reverse;
}
EXPORT_SYMBOL(get_video_reverse);

const char video_dev_id[] = "amvideo-dev";

static u32 stop_update;

static u32 pts_rollback_threshold = 20;

#ifdef CONFIG_AMLOGIC_MEDIA_VSYNC_RDMA
static bool first_irq = true;
#endif

#ifdef CONFIG_PM
struct video_pm_state_s {
	int event;
	u32 vpp_misc;
	int mem_pd_vd1;
	int mem_pd_vd2;
	int mem_pd_di_post;
};
#endif

#define PTS_LOGGING
#define PTS_THROTTLE
/* #define PTS_TRACE_DEBUG */
/* #define PTS_TRACE_START */
#define AVSYNC_COUNT
#ifdef AVSYNC_COUNT
static bool av_discontinue;
static u32 avsync_count;
#endif
#define PTS_ROLLBACK

#ifdef PTS_TRACE_DEBUG
static int pts_trace_his[16];
static u32 pts_his[16];
static u32 scr_his[16];
static int pts_trace_his_rd;
#endif

#if defined(PTS_LOGGING) || defined(PTS_TRACE_DEBUG)
static int pts_trace;
#endif

#if defined(PTS_LOGGING)
#define PTS_32_PATTERN_DETECT_RANGE 10
#define PTS_22_PATTERN_DETECT_RANGE 10
#define PTS_41_PATTERN_DETECT_RANGE 2
#define PTS_32_PATTERN_DURATION 3750
#define PTS_22_PATTERN_DURATION 3000


enum video_refresh_pattern {
	PTS_32_PATTERN = 0,
	PTS_22_PATTERN,
	PTS_41_PATTERN,
	PTS_MAX_NUM_PATTERNS
};

int n_patterns = PTS_MAX_NUM_PATTERNS;

static int pts_pattern[3] = {0, 0, 0};
static int pts_pattern_enter_cnt[3] = {0, 0, 0};
static int pts_pattern_exit_cnt[3] = {0, 0, 0};
static int pts_log_enable[3] = {0, 0, 0};
static int pre_pts_trace;
static int pts_escape_vsync = -1;

#define PTS_41_PATTERN_SINK_MAX 4
static int pts_41_pattern_sink[PTS_41_PATTERN_SINK_MAX];
static int pts_41_pattern_sink_index;
static int pts_pattern_detected = -1;
static bool pts_enforce_pulldown = true;
#endif

static DEFINE_MUTEX(video_module_mutex);
static DEFINE_MUTEX(video_inuse_mutex);
static DEFINE_SPINLOCK(lock);
#if ENABLE_UPDATE_HDR_FROM_USER
static DEFINE_SPINLOCK(omx_hdr_lock);
#endif
static DEFINE_SPINLOCK(hdmi_avsync_lock);

static u32 vpts_remainder;
static u32 video_notify_flag;
static int enable_video_discontinue_report = 1;
static struct amvideo_device_data_s amvideo_meson_dev;
static bool video_suspend;
static u32 video_suspend_cycle;
static int log_out;

#ifdef CONFIG_AMLOGIC_POST_PROCESS_MANAGER_PPSCALER
static u32 video_scaler_mode;
static int content_top = 0, content_left = 0, content_w = 0, content_h;
static int scaler_pos_changed;
#endif

unsigned int min_cpufreq = 1000000;

u32 get_video_angle(void)
{
	return glayer_info[0].angle;
}
EXPORT_SYMBOL(get_video_angle);

void set_video_zorder(u32 zorder, u32 index)
{
	if (index < 2)
		glayer_info[index].zorder = zorder;
}
EXPORT_SYMBOL(set_video_zorder);

/*for video related files only.*/
void video_module_lock(void)
{
	mutex_lock(&video_module_mutex);
}
void video_module_unlock(void)
{
	mutex_unlock(&video_module_mutex);
}

int video_property_notify(int flag)
{
	vd_layer[0].property_changed = flag ? true : false;
	return 0;
}

#if defined(PTS_LOGGING)
static ssize_t pts_pattern_enter_cnt_read_file(struct file *file,
			char __user *userbuf, size_t count, loff_t *ppos)
{
	char buf[20];
	ssize_t len;

	len = snprintf(buf, 20, "%d,%d,%d\n", pts_pattern_enter_cnt[0],
			pts_pattern_enter_cnt[1], pts_pattern_enter_cnt[2]);
	return simple_read_from_buffer(userbuf, count, ppos, buf, len);
}

static ssize_t pts_pattern_exit_cnt_read_file(struct file *file,
			char __user *userbuf, size_t count, loff_t *ppos)
{
	char buf[20];
	ssize_t len;

	len = snprintf(buf, 20, "%d,%d,%d\n", pts_pattern_exit_cnt[0],
			pts_pattern_exit_cnt[1], pts_pattern_exit_cnt[2]);
	return simple_read_from_buffer(userbuf, count, ppos, buf, len);
}

static ssize_t pts_log_enable_read_file(struct file *file,
			char __user *userbuf, size_t count, loff_t *ppos)
{
	char buf[20];
	ssize_t len;

	len = snprintf(buf, 20, "%d,%d,%d\n", pts_log_enable[0],
			pts_log_enable[1], pts_log_enable[2]);
	return simple_read_from_buffer(userbuf, count, ppos, buf, len);
}

static ssize_t pts_log_enable_write_file(struct file *file,
			const char __user *userbuf, size_t count, loff_t *ppos)
{
	char buf[20];
	int ret;

	count = min_t(size_t, count, (sizeof(buf)-1));
	if (copy_from_user(buf, userbuf, count))
		return -EFAULT;
	buf[count] = 0;
	/* pts_pattern_log_enable (3:2) (2:2) (4:1) */
	ret = sscanf(buf, "%d,%d,%d", &pts_log_enable[0], &pts_log_enable[1],
			&pts_log_enable[2]);
	if (ret != 3) {
		pr_info("use echo 0/1,0/1,0/1 > /sys/kernel/debug/pts_log_enable\n");
	} else {
		pr_info("pts_log_enable: %d,%d,%d\n", pts_log_enable[0],
			pts_log_enable[1], pts_log_enable[2]);
	}
	return count;
}
static ssize_t pts_enforce_pulldown_read_file(struct file *file,
			char __user *userbuf, size_t count, loff_t *ppos)
{
	char buf[16];
	ssize_t len;

	len = snprintf(buf, 16, "%d\n", pts_enforce_pulldown);
	return simple_read_from_buffer(userbuf, count, ppos, buf, len);
}

static ssize_t pts_enforce_pulldown_write_file(struct file *file,
			const char __user *userbuf, size_t count, loff_t *ppos)
{
	unsigned int write_val;
	char buf[16];
	int ret;

	count = min_t(size_t, count, (sizeof(buf)-1));
	if (copy_from_user(buf, userbuf, count))
		return -EFAULT;
	buf[count] = 0;
	ret = kstrtoint(buf, 0, &write_val);
	if (ret != 0)
		return -EINVAL;
	pr_info("pts_enforce_pulldown: %d->%d\n",
		pts_enforce_pulldown, write_val);
	pts_enforce_pulldown = write_val;
	return count;
}

static ssize_t dump_reg_write(struct file *file, const char __user *userbuf,
			      size_t count, loff_t *ppos)
{
	return count;
}

static int dump_reg_show(struct seq_file *s, void *what)
{
	unsigned int  i = 0, count;
	u32 reg_addr, reg_val = 0;
	struct sr_info_s *sr;

	/* viu top regs */
	seq_puts(s, "\nviu top registers:\n");
	reg_addr = 0x1a04;
	count = 12;
	for (i = 0; i < count; i++) {
		reg_val = READ_VCBUS_REG(reg_addr);
		seq_printf(s, "[0x%x] = 0x%X\n",
			   reg_addr, reg_val);
		reg_addr += 1;
	}
	/* vpp registers begin from 0x1d00*/
	seq_puts(s, "vpp registers:\n");
	reg_addr = VPP_DUMMY_DATA + cur_dev->vpp_off;
	count = 256;
	for (i = 0; i < count; i++) {
		reg_val = READ_VCBUS_REG(reg_addr);
		seq_printf(s, "[0x%x] = 0x%X\n",
			   reg_addr, reg_val);
		reg_addr += 1;
	}
	/* vd1 afbc regs */
	seq_puts(s, "\nvd1 afbc registers:\n");
	reg_addr = vd_layer[0].vd_afbc_reg.afbc_enable;
	count = 32;
	for (i = 0; i < count; i++) {
		reg_val = READ_VCBUS_REG(reg_addr);
		seq_printf(s, "[0x%x] = 0x%X\n",
			   reg_addr, reg_val);
		reg_addr += 1;
	}
	/* vd2 afbc regs */
	seq_puts(s, "\nvd2 afbc registers:\n");
	reg_addr = vd_layer[1].vd_afbc_reg.afbc_enable;
	count = 32;
	for (i = 0; i < count; i++) {
		reg_val = READ_VCBUS_REG(reg_addr);
		seq_printf(s, "[0x%x] = 0x%X\n",
			   reg_addr, reg_val);
		reg_addr += 1;
	}
	/* vd1 mif */
	seq_puts(s, "\nvd1 mif registers:\n");
	reg_addr = vd_layer[0].vd_mif_reg.vd_if0_gen_reg;
	count = 32;
	for (i = 0; i < count; i++) {
		reg_val = READ_VCBUS_REG(reg_addr);
		seq_printf(s, "[0x%x] = 0x%X\n",
			   reg_addr, reg_val);
		reg_addr += 1;
	}
	/* vd2 mif */
	seq_puts(s, "\nvd2 mif registers:\n");
	reg_addr = vd_layer[1].vd_mif_reg.vd_if0_gen_reg;
	count = 32;
	for (i = 0; i < count; i++) {
		reg_val = READ_VCBUS_REG(reg_addr);
		seq_printf(s, "[0x%x] = 0x%X\n",
			   reg_addr, reg_val);
		reg_addr += 1;
	}

	/* vd1(0x3800) & vd2(0x3850) hdr */
	/* osd hdr (0x38a0) */
	seq_puts(s, "\nvd1(0x3800) & vd2(0x3850) hdr registers:\n");
	seq_puts(s, "osd hdr (0x38a0) registers:\n");
	reg_addr = 0x3800;
	count = 256;
	for (i = 0; i < count; i++) {
		reg_val = READ_VCBUS_REG(reg_addr);
		seq_printf(s, "[0x%x] = 0x%X\n",
			   reg_addr, reg_val);
		reg_addr += 1;
	}
	/* super scaler */
	sr = &sr_info;
	/* g12a ~ tm2: 0x3e00 */
	/* tm2 revb: 0x5000 */
	seq_puts(s, "\nsuper scaler 0 registers:\n");
	reg_addr = SRSHARP0_SHARP_HVSIZE + sr->sr_reg_offt;
	count = 128;
	for (i = 0; i < count; i++) {
		reg_val = READ_VCBUS_REG(reg_addr);
		seq_printf(s, "[0x%x] = 0x%X\n",
			   reg_addr, reg_val);
		reg_addr += 1;
	}
	/* tl1, tm2 : 0x3f00*/
	/* tm2 revb: 0x5200 */
	seq_puts(s, "\nsuper scaler 1 registers:\n");
	reg_addr = SRSHARP1_SHARP_HVSIZE + sr->sr_reg_offt2;
	count = 128;
	for (i = 0; i < count; i++) {
		reg_val = READ_VCBUS_REG(reg_addr);
		seq_printf(s, "[0x%x] = 0x%X\n",
			   reg_addr, reg_val);
		reg_addr += 1;
	}
	return 0;
}

static int dump_reg_open(struct inode *inode, struct file *file)
{
	return single_open(file, dump_reg_show, inode->i_private);
}

static const struct file_operations pts_pattern_enter_cnt_file_ops = {
	.open		= simple_open,
	.read		= pts_pattern_enter_cnt_read_file,
};

static const struct file_operations pts_pattern_exit_cnt_file_ops = {
	.open		= simple_open,
	.read		= pts_pattern_exit_cnt_read_file,
};

static const struct file_operations pts_log_enable_file_ops = {
	.open		= simple_open,
	.read		= pts_log_enable_read_file,
	.write		= pts_log_enable_write_file,
};

static const struct file_operations pts_enforce_pulldown_file_ops = {
	.open		= simple_open,
	.read		= pts_enforce_pulldown_read_file,
	.write		= pts_enforce_pulldown_write_file,
};
#endif

static const struct file_operations reg_dump_file_ops = {
	.open		= dump_reg_open,
	.read		= seq_read,
	.write		= dump_reg_write,
	.release	= single_release,
};

struct video_debugfs_files_s {
	const char *name;
	const umode_t mode;
	const struct file_operations *fops;
};

static struct video_debugfs_files_s video_debugfs_files[] = {
#if defined(PTS_LOGGING)
	{"pts_pattern_enter_cnt", S_IFREG | 0444,
		&pts_pattern_enter_cnt_file_ops
	},
	{"pts_pattern_exit_cnt", S_IFREG | 0444,
		&pts_pattern_exit_cnt_file_ops
	},
	{"pts_log_enable", S_IFREG | 0644,
		&pts_log_enable_file_ops
	},
	{"pts_enforce_pulldown", S_IFREG | 0644,
		&pts_enforce_pulldown_file_ops
	},
#endif
	{"reg_dump", S_IFREG | 0644,
		&reg_dump_file_ops
	},
};

static struct dentry *video_debugfs_root;
static void video_debugfs_init(void)
{
	struct dentry *ent;
	int i;

	if (video_debugfs_root)
		return;
	video_debugfs_root = debugfs_create_dir("video", NULL);
	if (!video_debugfs_root)
		pr_err("can't create video debugfs dir\n");

	for (i = 0; i < ARRAY_SIZE(video_debugfs_files); i++) {
		ent = debugfs_create_file(video_debugfs_files[i].name,
			video_debugfs_files[i].mode,
			video_debugfs_root, NULL,
			video_debugfs_files[i].fops);
		if (!ent)
			pr_info("debugfs create file %s failed\n",
				video_debugfs_files[i].name);
	}
}

static void video_debugfs_exit(void)
{
	debugfs_remove(video_debugfs_root);
}

#ifdef CONFIG_AMLOGIC_POST_PROCESS_MANAGER_PPSCALER
int video_scaler_notify(int flag)
{
	video_scaler_mode = flag;
	vd_layer[0].property_changed = true;
	return 0;
}

u32 amvideo_get_scaler_para(int *x, int *y, int *w, int *h, u32 *ratio)
{
	*x = content_left;
	*y = content_top;
	*w = content_w;
	*h = content_h;
	/* *ratio = 100; */
	return video_scaler_mode;
}

void amvideo_set_scaler_para(int x, int y, int w, int h, int flag)
{
	struct disp_info_s *layer = &glayer_info[0];

	mutex_lock(&video_module_mutex);
	if (w < 2)
		w = 0;
	if (h < 2)
		h = 0;
	if (flag) {
		if ((content_left != x) || (content_top != y)
		    || (content_w != w) || (content_h != h))
			scaler_pos_changed = 1;
		content_left = x;
		content_top = y;
		content_w = w;
		content_h = h;
	} else {
		layer->layer_left = x;
		layer->layer_top = y;
		layer->layer_width = w;
		layer->layer_height = h;
	}
	vd_layer[0].property_changed = true;
	mutex_unlock(&video_module_mutex);
}

u32 amvideo_get_scaler_mode(void)
{
	return video_scaler_mode;
}
#endif

bool to_notify_trick_wait;
/* display canvas */

#ifdef CONFIG_AMLOGIC_MEDIA_VSYNC_RDMA
static struct vframe_s *cur_rdma_buf;
/*
 *void vsync_rdma_config(void);
 *void vsync_rdma_config_pre(void);
 *bool is_vsync_rdma_enable(void);
 *void start_rdma(void);
 *void enable_rdma_log(int flag);
 */
static int enable_rdma_log_count;

bool rdma_enable_pre;

static struct vframe_s *pip_rdma_buf;
static struct vframe_s *pipbuf_to_put[DISPBUF_TO_PUT_MAX];
static s8 pipbuf_to_put_num;
static struct vframe_s *dispbuf_to_put[DISPBUF_TO_PUT_MAX];
static s8 dispbuf_to_put_num;
#endif
/* 0: amvideo, 1: pip */
/* rdma buf + dispbuf + to_put_buf */
static struct vframe_s *recycle_buf[2][1 + DISPBUF_TO_PUT_MAX];
static s32 recycle_cnt[2];

static u32 post_canvas;

static u32 blackout = 1;
u32 force_blackout;
static u32 blackout_pip;

/* disable video */

static u32 pip_frame_count;
static u32 pip_loop;

static u32 hold_video;

/* show first frame*/
static bool show_first_frame_nosync;
bool show_first_picture;
/* static bool first_frame=false; */

/* test screen*/
static u32 test_screen;
/* rgb screen*/
static u32 rgb_screen;

/* video frame repeat count */
static u32 frame_repeat_count;

/* vout */
static const struct vinfo_s *vinfo;

/* config */
struct vframe_s *cur_dispbuf;
static struct vframe_s *cur_dispbuf2;
bool need_disable_vd2;
static bool last_mvc_status;

struct vframe_s vf_local, vf_local2, vf_local_ext;
static u32 vsync_pts_inc;
static u32 vsync_pts_inc_scale;
static u32 vsync_pts_inc_scale_base = 1;
static u32 vsync_pts_inc_upint;
static u32 vsync_pts_inc_adj;
static u32 vsync_pts_125;
static u32 vsync_pts_112;
static u32 vsync_pts_101;
static u32 vsync_pts_100;
static u32 vsync_freerun;
/* extend this value to support both slow and fast playback
 * 0,1: normal playback
 * [2,1000]: speed/vsync_slow_factor
 * >1000: speed*(vsync_slow_factor/1000000)
 */
static u32 vsync_slow_factor = 1;
static int video_delay_val;

/* pts alignment */
static bool vsync_pts_aligned;
static s32 vsync_pts_align;

/* frame rate calculate */
static u32 last_frame_count;
static u32 frame_count;
static u32 new_frame_count;
static u32 first_frame_toggled;
static u32 toggle_count;
static u32 last_frame_time;
static u32 timer_count;
static u32 vsync_count;
static u64 last_frame_duration;

static struct vpp_frame_par_s *cur_frame_par;

/* is fffb or seeking*/
static u32 video_seek_flag;

#ifdef FIQ_VSYNC
static bridge_item_t vsync_fiq_bridge;
#endif

/* trickmode i frame*/
u32 trickmode_i;
EXPORT_SYMBOL(trickmode_i);

/* trickmode ff/fb */
u32 trickmode_fffb;
atomic_t trickmode_framedone = ATOMIC_INIT(0);
atomic_t video_prop_change = ATOMIC_INIT(0);
atomic_t status_changed = ATOMIC_INIT(0);
atomic_t axis_changed = ATOMIC_INIT(0);
atomic_t video_unreg_flag = ATOMIC_INIT(0);
atomic_t video_inirq_flag = ATOMIC_INIT(0);
atomic_t video_pause_flag = ATOMIC_INIT(0);
int trickmode_duration;
int trickmode_duration_count;
u32 trickmode_vpts;
/* last_playback_filename */
char file_name[512];

/* video freerun mode */
#define FREERUN_NONE    0	/* no freerun mode */
#define FREERUN_NODUR   1	/* freerun without duration */
#define FREERUN_DUR     2	/* freerun with duration */
static u32 freerun_mode;
static u32 slowsync_repeat_enable;
static bool dmc_adjust = true;
module_param_named(dmc_adjust, dmc_adjust, bool, 0644);
static u32 dmc_config_state;
static u32 last_toggle_count;
static u32 toggle_same_count;
static int hdmin_delay_start;
static int hdmin_delay_start_time;
static int hdmin_delay_duration;
static int hdmin_delay_max_ms = 128;
static int hdmin_delay_done = true;
static int hdmin_need_drop_count;
static int vframe_walk_delay;
static int last_required_total_delay;
static int hdmi_vframe_count;
static bool hdmi_delay_first_check;
static bool hdmi_delay_normal_check;
static u32  hdmin_delay_count_debug;
static bool enable_hdmi_delay_normal_check;/*default is false*/

#define HDMI_DELAY_FIRST_CHECK_COUNT 60
#define HDMI_DELAY_NORMAL_CHECK_COUNT 300
#define HDMI_VIDEO_MIN_DELAY 3
/* video_inuse */
u32 video_inuse;
EXPORT_SYMBOL(video_inuse);

void set_freerun_mode(int mode)
{
	freerun_mode = mode;
}
EXPORT_SYMBOL(set_freerun_mode);

void set_pts_realign(void)
{
	vsync_pts_aligned = false;
}
EXPORT_SYMBOL(set_pts_realign);

u32 get_first_pic_coming(void)
{
	return first_frame_toggled;
}
EXPORT_SYMBOL(get_first_pic_coming);

u32 get_toggle_frame_count(void)
{
	return new_frame_count;
}
EXPORT_SYMBOL(get_toggle_frame_count);

void set_video_peek(void)
{
	videopeek = true;
}
EXPORT_SYMBOL(set_video_peek);

u32 get_first_frame_toggled(void)
{
	return first_frame_toggled;
}
EXPORT_SYMBOL(get_first_frame_toggled);

/* wait queue for poll */
static wait_queue_head_t amvideo_trick_wait;

/* wait queue for poll */
static wait_queue_head_t amvideo_prop_change_wait;

static u32 vpts_ref;
static u32 video_frame_repeat_count;
static u32 smooth_sync_enable;
static u32 hdmi_in_onvideo;

/* vpp_crc */
static u32 vpp_crc_en;
static int vpp_crc_result;

/* viu2 vpp_crc */
static u32 vpp_crc_viu2_en;

/* source fmt string */
const char *src_fmt_str[] = {
	"SDR", "HDR10", "HDR10+", "HDR Prime", "HLG",
	"Dolby Vison", "Dolby Vison Low latency", "MVC"
};

atomic_t primary_src_fmt =
	ATOMIC_INIT(VFRAME_SIGNAL_FMT_INVALID);
atomic_t cur_primary_src_fmt =
	ATOMIC_INIT(VFRAME_SIGNAL_FMT_INVALID);

u32 video_prop_status;

static u32 force_switch_vf_mode;

#define CONFIG_AM_VOUT

static s32 is_afbc_for_vpp(u8 id)
{
	s32 ret = -1;
	u32 val;

	if ((id >= MAX_VD_LAYERS)
		|| legacy_vpp)
		return ret;

	if (id == 0)
		val = READ_VCBUS_REG(
			VD1_AFBCD0_MISC_CTRL);
	else
		val = READ_VCBUS_REG(
			VD2_AFBCD1_MISC_CTRL);

	if ((val & (1 << 10)) && (val & (1 << 12))
		&& !(val & (1 << 9)))
		ret = 1;
	else
		ret = 0;
	return ret;
}

s32 di_request_afbc_hw(u8 id, bool on)
{
	u32 cur_afbc_request;
	u32 next_request = 0;
	s32 ret = -1;

	if (id >= MAX_VD_LAYERS)
		return ret;

	if (!glayer_info[id].afbc_support || legacy_vpp)
		return ret;

	next_request = 1 << id;
	cur_afbc_request = atomic_read(&gafbc_request);
	if (on) {
		if (cur_afbc_request & next_request)
			return is_afbc_for_vpp(id);

		atomic_add(next_request, &gafbc_request);
		ret = 1;
	} else {
		if ((cur_afbc_request & next_request) == 0)
			return is_afbc_for_vpp(id);

		atomic_sub(next_request, &gafbc_request);
		ret = 1;
	}
	vd_layer[id].property_changed = true;
	return ret;
}
EXPORT_SYMBOL(di_request_afbc_hw);

bool is_enable_3d_to_2d(void) {
	return (process_3d_type & MODE_3D_ENABLE) &&
		(process_3d_type & MODE_3D_TO_2D_MASK);
}

/*********************************************************/
static inline struct vframe_s *pip_vf_peek(void)
{
	if (pip_loop && (cur_dispbuf != cur_pipbuf))
		return cur_dispbuf;
	return vf_peek(RECEIVERPIP_NAME);
}

static inline struct vframe_s *pip_vf_get(void)
{
	struct vframe_s *vf = NULL;

	if (pip_loop && (cur_dispbuf != cur_pipbuf))
		return cur_dispbuf;

	vf = vf_get(RECEIVERPIP_NAME);

	if (vf) {
		get_count_pip++;
		if (vf->type & VIDTYPE_V4L_EOS) {
			vf_put(vf, RECEIVERPIP_NAME);
			return NULL;
		}
		/* video_notify_flag |= VIDEO_NOTIFY_PROVIDER_GET; */
	}
	return vf;

}

#if 0
static int pip_vf_get_states(struct vframe_states *states)
{
	int ret = -1;
	unsigned long flags;

	spin_lock_irqsave(&lock, flags);
	ret = vf_get_states_by_name(RECEIVERPIP_NAME, states);
	spin_unlock_irqrestore(&lock, flags);
	return ret;
}
#endif

static inline int pip_vf_put(struct vframe_s *vf)
{
	struct vframe_provider_s *vfp = vf_get_provider(RECEIVERPIP_NAME);

	if (pip_loop)
		return 0;

	if (vfp && vf) {
		if (vf_put(vf, RECEIVERPIP_NAME) < 0)
			return -EFAULT;
#ifdef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
		if ((glayer_info[0].display_path_id
			== VFM_PATH_PIP) &&
			is_dolby_vision_enable())
			dolby_vision_vf_put(vf);
#endif
		/* video_notify_flag |= VIDEO_NOTIFY_PROVIDER_PUT; */
	} else {
		if (vf)
			return -EINVAL;
	}
	return 0;
}

static inline struct vframe_s *video_vf_peek(void)
{
	int ret = 0;
	struct vframe_s *vf = vf_peek(RECEIVER_NAME);

	if (hist_test_flag) {
		if (cur_dispbuf != &hist_test_vf)
			vf = &hist_test_vf;
		else
			vf = NULL;
	}

	if (vf && vf->disp_pts && vf->disp_pts_us64) {
		vf->pts = vf->disp_pts;
		vf->pts_us64 = vf->disp_pts_us64;
		vf->disp_pts = 0;
		vf->disp_pts_us64 = 0;
	}

	if (vf && vf->fence) {
		/*
		 * the ret of fence status.
		 * 0: has not been signaled.
		 * 1: signaled without err.
		 * other: fence err.
		 */
		ret = fence_get_status(vf->fence);
		if (ret < 0) {
			vf = vf_get(RECEIVER_NAME);
			if (vf)
				vf_put(vf, RECEIVER_NAME);
		} else if (ret == 0) {
			vf = NULL;
		}
	}

	return vf;
}

static inline struct vframe_s *video_vf_get(void)
{
	struct vframe_s *vf = NULL;
	int frame_width, frame_height;

	if (hist_test_flag) {
		if (cur_dispbuf != &hist_test_vf)
			vf = &hist_test_vf;
		return vf;
	}

	vf = vf_get(RECEIVER_NAME);
	if (vf) {
		get_count++;
		if (vf->type & VIDTYPE_V4L_EOS) {
			vf_put(vf, RECEIVER_NAME);
			return NULL;
		}
		if (vf->type & VIDTYPE_DI_PW)
			get_di_count++;
		if (vf->disp_pts && vf->disp_pts_us64) {
			vf->pts = vf->disp_pts;
			vf->pts_us64 = vf->disp_pts_us64;
			vf->disp_pts = 0;
			vf->disp_pts_us64 = 0;
		}
		if (vf->type & VIDTYPE_COMPRESS) {
			frame_width = vf->compWidth;
			frame_height = vf->compHeight;
		} else {
			frame_width = vf->width;
			frame_height = vf->height;
		}
		video_notify_flag |= VIDEO_NOTIFY_PROVIDER_GET;
		if ((vf->type & VIDTYPE_MVC) && (framepacking_support)
		&&(framepacking_width) && (framepacking_height)) {
			vf->width = framepacking_width;
			vf->height = framepacking_height;
		}
#ifdef TV_3D_FUNCTION_OPEN
		/*can be moved to h264mvc.c */
		if ((vf->type & VIDTYPE_MVC)
		    && (process_3d_type & MODE_3D_ENABLE) && vf->trans_fmt) {
			process_3d_type |= MODE_3D_MVC;
			mvc_flag = 1;
		} else {
			process_3d_type &= (~MODE_3D_MVC);
			mvc_flag = 0;
		}
		if (((process_3d_type & MODE_FORCE_3D_TO_2D_LR)
		|| (process_3d_type & MODE_FORCE_3D_LR)
		|| (process_3d_type & MODE_FORCE_3D_FA_LR))
		&& (!(vf->type & VIDTYPE_MVC))
		&& (vf->trans_fmt != TVIN_TFMT_3D_FP)) {
			vf->trans_fmt = TVIN_TFMT_3D_DET_LR;
			vf->left_eye.start_x = 0;
			vf->left_eye.start_y = 0;
			vf->left_eye.width = frame_width / 2;
			vf->left_eye.height = frame_height;

			vf->right_eye.start_x = frame_width / 2;
			vf->right_eye.start_y = 0;
			vf->right_eye.width = frame_width / 2;
			vf->right_eye.height = frame_height;
		}
		if (((process_3d_type & MODE_FORCE_3D_TO_2D_TB)
		|| (process_3d_type & MODE_FORCE_3D_TB)
		|| (process_3d_type & MODE_FORCE_3D_FA_TB))
		&& (!(vf->type & VIDTYPE_MVC))
		&& (vf->trans_fmt != TVIN_TFMT_3D_FP)) {
			vf->trans_fmt = TVIN_TFMT_3D_TB;
			vf->left_eye.start_x = 0;
			vf->left_eye.start_y = 0;
			vf->left_eye.width = frame_width;
			vf->left_eye.height = frame_height/2;

			vf->right_eye.start_x = 0;
			vf->right_eye.start_y = frame_height/2;
			vf->right_eye.width = frame_width;
			vf->right_eye.height = frame_height/2;
		}
		receive_frame_count++;
#endif
	}

	return vf;
}

static int video_vf_get_states(struct vframe_states *states)
{
	int ret = -1;
	unsigned long flags;

	spin_lock_irqsave(&lock, flags);
	ret = vf_get_states_by_name(RECEIVER_NAME, states);
	spin_unlock_irqrestore(&lock, flags);
	return ret;
}

static inline int video_vf_put(struct vframe_s *vf)
{
	if (vf == &hist_test_vf)
		return 0;

	if (!vf)
		return 0;

	if (vf_put(vf, RECEIVER_NAME) < 0)
		return -EFAULT;
	if (vf->type & VIDTYPE_DI_PW)
		put_di_count++;
#ifdef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
	if (is_dolby_vision_enable())
		dolby_vision_vf_put(vf);
#endif
	video_notify_flag |= VIDEO_NOTIFY_PROVIDER_PUT;

	return 0;
}

static bool check_dispbuf(struct vframe_s *vf, bool is_put_err)
{
	int i;
	bool done = false;

	if (!vf)
		return true;
	for (i = 0; i < DISPBUF_TO_PUT_MAX; i++) {
		if (!is_put_err ||
		    (is_put_err && (vf->type & VIDTYPE_DI_PW))) {
			if (!dispbuf_to_put[i]) {
				dispbuf_to_put[i] = vf;
				dispbuf_to_put_num++;
				done = true;
				break;
			}
		}
	}
	return done;
}

static void dump_vframe_status(const char *name)
{
	int ret = -1;
	struct vframe_states states;
	struct vframe_provider_s *vfp;

	vfp = vf_get_provider_by_name(name);
	if (vfp && vfp->ops && vfp->ops->vf_states)
		ret = vfp->ops->vf_states(&states, vfp->op_arg);

	if (ret == 0) {
		ret += pr_info("%s_pool_size=%d\n",
			name, states.vf_pool_size);
		ret += pr_info("%s buf_free_num=%d\n",
			name, states.buf_free_num);
		ret += pr_info("%s buf_avail_num=%d\n",
			name, states.buf_avail_num);
	} else {
		ret += pr_info("%s vframe no states\n", name);
	}
}

static void dump_vdin_reg(void)
{
	unsigned int reg001, reg002;

	reg001 = READ_VCBUS_REG(0x1204);
	reg002 = READ_VCBUS_REG(0x1205);
	pr_info("VDIN_LCNT_STATUS:0x%x,VDIN_COM_STATUS0:0x%x\n",
		reg001, reg002);
}

#ifdef CONFIG_AMLOGIC_MEDIA_VIDEOCAPTURE
int ext_get_cur_video_frame(struct vframe_s **vf, int *canvas_index)
{
	struct video_layer_s *layer = &vd_layer[0];

	if (cur_dispbuf == NULL)
		return -1;
	*canvas_index = READ_VCBUS_REG(layer->vd_mif_reg.vd_if0_canvas0);
	*vf = cur_dispbuf;
	return 0;
}

int ext_put_video_frame(struct vframe_s *vf)
{
	return 0;
}

int ext_register_end_frame_callback(struct amvideocap_req *req)
{
	struct video_layer_s *layer = &vd_layer[0];
	enum video_capture_state capture_state = atomic_read(&layer->capture_use_cnt);

	if (capture_state == CAPTURE_STATE_OFF)
	{
		return -ENODATA;
	}
	else if (capture_state == CAPTURE_STATE_CAPTURE)
	{
		if (req && !req->callback) {
			atomic_set(&layer->capture_use_cnt, CAPTURE_STATE_ON);
			layer->capture_frame_req = NULL;
		}
		return -EBUSY;
	}
	else if (capture_state == CAPTURE_STATE_ON && req && req->callback)
	{
		layer->capture_frame_req = req;
		atomic_set(&layer->capture_use_cnt, CAPTURE_STATE_CAPTURE);
		return -EBUSY;
	}

	return 0;
}

int ext_frame_capture_poll(struct vframe_s *vf)
{
	int ret = -EAGAIN;
	static int capture_frame_toggle = 0;
	struct video_layer_s *layer = &vd_layer[0];

	if (capture_frame_toggle == 0 && vf) {
		if (vf->duration > 0 && (96000 / vf->duration) > 30)
			capture_frame_toggle = 1;
		if ((atomic_read(&layer->capture_use_cnt) == CAPTURE_STATE_CAPTURE) && layer->capture_frame_req) {
			struct amvideocap_req_data *reqdata =
				(struct amvideocap_req_data *)layer->capture_frame_req->data;
			int index = READ_VCBUS_REG(layer->vd_mif_reg.vd_if0_canvas0);

			if (layer->capture_frame_req && layer->capture_frame_req->callback && reqdata && reqdata->privdata)
				ret = layer->capture_frame_req->callback(reqdata->privdata, vf, index);

			layer->capture_frame_req = NULL;
			atomic_set(&layer->capture_use_cnt, CAPTURE_STATE_ON);
		}
	}
	else
		capture_frame_toggle = 0;

	return ret;
}
#endif

#ifdef TV_3D_FUNCTION_OPEN
/* judge the out mode is 240:LBRBLRBR  or 120:LRLRLR */
static void judge_3d_fa_out_mode(void)
{
	if ((process_3d_type & MODE_3D_OUT_FA_MASK) &&
	    pause_one_3d_fl_frame == 2) {
		toggle_3d_fa_frame = OUT_FA_B_FRAME;
	} else if ((process_3d_type & MODE_3D_OUT_FA_MASK) &&
		 pause_one_3d_fl_frame == 1) {
		toggle_3d_fa_frame = OUT_FA_A_FRAME;
	} else if ((process_3d_type & MODE_3D_OUT_FA_MASK) &&
		 pause_one_3d_fl_frame == 0) {
		/* toggle_3d_fa_frame  determine*/
		/*the out frame is L or R or blank */
		if ((process_3d_type & MODE_3D_OUT_FA_L_FIRST)) {
			if ((vsync_count % 2) == 0)
				toggle_3d_fa_frame = OUT_FA_A_FRAME;
			else
				toggle_3d_fa_frame = OUT_FA_B_FRAME;
		} else if ((process_3d_type & MODE_3D_OUT_FA_R_FIRST)) {
			if ((vsync_count % 2) == 0)
				toggle_3d_fa_frame = OUT_FA_B_FRAME;
			else
				toggle_3d_fa_frame = OUT_FA_A_FRAME;
		} else if ((process_3d_type & MODE_3D_OUT_FA_LB_FIRST)) {
			if ((vsync_count % 4) == 0)
				toggle_3d_fa_frame = OUT_FA_A_FRAME;
			else if ((vsync_count % 4) == 2)
				toggle_3d_fa_frame = OUT_FA_B_FRAME;
			else
				toggle_3d_fa_frame = OUT_FA_BANK_FRAME;
		} else if ((process_3d_type & MODE_3D_OUT_FA_RB_FIRST)) {
			if ((vsync_count % 4) == 0)
				toggle_3d_fa_frame = OUT_FA_B_FRAME;
			else if ((vsync_count % 4) == 2)
				toggle_3d_fa_frame = OUT_FA_A_FRAME;
			else
				toggle_3d_fa_frame = OUT_FA_BANK_FRAME;
		}
	} else {
		toggle_3d_fa_frame = OUT_FA_A_FRAME;
	}
}
#endif

#ifdef PTS_LOGGING
static void log_vsync_video_pattern(int pattern)
{
	int factor1 = 0, factor2 = 0, pattern_range = 0;

	if (pattern >= PTS_MAX_NUM_PATTERNS)
		return;

	if (pattern == PTS_32_PATTERN) {
		factor1 = 3;
		factor2 = 2;
		pattern_range =  PTS_32_PATTERN_DETECT_RANGE;
	} else if (pattern == PTS_22_PATTERN) {
		factor1 = 2;
		factor2 = 2;
		pattern_range =  PTS_22_PATTERN_DETECT_RANGE;
	} else if (pattern == PTS_41_PATTERN) {
		/* update 2111 mode detection */
		if (pts_trace == 2) {
			if ((pts_41_pattern_sink[1] == 1) &&
			    (pts_41_pattern_sink[2] == 1) &&
			    (pts_41_pattern_sink[3] == 1) &&
			    (pts_pattern[PTS_41_PATTERN] <
			     PTS_41_PATTERN_DETECT_RANGE)) {
				pts_pattern[PTS_41_PATTERN]++;
				if (pts_pattern[PTS_41_PATTERN] ==
					PTS_41_PATTERN_DETECT_RANGE) {
					pts_pattern_enter_cnt[PTS_41_PATTERN]++;
					pts_pattern_detected = pattern;
					if (pts_log_enable[PTS_41_PATTERN])
						pr_info("video 4:1 mode detected\n");
				}
			}
			pts_41_pattern_sink[0] = 2;
			pts_41_pattern_sink_index = 1;
		} else if (pts_trace == 1) {
			if ((pts_41_pattern_sink_index <
				PTS_41_PATTERN_SINK_MAX) &&
				(pts_41_pattern_sink_index > 0)) {
				pts_41_pattern_sink[pts_41_pattern_sink_index]
					= 1;
				pts_41_pattern_sink_index++;
			} else if (pts_pattern[PTS_41_PATTERN] ==
				PTS_41_PATTERN_DETECT_RANGE) {
				pts_pattern[PTS_41_PATTERN] = 0;
				pts_41_pattern_sink_index = 0;
				pts_pattern_exit_cnt[PTS_41_PATTERN]++;
				memset(&pts_41_pattern_sink[0], 0,
				       PTS_41_PATTERN_SINK_MAX);
				if (pts_log_enable[PTS_41_PATTERN])
					pr_info("video 4:1 mode broken\n");
			} else {
				pts_pattern[PTS_41_PATTERN] = 0;
				pts_41_pattern_sink_index = 0;
				memset(&pts_41_pattern_sink[0], 0,
				       PTS_41_PATTERN_SINK_MAX);
			}
		} else if (pts_pattern[PTS_41_PATTERN] ==
			PTS_41_PATTERN_DETECT_RANGE) {
			pts_pattern[PTS_41_PATTERN] = 0;
			pts_41_pattern_sink_index = 0;
			memset(&pts_41_pattern_sink[0], 0,
			       PTS_41_PATTERN_SINK_MAX);
			pts_pattern_exit_cnt[PTS_41_PATTERN]++;
			if (pts_log_enable[PTS_41_PATTERN])
				pr_info("video 4:1 mode broken\n");
		} else {
			pts_pattern[PTS_41_PATTERN] = 0;
			pts_41_pattern_sink_index = 0;
			memset(&pts_41_pattern_sink[0], 0,
			       PTS_41_PATTERN_SINK_MAX);
		}
		return;
	}

	/* update 3:2 or 2:2 mode detection */
	if (((pre_pts_trace == factor1) && (pts_trace == factor2)) ||
	    ((pre_pts_trace == factor2) && (pts_trace == factor1))) {
		if (pts_pattern[pattern] < pattern_range) {
			pts_pattern[pattern]++;
			if (pts_pattern[pattern] == pattern_range) {
				pts_pattern_enter_cnt[pattern]++;
				pts_pattern_detected = pattern;
				if (pts_log_enable[pattern])
					pr_info("video %d:%d mode detected\n",
						factor1, factor2);
			}
		}
	} else if (pts_pattern[pattern] == pattern_range) {
		pts_pattern[pattern] = 0;
		pts_pattern_exit_cnt[pattern]++;
		if (pts_log_enable[pattern])
			pr_info("video %d:%d mode broken\n", factor1, factor2);
	} else {
		pts_pattern[pattern] = 0;
	}
}

static void vsync_video_pattern(void)
{
	/* Check for 3:2*/
	log_vsync_video_pattern(PTS_32_PATTERN);
	/* Check for 2:2*/
	log_vsync_video_pattern(PTS_22_PATTERN);
	/* Check for 4:1*/
	log_vsync_video_pattern(PTS_41_PATTERN);
}
#endif

static bool check_pipbuf(struct vframe_s *vf, bool is_put_err)
{
	int i;
	bool done = false;

	if (!vf)
		return true;
	for (i = 0; i < DISPBUF_TO_PUT_MAX; i++) {
		if (!is_put_err ||
		    (is_put_err && (vf->type & VIDTYPE_DI_PW))) {
			if (!pipbuf_to_put[i]) {
				pipbuf_to_put[i] = vf;
				pipbuf_to_put_num++;
				done = true;
				break;
			}
		}
	}
	return done;
}

static struct vframe_s *pip_toggle_frame(struct vframe_s *vf)
{
	u32 first_picture = 0;

	if (!vf)
		return NULL;

	if (debug_flag & DEBUG_FLAG_PRINT_TOGGLE_FRAME)
		pr_info("%s(%p)\n", __func__, vf);

	if ((vf->width == 0) || (vf->height == 0)) {
		amlog_level(
			LOG_LEVEL_ERROR,
			"Video: invalid frame dimension\n");
		return vf;
	}
	if (cur_pipbuf &&
	    (cur_pipbuf != &local_pip) &&
	    (cur_pipbuf != vf)) {
#ifdef CONFIG_AMLOGIC_MEDIA_VSYNC_RDMA
		int i = 0;

		if (is_vsync_rdma_enable()) {
			if (pip_rdma_buf == cur_pipbuf) {
				if (!check_pipbuf(cur_pipbuf, false)) {
					if (pip_vf_put(cur_pipbuf) < 0)
						pr_info("put err, line: %d\n",
							__LINE__);
				}
			} else {
				if (pip_vf_put(cur_pipbuf) < 0) {
					if (!check_pipbuf(cur_pipbuf, true))
						pr_info("put err,pip full\n");
				}
			}
		} else {
			for (i = 0; i < pipbuf_to_put_num; i++) {
				if (pipbuf_to_put[i]) {
					if (!pip_vf_put(pipbuf_to_put[i])) {
						pipbuf_to_put[i] = NULL;
						pipbuf_to_put_num--;
					}
				}
			}
			if (pip_vf_put(cur_pipbuf) < 0)
				check_pipbuf(cur_pipbuf, true);
		}
#else
		if (pip_vf_put(cur_pipbuf) < 0)
			check_pipbuf(cur_pipbuf, true);
#endif
		pip_frame_count++;
		if (pip_frame_count == 1)
			first_picture = 1;
	} else if (!cur_pipbuf || (cur_pipbuf == &local_pip))
		first_picture = 1;

	if (cur_pipbuf != vf)
		vf->type_backup = vf->type;

	cur_pipbuf = vf;
	return cur_pipbuf;
}

#ifdef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
/* for sdr/hdr/single dv switch with dual dv */
u32 last_el_status;
bool has_enhanced_layer(struct vframe_s *vf)
{
	struct provider_aux_req_s req;
	enum vframe_signal_fmt_e fmt;

	if (is_dolby_vision_el_disable() &&
	    !for_dolby_vision_certification())
		return 0;

	if (!vf)
		return 0;

	if (vf->source_type != VFRAME_SOURCE_TYPE_OTHERS)
		return 0;

	if (!is_dolby_vision_on())
		return 0;

	fmt = get_vframe_src_fmt(vf);
	/* valid src_fmt = DOVI or invalid src_fmt will check dual layer */
	/* otherwise, it certainly is a non-dv vframe */
	if ((fmt != VFRAME_SIGNAL_FMT_DOVI) &&
	    (fmt != VFRAME_SIGNAL_FMT_INVALID))
		return 0;

	req.vf = vf;
	req.bot_flag = 0;
	req.aux_buf = NULL;
	req.aux_size = 0;
	req.dv_enhance_exist = 0;
	vf_notify_provider_by_name(
		"dvbldec",
		VFRAME_EVENT_RECEIVER_GET_AUX_DATA,
		(void *)&req);
	return req.dv_enhance_exist;
}
#endif

static bool has_receive_dummy_vframe(void)
{
	struct vframe_s *vf;
#ifdef CONFIG_AMLOGIC_MEDIA_VSYNC_RDMA
	int i;
#endif

	vf = video_vf_peek();

	if (vf && vf->flag & VFRAME_FLAG_EMPTY_FRAME_V4L) {
		/* get dummy vf. */
		vf = video_vf_get();

#ifdef CONFIG_AMLOGIC_MEDIA_VSYNC_RDMA /* recycle vframe. */
		for (i = 0; i < DISPBUF_TO_PUT_MAX; i++) {
			if (dispbuf_to_put[i]) {
				if (!video_vf_put(dispbuf_to_put[i])) {
					dispbuf_to_put[i] = NULL;
					dispbuf_to_put_num--;
				}
			}
		}
#endif
		/* recycle the last vframe. */
		if (cur_dispbuf && (cur_dispbuf != &vf_local)) {
			if (!video_vf_put(cur_dispbuf))
				cur_dispbuf = NULL;
		}

		/*pr_info("put dummy vframe.\n");*/
		if (video_vf_put(vf) < 0)
			check_dispbuf(vf, true);
		return true;
	}

	return false;
}

static u64 func_div(u64 number, u32 divid)
{
	u64 tmp = number;

	do_div(tmp, divid);
	return tmp;
}

static void display_latency_debug(struct vframe_s *vf)
{
	long long vsync_duration;
	long long delay_time;
	long long delay_time_1;
	u32 i;
	u32 index;
	struct timespec mon;
	long long current_time;

	if (!(debug_flag & DEBUG_FLAG_PRINT_DISPLAY_TIME))
		return;

	ktime_get_ts(&mon);
	current_time = mon.tv_sec;
	current_time = current_time * 1000 * 1000000;
	current_time = current_time + mon.tv_nsec;

	vsync_duration = vsync_pts_inc_scale;
	vsync_duration = vsync_duration * 1000000 * 1000;
	vsync_duration = func_div(vsync_duration,
				  vsync_pts_inc_scale_base);

	index = vf->omx_index % 20;
	time_info[index].toogle_index = vf->omx_index;
	time_info[index].display_time = current_time + vsync_duration;

	if (index > 9)
		i = index - 10;
	else
		i = index + 10;

	if (time_info[i].toogle_index == time_info[i].set_index) {
		delay_time = time_info[i].display_time - time_info[i].hwc_time;
		delay_time_1 = time_info[i].hwc_time - (vf->pts_us64 * 1000);
		pr_info("index =%d dis_hwc=%lld hwc_time-pts64=%lld\n",
			time_info[i].toogle_index, delay_time, delay_time_1);
		min_dis_hwc = delay_time;
		max_dis_hwc = delay_time;
	}

	pr_info("toogle: index=%d, pts=%lld, dis_time=%lld\n",
		vf->omx_index, vf->pts_us64, time_info[index].display_time);
}

static void update_process_hdmi_avsync_flag(bool flag)
{
	char *provider_name = vf_get_provider_name(RECEIVER_NAME);
	unsigned long flags;

	/*enable hdmi delay process only when audio have required*/
	if (last_required_total_delay <= 0)
		return;

	while (provider_name) {
		if (!vf_get_provider_name(provider_name))
			break;
		provider_name =
			vf_get_provider_name(provider_name);
	}
	if (provider_name && (!strcmp(provider_name, "dv_vdin") ||
		!strcmp(provider_name, "vdin0"))) {
		spin_lock_irqsave(&hdmi_avsync_lock, flags);
		hdmi_delay_first_check = flag;
		hdmi_vframe_count = 0;
		hdmin_delay_count_debug = 0;
		if (enable_hdmi_delay_normal_check)
			hdmi_delay_normal_check = flag;
		pr_info("update hdmi_delay_check %d\n", flag);
		spin_unlock_irqrestore(&hdmi_avsync_lock, flags);
	}
}

static inline bool is_valid_drop_count(int drop_count)
{
	if (drop_count > 0 && drop_count < 8)
		return true;
	return false;
}

static void process_hdmi_video_sync(struct vframe_s *vf)
{
	char *provider_name = vf_get_provider_name(RECEIVER_NAME);
	int update_value = 0;
	unsigned long flags;
	int vsync_dur = 16;
	int need_drop = 0;
	int hdmin_delay_min_ms = vsync_dur * HDMI_VIDEO_MIN_DELAY;

	if ((!hdmi_delay_first_check && !hdmi_delay_normal_check &&
	     hdmin_delay_start == 0) || !vf || last_required_total_delay <= 0)
		return;

	hdmin_delay_duration = 0;
	while (provider_name) {
		if (!vf_get_provider_name(provider_name))
			break;
		provider_name =
			vf_get_provider_name(provider_name);
	}
	if (provider_name && (!strcmp(provider_name, "dv_vdin") ||
		!strcmp(provider_name, "vdin0"))) {
		if (vf->duration > 0) {
			vsync_dur = (int)(vf->duration / 96);
			hdmin_delay_min_ms = vsync_dur *
				HDMI_VIDEO_MIN_DELAY;
		}
		spin_lock_irqsave(&hdmi_avsync_lock, flags);

		if (last_required_total_delay > vframe_walk_delay) {
			/*delay video*/
			vframe_walk_delay = (int)div_u64(((jiffies_64 -
			vf->ready_jiffies64) * 1000), HZ);
			/*check hdmi max delay*/
			if (last_required_total_delay > hdmin_delay_max_ms) {
				if (hdmin_delay_max_ms > vframe_walk_delay)
					update_value = hdmin_delay_max_ms -
					vframe_walk_delay;
			} else {
				update_value = last_required_total_delay -
					vframe_walk_delay;
			}
			/*set only if delay bigger than half vsync*/
			if (update_value > vsync_dur / 2) {
				hdmin_delay_duration = update_value;
				hdmin_delay_start_time = -1;
				hdmin_delay_count_debug++;
				hdmin_delay_done = false;
				hdmin_need_drop_count = 0;
			}
		} else { /*drop video*/
			/*check hdmi min delay*/
			if (last_required_total_delay >= hdmin_delay_min_ms)
				update_value = vframe_walk_delay -
					last_required_total_delay;
			else
				update_value = vframe_walk_delay -
					hdmin_delay_min_ms;

			/*drop only if diff bigger than half vsync*/
			if (update_value > vsync_dur / 2) {
				need_drop = update_value / vsync_dur;
				/*check if drop need_drop + 1 is closer to*/
				/*required than need_drop*/
				if ((update_value - need_drop * vsync_dur) >
					vsync_dur / 2) {
					if ((vframe_walk_delay -
						(need_drop + 1) * vsync_dur) >=
						hdmin_delay_min_ms)
						need_drop = need_drop + 1;
				}
				hdmin_delay_duration = -update_value;
				hdmin_delay_done = true;
				if (is_valid_drop_count(need_drop))
					hdmin_need_drop_count = need_drop;
			}
		}
		//if (debug_flag & DEBUG_FLAG_HDMI_AVSYNC_DEBUG)
			pr_info("required delay:%d, current %d, extra %d, delay %s, drop cnt %d\n",
				last_required_total_delay,
				vframe_walk_delay,
				hdmin_delay_duration,
				hdmin_delay_done ? "false" : "true",
				need_drop);
		spin_unlock_irqrestore(&hdmi_avsync_lock, flags);
	}
}

static struct vframe_s *vsync_toggle_frame(struct vframe_s *vf, int line)
{
	static u32 last_pts;
	u32 diff_pts;
	u32 first_picture = 0;
	long long *clk_array;
	static long long clock_vdin_last;
	static long long clock_last;

	ATRACE_COUNTER(__func__,  line);
	if (!vf)
		return NULL;
	ATRACE_COUNTER("vsync_toggle_frame_pts", vf->pts);

	display_latency_debug(vf);

	diff_pts = vf->pts - last_pts;
	if (last_pts && diff_pts < 90000)
		ATRACE_COUNTER("vsync_toggle_frame_inc", diff_pts);
	else
		ATRACE_COUNTER("vsync_toggle_frame_inc", 0);  /* discontinue */

	last_pts = vf->pts;

	frame_count++;
	toggle_count++;

#ifdef PTS_TRACE_DEBUG
#ifdef PTS_TRACE_START
		if (pts_trace_his_rd < 16) {
#endif
			pts_trace_his[pts_trace_his_rd] = pts_trace;
			pts_his[pts_trace_his_rd] = vf->pts;
			scr_his[pts_trace_his_rd] = timestamp_pcrscr_get();
			pts_trace_his_rd++;
			if (pts_trace_his_rd >= 16)
				pts_trace_his_rd = 0;
#ifdef PTS_TRACE_START
		}
#endif
#endif

#ifdef PTS_LOGGING
	if (pts_escape_vsync == 1) {
		pts_trace++;
		pts_escape_vsync = 0;
	}
	vsync_video_pattern();
	pre_pts_trace = pts_trace;
#endif

#if defined(PTS_LOGGING) || defined(PTS_TRACE_DEBUG)
	pts_trace = 0;
#endif

	if (debug_flag & DEBUG_FLAG_PRINT_TOGGLE_FRAME) {
		u32 pcr = timestamp_pcrscr_get();
		u32 vpts = timestamp_vpts_get();
		u32 apts = timestamp_apts_get();

		pr_info("%s pts:%d.%06d pcr:%d.%06d vpts:%d.%06d apts:%d.%06d\n",
			__func__, (vf->pts) / 90000,
			((vf->pts) % 90000) * 1000 / 90, (pcr) / 90000,
			((pcr) % 90000) * 1000 / 90, (vpts) / 90000,
			((vpts) % 90000) * 1000 / 90, (apts) / 90000,
			((apts) % 90000) * 1000 / 90);
	}

	if (trickmode_i || trickmode_fffb)
		trickmode_duration_count = trickmode_duration;

#ifdef OLD_DI
	if (vf->early_process_fun) {
		if (vf->early_process_fun(vf->private_data, vf) == 1)
			first_picture = 1;
	} else {
#ifdef CONFIG_AMLOGIC_MEDIA_DEINTERLACE
		if ((DI_POST_REG_RD(DI_IF1_GEN_REG) & 0x1) != 0) {
			/* check mif enable status, disable post di */
			VSYNC_WR_MPEG_REG(DI_POST_CTRL, 0x3 << 30);
			VSYNC_WR_MPEG_REG(DI_POST_SIZE,
					  (32 - 1) | ((128 - 1) << 16));
			VSYNC_WR_MPEG_REG(DI_IF1_GEN_REG,
					  READ_VCBUS_REG(DI_IF1_GEN_REG) &
					  0xfffffffe);
		}
#endif
	}
#endif

	timer_count = 0;
	if ((vf->width == 0) && (vf->height == 0)) {
		amlog_level(
			LOG_LEVEL_ERROR,
			"Video: invalid frame dimension\n");
		ATRACE_COUNTER(__func__,  __LINE__);
		return vf;
	}

	if (hold_video) {
		if (cur_dispbuf != vf) {
			u32 old_w, old_h;

			new_frame_count++;
			if (vf->pts != 0) {
				amlog_mask(
					LOG_MASK_TIMESTAMP,
					"vpts to: 0x%x, scr: 0x%x, abs_scr: 0x%x\n",
					vf->pts, timestamp_pcrscr_get(),
					READ_MPEG_REG(SCR_HIU));

				timestamp_vpts_set(vf->pts);
				timestamp_vpts_set_u64(vf->pts_us64);
				last_frame_duration = vf->duration;
			} else if (last_frame_duration) {
				amlog_mask(
					LOG_MASK_TIMESTAMP,
					"vpts inc: 0x%x, scr: 0x%x, abs_scr: 0x%x\n",
					timestamp_vpts_get() +
					DUR2PTS(cur_dispbuf->duration),
					timestamp_pcrscr_get(),
					READ_MPEG_REG(SCR_HIU));

				timestamp_vpts_inc(
					DUR2PTS(last_frame_duration));
				timestamp_vpts_inc_u64(
					DUR2PTS(last_frame_duration));

				vpts_remainder +=
					DUR2PTS_RM(last_frame_duration);
				if (vpts_remainder >= 0xf) {
					vpts_remainder -= 0xf;
					timestamp_vpts_inc(-1);
					timestamp_vpts_inc_u64(-1);
				}
			}

			old_w = cur_width;
			old_h = cur_height;
			if (vf->type & VIDTYPE_COMPRESS) {
				cur_width = vf->compWidth;
				cur_height = vf->compHeight;
			} else {
				cur_width = vf->width;
				cur_height = vf->height;
			}
			if ((old_w != cur_width) ||
			    (old_h != cur_height))
				video_prop_status |= VIDEO_PROP_CHANGE_SIZE;
			if (video_vf_put(vf) < 0)
				check_dispbuf(vf, true);
			ATRACE_COUNTER(__func__,  __LINE__);
			return NULL;
		}
	}

	if (cur_dispbuf != vf) {
		new_frame_count++;
		if (new_frame_count == 1)
			first_picture = 1;
	}

	if (cur_dispbuf &&
	    (cur_dispbuf != &vf_local) &&
	    (cur_dispbuf != vf)) {
#ifdef CONFIG_AMLOGIC_MEDIA_VSYNC_RDMA
		int i = 0, done = false;

		if (is_vsync_rdma_enable()) {
			if (cur_rdma_buf == cur_dispbuf) {
				done = check_dispbuf(cur_dispbuf, false);
				if (!done) {
					if (video_vf_put(cur_dispbuf) < 0)
						pr_info("put err,line: %d\n",
							__LINE__);
				}
			} else {
				if (video_vf_put(cur_dispbuf) < 0) {
					done = check_dispbuf(cur_dispbuf, true);
					if (!done)
						pr_info("put err,que full\n");
				}
			}
		} else {
			for (i = 0; i < DISPBUF_TO_PUT_MAX; i++) {
				if (dispbuf_to_put[i]) {
					if (!video_vf_put(dispbuf_to_put[i])) {
						dispbuf_to_put[i] = NULL;
						dispbuf_to_put_num--;
					}
				}
			}
			if (video_vf_put(cur_dispbuf) < 0)
				check_dispbuf(cur_dispbuf, true);
		}
#else
		if (video_vf_put(cur_dispbuf) < 0)
			check_dispbuf(cur_dispbuf, true);
#endif
		if (debug_flag & DEBUG_FLAG_LATENCY) {
			vf->ready_clock[3] = sched_clock();
			pr_info(
				"video toggle latency %lld us, diff %lld, get latency %lld us, vdin put latency %lld us, first %lld us, diff %lld.\n",
				func_div(vf->ready_clock[3], 1000),
				func_div(vf->ready_clock[3] - clock_last, 1000),
				func_div(vf->ready_clock[2], 1000),
				func_div(vf->ready_clock[1], 1000),
				func_div(vf->ready_clock[0], 1000),
				func_div(vf->ready_clock[0] -
				clock_vdin_last, 1000));

			clock_vdin_last = vf->ready_clock[0];
			clock_last = vf->ready_clock[3];
			cur_dispbuf->ready_clock[4] = sched_clock();
			clk_array = cur_dispbuf->ready_clock;
			pr_info("video put latency %lld us, video toggle latency %lld us, video get latency %lld us, vdin put latency %lld us, first %lld us.\n",
				func_div(*(clk_array + 4), 1000),
				func_div(*(clk_array + 3), 1000),
				func_div(*(clk_array + 2), 1000),
				func_div(*(clk_array + 1), 1000),
				func_div(*clk_array, 1000));
		}
	} else
		first_picture = 1;

	if ((debug_flag & DEBUG_FLAG_BASIC_INFO) && first_picture)
		pr_info("[vpp-kpi] first toggle picture {%d,%d} pts:%x\n",
			vf->width, vf->height, vf->pts);
	vframe_walk_delay = (int)div_u64(((jiffies_64 -
		vf->ready_jiffies64) * 1000), HZ);

	if (debug_flag & DEBUG_FLAG_HDMI_AVSYNC_DEBUG)
		pr_info("toggle vf %p, ready_jiffies64 %d, walk_delay %d\n",
			vf, jiffies_to_msecs(vf->ready_jiffies64),
			vframe_walk_delay);

	/* set video PTS */
	if (cur_dispbuf != vf) {
		if (vf->pts != 0) {
			amlog_mask(
				LOG_MASK_TIMESTAMP,
				"vpts to: 0x%x, scr: 0x%x, abs_scr: 0x%x\n",
				vf->pts, timestamp_pcrscr_get(),
				READ_MPEG_REG(SCR_HIU));
				timestamp_vpts_set(vf->pts);
				timestamp_vpts_set_u64(vf->pts_us64);
		} else if (cur_dispbuf) {
			amlog_mask(
				LOG_MASK_TIMESTAMP,
				"vpts inc: 0x%x, scr: 0x%x, abs_scr: 0x%x\n",
				timestamp_vpts_get() +
				DUR2PTS(cur_dispbuf->duration),
				timestamp_pcrscr_get(),
				READ_MPEG_REG(SCR_HIU));

			timestamp_vpts_inc(
				DUR2PTS(cur_dispbuf->duration));
			timestamp_vpts_inc_u64(
				DUR2PTS(cur_dispbuf->duration));

			vpts_remainder +=
				DUR2PTS_RM(cur_dispbuf->duration);
			if (vpts_remainder >= 0xf) {
				vpts_remainder -= 0xf;
				timestamp_vpts_inc(-1);
				timestamp_vpts_inc_u64(-1);
			}
		}
		vf->type_backup = vf->type;
	}

	cur_dispbuf = vf;

	if (cur_dispbuf && omx_secret_mode)
		cur_disp_omx_index = cur_dispbuf->omx_index;

#ifdef CONFIG_AMLOGIC_MEDIA_VIDEOCAPTURE
	ext_frame_capture_poll(cur_dispbuf);
#endif

	if (first_picture) {
		first_frame_toggled = 1;
#ifdef VIDEO_PTS_CHASE
		av_sync_flag = 0;
#endif
	}

	if ((vf != &vf_local) && (vf) && !vsync_pts_aligned) {
#ifdef PTS_TRACE_DEBUG
		pr_info("####timestamp_pcrscr_get() = 0x%x, vf->pts = 0x%x, vsync_pts_inc = %d\n",
			timestamp_pcrscr_get(), vf->pts, vsync_pts_inc);
#endif
		if ((abs(timestamp_pcrscr_get() - vf->pts) <= vsync_pts_inc) &&
		    ((int)(timestamp_pcrscr_get() - vf->pts) >= 0)) {
			vsync_pts_align =  vsync_pts_inc / 4 -
				(timestamp_pcrscr_get() - vf->pts);
			vsync_pts_aligned = true;
#ifdef PTS_TRACE_DEBUG
			pts_trace_his_rd = 0;
			pr_info("####vsync_pts_align set to %d\n",
				vsync_pts_align);
#endif
		}
	}
	ATRACE_COUNTER(__func__,  0);
	return cur_dispbuf;
}

#ifdef INTERLACE_FIELD_MATCH_PROCESS
static inline bool interlace_field_type_need_match(int vout_type,
						   struct vframe_s *vf)
{
	if (DUR2PTS(vf->duration) != vsync_pts_inc)
		return false;

	if ((vout_type == VOUT_TYPE_TOP_FIELD) &&
	    ((vf->type & VIDTYPE_TYPEMASK) == VIDTYPE_INTERLACE_BOTTOM))
		return true;
	else if ((vout_type == VOUT_TYPE_BOT_FIELD) &&
		 ((vf->type & VIDTYPE_TYPEMASK) == VIDTYPE_INTERLACE_TOP))
		return true;

	return false;
}
#endif

/* add a new function to check if current display frame has been*/
/*displayed for its duration */
static inline bool duration_expire(struct vframe_s *cur_vf,
				   struct vframe_s *next_vf, u32 dur)
{
	u32 pts;
	s32 dur_disp;
	static s32 rpt_tab_idx;
	static const u32 rpt_tab[4] = { 0x100, 0x100, 0x300, 0x300 };

	/* do not switch to new frames in none-normal speed */
	if (vsync_slow_factor > 1000)
		return false;

	if ((cur_vf == NULL) || (cur_dispbuf == &vf_local))
		return true;

	pts = next_vf->pts;
	if (pts == 0)
		dur_disp = DUR2PTS(cur_vf->duration);
	else
		dur_disp = pts - timestamp_vpts_get();

	if ((dur << 8) >= (dur_disp * rpt_tab[rpt_tab_idx & 3])) {
		rpt_tab_idx = (rpt_tab_idx + 1) & 3;
		return true;
	} else
		return false;
}

#define VPTS_RESET_THRO

#ifdef PTS_LOGGING
static inline void vpts_perform_pulldown(struct vframe_s *next_vf,
					bool *expired)
{
	int pattern_range, expected_curr_interval;
	int expected_prev_interval;
	int next_vf_nextpts = 0;

	/* Dont do anything if we have invalid data */
	if (!next_vf || !next_vf->pts)
		return;
	if (next_vf->next_vf_pts_valid)
		next_vf_nextpts = next_vf->next_vf_pts;

	switch (pts_pattern_detected) {
	case PTS_32_PATTERN:
		pattern_range = PTS_32_PATTERN_DETECT_RANGE;
		switch (pre_pts_trace) {
		case 3:
			expected_prev_interval = 3;
			expected_curr_interval = 2;
			break;
		case 2:
			expected_prev_interval = 2;
			expected_curr_interval = 3;
			break;
		default:
			return;
		}
		if (!next_vf_nextpts)
			next_vf_nextpts = next_vf->pts +
				PTS_32_PATTERN_DURATION;
		break;
	case PTS_22_PATTERN:
		if (pre_pts_trace != 2)
			return;
		pattern_range =  PTS_22_PATTERN_DETECT_RANGE;
		expected_prev_interval = 2;
		expected_curr_interval = 2;
		if (!next_vf_nextpts)
			next_vf_nextpts = next_vf->pts +
				PTS_22_PATTERN_DURATION;
		break;
	case PTS_41_PATTERN:
		/* TODO */
	default:
		return;
	}

	/* We do nothing if  we dont have enough data*/
	if (pts_pattern[pts_pattern_detected] != pattern_range)
		return;

	if (*expired) {
		if (pts_trace < expected_curr_interval) {
			/* 2323232323..2233..2323, prev=2, curr=3,*/
			/* check if next frame will toggle after 3 vsyncs */
			/* 22222...22222 -> 222..2213(2)22...22 */
			/* check if next frame will toggle after 3 vsyncs */
			int nextPts = timestamp_pcrscr_get() + vsync_pts_align;

			if (/*((int)(nextPts + expected_prev_interval * */
			/*vsync_pts_inc - next_vf->next_vf_pts) < 0) && */
				((int)(nextPts + (expected_prev_interval + 1) *
				vsync_pts_inc - next_vf_nextpts) >= 0)) {
				*expired = false;
				if (pts_log_enable[PTS_32_PATTERN]
					|| pts_log_enable[PTS_22_PATTERN])
					pr_info("hold frame for pattern: %d",
						pts_pattern_detected);
			}

			/* here need to escape a vsync */
			if (timestamp_pcrscr_get() >
				(next_vf->pts + vsync_pts_inc)) {
				*expired = true;
				pts_escape_vsync = 1;
				if (pts_log_enable[PTS_32_PATTERN]
					|| pts_log_enable[PTS_22_PATTERN])
					pr_info("escape a vsync pattern: %d",
						pts_pattern_detected);
			}
		}
	} else {
		if (pts_trace == expected_curr_interval) {
			/* 23232323..233223...2323 curr=2, prev=3 */
			/* check if this frame will expire next vsyncs and */
			/* next frame will expire after 3 vsyncs */
			/* 22222...22222 -> 222..223122...22 */
			/* check if this frame will expire next vsyncs and */
			/* next frame will expire after 2 vsyncs */
			int nextPts = timestamp_pcrscr_get() + vsync_pts_align;

			if (((int)(nextPts + vsync_pts_inc - next_vf->pts)
				>= 0) &&
			    ((int)(nextPts +
			    vsync_pts_inc * (expected_prev_interval - 1)
			    - next_vf_nextpts) < 0) &&
			    ((int)(nextPts + expected_prev_interval *
				vsync_pts_inc - next_vf_nextpts) >= 0)) {
				*expired = true;
				if (pts_log_enable[PTS_32_PATTERN]
					|| pts_log_enable[PTS_22_PATTERN])
					pr_info("pull frame for pattern: %d",
						pts_pattern_detected);
			}
		}
	}
}
#endif

static inline bool vpts_expire(struct vframe_s *cur_vf,
			       struct vframe_s *next_vf,
			       int toggled_cnt)
{
	u32 pts;
#ifdef VIDEO_PTS_CHASE
	u32 vid_pts, scr_pts;
#endif
	u32 systime;
	u32 adjust_pts, org_vpts;
	bool expired;
	u64 pts_u64;

	if (next_vf == NULL)
		return false;

	if (videopeek) {
		videopeek = false;
		pr_info("video peek toogle the first frame\n");
		return true;
	}

	if (debug_flag & DEBUG_FLAG_TOGGLE_FRAME_PER_VSYNC)
		return true;
	if (/*(cur_vf == NULL) || (cur_dispbuf == &vf_local) ||*/ debugflags &
	    DEBUG_FLAG_FFPLAY)
		return true;

	if ((freerun_mode == FREERUN_NODUR) || hdmi_in_onvideo)
		return true;
	/*freerun for game mode*/
	if (next_vf->flag & VFRAME_FLAG_GAME_MODE)
		return true;

	/*freerun for drm video*/
	if (next_vf->flag & VFRAME_FLAG_VIDEO_DRM)
		return true;

	/* FIXME: remove it */
	if (next_vf->flag & VFRAME_FLAG_VIDEO_COMPOSER)
		return true;

	if (step_enable) {
		if (step_flag)
			return false;
		if (!step_flag) {
			step_flag = 1;
			return true;
		}
	}

	if ((trickmode_i == 1) || ((trickmode_fffb == 1))) {
		if (((atomic_read(&trickmode_framedone) == 0)
		     || (trickmode_i == 1)) && (!to_notify_trick_wait)
		    && (trickmode_duration_count <= 0)) {
#if 0
			if (cur_vf)
				pts = timestamp_vpts_get() +
				trickmode_duration;
			else
				return true;
#else
			return true;
#endif
		} else
			return false;
	}
	if (omx_secret_mode && (!omx_run || !omx_drop_done))
		return false;

	if (next_vf->duration == 0)

		return true;

	systime = timestamp_pcrscr_get();
	pts = next_vf->pts;
	pts_u64 = next_vf->pts_us64;

#ifdef AVSYNC_COUNT
	if (abs(timestamp_apts_get() - timestamp_vpts_get()) > 90 * 500)
		av_discontinue = true;
	if (abs(timestamp_apts_get() - timestamp_vpts_get()) <= 90 * 500 &&
	    av_discontinue) {
		avsync_count++;
		av_discontinue = false;
		timestamp_avsync_counter_set(avsync_count);
	}
#endif
	if (((pts == 0) && ((cur_dispbuf && (cur_dispbuf != &vf_local))
		|| (hold_property_changed == 1)))
	    || (freerun_mode == FREERUN_DUR)) {
		pts =
		    timestamp_vpts_get() +
		    (cur_vf ? DUR2PTS(cur_vf->duration) : 0);

		pts_u64 = timestamp_vpts_get_u64() +
			(cur_vf ? DUR2PTS(cur_vf->duration) : 0);
		if (hold_property_changed == 1)
			hold_property_changed = 0;
	}
	/* check video PTS discontinuity */
	if ((enable_video_discontinue_report) &&
	    (first_frame_toggled) &&
	    (AM_ABSSUB(systime, pts) > tsync_vpts_discontinuity_margin()) &&
	    ((next_vf->flag & VFRAME_FLAG_NO_DISCONTINUE) == 0)) {
		/*
		 * if paused ignore discontinue
		 */
		if (!timestamp_pcrscr_enable_state() &&
			tsync_get_mode() != TSYNC_MODE_PCRMASTER) {
			/*pr_info("video pts discontinue,
			 * but pcrscr is disabled,
			 * return false\n");
			 */
			return false;
		}
		pts =
		    timestamp_vpts_get() +
		    (cur_vf ? DUR2PTS(cur_vf->duration) : 0);
		if (debug_flag & DEBUG_FLAG_OMX_DEBUG_DROP_FRAME) {
			pr_info("system=0x%x ,dur:%d,pts:0x%x,align:%d\n",
				systime,
				DUR2PTS(cur_vf->duration), pts,
				vsync_pts_align);
		}
		/* pr_info("system=0x%x vpts=0x%x\n", systime,*/
		/*timestamp_vpts_get()); */
		/* [SWPL-21116] If pts and systime diff is smaller than
		 * vsync_pts_align, will return true at end of this
		 * function and show this frame. In this bug, next_vf->pts
		 * is not 0 and have large diff with systime, need get into
		 * discontinue process and shouldn't send out this frame.
		 */
		if (((int)(systime - pts) >= 0) ||
			((next_vf->pts > 0) &&
			((int)(systime + vsync_pts_align - pts) >= 0))) {
			if (next_vf->pts != 0)
				tsync_avevent_locked(VIDEO_TSTAMP_DISCONTINUITY,
						     next_vf->pts);
			else if (next_vf->pts == 0 &&
				(tsync_get_mode() != TSYNC_MODE_PCRMASTER))
				tsync_avevent_locked(VIDEO_TSTAMP_DISCONTINUITY,
						     pts);

			/* pr_info("discontinue,
			 *	systime=0x%x vpts=0x%x next_vf->pts = 0x%x\n",
			 *	systime,
			 *	pts,
			 *	next_vf->pts);
			 */

			/* pts==0 is a keep frame maybe. */
			if (systime > next_vf->pts || next_vf->pts == 0 ||
			    (systime < pts &&
			    (pts > 0xFFFFFFFF - TIME_UNIT90K)))
				return true;
			if (omx_secret_mode == true
					&& cur_omx_index >= next_vf->omx_index)
				return true;

			return false;
		} else if (omx_secret_mode == true
				&& cur_omx_index >= next_vf->omx_index) {
			return true;
		} else if (tsync_check_vpts_discontinuity(pts) &&
			tsync_get_mode() == TSYNC_MODE_PCRMASTER) {
			/* in pcrmaster mode and pcr clk was used by tync,
			 * when the stream was replayed, the pcr clk was
			 * changed to the head of the stream. in this case,
			 * we send the "VIDEO_TSTAMP_DISCONTINUITY" signal
			 *  to notify tsync and adjust the sysclock to
			 * make playback smooth.
			 */
			if (next_vf->pts != 0)
				tsync_avevent_locked(VIDEO_TSTAMP_DISCONTINUITY,
					next_vf->pts);
			else if (next_vf->pts == 0) {
				tsync_avevent_locked(VIDEO_TSTAMP_DISCONTINUITY,
					pts);
				return true;
			}
		} else {
			/* +[SE] [BUG][SWPL-21070][zihao.ling]
			 *when vdiscontinue, not displayed
			 */
			return false;
		}
	} else if (omx_run
			&& omx_secret_mode
			&& (omx_pts + omx_pts_interval_upper < next_vf->pts)
			&& (omx_pts_set_index >= next_vf->omx_index)) {
		pr_info("omx, omx_pts=%d omx_pts_set_index=%d pts=%d omx_index=%d\n",
					omx_pts,
					omx_pts_set_index,
					next_vf->pts,
					next_vf->omx_index);
		return true;
	}
#if 1
	if (vsync_pts_inc_upint && (!freerun_mode)) {
		struct vframe_states frame_states;
		u32 delayed_ms, t1, t2;

		delayed_ms =
		    calculation_stream_delayed_ms(PTS_TYPE_VIDEO, &t1, &t2);
		if (video_vf_get_states(&frame_states) == 0) {
			u32 pcr = timestamp_pcrscr_get();
			u32 vpts = timestamp_vpts_get();
			u32 diff = pcr - vpts;

			if (delayed_ms > 200) {
				vsync_freerun++;
				if (pcr < next_vf->pts
				    || pcr < vpts + next_vf->duration) {
					if (next_vf->pts > 0) {
						timestamp_pcrscr_set
						    (next_vf->pts);
					} else {
						timestamp_pcrscr_set(vpts +
							next_vf->duration);
					}
				}
				return true;
			} else if ((frame_states.buf_avail_num >= 3)
				   && diff < vsync_pts_inc << 2) {
				vsync_pts_inc_adj =
				    vsync_pts_inc + (vsync_pts_inc >> 2);
				vsync_pts_125++;
			} else if ((frame_states.buf_avail_num >= 2
				    && diff < vsync_pts_inc << 1)) {
				vsync_pts_inc_adj =
				    vsync_pts_inc + (vsync_pts_inc >> 3);
				vsync_pts_112++;
			} else if (frame_states.buf_avail_num >= 1
				   && diff < vsync_pts_inc - 20) {
				vsync_pts_inc_adj = vsync_pts_inc + 10;
				vsync_pts_101++;
			} else {
				vsync_pts_inc_adj = 0;
				vsync_pts_100++;
			}
		}
	}
#endif

#ifdef VIDEO_PTS_CHASE
	vid_pts = timestamp_vpts_get();
	scr_pts = timestamp_pcrscr_get();
	vid_pts += vsync_pts_inc;

	if (av_sync_flag) {
		if (vpts_chase) {
			if ((abs(vid_pts - scr_pts) < 6000)
			    || (abs(vid_pts - scr_pts) > 90000)) {
				vpts_chase = 0;
				pr_info("leave vpts chase mode, diff:%d\n",
				       vid_pts - scr_pts);
			}
		} else if ((abs(vid_pts - scr_pts) > 9000)
			   && (abs(vid_pts - scr_pts) < 90000)) {
			vpts_chase = 1;
			if (vid_pts < scr_pts)
				vpts_chase_pts_diff = 50;
			else
				vpts_chase_pts_diff = -50;
			vpts_chase_counter =
			    ((int)(scr_pts - vid_pts)) / vpts_chase_pts_diff;
			pr_info("enter vpts chase mode, diff:%d\n",
			       vid_pts - scr_pts);
		} else if (abs(vid_pts - scr_pts) >= 90000) {
			pr_info("video pts discontinue, diff:%d\n",
			       vid_pts - scr_pts);
		}
	} else
		vpts_chase = 0;

	if (vpts_chase) {
		u32 curr_pts =
		    scr_pts - vpts_chase_pts_diff * vpts_chase_counter;

	/* pr_info("vchase pts %d, %d, %d, %d, %d\n",*/
	/*curr_pts, scr_pts, curr_pts-scr_pts, vid_pts, vpts_chase_counter); */
		return ((int)(curr_pts - pts)) >= 0;
	} else {
		int aud_start = (timestamp_apts_get() != -1);

	if (!av_sync_flag && aud_start && (abs(scr_pts - pts) < 9000)
	    && ((int)(scr_pts - pts) < 0)) {
		av_sync_flag = 1;
		pr_info("av sync ok\n");
	}
	return ((int)(scr_pts - pts)) >= 0;
	}
#else
	if (smooth_sync_enable) {
		org_vpts = timestamp_vpts_get();
		if ((abs(org_vpts + vsync_pts_inc + video_delay_val - systime) <
			M_PTS_SMOOTH_MAX) &&
		(abs(org_vpts + vsync_pts_inc + video_delay_val - systime)
			> M_PTS_SMOOTH_MIN)) {

			if (!video_frame_repeat_count) {
				vpts_ref = org_vpts;
				video_frame_repeat_count++;
			}

			if ((int)(org_vpts + vsync_pts_inc +
				video_delay_val - systime) > 0) {
				adjust_pts =
				    vpts_ref + (vsync_pts_inc -
						M_PTS_SMOOTH_ADJUST) *
				    video_frame_repeat_count;
			} else {
				adjust_pts =
				    vpts_ref + (vsync_pts_inc +
						M_PTS_SMOOTH_ADJUST) *
				    video_frame_repeat_count;
			}

			return (int)(adjust_pts - pts) >= 0;
		}

		if (video_frame_repeat_count) {
			vpts_ref = 0;
			video_frame_repeat_count = 0;
		}
	}
	if (video_delay_val && !omx_secret_mode) {
		expired = ((int)(timestamp_pcrscr_get() +
			vsync_pts_align - pts - video_delay_val)) >= 0;
		return expired;
	}
	if (tsync_get_mode() == TSYNC_MODE_PCRMASTER)
		expired = (timestamp_pcrscr_get() + vsync_pts_align >= pts) ?
				true : false;
	else
		expired = ((int)(timestamp_pcrscr_get() +
			vsync_pts_align - pts)) >= 0;
	if (debug_flag & DEBUG_FLAG_HDMI_AVSYNC_DEBUG)
		pr_info("vf/next %p/%p, pcr %d, pts %d, expired %d\n",
			cur_vf, next_vf, timestamp_pcrscr_get(), pts, expired);
#ifdef PTS_THROTTLE
	if (expired && next_vf && next_vf->next_vf_pts_valid &&
		(vsync_slow_factor == 1) &&
		next_vf->next_vf_pts &&
		(toggled_cnt > 0) &&
		((int)(timestamp_pcrscr_get() + vsync_pts_inc +
		vsync_pts_align - next_vf->next_vf_pts) < 0)) {
		expired = false;
	} else if (!expired && next_vf && next_vf->next_vf_pts_valid &&
		(vsync_slow_factor == 1) &&
		next_vf->next_vf_pts &&
		(toggled_cnt == 0) &&
		((int)(timestamp_pcrscr_get() + vsync_pts_inc +
		vsync_pts_align - next_vf->next_vf_pts) >= 0) &&
		(next_vf->next_vf_pts > next_vf->pts)) {
		expired = true;
	}
#endif

#ifdef PTS_ROLLBACK
	if (show_first_frame_nosync == 0 &&
	   show_first_picture == 0 &&
	   !expired &&
	   tsync_get_mode() == TSYNC_MODE_AMASTER &&
	   first_frame_toggled == 1 &&
	   systime < pts &&
	   (pts - systime) > pts_rollback_threshold * 90000) {
		expired = true;
		pr_info("pts roll back and force toggle, pts:%x\n", pts);
	}
#endif

#ifdef PTS_LOGGING
	if (pts_enforce_pulldown) {
		/* Perform Pulldown if needed*/
		vpts_perform_pulldown(next_vf, &expired);
	}
#endif
	return expired;
#endif
}

static void vsync_notify(void)
{
	if (video_notify_flag & VIDEO_NOTIFY_TRICK_WAIT) {
		wake_up_interruptible(&amvideo_trick_wait);
		video_notify_flag &= ~VIDEO_NOTIFY_TRICK_WAIT;
	}
	if (video_notify_flag & VIDEO_NOTIFY_FRAME_WAIT) {
		video_notify_flag &= ~VIDEO_NOTIFY_FRAME_WAIT;
		vf_notify_provider(RECEIVER_NAME,
				   VFRAME_EVENT_RECEIVER_FRAME_WAIT, NULL);
	}
#ifdef CONFIG_AMLOGIC_POST_PROCESS_MANAGER_PPSCALER
	if (video_notify_flag & VIDEO_NOTIFY_POS_CHANGED) {
		video_notify_flag &= ~VIDEO_NOTIFY_POS_CHANGED;
		vf_notify_provider(RECEIVER_NAME,
				   VFRAME_EVENT_RECEIVER_POS_CHANGED, NULL);
	}
#endif
	if (video_notify_flag &
	    (VIDEO_NOTIFY_PROVIDER_GET | VIDEO_NOTIFY_PROVIDER_PUT)) {
		int event = 0;

		if (video_notify_flag & VIDEO_NOTIFY_PROVIDER_GET)
			event |= VFRAME_EVENT_RECEIVER_GET;
		if (video_notify_flag & VIDEO_NOTIFY_PROVIDER_PUT)
			event |= VFRAME_EVENT_RECEIVER_PUT;

		vf_notify_provider(RECEIVER_NAME, event, NULL);

		video_notify_flag &=
		    ~(VIDEO_NOTIFY_PROVIDER_GET | VIDEO_NOTIFY_PROVIDER_PUT);
	}
	if (video_notify_flag & VIDEO_NOTIFY_NEED_NO_COMP) {
		char *provider_name = vf_get_provider_name(RECEIVER_NAME);

		while (provider_name) {
			if (!vf_get_provider_name(provider_name))
				break;
			provider_name =
				vf_get_provider_name(provider_name);
		}
		if (provider_name)
			vf_notify_provider_by_name(provider_name,
			VFRAME_EVENT_RECEIVER_NEED_NO_COMP,
			(void *)&vpp_hold_setting_cnt);
		video_notify_flag &= ~VIDEO_NOTIFY_NEED_NO_COMP;
	}
#ifdef CONFIG_CLK81_DFS
	check_and_set_clk81();
#endif

#ifdef CONFIG_GAMMA_PROC
	gamma_adjust();
#endif

}

#ifdef FIQ_VSYNC
static irqreturn_t vsync_bridge_isr(int irq, void *dev_id)
{
	vsync_notify();

	return IRQ_HANDLED;
}
#endif

int get_vsync_count(unsigned char reset)
{
	if (reset)
		vsync_count = 0;
	return vsync_count;
}
EXPORT_SYMBOL(get_vsync_count);

int get_vsync_pts_inc_mode(void)
{
	return vsync_pts_inc_upint;
}
EXPORT_SYMBOL(get_vsync_pts_inc_mode);

void set_vsync_pts_inc_mode(int inc)
{
	vsync_pts_inc_upint = inc;
}
EXPORT_SYMBOL(set_vsync_pts_inc_mode);

#ifdef CONFIG_AMLOGIC_MEDIA_VSYNC_RDMA
void vsync_rdma_process(void)
{
	vsync_rdma_config();
}
#endif

static char old_vmode[32];
static char new_vmode[32];
static inline bool video_vf_disp_mode_check(struct vframe_s *vf)
{
	struct provider_disp_mode_req_s req;
	char *provider_name = vf_get_provider_name(RECEIVER_NAME);
	int ret = -1;
	req.vf = vf;
	req.disp_mode = 0;
	req.req_mode = 1;

	while (provider_name) {
		if (!vf_get_provider_name(provider_name))
			break;
		provider_name =
			vf_get_provider_name(provider_name);
	}
	if (provider_name)
		ret = vf_notify_provider_by_name(provider_name,
			VFRAME_EVENT_RECEIVER_DISP_MODE, (void *)&req);
	if ((req.disp_mode == VFRAME_DISP_MODE_OK) ||
		(req.disp_mode == VFRAME_DISP_MODE_NULL))
		return false;

	/*set video vpts*/
	if (cur_dispbuf != vf) {
		if (vf->pts != 0) {
			amlog_mask(LOG_MASK_TIMESTAMP,
				   "vpts to vf->pts:0x%x,scr:0x%x,abs_scr: 0x%x\n",
			vf->pts, timestamp_pcrscr_get(),
			READ_MPEG_REG(SCR_HIU));
			timestamp_vpts_set(vf->pts);
		} else if (cur_dispbuf) {
			amlog_mask(LOG_MASK_TIMESTAMP,
				   "vpts inc:0x%x,scr: 0x%x, abs_scr: 0x%x\n",
			timestamp_vpts_get() +
			DUR2PTS(cur_dispbuf->duration),
			timestamp_pcrscr_get(),
			READ_MPEG_REG(SCR_HIU));
			timestamp_vpts_inc(
				DUR2PTS(cur_dispbuf->duration));

			vpts_remainder +=
				DUR2PTS_RM(cur_dispbuf->duration);
			if (vpts_remainder >= 0xf) {
				vpts_remainder -= 0xf;
				timestamp_vpts_inc(-1);
			}
		}
	}
	if (debug_flag & DEBUG_FLAG_HDMI_AVSYNC_DEBUG)
		pr_info("video %p disp_mode %d\n", vf, req.disp_mode);
	if (video_vf_put(vf) < 0)
		check_dispbuf(vf, true);
	return true;
}
static enum vframe_disp_mode_e video_vf_disp_mode_get(struct vframe_s *vf)
{
	struct provider_disp_mode_req_s req;
	char *provider_name = vf_get_provider_name(RECEIVER_NAME);
	req.vf = vf;
	req.disp_mode = 0;
	req.req_mode = 0;

	while (provider_name) {
		if (!vf_get_provider_name(provider_name))
			break;
		provider_name =
			vf_get_provider_name(provider_name);
	}
	if (provider_name)
		vf_notify_provider_by_name(provider_name,
			VFRAME_EVENT_RECEIVER_DISP_MODE, (void *)&req);
	return req.disp_mode;
}

static int video_vdin_buf_info_get(void)
{
	char *provider_name = vf_get_provider_name(RECEIVER_NAME);
	int max_buf_cnt = -1;

	while (provider_name) {
		if (!vf_get_provider_name(provider_name))
			break;
		provider_name =
			vf_get_provider_name(provider_name);
	}
	if (provider_name && (!strcmp(provider_name, "dv_vdin") ||
		!strcmp(provider_name, "vdin0")))
		vf_notify_provider_by_name(provider_name,
			VFRAME_EVENT_RECEIVER_BUF_COUNT, (void *)&max_buf_cnt);
	return max_buf_cnt;
}
static inline bool video_vf_dirty_put(struct vframe_s *vf)
{
	if (!vf->frame_dirty)
		return false;
	if (cur_dispbuf != vf) {
		if (vf->pts != 0) {
			amlog_mask(LOG_MASK_TIMESTAMP,
			"vpts to vf->pts:0x%x,scr:0x%x,abs_scr: 0x%x\n",
			vf->pts, timestamp_pcrscr_get(),
			READ_MPEG_REG(SCR_HIU));
			timestamp_vpts_set(vf->pts);
			timestamp_vpts_set_u64(vf->pts_us64);
		} else if (cur_dispbuf) {
			amlog_mask(LOG_MASK_TIMESTAMP,
			"vpts inc:0x%x,scr: 0x%x, abs_scr: 0x%x\n",
			timestamp_vpts_get() +
			DUR2PTS(cur_dispbuf->duration),
			timestamp_pcrscr_get(),
			READ_MPEG_REG(SCR_HIU));
			timestamp_vpts_inc(
				DUR2PTS(cur_dispbuf->duration));
			timestamp_vpts_inc_u64(
				DUR2PTS(cur_dispbuf->duration));

			vpts_remainder +=
				DUR2PTS_RM(cur_dispbuf->duration);
			if (vpts_remainder >= 0xf) {
				vpts_remainder -= 0xf;
				timestamp_vpts_inc(-1);
				timestamp_vpts_inc_u64(-1);
			}
		}
	}
	if (video_vf_put(vf) < 0)
		check_dispbuf(vf, true);
	return true;

}
#ifdef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
static bool dvel_status;
static bool dvel_changed;
static u32 dvel_size;
static int dolby_vision_need_wait(void)
{
	struct vframe_s *vf;

	if (!is_dolby_vision_enable())
		return 0;

	vf = video_vf_peek();
	if (!vf || (dolby_vision_wait_metadata(vf) == 1))
		return 1;
	return 0;
}

static int dolby_vision_need_wait_pip(void)
{
	struct vframe_s *vf;

	if (!is_dolby_vision_enable())
		return 0;

	vf = pip_vf_peek();
	if (!vf || (dolby_vision_wait_metadata(vf) == 1))
		return 1;
	return 0;
}

static int dvel_swap_frame(struct vframe_s *vf)
{
	int ret = 0;
	struct video_layer_s *layer = NULL;
	struct disp_info_s *layer_info = NULL;

	/* use bl layer info */
	layer = &vd_layer[0];
	layer_info = &glayer_info[0];

	if (!is_dolby_vision_enable()) {
		if (dvel_status) {
			//safe_switch_videolayer(1, false, true);
			dvel_status = false;
			need_disable_vd2 = true;
		}
	} else if (vf) {
		/* enable the vd2 when the first el vframe coming */
		u32 new_dvel_w = (vf->type
			& VIDTYPE_COMPRESS) ?
			vf->compWidth :
			vf->width;

		/* check if el available first */
		if (!dvel_status) {
			dvel_changed = true;
			dvel_size = new_dvel_w;
			need_disable_vd2 = false;
			safe_switch_videolayer(1, true, true);
		}
		/* check the el size */
		if (dvel_size != new_dvel_w)
			dvel_changed = true;
		ret = set_layer_display_canvas(
			1, vf, layer->cur_frame_par,
			layer_info);
		dvel_status = true;
	} else if (dvel_status) {
		dvel_changed = true;
		dvel_status = false;
		dvel_size = 0;
		need_disable_vd2 = true;
	}
	return ret;
}

/*SDK test: check metadata crc*/
/*frame crc error ---> drop err frame and repeat last frame*/
/*err count >= 6 ---> mute*/
static inline bool dv_vf_crc_check(struct vframe_s *vf)
{
	bool crc_err = false;
	char *provider_name = vf_get_provider_name(RECEIVER_NAME);

	while (provider_name) {
		if (!vf_get_provider_name(provider_name))
			break;
		provider_name =
			vf_get_provider_name(provider_name);
	}
	if (provider_name && (!strcmp(provider_name, "dv_vdin") ||
			      !strcmp(provider_name, "vdin0"))) {
		if (!vf->dv_crc_sts) {
			/*drop err crc frame*/
			vdin_err_crc_cnt++;
			if (debug_flag & DEBUG_FLAG_HDMI_DV_CRC)
				pr_info("vdin_err_crc_cnt %d\n",
					vdin_err_crc_cnt);

			/*need set video vpts when drop frame*/
			if (cur_dispbuf != vf) {
				if (vf->pts != 0) {
					amlog_mask(LOG_MASK_TIMESTAMP,
						   "vpts to vf->pts:0x%x,scr:0x%x,abs_scr: 0x%x\n",
					vf->pts, timestamp_pcrscr_get(),
					READ_MPEG_REG(SCR_HIU));
					timestamp_vpts_set(vf->pts);
				} else if (cur_dispbuf) {
					amlog_mask(LOG_MASK_TIMESTAMP,
						   "vpts inc:0x%x,scr: 0x%x, abs_scr: 0x%x\n",
					timestamp_vpts_get() +
					DUR2PTS(cur_dispbuf->duration),
					timestamp_pcrscr_get(),
					READ_MPEG_REG(SCR_HIU));
					timestamp_vpts_inc(
						DUR2PTS(cur_dispbuf->duration));

					vpts_remainder +=
					DUR2PTS_RM(cur_dispbuf->duration);
					if (vpts_remainder >= 0xf) {
						vpts_remainder -= 0xf;
						timestamp_vpts_inc(-1);
					}
				}
			}
			if (video_vf_put(vf) < 0)
				check_dispbuf(vf, true);
			crc_err = true;
		} else {
			vdin_err_crc_cnt = 0;
		}

	} else {
		vdin_err_crc_cnt = 0;
	}

	/*mute when err crc > = 6*/
	if (vdin_err_crc_cnt >= ERR_CRC_COUNT) {
		set_video_mute(true);
		dv_mute_vpp_flag = true;
	} else if (dv_mute_vpp_flag) {
		set_video_mute(false);
		dv_mute_vpp_flag = false;
	}
	return crc_err;
}

struct vframe_s *dvel_toggle_frame(
	struct vframe_s *vf, bool new_frame)
{
	struct vframe_s *toggle_vf = NULL;

	if (!is_dolby_vision_enable()) {
		cur_dispbuf2 = NULL;
		dvel_size = 0;
		dvel_changed = false;
		return NULL;
	}

	if (new_frame) {
		int ret = dolby_vision_update_metadata(vf, false);

		if (!is_dolby_vision_el_disable() ||
		    for_dolby_vision_certification())
			cur_dispbuf2 = dolby_vision_vf_peek_el(vf);
		if (ret == 0) {
			/* setting generated for this frame */
			/* or DOVI in bypass mode */
			toggle_vf = vf;
			dolby_vision_set_toggle_flag(1);
		} else if (ret == 1) {
			/* both dolby and hdr module bypass */
			toggle_vf = vf;
			dolby_vision_set_toggle_flag(0);
		} else {
			/* fail generating setting for this frame */
			toggle_vf = NULL;
			dolby_vision_set_toggle_flag(0);
		}
	} else {
		/* FIXME: if need the is on condition */
		/* if (is_dolby_vision_on() && get_video_enabled()) */
		if (!dolby_vision_parse_metadata(vf, 2, false, false))
			dolby_vision_set_toggle_flag(1);
	}
	return toggle_vf;
}

static void dolby_vision_proc(
	struct video_layer_s *layer,
	struct vpp_frame_par_s *cur_frame_par)
{

	static struct vframe_s *cur_dv_vf;
	static u32 cur_frame_size;
	struct vframe_s *disp_vf;
	u8 toggle_mode;

	if (is_dolby_vision_enable()) {
		u32 frame_size = 0, h_size, v_size;
		u8 pps_state = 0; /* pps no change */

/* TODO: check if need */
#ifdef OLD_DV_FLOW
		/* force toggle when keeping frame after playing */
		if (is_local_vf(layer->dispbuf) &&
		    !layer->new_frame &&
		    is_dolby_vision_video_on() &&
		    get_video_enabled()) {
			if (!dolby_vision_parse_metadata(
				layer->dispbuf, 2, false, false))
				dolby_vision_set_toggle_flag(1);
		}
#endif
		if (layer->new_frame)
			toggle_mode = 1; /* new frame */
		else if (!layer->dispbuf ||
			is_local_vf(layer->dispbuf))
			toggle_mode = 2; /* keep frame */
		else
			toggle_mode = 0; /* pasue frame */

		if (layer->switch_vf && layer->vf_ext)
			disp_vf = layer->vf_ext;
		else
			disp_vf = layer->dispbuf;

		if (cur_frame_par) {
			if (layer->new_vpp_setting) {
				struct vppfilter_mode_s *vpp_filter =
					&cur_frame_par->vpp_filter;
				if ((vpp_filter->vpp_hsc_start_phase_step
					== 0x1000000) &&
					(vpp_filter->vpp_vsc_start_phase_step
					== 0x1000000) &&
					(vpp_filter->vpp_hsc_start_phase_step ==
					vpp_filter->vpp_hf_start_phase_step) &&
					!vpp_filter->vpp_pre_vsc_en &&
					!vpp_filter->vpp_pre_hsc_en &&
					!cur_frame_par->supsc0_enable &&
					!cur_frame_par->supsc1_enable &&
					layer->bypass_pps)
					pps_state = 2; /* pps disable */
				else
					pps_state = 1; /* pps enable */
			}
			if (cur_frame_par->VPP_hd_start_lines_
				>=  cur_frame_par->VPP_hd_end_lines_)
				h_size = 0;
			else
				h_size = cur_frame_par->VPP_hd_end_lines_
				- cur_frame_par->VPP_hd_start_lines_ + 1;
			h_size /= (cur_frame_par->hscale_skip_count + 1);
			if (cur_frame_par->VPP_vd_start_lines_
				>=  cur_frame_par->VPP_vd_end_lines_)
				v_size = 0;
			else
				v_size = cur_frame_par->VPP_vd_end_lines_
				- cur_frame_par->VPP_vd_start_lines_ + 1;
			v_size /=
				(cur_frame_par->vscale_skip_count + 1);
			frame_size = (h_size << 16) | v_size;
		} else if (disp_vf) {
			h_size = (disp_vf->type & VIDTYPE_COMPRESS) ?
				disp_vf->compWidth : disp_vf->width;
			v_size = (disp_vf->type & VIDTYPE_COMPRESS) ?
				disp_vf->compHeight : disp_vf->height;
			frame_size = (h_size << 16) | v_size;
		}

		/* trigger dv process once when stop playing */
		/* because disp_vf is not sync with video off */
		if (cur_dv_vf && !disp_vf)
			dolby_vision_set_toggle_flag(1);
		cur_dv_vf = disp_vf;

		if (cur_frame_size != frame_size) {
			cur_frame_size = frame_size;
			dolby_vision_set_toggle_flag(1);
		}
		dolby_vision_process(
			disp_vf, frame_size,
			toggle_mode, pps_state);
		dolby_vision_update_setting();
	}
	return;
}

/* 1: drop fail; 0: drop success*/
static int dolby_vision_drop_frame(void)
{
	struct vframe_s *vf;

	if (dolby_vision_need_wait()) {
		if (debug_flag & DEBUG_FLAG_OMX_DV_DROP_FRAME)
			pr_info("drop frame need wait!\n");
		return 1;
	}
	vf = video_vf_get();

	if (debug_flag & DEBUG_FLAG_OMX_DV_DROP_FRAME)
		pr_info("drop vf %p, index %d, pts %d\n",
			vf, vf->omx_index, vf->pts);

	dolby_vision_update_metadata(vf, true);
	if (video_vf_put(vf) < 0)
		check_dispbuf(vf, true);

	if (debug_flag & DEBUG_FLAG_OMX_DV_DROP_FRAME)
		pr_info("drop vf %p done\n", vf);

	return 0;
}
#endif

/* patch for 4k2k bandwidth issue, skiw mali and vpu mif */
static void dmc_adjust_for_mali_vpu(unsigned int width,
	unsigned int height, bool force_adjust)
{
	if (toggle_count == last_toggle_count)
		toggle_same_count++;
	else {
		last_toggle_count = toggle_count;
		toggle_same_count = 0;
	}
	/*avoid 3840x2160 crop*/
	if ((width >= 2000) && (height >= 1400) &&
		(((dmc_config_state != 1) && (toggle_same_count < 30))
		|| force_adjust)) {
		if (0) {/* if (is_dolby_vision_enable()) { */
			/* vpu dmc */
			WRITE_DMCREG(
				DMC_AM0_CHAN_CTRL,
				0x85f403f4);
			WRITE_DMCREG(
				DMC_AM1_CHAN_CTRL,
				0x85f403f4);
			WRITE_DMCREG(
				DMC_AM2_CHAN_CTRL,
				0x85f403f4);

			/* mali dmc */
			WRITE_DMCREG(
				DMC_AXI1_CHAN_CTRL,
				0xff10ff4);
			WRITE_DMCREG(
				DMC_AXI2_CHAN_CTRL,
				0xff10ff4);
			WRITE_DMCREG(
				DMC_AXI1_HOLD_CTRL,
				0x08040804);
			WRITE_DMCREG(
				DMC_AXI2_HOLD_CTRL,
				0x08040804);
		} else {
			/* vpu dmc */
			WRITE_DMCREG(
				DMC_AM1_CHAN_CTRL,
				0x43028);
			WRITE_DMCREG(
				DMC_AM1_HOLD_CTRL,
				0x18101818);
			WRITE_DMCREG(
				DMC_AM3_CHAN_CTRL,
				0x85f403f4);
			WRITE_DMCREG(
				DMC_AM4_CHAN_CTRL,
				0x85f403f4);
			/* mali dmc */
			WRITE_DMCREG(
				DMC_AXI1_HOLD_CTRL,
				0x10080804);
			WRITE_DMCREG(
				DMC_AXI2_HOLD_CTRL,
				0x10080804);
		}
		dmc_config_state = 1;
	} else if (((toggle_same_count >= 30) ||
		((width < 2000) && (height < 1400))) &&
		(dmc_config_state != 2)) {
		/* vpu dmc */
		WRITE_DMCREG(
			DMC_AM0_CHAN_CTRL,
			0x8FF003C4);
		WRITE_DMCREG(
			DMC_AM1_CHAN_CTRL,
			0x3028);
		WRITE_DMCREG(
			DMC_AM1_HOLD_CTRL,
			0x18101810);
		WRITE_DMCREG(
			DMC_AM2_CHAN_CTRL,
			0x8FF003C4);
		WRITE_DMCREG(
			DMC_AM2_HOLD_CTRL,
			0x3028);
		WRITE_DMCREG(
			DMC_AM3_CHAN_CTRL,
			0x85f003f4);
		WRITE_DMCREG(
			DMC_AM4_CHAN_CTRL,
			0x85f003f4);

		/* mali dmc */
		WRITE_DMCREG(
			DMC_AXI1_CHAN_CTRL,
			0x8FF00FF4);
		WRITE_DMCREG(
			DMC_AXI2_CHAN_CTRL,
			0x8FF00FF4);
		WRITE_DMCREG(
			DMC_AXI1_HOLD_CTRL,
			0x18101810);
		WRITE_DMCREG(
			DMC_AXI2_HOLD_CTRL,
			0x18101810);
		toggle_same_count = 30;
		dmc_config_state = 2;
	}
}

/*ret = 0: no need delay*/
/*ret = 1: need to delay*/
static int hdmi_in_delay_check(struct vframe_s *vf)
{
	int expire;
	int vsync_duration = 0;
	u64 pts;
	u64 us;
	unsigned long flags;
	int expire_align = 0;

	char *provider_name = vf_get_provider_name(RECEIVER_NAME);

	if (hdmin_delay_done)
		return 0;

	if (!vf || vf->duration == 0)
		return 0;

	while (provider_name) {
		if (!vf_get_provider_name(provider_name))
			break;
		provider_name =
			vf_get_provider_name(provider_name);
	}
	if (!provider_name || (strcmp(provider_name, "dv_vdin") &&
		strcmp(provider_name, "vdin0"))) {
		return 0;
	}

	spin_lock_irqsave(&hdmi_avsync_lock, flags);

	/* update duration */
	vsync_duration = (int)(vf->duration / 96);

	if (hdmin_delay_start_time == -1) {
		hdmin_delay_start_time = jiffies_to_msecs(jiffies);
		/*this funtcion lead to one vsync delay */
		/*hdmin_delay_start_time -= vsync_duration;*/

		if (debug_flag & DEBUG_FLAG_HDMI_AVSYNC_DEBUG)
			pr_info("vsync_duration %d, hdmin_delay_start_time %d\n",
				vsync_duration, hdmin_delay_start_time);
		spin_unlock_irqrestore(&hdmi_avsync_lock, flags);
		return 1;
	}

	expire = jiffies_to_msecs(jiffies) -
		hdmin_delay_start_time;

	if (last_required_total_delay >= hdmin_delay_max_ms) {
		/*when required more than hdmin_delay_max_ms, */
		expire_align = -vsync_duration;
	} else {
		/*delay one more vsync? select the one that closer to required*/
		expire_align = -vsync_duration / 2;
	}
	if (debug_flag & DEBUG_FLAG_HDMI_AVSYNC_DEBUG)
		pr_info("expire %d, hdmin_delay_duration %d, expire_align %d\n",
			expire, hdmin_delay_duration, expire_align);

	if (expire - hdmin_delay_duration <= expire_align) {
		spin_unlock_irqrestore(&hdmi_avsync_lock, flags);
		return 1;
	}
	hdmin_delay_done = true;

	if (debug_flag & DEBUG_FLAG_HDMI_AVSYNC_DEBUG)
		pr_info("hdmi video delay done! expire %d\n", expire);

	/*reset vpts=pcr will lead vpts_expire delay 1 vsync - vsync_pts_align*/
	timestamp_vpts_set(timestamp_pcrscr_get() -
		(DUR2PTS(vf->duration) - vsync_pts_align));
	timestamp_vpts_set_u64((u64)(timestamp_pcrscr_get() -
		(DUR2PTS(vf->duration) - vsync_pts_align)));
	pts = (u64)timestamp_pcrscr_get_u64();
	pts = pts - (DUR2PTS(vf->duration) - vsync_pts_align);
	us = div64_u64(pts * 100, 9);
	timestamp_vpts_set_u64(us);
	spin_unlock_irqrestore(&hdmi_avsync_lock, flags);

	return 0;
}

static void hdmi_in_drop_frame(void)
{
	struct vframe_s *vf;

	while (hdmin_need_drop_count > 0) {
		vf = video_vf_get();
		if (!vf) { /*no video frame, drop done*/
			/*hdmi_need_drop_count = 0;*/
			break;
		}
		if (video_vf_put(vf) < 0)
			check_dispbuf(vf, true);

		if (debug_flag & DEBUG_FLAG_PRINT_DROP_FRAME)
			pr_info("#line %d: drop %p\n", __LINE__, vf);
		video_drop_vf_cnt++;
		--hdmin_need_drop_count;
	}
}

#if ENABLE_UPDATE_HDR_FROM_USER
void set_hdr_to_frame(struct vframe_s *vf)
{
	unsigned long flags;

	spin_lock_irqsave(&omx_hdr_lock, flags);

	if (has_hdr_info) {
		vf->prop.master_display_colour = vf_hdr;

		//config static signal_type for vp9
		vf->signal_type = (1 << 29)
			| (5 << 26) /* unspecified */
			| (0 << 25) /* limit */
			| (1 << 24) /* color available */
			| (9 << 16) /* 2020 */
			| (16 << 8) /* 2084 */
			| (9 << 0); /* 2020 */

		//pr_info("set_hdr_to_frame %d, signal_type 0x%x",
		//vf->prop.master_display_colour.present_flag,vf->signal_type);
	}
	spin_unlock_irqrestore(&omx_hdr_lock, flags);
}
#endif

bool black_threshold_check(u8 id)
{
	struct video_layer_s *layer = NULL;
	struct disp_info_s *layer_info = NULL;
	struct vpp_frame_par_s *frame_par = NULL;
	bool ret = false;

	if (id >= MAX_VD_LAYERS)
		return ret;

	if ((black_threshold_width <= 0) ||
	    (black_threshold_height <= 0))
		return ret;

	layer = &vd_layer[id];
	layer_info = &glayer_info[id];
	if ((layer_info->layer_top == 0) &&
	    (layer_info->layer_left == 0) &&
	    (layer_info->layer_width <= 1) &&
	    (layer_info->layer_height <= 1))
		/* special case to do full screen display */
		return ret;

	frame_par = layer->cur_frame_par;
	if ((layer_info->layer_width <= black_threshold_width) ||
	    (layer_info->layer_height <= black_threshold_height)) {
		if (frame_par &&
		    (frame_par->vscale_skip_count == 8) &&
		    (frame_par->hscale_skip_count == 1))
			ret = true;
	}
	return ret;
}

static void _set_video_crop(
	struct disp_info_s *layer, int *p)
{
	int last_l, last_r, last_t, last_b;
	int new_l, new_r, new_t, new_b;

	if (!layer)
		return;

	last_t = layer->crop_top;
	last_l = layer->crop_left;
	last_b = layer->crop_bottom;
	last_r = layer->crop_right;

	layer->crop_top = p[0];
	layer->crop_left = p[1];
	layer->crop_bottom = p[2];
	layer->crop_right = p[3];

	new_t = layer->crop_top;
	new_l = layer->crop_left;
	new_b = layer->crop_bottom;
	new_r = layer->crop_right;
	if ((new_t != last_t) || (new_l != last_l) ||
	    (new_b != last_b) || (new_r != last_r)) {
		if (layer->layer_id == 0)
			vd_layer[0].property_changed = true;
		else if (layer->layer_id == 1)
			vd_layer[1].property_changed = true;
	}
}

static void _set_video_window(
	struct disp_info_s *layer, int *p)
{
	int w, h;
	int *parsed = p;
	int last_x, last_y, last_w, last_h;
	int new_x, new_y, new_w, new_h;
#ifdef TV_REVERSE
	int temp, temp1;
	const struct vinfo_s *info = get_current_vinfo();
#endif

	if (!layer)
		return;

#ifdef TV_REVERSE
	if (reverse) {
		temp = parsed[0];
		temp1 = parsed[1];
		if (get_osd_reverse() & 1) {
			parsed[0] = info->width - parsed[2] - 1;
			parsed[2] = info->width - temp - 1;
		}
		if (get_osd_reverse() & 2) {
			parsed[1] = info->height - parsed[3] - 1;
			parsed[3] = info->height - temp1 - 1;
		}
	}
#endif

	last_x = layer->layer_left;
	last_y = layer->layer_top;
	last_w = layer->layer_width;
	last_h = layer->layer_height;

	if (parsed[0] < 0 && parsed[2] < 2) {
		parsed[2] = 2;
		parsed[0] = 0;
	}
	if (parsed[1] < 0 && parsed[3] < 2) {
		parsed[3] = 2;
		parsed[1] = 0;
	}
	w = parsed[2] - parsed[0] + 1;
	h = parsed[3] - parsed[1] + 1;

#ifdef CONFIG_AMLOGIC_POST_PROCESS_MANAGER_PPSCALER
	if (video_scaler_mode) {
		if ((w == 1) && (h == 1)) {
			w = 0;
			h = 0;
		}
		if ((content_left != parsed[0]) ||
		    (content_top != parsed[1]) ||
		    (content_w != w) ||
		    (content_h != h))
			scaler_pos_changed = 1;
		content_left = parsed[0];
		content_top = parsed[1];
		content_w = w;
		content_h = h;
		/* video_notify_flag =*/
		/*video_notify_flag|VIDEO_NOTIFY_POS_CHANGED; */
	} else
#endif
	{
		if ((w > 0) && (h > 0)) {
			if ((w == 1) && (h == 1)) {
				w = 0;
				h = 0;
			}
			layer->layer_left = parsed[0];
			layer->layer_top = parsed[1];
			layer->layer_width = w;
			layer->layer_height = h;
		}
	}

	new_x = layer->layer_left;
	new_y = layer->layer_top;
	new_w = layer->layer_width;
	new_h = layer->layer_height;
	trace_axis("VIDEO",
		   layer->layer_left,
		   layer->layer_top,
		   layer->layer_left + layer->layer_width - 1,
		   layer->layer_top + layer->layer_height - 1);

	if ((last_x != new_x) || (last_y != new_y) ||
	    (last_w != new_w) || (last_h != new_h)) {
		if (layer->layer_id == 0) {
			atomic_set(&axis_changed, 1);
			vd_layer[0].property_changed = true;
			if (debug_flag & DEBUG_FLAG_TRACE_EVENT)
				pr_info("VD1 axis changed: %d %d (%d %d)->%d %d (%d %d)\n",
					last_x, last_y, last_w, last_h,
					new_x, new_y, new_w, new_h);
		} else if (layer->layer_id == 1) {
			vd_layer[1].property_changed = true;
		}
	}
}

void set_video_crop_ext(int layer_index, int *p)
{
	struct disp_info_s *layer = &glayer_info[layer_index];

	_set_video_crop(layer, p);
}

void set_video_window_ext(int layer_index, int *p)
{
	struct disp_info_s *layer = &glayer_info[layer_index];

	_set_video_window(layer, p);
}

void set_video_zorder_ext(int layer_index, int zorder)
{
	struct disp_info_s *layer = &glayer_info[layer_index];

	if (layer->zorder != zorder) {
		layer->zorder = zorder;
		if (layer->layer_id == 0) {
			vd_layer[0].property_changed = true;
		} else if (layer->layer_id == 1) {
			vd_layer[1].property_changed = true;
		}
	}
}

static void pip_swap_frame(struct vframe_s *vf)
{
	struct video_layer_s *layer = NULL;
	struct disp_info_s *layer_info = NULL;
	int axis[4];
	int crop[4];
	int ret;

	if (!vf)
		return;

	layer = &vd_layer[1];
	layer_info = &glayer_info[1];

	if (layer->global_debug &
		DEBUG_FLAG_PRINT_TOGGLE_FRAME)
		pr_info("%s()\n", __func__);

	if ((vf->flag & (VFRAME_FLAG_VIDEO_COMPOSER |
	    VFRAME_FLAG_VIDEO_DRM)) &&
	    !(debug_flag & DEBUG_FLAG_AXIS_NO_UPDATE)) {
		axis[0] = vf->axis[0];
		axis[1] = vf->axis[1];
		axis[2] = vf->axis[2];
		axis[3] = vf->axis[3];
		crop[0] = vf->crop[0];
		crop[1] = vf->crop[1];
		crop[2] = vf->crop[2];
		crop[3] = vf->crop[3];
		_set_video_window(&glayer_info[1], axis);
		_set_video_crop(&glayer_info[1], crop);

		glayer_info[1].zorder = vf->zorder;
	}

	ret = layer_swap_frame(
		vf, layer->layer_id, false, vinfo);

	/* FIXME: free correct keep frame */
	if (!is_local_vf(layer->dispbuf)) {
		if (layer->keep_frame_id == 1)
			video_pip_keeper_new_frame_notify();
		else if (layer->keep_frame_id == 0)
			video_keeper_new_frame_notify();
	}
	fgrain_update_table(layer->layer_id, vf);
	if (stop_update)
		layer->new_vpp_setting = false;
}

static s32 pip_render_frame(struct video_layer_s *layer)
{
	struct vpp_frame_par_s *frame_par;
	u32 zoom_start_y, zoom_end_y;
	struct vframe_s *dispbuf = NULL;
	bool force_setting = false;

	if (!layer)
		return -1;

	if (layer->new_vpp_setting) {
		layer->cur_frame_par = layer->next_frame_par;
		/* just keep the order variable */
		curpip_frame_par = layer->cur_frame_par;
	}
	if (glayer_info[layer->layer_id].fgrain_force_update) {
		force_setting = true;
		glayer_info[layer->layer_id].fgrain_force_update = false;
	}

	frame_par = layer->cur_frame_par;

	if (layer->switch_vf && layer->vf_ext)
		dispbuf = layer->vf_ext;
	else
		dispbuf = layer->dispbuf;

	/* process cur frame for each vsync */
	if (dispbuf) {
		int need_afbc =
			(dispbuf->type & VIDTYPE_COMPRESS);
		int afbc_need_reset =
			(layer->enabled && need_afbc &&
			 !is_afbc_enabled(layer->layer_id));

		/*video on && afbc is off && is compress frame.*/
		if (layer->new_vpp_setting || afbc_need_reset)
			vd_set_dcu(
				layer->layer_id, layer,
				frame_par, dispbuf);

		/* for vout change or interlace frame */
		proc_vd_vsc_phase_per_vsync(
			layer->layer_id, layer,
			frame_par, dispbuf);
	}

	if (!layer->new_vpp_setting && !force_setting) {
		/* when new frame is toggled but video layer is not on */
		/* need always flush pps register before pwr on */
		/* to avoid pps coeff lost */
		if (frame_par && dispbuf && !is_local_vf(dispbuf) &&
		    (!get_videopip_enabled() ||
		     vd_layer[1].onoff_state ==
		     VIDEO_ENABLE_STATE_ON_PENDING))
			vd_scaler_setting(1, &layer->sc_setting);
		return 0;
	}

	if (dispbuf) {
		/* progressive or decode interlace case height 1:1 */
		zoom_start_y = frame_par->VPP_vd_start_lines_;
		zoom_end_y = frame_par->VPP_vd_end_lines_;
		if (dispbuf &&
		    (dispbuf->type & VIDTYPE_INTERLACE) &&
		    (dispbuf->type & VIDTYPE_VIU_FIELD)) {
			/* vdin interlace frame case height/2 */
			zoom_start_y /= 2;
			zoom_end_y = ((zoom_end_y + 1) >> 1) - 1;
		}
		layer->start_x_lines = frame_par->VPP_hd_start_lines_;
		layer->end_x_lines = frame_par->VPP_hd_end_lines_;
		layer->start_y_lines = zoom_start_y;
		layer->end_y_lines = zoom_end_y;
		config_vd_position(
			layer, &layer->mif_setting);
		vd_mif_setting(
			layer->layer_id, &layer->mif_setting);
		fgrain_config(layer->layer_id,
			      layer->cur_frame_par,
			      &layer->mif_setting,
			      &layer->fgrain_setting,
			      dispbuf);
	}

	config_vd_pps(
		layer, &layer->sc_setting, vinfo);
	vd_scaler_setting(
		layer->layer_id, &layer->sc_setting);

	config_vd_blend(
		layer, &layer->bld_setting);
	vd_blend_setting(
		layer->layer_id, &layer->bld_setting);
	fgrain_setting(layer->layer_id,
		       &layer->fgrain_setting,
		       dispbuf);
	layer->new_vpp_setting = false;
	return 1;
}

static void primary_swap_frame(
	struct vframe_s *vf1, int line)
{
#ifdef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
	bool vf_with_el = false;
#endif
	bool force_toggle = false;
	int ret;
	struct video_layer_s *layer = NULL;
	struct disp_info_s *layer_info = NULL;
	int axis[4];
	int crop[4];
	struct vframe_s *vf;

	ATRACE_COUNTER(__func__,  line);

	if (!vf1)
		return;

	vf = vf1;

	layer = &vd_layer[0];
	layer_info = &glayer_info[0];

	layer->do_switch = false;

	if (layer->need_switch_vf) {
		if ((vf1->flag & VFRAME_FLAG_DOUBLE_FRAM) &&
		    is_di_post_mode(vf1)) {
			if (di_api_get_instance_id() == vf1->di_instance_id) {
				layer->need_switch_vf = false;
				pr_info("set need_switch_vf false\n");
			} else {
				vf = vf1->vf_ext;
				layer->do_switch = true;
				di_api_post_disable();
			}
		} else
			layer->need_switch_vf = false;
	}

	if ((vf->flag & (VFRAME_FLAG_VIDEO_COMPOSER |
		VFRAME_FLAG_VIDEO_DRM)) &&
		!(vf->flag & VFRAME_FLAG_FAKE_FRAME) &&
	    !(debug_flag & DEBUG_FLAG_AXIS_NO_UPDATE)) {
		axis[0] = vf->axis[0];
		axis[1] = vf->axis[1];
		axis[2] = vf->axis[2];
		axis[3] = vf->axis[3];
		crop[0] = vf->crop[0];
		crop[1] = vf->crop[1];
		crop[2] = vf->crop[2];
		crop[3] = vf->crop[3];
		_set_video_window(&glayer_info[0], axis);
		_set_video_crop(&glayer_info[0], crop);
		glayer_info[0].zorder = vf->zorder;
	}

	if ((layer->layer_id == 0) && vf &&
	    !(vf->type & VIDTYPE_COMPRESS) &&
	    layer_info->need_no_compress) {
		atomic_sub(1, &gafbc_request);
		layer_info->need_no_compress = false;
		force_toggle = true;
	}

#ifdef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
	if (is_dolby_vision_enable())
		vf_with_el = has_enhanced_layer(vf);
#endif

	/* FIXME: need check the pre seq */
	if (vf->early_process_fun) {
		if (vf->early_process_fun(vf->private_data, vf) == 1)
			force_toggle = true;
	} else {
#ifdef CONFIG_AMLOGIC_MEDIA_DEINTERLACE
		/* FIXME: is_di_on */
		if (is_di_post_mode(vf)) {
			/* check mif enable status, disable post di */
			VSYNC_WR_MPEG_REG(DI_POST_CTRL, 0x3 << 30);
			VSYNC_WR_MPEG_REG(
				DI_POST_SIZE,
				(32 - 1) | ((128 - 1) << 16));
			VSYNC_WR_MPEG_REG(
				DI_IF1_GEN_REG,
				READ_VCBUS_REG(DI_IF1_GEN_REG) &
				0xfffffffe);
		}
#endif
	}

#ifdef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
	if (last_el_status != vf_with_el)
		force_toggle = true;
#endif
	if (last_process_3d_type != process_3d_type)
		force_toggle = true;

	if (!layer->dispbuf ||
	    ((vf->type & VIDTYPE_COMPRESS) &&
	     ((layer->dispbuf->compWidth != vf->compWidth) ||
	     (layer->dispbuf->compHeight != vf->compHeight))) ||
	    ((layer->dispbuf->width != vf->width) ||
	     (layer->dispbuf->height != vf->height))) {
		video_prop_status |= VIDEO_PROP_CHANGE_SIZE;
		if (debug_flag & DEBUG_FLAG_TRACE_EVENT)
			pr_info("VD1 src size changed: %dx%x->%dx%d. cur:%p, new:%p\n",
				layer->dispbuf ? layer->dispbuf->width : 0,
				layer->dispbuf ? layer->dispbuf->height : 0,
				vf->width, vf->height,
				layer->dispbuf, vf);
	}

	/* switch buffer */
	post_canvas = vf->canvas0Addr;
	ret = layer_swap_frame(
		vf, layer->layer_id, force_toggle, vinfo);
	if (ret >= vppfilter_success) {
		amlog_mask(
			LOG_MASK_FRAMEINFO,
			"%s %dx%d  ar=0x%x\n",
			((vf->type & VIDTYPE_TYPEMASK) ==
			VIDTYPE_INTERLACE_TOP) ? "interlace-top"
			: ((vf->type & VIDTYPE_TYPEMASK)
			== VIDTYPE_INTERLACE_BOTTOM)
			? "interlace-bottom" : "progressive", vf->width,
			vf->height, vf->ratio_control);
#ifdef TV_3D_FUNCTION_OPEN
		amlog_mask(
			LOG_MASK_FRAMEINFO,
			"%s trans_fmt=%u\n", __func__, vf->trans_fmt);

#endif
	}
	if (ret == vppfilter_changed_but_hold) {
		video_notify_flag |=
			VIDEO_NOTIFY_NEED_NO_COMP;
		vpp_hold_setting_cnt++;
		if (layer->global_debug & DEBUG_FLAG_BASIC_INFO)
			pr_info("toggle_frame vpp hold setting cnt: %d\n",
				vpp_hold_setting_cnt);
	} else {/* apply new vpp settings */
		if ((layer->next_frame_par->vscale_skip_count <= 1) &&
		    (vf->type & VIDTYPE_SUPPORT_COMPRESS)) {
			video_notify_flag |=
				VIDEO_NOTIFY_NEED_NO_COMP;
			if (layer->global_debug & DEBUG_FLAG_BASIC_INFO)
				pr_info("disable no compress mode\n");
		}
		vpp_hold_setting_cnt = 0;
	}

	if (((last_process_3d_type & MODE_3D_ENABLE) &&
	    !(process_3d_type & MODE_3D_ENABLE)) |
	   (!(last_process_3d_type & MODE_3D_TO_2D_MASK) &&
	     (process_3d_type & MODE_3D_TO_2D_MASK))) {
		need_disable_vd2 = true;
	}

	last_process_3d_type = process_3d_type;

#ifdef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
	/* if el is unnecessary, afbc2 need to be closed */
	if ((last_el_status == 1) && (vf_with_el == 0))
		need_disable_vd2 = true;
	last_el_status = vf_with_el;
#endif

	if (((vf->type & VIDTYPE_MVC) == 0) && last_mvc_status)
		need_disable_vd2 = true;

	if (vf && (vf->type & VIDTYPE_MVC))
		last_mvc_status = true;
	else
		last_mvc_status = false;

	/* FIXME: free correct keep frame */
	if (!is_local_vf(layer->dispbuf) && !layer->do_switch) {
		if (layer->keep_frame_id == 1)
			video_pip_keeper_new_frame_notify();
		else if (layer->keep_frame_id == 0)
			video_keeper_new_frame_notify();
	}

	ATRACE_COUNTER("swap frame", (uintptr_t)layer->dispbuf);
	fgrain_update_table(layer->layer_id, vf);
	if (stop_update)
		layer->new_vpp_setting = false;
	ATRACE_COUNTER(__func__,  0);
}

static s32 primary_render_frame(struct video_layer_s *layer)
{
	struct vpp_frame_par_s *frame_par;
	bool force_setting = false;
	u32 zoom_start_y, zoom_end_y, blank = 0;
	struct scaler_setting_s local_vd2_pps;
	struct blend_setting_s local_vd2_blend;
	struct mif_pos_s local_vd2_mif;
	bool update_vd2 = false;
	struct vframe_s *dispbuf = NULL;

	if (!layer)
		return -1;

	/* filter setting management */
	if (layer->new_vpp_setting) {
		layer->cur_frame_par = layer->next_frame_par;
		cur_frame_par = layer->cur_frame_par;
	}
	if (glayer_info[layer->layer_id].fgrain_force_update) {
		force_setting = true;
		glayer_info[layer->layer_id].fgrain_force_update = false;
	}
	frame_par = layer->cur_frame_par;
	if (layer->switch_vf && layer->vf_ext)
		dispbuf = layer->vf_ext;
	else
		dispbuf = layer->dispbuf;

	/* process cur frame for each vsync */
	if (dispbuf) {
		int need_afbc =
			(dispbuf->type & VIDTYPE_COMPRESS);
		int afbc_need_reset =
			(layer->enabled && need_afbc &&
			 !is_afbc_enabled(layer->layer_id));

		/*video on && afbc is off && is compress frame.*/
		if (layer->new_vpp_setting || afbc_need_reset)
			vd_set_dcu(
				layer->layer_id, layer,
				frame_par, dispbuf);
#ifdef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
		if (cur_dispbuf2 &&
		    (layer->new_vpp_setting ||
		     afbc_need_reset ||
		     dvel_changed))
			vd_set_dcu(
				1, &vd_layer[1],
				frame_par, cur_dispbuf2);
		if (dvel_changed)
			force_setting = true;
		dvel_changed = false;
#endif

#ifdef TV_3D_FUNCTION_OPEN
		if (last_mode_3d &&
		    (layer->new_vpp_setting ||
		     afbc_need_reset))
			vd_set_dcu(
				1, &vd_layer[1],
				frame_par, dispbuf);
#endif
		/* for vout change or interlace frame */
		proc_vd_vsc_phase_per_vsync(
			layer->layer_id, layer,
			frame_par, dispbuf);

		/* because 3d and dv process, vd2 need no scale. */
		/* so don't call the vd2 proc_vd1_vsc_phase_per_vsync */

		/* Do 3D process if enabled */
		switch_3d_view_per_vsync(layer);
	}

	/* no frame parameter change */
	if ((!layer->new_vpp_setting && !force_setting) || !frame_par) {
		/* when new frame is toggled but video layer is not on */
		/* need always flush pps register before pwr on */
		/* to avoid pps coeff lost */
		if (frame_par && dispbuf && !is_local_vf(dispbuf) &&
		    (!get_video_enabled() ||
		     vd_layer[0].onoff_state ==
		     VIDEO_ENABLE_STATE_ON_PENDING))
			vd_scaler_setting(0, &layer->sc_setting);

		/* dolby vision process for each vsync */
#ifdef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
		dolby_vision_proc(layer, frame_par);
#endif
		return 0;
	}
	/* VPP one time settings */
	if (dispbuf) {
#ifdef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
		correct_vd1_mif_size_for_DV(frame_par, cur_dispbuf2);
#endif
		/* progressive or decode interlace case height 1:1 */
		/* vdin afbc and interlace case height 1:1 */
		zoom_start_y = frame_par->VPP_vd_start_lines_;
		zoom_end_y = frame_par->VPP_vd_end_lines_;
		if ((dispbuf->type & VIDTYPE_INTERLACE) &&
		    (dispbuf->type & VIDTYPE_VIU_FIELD)) {
			/* vdin interlace non afbc frame case height/2 */
			zoom_start_y /= 2;
			zoom_end_y = ((zoom_end_y + 1) >> 1) - 1;
		} else if ((dispbuf->type & VIDTYPE_MVC) &&
		    !is_enable_3d_to_2d()) {
			/* mvc case, (height - blank)/2 */
			if (framepacking_support)
				blank = framepacking_blank;
			else
				blank = 0;
			zoom_start_y /= 2;
			zoom_end_y = ((zoom_end_y - blank + 1) >> 1) - 1;
		}

		layer->start_x_lines = frame_par->VPP_hd_start_lines_;
		layer->end_x_lines = frame_par->VPP_hd_end_lines_;
		layer->start_y_lines = zoom_start_y;
		layer->end_y_lines = zoom_end_y;
		config_vd_position(
			layer, &layer->mif_setting);
#ifdef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
		if (is_dolby_vision_on() && cur_dispbuf2) {
			config_dvel_position(
				layer,
				&local_vd2_mif,
				cur_dispbuf2);
			update_vd2 = true;
		}
#endif
#ifdef TV_3D_FUNCTION_OPEN
		if ((dispbuf->type & VIDTYPE_MVC) ||
		    last_mode_3d) {
			config_3d_vd2_position(
				layer, &local_vd2_mif);
			update_vd2 = true;
		}
#endif
		if (layer->vd1_vd2_mux) {
			layer->mif_setting.p_vd_mif_reg =
				&vd_layer[1].vd_mif_reg;
			vd_mif_setting(1, &layer->mif_setting);
		} else {
			vd_mif_setting(0, &layer->mif_setting);
		}
		if (update_vd2)
			vd_mif_setting(
				1, &local_vd2_mif);
		fgrain_config(layer->layer_id,
			      layer->cur_frame_par,
			      &layer->mif_setting,
			      &layer->fgrain_setting,
			      dispbuf);
	}

#ifdef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
	/* work around to cut the last green line */
	/* when two layer dv display and do vskip */
	if (is_dolby_vision_on() &&
	    (frame_par->vscale_skip_count > 0) &&
	    cur_dispbuf2 &&
	    (frame_par->VPP_pic_in_height_ > 0))
		frame_par->VPP_pic_in_height_--;
#endif
	config_vd_pps(
		layer, &layer->sc_setting, vinfo);
	config_vd_blend(
		layer, &layer->bld_setting);

	update_vd2 = false;
#ifdef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
	if (for_dolby_vision_certification())
		layer->sc_setting.sc_top_enable = false;

	if (is_dolby_vision_on() && cur_dispbuf2) {
		config_dvel_pps(
			layer, &local_vd2_pps, vinfo);
		config_dvel_blend(
			layer, &local_vd2_blend, cur_dispbuf2);
		update_vd2 = true;
	}
#endif
#ifdef TV_3D_FUNCTION_OPEN
	/*turn off vertical scaler when 3d display */
	if ((dispbuf &&
	     (dispbuf->type & VIDTYPE_MVC)) ||
	    last_mode_3d) {
		layer->sc_setting.sc_v_enable = is_enable_3d_to_2d() ||
			(dispbuf && (dispbuf->type & VIDTYPE_INTERLACE));
		config_3d_vd2_pps(
			layer, &local_vd2_pps, vinfo);
		config_3d_vd2_blend(
			layer, &local_vd2_blend);
		update_vd2 = true;
	}
#endif

	vd_scaler_setting(0, &layer->sc_setting);
	vd_blend_setting(0, &layer->bld_setting);

	if (update_vd2) {
		vd_scaler_setting(1, &local_vd2_pps);
		vd_blend_setting(1, &local_vd2_blend);
	}
	/* dolby vision process for each vsync */
	/* need set after correct_vd1_mif_size_for_DV */
#ifdef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
	dolby_vision_proc(layer, frame_par);
#endif
	fgrain_setting(0, &layer->fgrain_setting, dispbuf);

	layer->new_vpp_setting = false;
	return 1;
}

static int update_amvideo_recycle_buffer(void)
{
	struct vframe_s *rdma_buf = NULL;
	struct vframe_s *to_put_buf[DISPBUF_TO_PUT_MAX];
	int i;
	bool mismatch  = true, need_force_recycle = true;
	int put_cnt = 0;

	memset(to_put_buf, 0, sizeof(to_put_buf));
#ifdef CONFIG_AMLOGIC_MEDIA_VSYNC_RDMA
	/* after one vsync processing, should be same */
	if ((cur_rdma_buf != cur_dispbuf) &&
	    (cur_dispbuf != &vf_local)) {
		pr_err("expection: amvideo rdma_buf:%p, dispbuf:%p!\n",
		       cur_rdma_buf, cur_dispbuf);
		return -1;
	}

	for (i = 0; i < DISPBUF_TO_PUT_MAX; i++)
		if (dispbuf_to_put[i] &&
		    (dispbuf_to_put[i]->type & VIDTYPE_DI_PW))
			to_put_buf[put_cnt++] = dispbuf_to_put[i];
#endif

	if (cur_dispbuf != &vf_local)
		rdma_buf = cur_dispbuf;

	if (rdma_buf &&
	    !(rdma_buf->type & VIDTYPE_DI_PW))
		rdma_buf = NULL;

	if (recycle_cnt[0] &&
	    (put_cnt > 0 || rdma_buf)) {
		for (i = 0; i < recycle_cnt[0]; i++) {
			if (recycle_buf[0][i] == rdma_buf)
				need_force_recycle = false;
		}
		if (need_force_recycle)
			return -EAGAIN;

		pr_err("expection: normal amvideo recycle_cnt:%d, put_cnt:%d, recycle_buf:%p-%p, to_put:%p, rdma_buf:%p!\n",
		       recycle_cnt[0], put_cnt,
		       recycle_buf[0][0], recycle_buf[0][1],
		       to_put_buf[0], rdma_buf);
		return -1;
	}
	if (!recycle_cnt[0]) {
		for (i = 0; i < DISPBUF_TO_PUT_MAX + 1; i++)
			recycle_buf[0][i] = NULL;

		for (i = 0; i < put_cnt; i++) {
			if (to_put_buf[i])
				recycle_buf[0][recycle_cnt[0]++] =
					to_put_buf[i];
			if (rdma_buf &&
			    (rdma_buf == to_put_buf[i]))
				mismatch = false;
		}
		if (rdma_buf && mismatch)
			recycle_buf[0][recycle_cnt[0]++] = rdma_buf;
		pr_debug(
			 "amvideo need recycle %d new buffers: %p, %p, %p, %p, put_cnt:%d, rdma_buf:%p\n",
			 recycle_cnt[0], recycle_buf[0][0], recycle_buf[0][1],
			 recycle_buf[0][2], recycle_buf[0][3],
			 put_cnt, rdma_buf);
	} else {
		pr_debug(
			"amvideo need recycle %d remained buffers: %p, %p, %p, %p\n",
			recycle_cnt[0], recycle_buf[0][0], recycle_buf[0][1],
			recycle_buf[0][2], recycle_buf[0][3]);
	}
	return 0;
}

static int update_pip_recycle_buffer(void)
{
	struct vframe_s *rdma_buf = NULL;
	struct vframe_s *to_put_buf[DISPBUF_TO_PUT_MAX];
	int i;
	bool mismatch  = true, need_force_recycle = true;
	int put_cnt = 0;

	memset(to_put_buf, 0, sizeof(to_put_buf));
#ifdef CONFIG_AMLOGIC_MEDIA_VSYNC_RDMA
	/* after one vsync processing, should be same */
	if ((pip_rdma_buf != cur_pipbuf) &&
	    (cur_pipbuf != &local_pip)) {
		pr_err("expection: pip rdma_buf:%p, dispbuf:%p!\n",
		       pip_rdma_buf, cur_pipbuf);
		return -1;
	}
	for (i = 0; i < DISPBUF_TO_PUT_MAX; i++)
		if (pipbuf_to_put[i] &&
		    (pipbuf_to_put[i]->type & VIDTYPE_DI_PW))
			to_put_buf[put_cnt++] = pipbuf_to_put[i];
#endif

	if (cur_pipbuf != &local_pip)
		rdma_buf = cur_pipbuf;

	if (rdma_buf &&
	    !(rdma_buf->type & VIDTYPE_DI_PW))
		rdma_buf = NULL;

	if (recycle_cnt[1] &&
	    (put_cnt > 0 || rdma_buf)) {
		for (i = 0; i < recycle_cnt[1]; i++) {
			if (recycle_buf[1][i] == rdma_buf)
				need_force_recycle = false;
		}
		if (need_force_recycle)
			return -EAGAIN;

		pr_err("expection: pip recycle_cnt:%d, put_cnt:%d, recycle_buf:%p-%p, to_put:%p, rdma_buf:%p!\n",
		       recycle_cnt[1], put_cnt,
		       recycle_buf[1][0], recycle_buf[1][1],
		       to_put_buf[0], rdma_buf);
		return -1;
	}
	if (!recycle_cnt[1]) {
		for (i = 0; i < DISPBUF_TO_PUT_MAX + 1; i++)
			recycle_buf[1][i] = NULL;

		for (i = 0; i < put_cnt; i++) {
			if (to_put_buf[i])
				recycle_buf[1][recycle_cnt[1]++] =
					to_put_buf[i];
			if (rdma_buf &&
			    (rdma_buf == to_put_buf[i]))
				mismatch = false;
		}
		if (rdma_buf && mismatch)
			recycle_buf[1][recycle_cnt[1]++] = rdma_buf;
		pr_debug(
			 "pip need recycle %d new buffers: %p, %p, %p, %p, put_cnt:%d, rdma_buf:%p\n",
			 recycle_cnt[1], recycle_buf[1][0], recycle_buf[1][1],
			 recycle_buf[1][2], recycle_buf[1][3],
			 put_cnt, rdma_buf);
	} else {
		pr_debug(
			"pip need recycle %d remained buffers: %p, %p, %p, %p\n",
			recycle_cnt[1], recycle_buf[1][0], recycle_buf[1][1],
			recycle_buf[1][2], recycle_buf[1][3]);
	}
	return 0;
}

static inline void trace_performance(
	int enc_line_start,
	struct timeval *start,
	struct timeval *end1,
	struct timeval *end2,
	struct timeval *end3,
	struct timeval *end4)
{
	u32 sync_duration;
	struct vinfo_s *video_info;
	unsigned long time_use1 = 0;
	unsigned long time_use2 = 0;
	unsigned long time_use3 = 0;
	unsigned long time_use4 = 0;
	unsigned long time_use5 = 0;
	struct timeval end;
	int enc_line;

	enc_line = get_cur_enc_line();
	do_gettimeofday(&end);

	time_use1 = (end1->tv_sec - start->tv_sec) * 1000000 +
				(end1->tv_usec - start->tv_usec);
	time_use2 = (end2->tv_sec - start->tv_sec) * 1000000 +
				(end2->tv_usec - start->tv_usec);
	time_use3 = (end3->tv_sec - start->tv_sec) * 1000000 +
				(end3->tv_usec - start->tv_usec);
	time_use4 = (end4->tv_sec - start->tv_sec) * 1000000 +
				(end4->tv_usec - start->tv_usec);
	time_use5 = (end.tv_sec - start->tv_sec) * 1000000 +
				(end.tv_usec - start->tv_usec);

	video_info = get_current_vinfo();
	if (video_info && video_info->sync_duration_num)
		sync_duration = video_info->sync_duration_den * 1000 /
				video_info->sync_duration_num;
	else
		sync_duration = 16;

	if (enc_line < enc_line_start || time_use5 > sync_duration * 1000) {
		over_sync = true;
		++over_sync_count;
		if (performance_debug & DEBUG_FLAG_OVER_VSYNC)
			pr_info("long vsync enc line: %4d/4%d, time %ld us\n",
				enc_line_start, enc_line, time_use5);
	}
	if (performance_debug & DEBUG_FLAG_VSYNC_PROCESS_TIME) {
		pr_info("vsync time: %ld %ld %ld %ld %ld us\n",
			time_use1, time_use2, time_use3, time_use4, time_use5);
		pr_info("vsync enc line: %4d/4%d, over_sync %d, count %d\n",
			enc_line_start, enc_line, over_sync, over_sync_count);
	}
}

void set_alpha_scpxn(int layer_id, struct componser_info_t *componser_info)
{
	struct pip_alpha_scpxn_s alpha_win;
	int win_num = 0;
	int win_en = 0;
	int i;

	if (componser_info)
		win_num = componser_info->count;

	for (i = 0; i < win_num; i++) {
		alpha_win.scpxn_bgn_h[i] = componser_info->axis[i][0];
		alpha_win.scpxn_end_h[i] = componser_info->axis[i][2];
		alpha_win.scpxn_bgn_v[i] = componser_info->axis[i][1];
		alpha_win.scpxn_end_v[i] = componser_info->axis[i][3];
		win_en |= 1 << i;
	}
	set_alpha(layer_id, win_en, &alpha_win);
}

static int ai_pq_disable;
static int ai_pq_debug;
static int ai_pq_value = -1;
static int ai_pq_policy = 1;

#ifdef FIQ_VSYNC
void vsync_fisr_in(void)
#else
static irqreturn_t vsync_isr_in(int irq, void *dev_id)
#endif
{
	int hold_line;
	int enc_line;
	int enc_line_start = 0;
	unsigned char frame_par_di_set = 0;
	s32 vout_type;
	struct vframe_s *vf;
	bool show_nosync = false;
	int toggle_cnt;
#ifdef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
	struct vframe_s *dv_new_vf = NULL;
#endif
	struct vframe_s *path0_new_frame = NULL;
	struct vframe_s *path1_new_frame = NULL;
	struct vframe_s *path2_new_frame = NULL;
	struct vframe_s *path3_new_frame = NULL;
	struct vframe_s *new_frame = NULL;
	struct vframe_s *new_frame2 = NULL;
	struct vframe_s *cur_dispbuf_back = cur_dispbuf;
	struct vframe_s *di_post_vf = NULL;
	bool di_post_process_done = false;
	u32 cur_blackout;
	static u32 interrupt_count;
	static s32 cur_vd1_path_id = VFM_PATH_INVAILD;
	static s32 cur_vd2_path_id = VFM_PATH_INVAILD;
	u32 next_afbc_request = atomic_read(&gafbc_request);
	s32 vd1_path_id = glayer_info[0].display_path_id;
	s32 vd2_path_id = glayer_info[1].display_path_id;
	int axis[4];
	int crop[4];
	int pq_process_debug[4];
	enum vframe_signal_fmt_e fmt;
	int i;
	struct timeval start;
	struct timeval end1;
	struct timeval end2;
	struct timeval end3;
	struct timeval end4;

	do_gettimeofday(&start);
	enc_line_start = get_cur_enc_line();
	end1 = start;
	end2 = start;
	end3 = start;
	end4 = start;

	if (video_suspend && (video_suspend_cycle >= 1)) {
		if (log_out)
			pr_info("video suspend, vsync exit\n");
		log_out = 0;
		return IRQ_HANDLED;
	}
	if (debug_flag & DEBUG_FLAG_VSYNC_DONONE)
		return IRQ_HANDLED;

#ifdef CONFIG_AMLOGIC_MEDIA_MSYNC
	msync_vsync_update();
#endif
#ifdef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
	if (is_dolby_vision_on())
		dolby_vision_update_backlight();
#endif

	if (cur_vd1_path_id == 0xff)
		cur_vd1_path_id = vd1_path_id;
	if (cur_vd2_path_id == 0xff)
		cur_vd2_path_id = vd2_path_id;

#ifdef CONFIG_AMLOGIC_MEDIA_VSYNC_RDMA
	/* Just a workaround to enable RDMA without any register config.
	 * Becasuse rdma is enabled after first rdma config.
	 * Previously, it will write register directly and
	 * maybe out of blanking in first irq.
	 */
	if (first_irq) {
		first_irq = false;
		goto RUN_FIRST_RDMA;
	}
#endif

	vout_type = detect_vout_type(vinfo);
	hold_line = calc_hold_line();

	get_count = 0;
	get_count_pip = 0;

	glayer_info[0].need_no_compress =
		(next_afbc_request & 1) ? true : false;
	glayer_info[1].need_no_compress =
		(next_afbc_request & 2) ? true : false;
	vd_layer[0].bypass_pps = bypass_pps;
	vd_layer[1].bypass_pps = bypass_pps;
	vd_layer[0].global_debug = debug_flag;
	vd_layer[1].global_debug = debug_flag;
	vd_layer[0].vout_type = vout_type;
	vd_layer[1].vout_type = vout_type;

	if (!hist_test_flag && (cur_dispbuf == &hist_test_vf))
		cur_dispbuf = NULL;

	if (frame_detect_flag == 1 &&
		receive_frame_count &&
		frame_detect_time &&
		atomic_read(&video_unreg_flag)) {
		struct vinfo_s *video_info;

		video_info = get_current_vinfo();
		if (video_frame_detect.interrupt_count == 0) {
			interrupt_count = 0;
			video_frame_detect.interrupt_count =
				frame_detect_time *
				video_info->sync_duration_num /
			    video_info->sync_duration_den;
			if (debug_flag & DEBUG_FLAG_FRAME_DETECT) {
				pr_info("sync_duration_num = %d\n",
					video_info->sync_duration_num);
				pr_info("sync_duration_den = %d\n",
					video_info->sync_duration_den);
			}
			video_frame_detect.start_receive_count =
				receive_frame_count;
		}

		interrupt_count++;

		if (interrupt_count == video_frame_detect.interrupt_count + 1) {
			u32 receive_count;
			u32 expect_frame_count;

			receive_count = receive_frame_count -
				video_frame_detect.start_receive_count;
			expect_frame_count =
				video_frame_detect.interrupt_count *
				frame_detect_fps *
				video_info->sync_duration_den /
				video_info->sync_duration_num /
				1000;

			if (receive_count < expect_frame_count) {
				frame_detect_drop_count +=
					expect_frame_count -
					receive_count;
				if (debug_flag & DEBUG_FLAG_FRAME_DETECT) {
					pr_info("drop_count = %d\n",
						expect_frame_count -
						receive_count);
				}
				frame_detect_receive_count +=
					expect_frame_count;
			} else
				frame_detect_receive_count += receive_count;

			if (debug_flag & DEBUG_FLAG_FRAME_DETECT) {
				pr_info("expect count = %d\n",
						expect_frame_count);
				pr_info("receive_count = %d, time = %ds\n",
					receive_count,
					frame_detect_time);
				pr_info("interrupt_count = %d\n",
					video_frame_detect.interrupt_count);
				pr_info("frame_detect_drop_count = %d\n",
					frame_detect_drop_count);
				pr_info("frame_detect_receive_count = %d\n",
					frame_detect_receive_count);
			}
			interrupt_count = 0;
			memset(&video_frame_detect, 0,
				sizeof(struct video_frame_detect_s));
		}
	}
#ifdef CONFIG_AMLOGIC_VIDEO_COMPOSER
	vsync_notify_video_composer();
#endif

#ifdef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
	if (is_dolby_vision_enable()) {
		char *provider_name = NULL;

		if (vd1_path_id == VFM_PATH_PIP) {
			provider_name = vf_get_provider_name(RECEIVERPIP_NAME);
			while (provider_name) {
				if (!vf_get_provider_name(provider_name))
					break;
				provider_name =
					vf_get_provider_name(provider_name);
			}
			if (provider_name)
				dolby_vision_set_provider(provider_name);
			else
				dolby_vision_set_provider(dv_provider);
		} else {
			provider_name = vf_get_provider_name(RECEIVER_NAME);
			while (provider_name) {
				if (!vf_get_provider_name(provider_name))
					break;
				provider_name =
					vf_get_provider_name(provider_name);
			}
			if (provider_name)
				dolby_vision_set_provider(provider_name);
			else
				dolby_vision_set_provider(dv_provider);
		}
	}

	if (is_dolby_vision_enable() && dovi_drop_flag) {
		struct vframe_s *vf = NULL;
		unsigned int cnt = 10;
		int max_drop_index;

		if (debug_flag & DEBUG_FLAG_OMX_DV_DROP_FRAME)
			pr_info("dovi_drop_frame_num %d, omx_run %d\n",
				dovi_drop_frame_num, omx_run);
		while (cnt--) {
			vf = video_vf_peek();
			if (vf && is_dovi_frame(vf)) {
				max_drop_index = omx_run ?
				omx_need_drop_frame_num : dovi_drop_frame_num;

				if (max_drop_index >= vf->omx_index) {
					if (dolby_vision_drop_frame() == 1)
						break;
				} else if (omx_run &&
					   (vf->omx_index >
					   omx_need_drop_frame_num)) {
					/* all drop done*/
					dovi_drop_flag = false;
					omx_drop_done = true;
					if (debug_flag &
					    DEBUG_FLAG_OMX_DV_DROP_FRAME)
						pr_info("dolby vision drop done\n");
					break;
				} else
					break;
			} else {
				break;
			}
		}
	}
#endif

	if (omx_need_drop_frame_num > 0 &&
		(!omx_drop_done || !omx_run) &&
		omx_secret_mode) {
		struct vframe_s *vf = NULL;

		while (1) {
			vf = video_vf_peek();

#ifdef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
			if (is_dolby_vision_enable()
				&& vf && is_dovi_frame(vf)) {
				break;
			}
#endif
			if (!vf)
				break;

			if (omx_need_drop_frame_num >= vf->omx_index) {
				vf = video_vf_get();
				if (video_vf_put(vf) < 0)
					check_dispbuf(vf, true);
				video_drop_vf_cnt++;
				if (debug_flag & DEBUG_FLAG_PRINT_DROP_FRAME)
					pr_info("#line %d: drop frame: drop count %d\n",
						__LINE__, video_drop_vf_cnt);
			} else {
				omx_drop_done = true;
				break;
			}
		}
	}

	vf = video_vf_peek();

	if (vf) {
		if (glayer_info[0].display_path_id == VFM_PATH_AUTO) {
			if ((vf->sidebind_type
				== glayer_info[0].sideband_type)
				|| (vf->sidebind_type == 0)) {
				pr_info("VID: path_id %d -> %d, type=%d, %d\n",
					glayer_info[0].display_path_id,
					VFM_PATH_AMVIDEO, vf->sidebind_type,
					glayer_info[0].sideband_type);
				glayer_info[0].display_path_id
					= VFM_PATH_AMVIDEO;
				vd1_path_id = glayer_info[0].display_path_id;
			} else if (glayer_info[0].sideband_type != -1)
				pr_info("vf->sideband_type =%d,layertype=%d\n",
					vf->sidebind_type,
					glayer_info[0].sideband_type);
		}
	}

	/* vout mode detection under old tunnel mode */
	if ((vf) && ((vf->type & VIDTYPE_NO_VIDEO_ENABLE) == 0)) {
		if (strcmp(old_vmode, new_vmode)) {
			vd_layer[0].property_changed = true;
			vd_layer[1].property_changed = true;
			pr_info("detect vout mode change!!!!!!!!!!!!\n");
			strcpy(old_vmode, new_vmode);
		}
	}
	toggle_cnt = 0;
	vsync_count++;
	timer_count++;
	if (display_frame_count == 0 && vf &&
	    (vf->source_type == VFRAME_SOURCE_TYPE_HDMI ||
	    vf->source_type == VFRAME_SOURCE_TYPE_CVBS)) {
		int buf_cnt = video_vdin_buf_info_get();

		if (buf_cnt > 2) {
			struct vinfo_s *video_info;

			video_info = get_current_vinfo();
			if (video_info->sync_duration_num > 0)
				hdmin_delay_max_ms = 1000 *
				video_info->sync_duration_den /
				video_info->sync_duration_num
				* (buf_cnt - 2);
		}
	}

#if defined(CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_VECM)
	if (cur_frame_par) {/*need call every vsync*/
		if (vf)
			vlock_process(vf, cur_frame_par);
		else
			vlock_process(NULL, cur_frame_par);
	}
#endif
	if (performance_debug & DEBUG_FLAG_VSYNC_PROCESS_TIME)
		do_gettimeofday(&end1);
	enc_line = get_cur_enc_line();
	if (enc_line > vsync_enter_line_max)
		vsync_enter_line_max = enc_line;

	if (is_meson_txlx_cpu() && dmc_adjust) {
		bool force_adjust = false;
		u32 vf_width = 0, vf_height = 0;
		struct vframe_s *chk_vf;

		chk_vf = (vf != NULL) ? vf : cur_dispbuf;
		if (chk_vf)
			force_adjust =
				((chk_vf->type & VIDTYPE_VIU_444) ||
				(chk_vf->type & VIDTYPE_RGB_444))
				? true : false;
		if (chk_vf) {
			if (cur_frame_par &&
				cur_frame_par->nocomp) {
				vf_width = chk_vf->width;
				vf_height = chk_vf->height;
			} else if ((chk_vf->type & VIDTYPE_COMPRESS)
				&& cur_frame_par
				&& cur_frame_par->vscale_skip_count) {
				vf_width = chk_vf->compWidth;
				vf_height = chk_vf->compHeight;
			} else {
				vf_width = chk_vf->width;
				vf_height = chk_vf->height;
			}
			dmc_adjust_for_mali_vpu(
				vf_width,
				vf_height,
				force_adjust);
		} else
			dmc_adjust_for_mali_vpu(
				0, 0, force_adjust);
	}
#ifdef CONFIG_AMLOGIC_MEDIA_VSYNC_RDMA
	vsync_rdma_config_pre();

	if (to_notify_trick_wait) {
		atomic_set(&trickmode_framedone, 1);
		video_notify_flag |= VIDEO_NOTIFY_TRICK_WAIT;
		to_notify_trick_wait = false;
		goto exit;
	}

	if (debug_flag & DEBUG_FLAG_PRINT_RDMA) {
		if (vd_layer[0].property_changed) {
			enable_rdma_log_count = 5;
			enable_rdma_log(1);
		}
		if (enable_rdma_log_count > 0)
			enable_rdma_log_count--;
	}
#endif
#ifdef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
	/* check video frame before VECM process */
	if (is_dolby_vision_enable() && vf &&
	    (vd1_path_id == VFM_PATH_AMVIDEO ||
	     vd1_path_id == VFM_PATH_DEF ||
	     vd1_path_id == VFM_PATH_AUTO)) {
		dolby_vision_check_mvc(vf);
		dolby_vision_check_hdr10(vf);
		dolby_vision_check_hdr10plus(vf);
		dolby_vision_check_hlg(vf);
		dolby_vision_check_cuva(vf);
	}
#endif

	if (vsync_pts_inc_upint) {
		if (vsync_pts_inc_adj) {
			/* pr_info("adj %d, org %d\n",*/
			/*vsync_pts_inc_adj, vsync_pts_inc); */
			timestamp_pcrscr_inc(vsync_pts_inc_adj);
			timestamp_apts_inc(vsync_pts_inc_adj);
#ifdef CONFIG_AMLOGIC_VIDEOSYNC
			videosync_pcrscr_inc(vsync_pts_inc_adj);
#endif
		} else {
			timestamp_pcrscr_inc(vsync_pts_inc + 1);
			timestamp_apts_inc(vsync_pts_inc + 1);
#ifdef CONFIG_AMLOGIC_VIDEOSYNC
			videosync_pcrscr_inc(vsync_pts_inc + 1);
#endif
		}
	} else {
		if (vsync_slow_factor == 0) {
			pr_info("invalid vsync_slow_factor, set to 1\n");
			vsync_slow_factor = 1;
		}

		if (vsync_slow_factor == 1) {
			timestamp_pcrscr_inc_scale(vsync_pts_inc_scale,
					vsync_pts_inc_scale_base);
			timestamp_apts_inc(vsync_pts_inc / vsync_slow_factor);
#ifdef CONFIG_AMLOGIC_VIDEOSYNC
			videosync_pcrscr_update(vsync_pts_inc_scale,
					vsync_pts_inc_scale_base);
#endif
		} else if (vsync_slow_factor > 1000) {
			u32 inc = (vsync_slow_factor / 1000)
				* vsync_pts_inc / 1000;

			timestamp_pcrscr_inc(inc);
			timestamp_apts_inc(inc);
#ifdef CONFIG_AMLOGIC_VIDEOSYNC
			videosync_pcrscr_inc(inc);
#endif
		} else {
			timestamp_pcrscr_inc(vsync_pts_inc / vsync_slow_factor);
			timestamp_apts_inc(vsync_pts_inc / vsync_slow_factor);
#ifdef CONFIG_AMLOGIC_VIDEOSYNC
			videosync_pcrscr_inc(vsync_pts_inc / vsync_slow_factor);
#endif
		}
	}
	if (omx_secret_mode == true) {
		u32 system_time = timestamp_pcrscr_get();
		int diff = 0;
		unsigned long delta1 = 0;
		unsigned long time_setomxpts_delta = 0;

		diff = system_time - omx_pts;

		video_notify_flag |= VIDEO_NOTIFY_TRICK_WAIT;
		atomic_set(&trickmode_framedone, 1);

		if (time_setomxpts > 0
			&& time_setomxpts_last > 0) {
			/* time_setomxpts record hwc setomxpts time, */
			/* when check  diff between pcr and  omx_pts, */
			/* add compensation will let omx_pts and pcr */
			/* is at the some time, more accurate. Also */
			/* remove the compensation when omx_pts */
			/* is not update for a while, in case when */
			/* paused, pcr is not paused */
			delta1 = func_div(sched_clock() - time_setomxpts, 1000);
			time_setomxpts_delta = func_div(time_setomxpts -
				time_setomxpts_last, 1000);
			if ((time_setomxpts_delta >
				(4 * vsync_pts_inc * 1000 / 90)) ||
				((diff - omx_pts_interval_upper * 3 / 2) > 0)
				|| ((diff - omx_pts_interval_lower * 3 / 2)
				< 0)) {
				time_setomxpts = 0;
				time_setomxpts_last = 0;
				if (debug_flag & DEBUG_FLAG_PTS_TRACE)
					pr_info("omxpts is not update for a while,do not need compenstate\n");
			} else {
				diff -=  delta1 * 90 / 1000;
			}
		}

		if (((diff - omx_pts_interval_upper) > 0
			|| (diff - omx_pts_interval_lower) < 0
			|| (omx_pts_set_from_hwc_count <
			OMX_MAX_COUNT_RESET_SYSTEMTIME))
			&& video_start_post) {
			timestamp_pcrscr_enable(1);
			if (debug_flag & DEBUG_FLAG_PTS_TRACE)
				pr_info("system_time=%d, omx_pts=%d, diff=%d\n",
					system_time, omx_pts, diff);
			/*add  greatest common divisor of duration*/
			/*1500(60fps) 3000(30fps) 3750(24fps) for some video*/
			/*that pts is not evenly*/
			if (debug_flag & DEBUG_FLAG_OMX_DEBUG_DROP_FRAME) {
				pr_info("pcrscr_set sys_time=%d, omx_pts=%d, diff=%d",
						system_time, omx_pts, diff);
			}
			timestamp_pcrscr_set(omx_pts + DURATION_GCD);
		} else if (omx_pts_set_from_hwc_count
			== OMX_MAX_COUNT_RESET_SYSTEMTIME) {
			timestamp_pcrscr_set(omx_pts + DURATION_GCD);
			omx_pts_set_from_hwc_count++;
			if (debug_flag & DEBUG_FLAG_OMX_DEBUG_DROP_FRAME)
				pr_info("second: sys_time=%d,omx=%d,diff=%d",
					system_time, omx_pts, diff);

		} else if (((diff - omx_pts_interval_upper / 2) > 0
			|| (diff - omx_pts_interval_lower / 2) < 0)
			&& (omx_pts_set_from_hwc_count_begin <
			OMX_MAX_COUNT_RESET_SYSTEMTIME_BEGIN)
			&& video_start_post) {
			timestamp_pcrscr_enable(1);
			if (debug_flag & DEBUG_FLAG_PTS_TRACE)
				pr_info("begin-system_time=%d, omx_pts=%d, diff=%d\n",
					system_time, omx_pts, diff);
			timestamp_pcrscr_set(omx_pts + DURATION_GCD);
		} else if (is_dolby_vision_enable()
			&& ((diff - omx_pts_dv_upper) > 0
			|| (diff - omx_pts_dv_lower) < 0)
			&& video_start_post) {
			timestamp_pcrscr_set(omx_pts + DURATION_GCD);
		}
	} else
		omx_pts = 0;
	if (trickmode_duration_count > 0)
		trickmode_duration_count -= vsync_pts_inc;
#ifdef VIDEO_PTS_CHASE
	if (vpts_chase)
		vpts_chase_counter--;
#endif

	if (slowsync_repeat_enable)
		frame_repeat_count++;

	if (smooth_sync_enable) {
		if (video_frame_repeat_count)
			video_frame_repeat_count++;
	}

	if (atomic_read(&video_unreg_flag))
		goto exit;

	if ((recycle_cnt[0] > 0) && (cur_dispbuf != &vf_local)) {
		for (i = 0; i < recycle_cnt[0]; i++) {
			if (recycle_buf[0][i] &&
			    (cur_dispbuf != recycle_buf[0][i]) &&
			    (recycle_buf[0][i]->flag & VFRAME_FLAG_DI_PW_VFM)) {
				di_release_count++;
				dim_post_keep_cmd_release2(recycle_buf[0][i]);
			} else if (recycle_buf[0][i] &&
				(cur_dispbuf == recycle_buf[0][i])) {
				pr_err(
					"recycle 0 di buffer conflict, recycle vf:%p\n",
				       recycle_buf[0][i]);
			}
			recycle_buf[0][i] = NULL;
		}
		recycle_cnt[0] = 0;
	}

	if ((recycle_cnt[1] > 0) && (cur_pipbuf != &local_pip)) {
		for (i = 0; i < recycle_cnt[1]; i++) {
			if (recycle_buf[1][i] &&
			    (cur_pipbuf != recycle_buf[1][i]) &&
			    (recycle_buf[1][i]->flag & VFRAME_FLAG_DI_PW_VFM))
				dim_post_keep_cmd_release2(recycle_buf[1][i]);
			else if (recycle_buf[1][i] &&
				(cur_pipbuf == recycle_buf[1][i]))
				pr_err(
					"recycle 1 di buffer conflict, recycle vf:%p\n",
					recycle_buf[1][i]);
			recycle_buf[1][i] = NULL;
		}
		recycle_cnt[1] = 0;
	}

	if ((vd_layer[0].dispbuf_mapping == &cur_dispbuf) &&
	    ((cur_dispbuf == &vf_local) ||
	     !cur_dispbuf) &&
	    (vd_layer[0].dispbuf != cur_dispbuf))
		vd_layer[0].dispbuf = cur_dispbuf;

	if ((vd_layer[0].dispbuf_mapping == &cur_pipbuf) &&
	    ((cur_pipbuf == &local_pip) ||
	     !cur_pipbuf) &&
	    (vd_layer[0].dispbuf != cur_pipbuf))
		vd_layer[0].dispbuf = cur_pipbuf;

	if (gvideo_recv[0] &&
	    (vd_layer[0].dispbuf_mapping == &gvideo_recv[0]->cur_buf) &&
	    ((gvideo_recv[0]->cur_buf == &gvideo_recv[0]->local_buf) ||
	     !gvideo_recv[0]->cur_buf) &&
	    (vd_layer[0].dispbuf != gvideo_recv[0]->cur_buf))
		vd_layer[0].dispbuf = gvideo_recv[0]->cur_buf;

	if (gvideo_recv[1] &&
	    (vd_layer[0].dispbuf_mapping == &gvideo_recv[1]->cur_buf) &&
	    ((gvideo_recv[1]->cur_buf == &gvideo_recv[1]->local_buf) ||
	     !gvideo_recv[1]->cur_buf) &&
	    (vd_layer[0].dispbuf != gvideo_recv[1]->cur_buf))
		vd_layer[0].dispbuf = gvideo_recv[1]->cur_buf;

	if ((vd_layer[1].dispbuf_mapping == &cur_dispbuf) &&
	    ((cur_dispbuf == &vf_local) ||
	     !cur_dispbuf) &&
	    (vd_layer[1].dispbuf != cur_dispbuf))
		vd_layer[1].dispbuf = cur_dispbuf;

	if ((vd_layer[1].dispbuf_mapping == &cur_pipbuf) &&
	    ((cur_pipbuf == &local_pip) ||
	     !cur_pipbuf) &&
	    (vd_layer[1].dispbuf != cur_pipbuf))
		vd_layer[1].dispbuf = cur_pipbuf;

	if (gvideo_recv[0] &&
	    (vd_layer[1].dispbuf_mapping == &gvideo_recv[0]->cur_buf) &&
	    ((gvideo_recv[0]->cur_buf == &gvideo_recv[0]->local_buf) ||
	     !gvideo_recv[0]->cur_buf) &&
	    (vd_layer[1].dispbuf != gvideo_recv[0]->cur_buf))
		vd_layer[1].dispbuf = gvideo_recv[0]->cur_buf;

	if (gvideo_recv[1] &&
	    (vd_layer[1].dispbuf_mapping == &gvideo_recv[1]->cur_buf) &&
	    ((gvideo_recv[1]->cur_buf == &gvideo_recv[1]->local_buf) ||
	     !gvideo_recv[1]->cur_buf) &&
	    (vd_layer[1].dispbuf != gvideo_recv[1]->cur_buf))
		vd_layer[1].dispbuf = gvideo_recv[1]->cur_buf;

	if (atomic_read(&video_pause_flag) &&
	    !((vd_layer[0].global_output == 1) &&
	      (vd_layer[0].enabled !=
	       vd_layer[0].enabled_status_saved)))
		goto exit;

	fmt = atomic_read(&primary_src_fmt);
	if (fmt != atomic_read(&cur_primary_src_fmt)) {
		if (debug_flag & DEBUG_FLAG_TRACE_EVENT) {
			char *old_str = NULL, *new_str = NULL;
			enum vframe_signal_fmt_e old_fmt;

			old_fmt = atomic_read(&cur_primary_src_fmt);
			if (old_fmt != VFRAME_SIGNAL_FMT_INVALID)
				old_str = (char *)src_fmt_str[old_fmt];
			if (fmt != VFRAME_SIGNAL_FMT_INVALID)
				new_str = (char *)src_fmt_str[fmt];
			pr_info("VD1 src fmt changed: %s->%s.\n",
				old_str ? old_str : "invalid",
				new_str ? new_str : "invalid");
		}
		atomic_set(&cur_primary_src_fmt, fmt);
		video_prop_status |= VIDEO_PROP_CHANGE_FMT;
	}

#ifdef CONFIG_AMLOGIC_MEDIA_VSYNC_RDMA
	if (is_vsync_rdma_enable()) {
		vd_layer[0].cur_canvas_id = vd_layer[0].next_canvas_id;
		vd_layer[1].cur_canvas_id = vd_layer[1].next_canvas_id;
	} else {
		if (rdma_enable_pre)
			goto exit;

		vd_layer[0].cur_canvas_id = 0;
		vd_layer[0].next_canvas_id = 1;
		vd_layer[1].cur_canvas_id = 0;
		vd_layer[1].next_canvas_id = 1;
	}
#ifdef CONFIG_AMLOGIC_MEDIA_VSYNC_RDMA
	if (!over_sync) {
		for (i = 0; i < DISPBUF_TO_PUT_MAX; i++) {
			if (dispbuf_to_put[i]) {
				dispbuf_to_put[i]->rendered = true;
				if (!video_vf_put(dispbuf_to_put[i])) {
					dispbuf_to_put[i] = NULL;
					dispbuf_to_put_num--;
				}
			}
		}

		for (i = 0; i < DISPBUF_TO_PUT_MAX; i++) {
			if (pipbuf_to_put[i]) {
				pipbuf_to_put[i]->rendered = true;
				if (!pip_vf_put(pipbuf_to_put[i])) {
					pipbuf_to_put[i] = NULL;
					pipbuf_to_put_num--;
				}
			}
		}
	}
#endif
#endif

	if (gvideo_recv[0])
		gvideo_recv[0]->func->early_proc(gvideo_recv[0],
						 over_sync ? 1 : 0);
	if (gvideo_recv[1])
		gvideo_recv[1]->func->early_proc(gvideo_recv[1],
						 over_sync ? 1 : 0);

	over_sync = false;

	if ((!cur_dispbuf) || (cur_dispbuf == &vf_local)) {
		vf = video_vf_peek();
		if (vf) {
			if (hdmi_in_onvideo == 0) {
				if (nopostvideostart == false)
					tsync_avevent_locked(VIDEO_START,
					    (vf->pts) ? vf->pts :
					    timestamp_vpts_get());
				video_start_post = true;
			}

			if (show_first_frame_nosync || show_first_picture)
				show_nosync = true;

			if (slowsync_repeat_enable)
				frame_repeat_count = 0;
		} else
			goto SET_FILTER;
	}

	/* buffer switch management */
	vf = video_vf_peek();

	/*process hdmi in video sync*/
	if (vf) {
		/*step 1: audio required*/
		if (hdmin_delay_start > 0) {
			process_hdmi_video_sync(vf);
			hdmin_delay_start = 0;
		}
		/*step 2: recheck video sync after hdmi-in start*/
		if (hdmi_delay_first_check) {
			hdmi_vframe_count++;
			if (hdmi_vframe_count > HDMI_DELAY_FIRST_CHECK_COUNT) {
				process_hdmi_video_sync(vf);
				hdmi_vframe_count = 0;
				hdmi_delay_first_check = false;
			}
		/*step 3: recheck video sync every 3s*/
		} else if (hdmi_delay_normal_check) {
			hdmi_vframe_count++;
			if (hdmi_vframe_count > HDMI_DELAY_NORMAL_CHECK_COUNT) {
				process_hdmi_video_sync(vf);
				hdmi_vframe_count = 0;
			}
		}

		/* HDMI-IN AV SYNC Control, delay video*/
		if (!hdmin_delay_done) {
			if (hdmi_in_delay_check(vf) > 0)
				goto exit;
		}
		/*HDMI-IN AV SYNC Control, drop video*/
		if (hdmin_need_drop_count > 0)
			hdmi_in_drop_frame();
	}

	/* buffer switch management */
	vf = video_vf_peek();
	/*debug info for skip & repeate vframe case*/
	if (!vf) {
		underflow++;
		/* video master mode, reduce pcrscr */
		if (tsync_get_mode() == TSYNC_MODE_VMASTER) {
			s32 pts_inc = 0 - vsync_pts_inc;

			timestamp_pcrscr_inc(pts_inc);
		}
		ATRACE_COUNTER("underflow",  1);
		if (video_dbg_vf&(1<<0))
			dump_vframe_status("vdin0");
		if (video_dbg_vf&(1<<1))
			dump_vframe_status("deinterlace");
		if (video_dbg_vf&(1<<2))
			dump_vframe_status("amlvideo2");
		if (video_dbg_vf&(1<<3))
			dump_vframe_status("ppmgr");
		if (video_dbg_vf&(1<<4))
			dump_vdin_reg();
	} else {
		ATRACE_COUNTER("underflow",  0);
	}

	video_get_vf_cnt = 0;

	/* toggle_3d_fa_frame*/
	/* determine the out frame is L or R or blank */
	judge_3d_fa_out_mode();

	while (vf && !video_suspend) {
		if (debug_flag & DEBUG_FLAG_OMX_DEBUG_DROP_FRAME) {
			pr_info("next pts= %x,index %x,pcr = %x,vpts = %x\n",
				vf->pts, vf->omx_index,
				timestamp_pcrscr_get(), timestamp_vpts_get());
		}
		if ((omx_continuous_drop_flag && omx_run)
			&& !(debug_flag
				& DEBUG_FLAG_OMX_DISABLE_DROP_FRAME)) {
#ifdef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
			if (is_dolby_vision_enable() && vf &&
			    is_dovi_frame(vf)) {
				if (debug_flag & DEBUG_FLAG_OMX_DV_DROP_FRAME)
					pr_info("dovi ignore continuous drop\n");
				/* if (omx_run)
				 *	dolby_vision_drop_frame();
				 */
			} else
#endif
			{
				if (debug_flag &
					DEBUG_FLAG_OMX_DEBUG_DROP_FRAME) {
					pr_info("drop omx_index %d, pts %d\n",
						vf->omx_index, vf->pts);
				}
				vf = vf_get(RECEIVER_NAME);
				if (vf) {
					vf_put(vf, RECEIVER_NAME);
					video_drop_vf_cnt++;
					if (debug_flag &
					    DEBUG_FLAG_PRINT_DROP_FRAME)
						pr_info("#line %d: drop frame: drop count %d\n",
							__LINE__,
							video_drop_vf_cnt);
				}
				vf = video_vf_peek();
				continue;
			}
		}

		if (vpts_expire(cur_dispbuf, vf, toggle_cnt) || show_nosync) {
#if defined(CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_VECM)
			int iret1 = 0, iret2 = 0;
#endif

			ATRACE_COUNTER(MODULE_NAME,  __LINE__);
			if (debug_flag & DEBUG_FLAG_PTS_TRACE) {
				pr_info("vpts = 0x%x, c.dur=0x%x, n.pts=0x%x, scr = 0x%x, pcr-pts-diff=%d, ptstrace=%d\n",
					timestamp_vpts_get(),
					(cur_dispbuf) ?
					cur_dispbuf->duration : 0,
					vf->pts, timestamp_pcrscr_get(),
					timestamp_pcrscr_get() - vf->pts +
					vsync_pts_align,
					pts_trace);
				if (pts_trace > 4 || pts_trace == 0)
					pr_info
					("exception:pts trace:%d.", pts_trace);
				if (vf->pts && vf->pts < timestamp_vpts_get()) {
					int t;

					t = timestamp_vpts_get() - vf->pts;
					t = t / 90;
					pr_info
					("exception:video jumpback %dms.", t);
				}
			}
			amlog_mask_if(toggle_cnt > 0, LOG_MASK_FRAMESKIP,
				      "skipped\n");
#ifdef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
			if ((vd1_path_id == VFM_PATH_AMVIDEO ||
			     vd1_path_id == VFM_PATH_DEF ||
			     vd1_path_id == VFM_PATH_AUTO) &&
			    dolby_vision_need_wait())
				break;
#endif
#if ENABLE_UPDATE_HDR_FROM_USER
			set_hdr_to_frame(vf);
#endif
			/*
			 *two special case:
			 *case1:4k display case,input buffer not enough &
			 *	quickly for display
			 *case2:input buffer all not OK
			 */
			if (vf &&
				((vf->source_type == VFRAME_SOURCE_TYPE_HDMI) ||
				(vf->source_type == VFRAME_SOURCE_TYPE_CVBS)) &&
				(video_vf_disp_mode_get(vf) ==
				VFRAME_DISP_MODE_UNKNOWN) &&
				(frame_skip_check_cnt++ < 10))
				break;
			else
				frame_skip_check_cnt = 0;
#if defined(CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_VECM)
			if (vd1_path_id == VFM_PATH_AMVIDEO ||
			    vd1_path_id == VFM_PATH_DEF)
				iret1 = amvecm_on_vs(
					(cur_dispbuf != &vf_local)
					? cur_dispbuf : NULL,
					vf, CSC_FLAG_CHECK_OUTPUT,
					0,
					0,
					0,
					0,
					0,
					0,
					VD1_PATH);
			if (vd2_path_id == VFM_PATH_AMVIDEO)
				iret2 = amvecm_on_vs(
					(cur_dispbuf != &vf_local)
					? cur_dispbuf : NULL,
					vf, CSC_FLAG_CHECK_OUTPUT,
					0,
					0,
					0,
					0,
					0,
					0,
					VD2_PATH);
			if ((iret1 == 1) || (iret2 == 1))
				break;
#endif
			if (performance_debug & DEBUG_FLAG_VSYNC_PROCESS_TIME)
				do_gettimeofday(&end2);
			vf = video_vf_get();
			if (!vf) {
				ATRACE_COUNTER(MODULE_NAME,  __LINE__);
				break;
			}
			if (debug_flag & DEBUG_FLAG_LATENCY) {
				vf->ready_clock[2] = sched_clock();
				pr_info("video get latency %lld ms vdin put latency %lld ms. first %lld ms.\n",
				func_div(vf->ready_clock[2], 1000),
				func_div(vf->ready_clock[1], 1000),
				func_div(vf->ready_clock[0], 1000));
			}
			if (video_vf_dirty_put(vf)) {
				ATRACE_COUNTER(MODULE_NAME,  __LINE__);
				break;
			}
			if (vf &&
				((vf->source_type == VFRAME_SOURCE_TYPE_HDMI) ||
				(vf->source_type == VFRAME_SOURCE_TYPE_CVBS)) &&
				video_vf_disp_mode_check(vf)) {
				vdin_frame_skip_cnt++;
				break;
			}
			force_blackout = 0;
			if (vf) {
				if (last_mode_3d !=
					vf->mode_3d_enable) {
					last_mode_3d =
						vf->mode_3d_enable;
					mode_3d_changed = 1;
				}
				video_3d_format = vf->trans_fmt;
			}
#ifdef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
			/*check metadata crc*/
			if (vf &&
			    ((vf->source_type == VFRAME_SOURCE_TYPE_HDMI) ||
			    (vf->source_type == VFRAME_SOURCE_TYPE_CVBS)) &&
				dv_vf_crc_check(vf)) {
				break; // not render err crc frame
			}
#endif
			path0_new_frame = vsync_toggle_frame(vf, __LINE__);
			if (hold_video)
				path0_new_frame = NULL;

			/* The v4l2 capture needs a empty vframe to flush */
			if (has_receive_dummy_vframe())
				break;

			if (performance_debug & DEBUG_FLAG_VSYNC_PROCESS_TIME)
				do_gettimeofday(&end3);

#ifdef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
			if (vd1_path_id == VFM_PATH_AMVIDEO ||
			    vd1_path_id == VFM_PATH_DEF ||
			    vd1_path_id == VFM_PATH_AUTO)
				dv_new_vf = dvel_toggle_frame(vf, true);
			if (hold_video)
				dv_new_vf = NULL;
#endif
			if (performance_debug & DEBUG_FLAG_VSYNC_PROCESS_TIME)
				do_gettimeofday(&end4);

			if (trickmode_fffb == 1) {
				trickmode_vpts = vf->pts;
#ifdef CONFIG_AMLOGIC_MEDIA_VSYNC_RDMA
				/* FIXME: replace is_di_on */
				if (is_di_post_mode(vf)) {
					atomic_set(&trickmode_framedone, 1);
					video_notify_flag |=
					    VIDEO_NOTIFY_TRICK_WAIT;
				} else {
					to_notify_trick_wait = true;
				}
#else
				atomic_set(&trickmode_framedone, 1);
				video_notify_flag |= VIDEO_NOTIFY_TRICK_WAIT;
#endif
				break;
			}
			if (slowsync_repeat_enable)
				frame_repeat_count = 0;
			vf = video_vf_peek();
			if (vf) {
				if ((vf->flag & VFRAME_FLAG_VIDEO_COMPOSER) &&
				    (debug_flag &
				     DEBUG_FLAG_COMPOSER_NO_DROP_FRAME)) {
					pr_info("composer not drop\n");
					vf = NULL;
				}
			}

			if (!vf)
				next_peek_underflow++;
#ifdef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
			if ((vd1_path_id == VFM_PATH_AMVIDEO ||
			     vd1_path_id == VFM_PATH_DEF ||
			     vd1_path_id == VFM_PATH_AUTO) &&
			    for_dolby_vision_certification() &&
			    dv_new_vf)
				break;
#endif
			if (debug_flag & DEBUG_FLAG_TOGGLE_FRAME_PER_VSYNC)
				break;
			video_get_vf_cnt++;
			if (video_get_vf_cnt >= 2) {
				video_drop_vf_cnt++;
				if (debug_flag & DEBUG_FLAG_PRINT_DROP_FRAME)
					pr_info("#line %d: drop frame: drop count %d\n",
						__LINE__, video_drop_vf_cnt);
			}
		} else {
			ATRACE_COUNTER(MODULE_NAME,  __LINE__);
			/* check if current frame's duration has expired,
			 *in this example
			 * it compares current frame display duration
			 * with 1/1/1/1.5 frame duration
			 * every 4 frames there will be one frame play
			 * longer than usual.
			 * you can adjust this array for any slow sync
			 * control as you want.
			 * The playback can be smoother than previous method.
			 */
			if (slowsync_repeat_enable) {
				ATRACE_COUNTER(MODULE_NAME,  __LINE__);
				if (duration_expire
					(cur_dispbuf, vf,
					frame_repeat_count
					* vsync_pts_inc) &&
					((timestamp_pcrscr_enable_state() ||
					tsync_get_mode() ==
					TSYNC_MODE_PCRMASTER))) {
#if defined(CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_VECM)
					int iret1 = 0, iret2 = 0;
#endif

					amlog_mask(LOG_MASK_SLOWSYNC,
					"slow sync toggle,repeat_count = %d\n",
					frame_repeat_count);
					amlog_mask(LOG_MASK_SLOWSYNC,
					"sys.time = 0x%x, video time = 0x%x\n",
					timestamp_pcrscr_get(),
					timestamp_vpts_get());
#ifdef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
					if ((vd1_path_id ==
					     VFM_PATH_AMVIDEO ||
					     vd1_path_id ==
					     VFM_PATH_DEF ||
					     vd1_path_id ==
					     VFM_PATH_AUTO) &&
					    dolby_vision_need_wait())
						break;
#endif

#if defined(CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_VECM)
					if (vd1_path_id == VFM_PATH_AMVIDEO ||
					    vd1_path_id == VFM_PATH_DEF)
						iret1 = amvecm_on_vs(
						(cur_dispbuf != &vf_local)
						? cur_dispbuf : NULL,
						vf, CSC_FLAG_CHECK_OUTPUT,
						0,
						0,
						0,
						0,
						0,
						0,
						VD1_PATH);
					if (vd2_path_id == VFM_PATH_AMVIDEO)
						iret2 = amvecm_on_vs(
						(cur_dispbuf != &vf_local)
						? cur_dispbuf : NULL,
						vf, CSC_FLAG_CHECK_OUTPUT,
						0,
						0,
						0,
						0,
						0,
						0,
						VD2_PATH);
					if ((iret1 == 1) || (iret2 == 1))
						break;
#endif
					vf = video_vf_get();
					if (!vf) {
						ATRACE_COUNTER(MODULE_NAME,
								__LINE__);
						break;
					}
					path0_new_frame =
					vsync_toggle_frame(vf, __LINE__);
					if (hold_video)
						path0_new_frame = NULL;
#ifdef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
					if (vd1_path_id == VFM_PATH_AMVIDEO ||
					    vd1_path_id == VFM_PATH_DEF ||
					    vd1_path_id == VFM_PATH_AUTO)
						dv_new_vf =
						dvel_toggle_frame(vf, true);
					if (hold_video)
						dv_new_vf = NULL;
#endif
					frame_repeat_count = 0;

					vf = video_vf_peek();
				} else if ((cur_dispbuf) &&
					(cur_dispbuf->duration_pulldown >
					vsync_pts_inc)) {
					frame_count++;
					cur_dispbuf->duration_pulldown -=
					    PTS2DUR(vsync_pts_inc);
				}
			} else {
				if (cur_dispbuf &&
				    (cur_dispbuf->duration_pulldown >
				     vsync_pts_inc)) {
					frame_count++;
					cur_dispbuf->duration_pulldown -=
					    PTS2DUR(vsync_pts_inc);
				}
			}
			break;
		}
		toggle_cnt++;
	}
	if (video_suspend)
		goto exit;
#ifdef INTERLACE_FIELD_MATCH_PROCESS
	if (interlace_field_type_need_match(vout_type, vf)) {
		if (field_matching_count++ == FIELD_MATCH_THRESHOLD) {
			field_matching_count = 0;
			/* adjust system time to get one more field toggle */
			/* at next vsync to match field */
			timestamp_pcrscr_inc(vsync_pts_inc);
		}
	} else {
		field_matching_count = 0;
	}
#endif
SET_FILTER:
	if (cur_dispbuf_back != cur_dispbuf) {
		display_frame_count++;
		drop_frame_count = receive_frame_count - display_frame_count;
	}

	vf = pip_vf_peek();
	videopip_get_vf_cnt = 0;
#ifdef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
	/* check video frame before VECM process */
	if (vd1_path_id == VFM_PATH_PIP && vf &&
	    is_dolby_vision_enable()) {
		dolby_vision_check_hdr10(vf);
		dolby_vision_check_hdr10plus(vf);
		dolby_vision_check_hlg(vf);
		dolby_vision_check_cuva(vf);
	}
#endif
	while (vf && !video_suspend) {
		if (!vf->frame_dirty) {
#if defined(CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_VECM)
			int iret1 = 0, iret2 = 0;
#endif

#ifdef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
			if (vd1_path_id == VFM_PATH_PIP &&
			    dolby_vision_need_wait_pip())
				break;
#endif
#if defined(CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_VECM)
			if (vd1_path_id == VFM_PATH_PIP)
				iret1 = amvecm_on_vs(
					(cur_pipbuf != &local_pip)
					? cur_pipbuf : NULL,
					vf, CSC_FLAG_CHECK_OUTPUT,
					0,
					0,
					0,
					0,
					0,
					0,
					VD1_PATH);
			if ((vd2_path_id == VFM_PATH_DEF) ||
			    (vd2_path_id == VFM_PATH_PIP))
				iret2 = amvecm_on_vs(
					(cur_pipbuf != &local_pip)
					? cur_pipbuf : NULL,
					vf, CSC_FLAG_CHECK_OUTPUT,
					0,
					0,
					0,
					0,
					0,
					0,
					VD2_PATH);
			if ((iret1 == 1) || (iret2 == 1))
				break;
#endif
			vf = pip_vf_get();
			if (vf) {
				videopip_get_vf_cnt++;
				path1_new_frame = pip_toggle_frame(vf);
#ifdef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
				if (vd1_path_id == VFM_PATH_PIP)
					dv_new_vf = dvel_toggle_frame(vf, true);
#endif
			}
		} else {
			vf = pip_vf_get();
			if (vf) {
				videopip_get_vf_cnt++;
				if (pip_vf_put(vf) < 0)
					check_pipbuf(vf, true);
			}
		}
		vf = pip_vf_peek();
	}
	vsync_notify_videosync();
	if (videopip_get_vf_cnt >= 2) {
		videopip_drop_vf_cnt += (videopip_get_vf_cnt - 1);
		if (debug_flag & DEBUG_FLAG_PRINT_DROP_FRAME)
			pr_info("#line %d: videopip drop frame: drop count %d\n",
				__LINE__, videopip_drop_vf_cnt);
	}
	if (video_suspend)
		goto exit;
	/* video_render.0 toggle frame */
	if (gvideo_recv[0]) {
		path2_new_frame =
			gvideo_recv[0]->func->dequeue_frame(gvideo_recv[0]);
		if (vd1_path_id == gvideo_recv[0]->path_id) {
			amvecm_on_vs(
				(gvideo_recv[0]->cur_buf !=
				 &gvideo_recv[0]->local_buf)
				? gvideo_recv[0]->cur_buf : NULL,
				path2_new_frame,
				CSC_FLAG_CHECK_OUTPUT,
				0,
				0,
				0,
				0,
				0,
				0,
				VD1_PATH);
		}
		if (vd2_path_id == gvideo_recv[0]->path_id)
			amvecm_on_vs(
				(gvideo_recv[0]->cur_buf !=
				 &gvideo_recv[0]->local_buf)
				? gvideo_recv[0]->cur_buf : NULL,
				path2_new_frame,
				CSC_FLAG_CHECK_OUTPUT,
				0,
				0,
				0,
				0,
				0,
				0,
				VD2_PATH);
	}

	/* video_render.1 toggle frame */
	if (gvideo_recv[1]) {
		path3_new_frame =
			gvideo_recv[1]->func->dequeue_frame(gvideo_recv[1]);
		if (vd1_path_id == gvideo_recv[1]->path_id) {
			amvecm_on_vs(
				(gvideo_recv[1]->cur_buf !=
				 &gvideo_recv[1]->local_buf)
				? gvideo_recv[1]->cur_buf : NULL,
				path3_new_frame,
				CSC_FLAG_CHECK_OUTPUT,
				0,
				0,
				0,
				0,
				0,
				0,
				VD1_PATH);
		}
		if (vd2_path_id == gvideo_recv[1]->path_id)
			amvecm_on_vs(
				(gvideo_recv[1]->cur_buf !=
				 &gvideo_recv[1]->local_buf)
				? gvideo_recv[1]->cur_buf : NULL,
				path3_new_frame,
				CSC_FLAG_CHECK_OUTPUT,
				0,
				0,
				0,
				0,
				0,
				0,
				VD2_PATH);
	}

	/* FIXME: if need enable for vd1 */
#ifdef CHECK_LATER
	if (!vd_layer[0].global_output) {
		cur_vd1_path_id = VFM_PATH_INVAILD;
		vd1_path_id = VFM_PATH_INVAILD;
	}
#endif
	if (!vd_layer[1].global_output) {
		cur_vd2_path_id = VFM_PATH_INVAILD;
		vd2_path_id = VFM_PATH_INVAILD;
	}

	vd_layer[0].force_switch_mode = force_switch_vf_mode;
	vd_layer[1].force_switch_mode = force_switch_vf_mode;

	if ((cur_vd1_path_id != vd1_path_id ||
	     cur_vd2_path_id != vd2_path_id) &&
	    (debug_flag & DEBUG_FLAG_PRINT_PATH_SWITCH)) {
		pr_info("VID: === before path switch ===\n");
		pr_info("VID: \tcur_path_id: %d, %d;\nVID: \tnew_path_id: %d, %d;\nVID: \ttoggle:%p, %p, %p %p\nVID: \tcur:%p, %p, %p, %p;\n",
			cur_vd1_path_id, cur_vd2_path_id,
			vd1_path_id, vd2_path_id,
			path0_new_frame, path1_new_frame,
			path2_new_frame, path3_new_frame,
			cur_dispbuf, cur_pipbuf,
			gvideo_recv[0] ? gvideo_recv[0]->cur_buf : NULL,
			gvideo_recv[1] ? gvideo_recv[1]->cur_buf : NULL);
		pr_info("VID: \tdispbuf:%p, %p; \tvf_ext:%p, %p;\nVID: \tlocal:%p, %p, %p, %p\n",
			vd_layer[0].dispbuf, vd_layer[1].dispbuf,
			vd_layer[0].vf_ext, vd_layer[1].vf_ext,
			&vf_local, &local_pip,
			gvideo_recv[0] ? &gvideo_recv[0]->local_buf : NULL,
			gvideo_recv[1] ? &gvideo_recv[1]->local_buf : NULL);
		pr_info("VID: \tblackout:%d %d force:%d;\n",
			blackout, blackout_pip, force_blackout);
	}

	if (debug_flag & DEBUG_FLAG_PRINT_DISBUF_PER_VSYNC)
		pr_info("VID: path id: %d, %d; new_frame:%p, %p, %p, %p cur:%p, %p, %p, %p; vd dispbuf:%p, %p; vf_ext:%p, %p; local:%p, %p, %p, %p\n",
			vd1_path_id, vd2_path_id,
			path0_new_frame, path1_new_frame,
			path2_new_frame, path3_new_frame,
			cur_dispbuf, cur_pipbuf,
			gvideo_recv[0] ? gvideo_recv[0]->cur_buf : NULL,
			gvideo_recv[1] ? gvideo_recv[1]->cur_buf : NULL,
			vd_layer[0].dispbuf, vd_layer[1].dispbuf,
			vd_layer[0].vf_ext, vd_layer[1].vf_ext,
			&vf_local, &local_pip,
			gvideo_recv[0] ? &gvideo_recv[0]->local_buf : NULL,
			gvideo_recv[1] ? &gvideo_recv[1]->local_buf : NULL);

	if ((vd_layer[0].dispbuf_mapping == &cur_dispbuf) &&
	    ((cur_dispbuf == &vf_local) ||
	     !cur_dispbuf) &&
	    (vd_layer[0].dispbuf != cur_dispbuf))
		vd_layer[0].dispbuf = cur_dispbuf;

	if ((vd_layer[0].dispbuf_mapping == &cur_pipbuf) &&
	    ((cur_pipbuf == &local_pip) ||
	     !cur_pipbuf) &&
	    (vd_layer[0].dispbuf != cur_pipbuf))
		vd_layer[0].dispbuf = cur_pipbuf;

	if (gvideo_recv[0] &&
	    (vd_layer[0].dispbuf_mapping == &gvideo_recv[0]->cur_buf) &&
	    ((gvideo_recv[0]->cur_buf == &gvideo_recv[0]->local_buf) ||
	     !gvideo_recv[0]->cur_buf) &&
	    (vd_layer[0].dispbuf != gvideo_recv[0]->cur_buf))
		vd_layer[0].dispbuf = gvideo_recv[0]->cur_buf;

	if (gvideo_recv[1] &&
	    (vd_layer[0].dispbuf_mapping == &gvideo_recv[1]->cur_buf) &&
	    ((gvideo_recv[1]->cur_buf == &gvideo_recv[1]->local_buf) ||
	     !gvideo_recv[1]->cur_buf) &&
	    (vd_layer[0].dispbuf != gvideo_recv[1]->cur_buf))
		vd_layer[0].dispbuf = gvideo_recv[1]->cur_buf;

	if (vd_layer[0].switch_vf &&
	    vd_layer[0].dispbuf &&
	    vd_layer[0].dispbuf->vf_ext)
		vd_layer[0].vf_ext =
			(struct vframe_s *)vd_layer[0].dispbuf->vf_ext;
	else
		vd_layer[0].vf_ext = NULL;

	/* vd1 config */
	if (gvideo_recv[0] &&
	    (gvideo_recv[0]->path_id == vd1_path_id)) {
		/* video_render.0 display on VD1 */
		new_frame = path2_new_frame;
		if (!new_frame) {
			if (!gvideo_recv[0]->cur_buf) {
				/* video_render.0 no frame in display */
				if (cur_vd1_path_id != vd1_path_id)
					safe_switch_videolayer(0, false, true);
				vd_layer[0].dispbuf = NULL;
			} else if (gvideo_recv[0]->cur_buf ==
				&gvideo_recv[0]->local_buf) {
				/* video_render.0 keep frame */
				vd_layer[0].dispbuf = gvideo_recv[0]->cur_buf;
			} else if (vd_layer[0].dispbuf
				!= gvideo_recv[0]->cur_buf) {
				/* video_render.0 has frame in display */
				new_frame = gvideo_recv[0]->cur_buf;
			}
		}
		if (new_frame || gvideo_recv[0]->cur_buf)
			vd_layer[0].dispbuf_mapping = &gvideo_recv[0]->cur_buf;
		cur_blackout = 1;
	} else if (gvideo_recv[1] &&
	    (gvideo_recv[1]->path_id == vd1_path_id)) {
		/* video_render.1 display on VD1 */
		new_frame = path3_new_frame;
		if (!new_frame) {
			if (!gvideo_recv[1]->cur_buf) {
				/* video_render.1 no frame in display */
				if (cur_vd1_path_id != vd1_path_id)
					safe_switch_videolayer(0, false, true);
				vd_layer[0].dispbuf = NULL;
			} else if (gvideo_recv[1]->cur_buf ==
				&gvideo_recv[1]->local_buf) {
				/* video_render.1 keep frame */
				vd_layer[0].dispbuf = gvideo_recv[1]->cur_buf;
			} else if (vd_layer[0].dispbuf
				!= gvideo_recv[1]->cur_buf) {
				/* video_render.1 has frame in display */
				new_frame = gvideo_recv[1]->cur_buf;
			}
		}
		if (new_frame || gvideo_recv[1]->cur_buf)
			vd_layer[0].dispbuf_mapping = &gvideo_recv[1]->cur_buf;
		cur_blackout = 1;
	} else if (vd1_path_id == VFM_PATH_PIP) {
		/* pip display on VD1 */
		new_frame = path1_new_frame;
		if (!new_frame) {
			if (!cur_pipbuf) {
				/* pip no frame in display */
				if (cur_vd1_path_id != vd1_path_id)
					safe_switch_videolayer(0, false, true);
				vd_layer[0].dispbuf = NULL;
			} else if (cur_pipbuf == &local_pip) {
				/* pip keep frame */
				vd_layer[0].dispbuf = cur_pipbuf;
			} else if (vd_layer[0].dispbuf
				!= cur_pipbuf) {
				/* pip has frame in display */
				new_frame = cur_pipbuf;
			}
		}
		if (new_frame || cur_pipbuf)
			vd_layer[0].dispbuf_mapping = &cur_pipbuf;
		cur_blackout = blackout_pip | force_blackout;
	} else if ((vd1_path_id != VFM_PATH_INVAILD) &&
		   (vd1_path_id != VFM_PATH_AUTO)) {
		/* priamry display on VD1 */
		new_frame = path0_new_frame;
		if (!new_frame) {
			if (!cur_dispbuf) {
				/* priamry no frame in display */
				if (cur_vd1_path_id != vd1_path_id)
					safe_switch_videolayer(0, false, true);
				vd_layer[0].dispbuf = NULL;
			} else if (cur_dispbuf == &vf_local) {
				/* priamry keep frame */
				vd_layer[0].dispbuf = cur_dispbuf;
			} else if (vd_layer[0].dispbuf
				!= cur_dispbuf) {
				/* primary has frame in display */
				new_frame = cur_dispbuf;
			}
		}
		if (new_frame || cur_dispbuf)
			vd_layer[0].dispbuf_mapping = &cur_dispbuf;
		cur_blackout = blackout | force_blackout;
	} else if (vd1_path_id == VFM_PATH_AUTO) {
		new_frame = NULL;
		if (path2_new_frame &&
			(path2_new_frame->flag & VFRAME_FLAG_FAKE_FRAME)) {
			new_frame = path2_new_frame;
			pr_info("vsync: auto path2 get a fake\n");
		}

		if (!new_frame) {
			if (cur_dispbuf == &vf_local)
				vd_layer[0].dispbuf = cur_dispbuf;

		if (gvideo_recv[0]->cur_buf &&
			gvideo_recv[0]->cur_buf->flag & VFRAME_FLAG_FAKE_FRAME)
			vd_layer[0].dispbuf = gvideo_recv[0]->cur_buf;

		}
		if (new_frame || cur_dispbuf)
			vd_layer[0].dispbuf_mapping = &cur_dispbuf;
		cur_blackout = blackout | force_blackout;
	} else {
		cur_blackout = 1;
	}

#ifdef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
	if (is_dolby_vision_enable() && vd_layer[0].global_output) {
		/* no new frame but path switched case, */
		if (new_frame && !is_local_vf(new_frame) &&
		    (!path0_new_frame || (new_frame != path0_new_frame)) &&
		    (!path1_new_frame || (new_frame != path1_new_frame)) &&
		    (!path2_new_frame || (new_frame != path2_new_frame)) &&
		    (!path3_new_frame || (new_frame != path3_new_frame)))
			dolby_vision_update_src_format(new_frame, 1);
		else if (!new_frame &&
			 vd_layer[0].dispbuf &&
			 !is_local_vf(vd_layer[0].dispbuf))
			dolby_vision_update_src_format(vd_layer[0].dispbuf, 0);
		/* pause and video off->on case */
	}
#endif
	/* vout mode detection under new non-tunnel mode */
	if (vd_layer[0].dispbuf || vd_layer[1].dispbuf) {
		if (strcmp(old_vmode, new_vmode)) {
			vd_layer[0].property_changed = true;
			vd_layer[1].property_changed = true;
			pr_info("detect vout mode change!!!!!!!!!!!!\n");
			strcpy(old_vmode, new_vmode);
		}
	}

	if (!new_frame && vd_layer[0].dispbuf &&
	    is_local_vf(vd_layer[0].dispbuf)) {
		if (cur_blackout) {
			vd_layer[0].property_changed = false;
		} else if (!is_di_post_mode(vd_layer[0].dispbuf)) {
			if (vd_layer[0].switch_vf && vd_layer[0].vf_ext)
				vd_layer[0].vf_ext->canvas0Addr =
					get_layer_display_canvas(0);
			else
			vd_layer[0].dispbuf->canvas0Addr =
				get_layer_display_canvas(0);
		}
	}
	if (vd_layer[0].dispbuf &&
	    (vd_layer[0].dispbuf->flag & (VFRAME_FLAG_VIDEO_COMPOSER |
		VFRAME_FLAG_VIDEO_DRM)) &&
	    !(vd_layer[0].dispbuf->flag & VFRAME_FLAG_FAKE_FRAME) &&
	    !(debug_flag & DEBUG_FLAG_AXIS_NO_UPDATE)) {
		axis[0] = vd_layer[0].dispbuf->axis[0];
		axis[1] = vd_layer[0].dispbuf->axis[1];
		axis[2] = vd_layer[0].dispbuf->axis[2];
		axis[3] = vd_layer[0].dispbuf->axis[3];
		crop[0] = vd_layer[0].dispbuf->crop[0];
		crop[1] = vd_layer[0].dispbuf->crop[1];
		crop[2] = vd_layer[0].dispbuf->crop[2];
		crop[3] = vd_layer[0].dispbuf->crop[3];
		_set_video_window(&glayer_info[0], axis);
		_set_video_crop(&glayer_info[0], crop);
		set_alpha_scpxn(0, vd_layer[0].dispbuf->componser_info);
		glayer_info[0].zorder = vd_layer[0].dispbuf->zorder;
	}
	/* setting video display property in underflow mode */
	if (!new_frame &&
	    vd_layer[0].dispbuf &&
	    vd_layer[0].property_changed) {
		primary_swap_frame(vd_layer[0].dispbuf, __LINE__);
#ifdef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
		dvel_swap_frame(cur_dispbuf2);
#endif
	} else if (new_frame) {
		primary_swap_frame(new_frame, __LINE__);
#ifdef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
		dvel_swap_frame(cur_dispbuf2);
#endif
	}

	if (atomic_read(&axis_changed)) {
		video_prop_status |= VIDEO_PROP_CHANGE_AXIS;
		atomic_set(&axis_changed, 0);
	}

	if (vd1_path_id == VFM_PATH_AMVIDEO ||
	    vd1_path_id == VFM_PATH_DEF)
		vd_layer[0].keep_frame_id = 0;
	else if (vd1_path_id == VFM_PATH_PIP)
		vd_layer[0].keep_frame_id = 1;
	else
		vd_layer[0].keep_frame_id = 0xff;

#if defined(CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_VECM)
	if (new_frame)
		refresh_on_vs(new_frame, vd_layer[0].dispbuf);

	amvecm_on_vs(
		!is_local_vf(vd_layer[0].dispbuf)
		? vd_layer[0].dispbuf : NULL,
		new_frame,
		new_frame ? CSC_FLAG_TOGGLE_FRAME : 0,
		cur_frame_par ?
		cur_frame_par->supsc1_hori_ratio :
		0,
		cur_frame_par ?
		cur_frame_par->supsc1_vert_ratio :
		0,
		cur_frame_par ?
		cur_frame_par->spsc1_w_in :
		0,
		cur_frame_par ?
		cur_frame_par->spsc1_h_in :
		0,
		cur_frame_par ?
		cur_frame_par->cm_input_w :
		0,
		cur_frame_par ?
		cur_frame_par->cm_input_h :
		0,
		VD1_PATH);
#endif

	/* work around which dec/vdin don't call update src_fmt function */
	if (vd_layer[0].dispbuf && !is_local_vf(vd_layer[0].dispbuf)) {
		int new_src_fmt = -1;
		u32 src_map[] = {
			VFRAME_SIGNAL_FMT_INVALID,
			VFRAME_SIGNAL_FMT_HDR10,
			VFRAME_SIGNAL_FMT_HDR10PLUS,
			VFRAME_SIGNAL_FMT_DOVI,
			VFRAME_SIGNAL_FMT_HDR10PRIME,
			VFRAME_SIGNAL_FMT_HLG,
			VFRAME_SIGNAL_FMT_SDR,
			VFRAME_SIGNAL_FMT_MVC
		};

#if defined(CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_VECM)
#ifdef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
		if (is_dolby_vision_enable())
			new_src_fmt = get_dolby_vision_src_format();
		else
#endif
			new_src_fmt = get_cur_source_type(VD1_PATH);
#endif
		if (new_src_fmt > 0 && new_src_fmt < 8)
			fmt = src_map[new_src_fmt];
		else
			fmt = VFRAME_SIGNAL_FMT_INVALID;
		if (fmt != atomic_read(&cur_primary_src_fmt)) {
			/* atomic_set(&primary_src_fmt, fmt); */
			if (debug_flag & DEBUG_FLAG_TRACE_EVENT) {
				char *old_str = NULL, *new_str = NULL;
				enum vframe_signal_fmt_e old_fmt;

				old_fmt = atomic_read(&cur_primary_src_fmt);
				if (old_fmt != VFRAME_SIGNAL_FMT_INVALID)
					old_str = (char *)src_fmt_str[old_fmt];
				if (fmt != VFRAME_SIGNAL_FMT_INVALID)
					new_str = (char *)src_fmt_str[fmt];
				pr_info("VD1 src fmt changed: %s->%s. vf: %p, signal_type:0x%x\n",
					old_str ? old_str : "invalid",
					new_str ? new_str : "invalid",
					vd_layer[0].dispbuf,
					vd_layer[0].dispbuf->signal_type);
			}
			atomic_set(&cur_primary_src_fmt, fmt);
			atomic_set(&primary_src_fmt, fmt);
			video_prop_status |= VIDEO_PROP_CHANGE_FMT;
		}
	}

	if ((vd_layer[1].dispbuf_mapping == &cur_dispbuf) &&
	    ((cur_dispbuf == &vf_local) ||
	     !cur_dispbuf) &&
	    (vd_layer[1].dispbuf != cur_dispbuf))
		vd_layer[1].dispbuf = cur_dispbuf;

	if ((vd_layer[1].dispbuf_mapping == &cur_pipbuf) &&
	    ((cur_pipbuf == &local_pip) ||
	     !cur_pipbuf) &&
	    (vd_layer[1].dispbuf != cur_pipbuf))
		vd_layer[1].dispbuf = cur_pipbuf;

	if (gvideo_recv[0] &&
	    (vd_layer[1].dispbuf_mapping == &gvideo_recv[0]->cur_buf) &&
	    ((gvideo_recv[0]->cur_buf == &gvideo_recv[0]->local_buf) ||
	     !gvideo_recv[0]->cur_buf) &&
	    (vd_layer[1].dispbuf != gvideo_recv[0]->cur_buf))
		vd_layer[1].dispbuf = gvideo_recv[0]->cur_buf;

	if (gvideo_recv[1] &&
	    (vd_layer[1].dispbuf_mapping == &gvideo_recv[1]->cur_buf) &&
	    ((gvideo_recv[1]->cur_buf == &gvideo_recv[1]->local_buf) ||
	     !gvideo_recv[1]->cur_buf) &&
	    (vd_layer[1].dispbuf != gvideo_recv[1]->cur_buf))
		vd_layer[1].dispbuf = gvideo_recv[1]->cur_buf;

	if (vd_layer[1].switch_vf &&
	    vd_layer[1].dispbuf &&
	    vd_layer[1].dispbuf->vf_ext)
		vd_layer[1].vf_ext =
			(struct vframe_s *)vd_layer[1].dispbuf->vf_ext;
	else
		vd_layer[1].vf_ext = NULL;

	/* vd2 config */
	if (gvideo_recv[0] &&
	    (gvideo_recv[0]->path_id == vd2_path_id)) {
		/* video_render.0 display on VD2 */
		new_frame2 = path2_new_frame;
		if (!new_frame2) {
			if (!gvideo_recv[0]->cur_buf) {
				/* video_render.0 no frame in display */
				if (cur_vd2_path_id != vd2_path_id)
					safe_switch_videolayer(1, false, true);
				vd_layer[1].dispbuf = NULL;
			} else if (gvideo_recv[0]->cur_buf ==
				&gvideo_recv[0]->local_buf) {
				/* video_render.0 keep frame */
				vd_layer[1].dispbuf = gvideo_recv[0]->cur_buf;
			} else if (vd_layer[1].dispbuf
				!= gvideo_recv[0]->cur_buf) {
				/* video_render.0 has frame in display */
				new_frame2 = gvideo_recv[0]->cur_buf;
			}
		}
		if (new_frame2 || gvideo_recv[0]->cur_buf)
			vd_layer[1].dispbuf_mapping = &gvideo_recv[0]->cur_buf;
		cur_blackout = 1;
	} else if (gvideo_recv[1] &&
	    (gvideo_recv[1]->path_id == vd2_path_id)) {
		/* video_render.1 display on VD1 */
		new_frame2 = path3_new_frame;
		if (!new_frame2) {
			if (!gvideo_recv[1]->cur_buf) {
				/* video_render.1 no frame in display */
				if (cur_vd2_path_id != vd2_path_id)
					safe_switch_videolayer(1, false, true);
				vd_layer[1].dispbuf = NULL;
			} else if (gvideo_recv[1]->cur_buf ==
				&gvideo_recv[1]->local_buf) {
				/* video_render.1 keep frame */
				vd_layer[1].dispbuf = gvideo_recv[1]->cur_buf;
			} else if (vd_layer[1].dispbuf
				!= gvideo_recv[1]->cur_buf) {
				/* video_render.1 has frame in display */
				new_frame2 = gvideo_recv[1]->cur_buf;
			}
		}
		if (new_frame2 || gvideo_recv[1]->cur_buf)
			vd_layer[1].dispbuf_mapping = &gvideo_recv[1]->cur_buf;
		cur_blackout = 1;
	} else if (vd2_path_id == VFM_PATH_AMVIDEO) {
		/* priamry display in VD2 */
		new_frame2 = path0_new_frame;
		if (!new_frame2) {
			if (!cur_dispbuf) {
				/* primary no frame in display */
				if (cur_vd2_path_id != vd2_path_id)
					safe_switch_videolayer(1, false, true);
				vd_layer[1].dispbuf = NULL;
			} else if (cur_dispbuf == &vf_local) {
				/* priamry keep frame */
				vd_layer[1].dispbuf = cur_dispbuf;
			} else if (vd_layer[1].dispbuf
				!= cur_dispbuf) {
				new_frame2 = cur_dispbuf;
			}
		}
		if (new_frame2 || cur_dispbuf)
			vd_layer[1].dispbuf_mapping = &cur_dispbuf;
		cur_blackout = blackout | force_blackout;
	} else if (vd2_path_id != VFM_PATH_INVAILD) {
		/* pip display in VD2 */
		new_frame2 = path1_new_frame;
		if (!new_frame2) {
			if (!cur_pipbuf) {
				/* pip no display frame */
				if (cur_vd2_path_id != vd2_path_id)
					safe_switch_videolayer(1, false, true);
				vd_layer[1].dispbuf = NULL;
			} else if (cur_pipbuf == &local_pip) {
				/* pip keep frame */
				vd_layer[1].dispbuf = cur_pipbuf;
			} else if (vd_layer[1].dispbuf
				!= cur_pipbuf) {
				new_frame2 = cur_pipbuf;
			}
		}
		if (new_frame2 || cur_pipbuf)
			vd_layer[1].dispbuf_mapping = &cur_pipbuf;
		cur_blackout = blackout_pip | force_blackout;
	} else {
		cur_blackout = 1;
	}

	if (!new_frame2 && vd_layer[1].dispbuf &&
	    is_local_vf(vd_layer[1].dispbuf)) {
		if (cur_blackout) {
			vd_layer[1].property_changed = false;
		} else if (vd_layer[1].dispbuf) {
			if (vd_layer[1].switch_vf && vd_layer[1].vf_ext)
				vd_layer[1].vf_ext->canvas0Addr =
					get_layer_display_canvas(1);
			else
				vd_layer[1].dispbuf->canvas0Addr =
					get_layer_display_canvas(1);
		}
	}

	if (vd_layer[1].dispbuf &&
	    (vd_layer[1].dispbuf->flag & (VFRAME_FLAG_VIDEO_COMPOSER |
		VFRAME_FLAG_VIDEO_DRM)) &&
	    !(debug_flag & DEBUG_FLAG_AXIS_NO_UPDATE)) {
		axis[0] = vd_layer[1].dispbuf->axis[0];
		axis[1] = vd_layer[1].dispbuf->axis[1];
		axis[2] = vd_layer[1].dispbuf->axis[2];
		axis[3] = vd_layer[1].dispbuf->axis[3];
		crop[0] = vd_layer[1].dispbuf->crop[0];
		crop[1] = vd_layer[1].dispbuf->crop[1];
		crop[2] = vd_layer[1].dispbuf->crop[2];
		crop[3] = vd_layer[1].dispbuf->crop[3];
		_set_video_window(&glayer_info[1], axis);
		_set_video_crop(&glayer_info[1], crop);
		set_alpha_scpxn(1, vd_layer[1].dispbuf->componser_info);
		glayer_info[1].zorder = vd_layer[1].dispbuf->zorder;
	}

	/* setting video display property in underflow mode */
	if (!new_frame2 &&
	    vd_layer[1].dispbuf &&
	    vd_layer[1].property_changed) {
		pip_swap_frame(vd_layer[1].dispbuf);
		need_disable_vd2 = false;
	} else if (new_frame2) {
		pip_swap_frame(new_frame2);
		need_disable_vd2 = false;
	}

	if ((vd2_path_id == VFM_PATH_PIP) ||
	    (vd2_path_id == VFM_PATH_DEF))
		vd_layer[1].keep_frame_id = 1;
	else if (vd2_path_id == VFM_PATH_AMVIDEO)
		vd_layer[1].keep_frame_id = 0;
	else
		vd_layer[1].keep_frame_id = 0xff;

#if defined(CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_VECM)
	amvecm_on_vs(
		!is_local_vf(vd_layer[1].dispbuf)
		? vd_layer[1].dispbuf : NULL,
		new_frame2,
		new_frame2 ? CSC_FLAG_TOGGLE_FRAME : 0,
		curpip_frame_par ?
		curpip_frame_par->supsc1_hori_ratio :
		0,
		curpip_frame_par ?
		curpip_frame_par->supsc1_vert_ratio :
		0,
		curpip_frame_par ?
		curpip_frame_par->spsc1_w_in :
		0,
		curpip_frame_par ?
		curpip_frame_par->spsc1_h_in :
		0,
		curpip_frame_par ?
		curpip_frame_par->cm_input_w :
		0,
		curpip_frame_par ?
		curpip_frame_par->cm_input_h :
		0,
		VD2_PATH);
#endif

	if (need_disable_vd2) {
		safe_switch_videolayer(1, false, true);
#ifdef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
		/* reset dvel statue when disable vd2 */
		dvel_status = false;
#endif
	}

	/* filter setting management */
	frame_par_di_set = primary_render_frame(&vd_layer[0]);
	pip_render_frame(&vd_layer[1]);
	video_secure_set();

	if (vd_layer[0].dispbuf &&
		(vd_layer[0].dispbuf->flag & VFRAME_FLAG_FAKE_FRAME)) {
		if ((vd_layer[0].force_black &&
			!(debug_flag & DEBUG_FLAG_NO_CLIP_SETTING)) ||
			!vd_layer[0].force_black) {
			if (vd_layer[0].dispbuf->type & VIDTYPE_RGB_444) {
				/* RGB */
				vd_layer[0].clip_setting.clip_max =
					(0x0 << 20) | (0x0 << 10) | 0;
				vd_layer[0].clip_setting.clip_min =
					vd_layer[0].clip_setting.clip_max;
			} else {
				/* YUV */
				vd_layer[0].clip_setting.clip_max =
					(0x0 << 20) | (0x200 << 10) | 0x200;
				vd_layer[0].clip_setting.clip_min =
					vd_layer[0].clip_setting.clip_max;
			}
			vd_layer[0].clip_setting.clip_done = false;
		}
		if (!vd_layer[0].force_black) {
			pr_debug("vsync: vd1 force black\n");
			vd_layer[0].force_black = true;
		}
	} else if (vd_layer[0].force_black) {
		pr_debug("vsync: vd1 black to normal\n");
		vd_layer[0].clip_setting.clip_max =
			(0x3ff << 20) | (0x3ff << 10) | 0x3ff;
		vd_layer[0].clip_setting.clip_min = 0;
		vd_layer[0].clip_setting.clip_done = false;
		vd_layer[0].force_black = false;
	}

	if (vd_layer[1].dispbuf &&
	    (vd_layer[1].dispbuf->flag & VFRAME_FLAG_FAKE_FRAME))
		safe_switch_videolayer(1, false, true);

	if ((cur_vd1_path_id != vd1_path_id ||
	     cur_vd2_path_id != vd2_path_id) &&
	    (debug_flag & DEBUG_FLAG_PRINT_PATH_SWITCH)) {
		pr_info("VID: === After path switch ===\n");
		pr_info("VID: \tpath_id: %d, %d;\nVID: \ttoggle:%p, %p, %p %p\nVID: \tcur:%p, %p, %p, %p;\n",
			vd1_path_id, vd2_path_id,
			path0_new_frame, path1_new_frame,
			path2_new_frame, path3_new_frame,
			cur_dispbuf, cur_pipbuf,
			gvideo_recv[0] ? gvideo_recv[0]->cur_buf : NULL,
			gvideo_recv[1] ? gvideo_recv[1]->cur_buf : NULL);
		pr_info("VID: \tdispbuf:%p, %p; \tvf_ext:%p, %p;\nVID: \tlocal:%p, %p, %p, %p\n",
			vd_layer[0].dispbuf, vd_layer[1].dispbuf,
			vd_layer[0].vf_ext, vd_layer[1].vf_ext,
			&vf_local, &local_pip,
			gvideo_recv[0] ? &gvideo_recv[0]->local_buf : NULL,
			gvideo_recv[1] ? &gvideo_recv[1]->local_buf : NULL);
		pr_info("VID: \tblackout:%d %d force:%d;\n",
			blackout, blackout_pip, force_blackout);
	}

	if (vd_layer[0].dispbuf &&
	    (vd_layer[0].dispbuf->type & VIDTYPE_MVC))
		vd_layer[0].enable_3d_mode = mode_3d_mvc_enable;
	else if (process_3d_type)
		vd_layer[0].enable_3d_mode = mode_3d_enable;
	else
		vd_layer[0].enable_3d_mode = mode_3d_disable;

	/* all frames has been renderred, so reset new frame flag */
	vd_layer[0].new_frame = false;
	vd_layer[1].new_frame = false;

	if (cur_dispbuf && cur_dispbuf->process_fun &&
	    (vd1_path_id == VFM_PATH_AMVIDEO ||
	     vd1_path_id == VFM_PATH_DEF))
		di_post_vf = cur_dispbuf;
	else if (vd_layer[0].dispbuf &&
		 vd_layer[0].dispbuf->process_fun &&
		 is_di_post_mode(vd_layer[0].dispbuf))
		di_post_vf = vd_layer[0].dispbuf;

	if (vd_layer[0].do_switch)
		di_post_vf = NULL;
	if (di_post_vf) {
		/* for new deinterlace driver */
#ifdef CONFIG_AMLOGIC_MEDIA_VSYNC_RDMA
		if (debug_flag & DEBUG_FLAG_PRINT_RDMA) {
			if (enable_rdma_log_count > 0)
				pr_info("call process_fun\n");
		}
#endif
		if (cur_frame_par)
			di_post_vf->process_fun(
				di_post_vf->private_data,
				vd_layer[0].start_x_lines |
				(cur_frame_par->vscale_skip_count <<
				24) | (frame_par_di_set << 16),
				vd_layer[0].end_x_lines,
				vd_layer[0].start_y_lines,
				vd_layer[0].end_y_lines,
				di_post_vf);
		di_post_process_done = true;
	}

	if (cur_dispbuf) {
		pq_process_debug[0] = ai_pq_value;
		pq_process_debug[1] = ai_pq_disable;
		pq_process_debug[2] = ai_pq_debug;
		pq_process_debug[3] = ai_pq_policy;
		vf_pq_process(cur_dispbuf, vpp_scenes, pq_process_debug);
		if (ai_pq_debug > 0x10) {
			ai_pq_debug--;
			if (ai_pq_debug == 0x10)
				ai_pq_debug = 0;
		}
		memcpy(nn_scenes_value, cur_dispbuf->nn_value,
		       sizeof(nn_scenes_value));
	}
exit:
#ifdef CONFIG_AMLOGIC_MEDIA_DEINTERLACE
	if (legacy_vpp &&
	    !di_post_process_done &&
	    is_di_post_on())
		DI_POST_UPDATE_MC();
#endif
#if defined(PTS_LOGGING) || defined(PTS_TRACE_DEBUG)
	pts_trace++;
#endif

	vd_clip_setting(0, &vd_layer[0].clip_setting);
	vd_clip_setting(1, &vd_layer[1].clip_setting);

	vpp_blend_update(vinfo);

	if (gvideo_recv[0])
		gvideo_recv[0]->func->late_proc(gvideo_recv[0]);
	if (gvideo_recv[1])
		gvideo_recv[1]->func->late_proc(gvideo_recv[1]);

#ifdef CONFIG_AMLOGIC_MEDIA_VSYNC_RDMA
	cur_rdma_buf = cur_dispbuf;
	pip_rdma_buf = cur_pipbuf;
RUN_FIRST_RDMA:
	/* vsync_rdma_config(); */
	vsync_rdma_process();

	trace_performance(enc_line_start, &start, &end1, &end2, &end3, &end4);

	if (debug_flag & DEBUG_FLAG_PRINT_RDMA) {
		if (enable_rdma_log_count == 0)
			enable_rdma_log(0);
	}
	rdma_enable_pre = is_vsync_rdma_enable();
#endif

	if (timer_count > 50) {
		timer_count = 0;
		video_notify_flag |= VIDEO_NOTIFY_FRAME_WAIT;
#ifdef CONFIG_AMLOGIC_POST_PROCESS_MANAGER_PPSCALER
		if ((video_scaler_mode) && (scaler_pos_changed)) {
			video_notify_flag |= VIDEO_NOTIFY_POS_CHANGED;
			scaler_pos_changed = 0;
		} else {
			scaler_pos_changed = 0;
			video_notify_flag &= ~VIDEO_NOTIFY_POS_CHANGED;
		}
#endif
	}

	enc_line = get_cur_enc_line();
	if (enc_line > vsync_exit_line_max)
		vsync_exit_line_max = enc_line;
	if (video_suspend)
		video_suspend_cycle++;
#ifdef FIQ_VSYNC
	if (video_notify_flag)
		fiq_bridge_pulse_trigger(&vsync_fiq_bridge);
#else
	if (video_notify_flag)
		vsync_notify();

	/* if prop_change not zero, event will be delayed to next vsync */
	if (video_prop_status &&
	    !atomic_read(&video_prop_change)) {
		if (debug_flag & DEBUG_FLAG_TRACE_EVENT)
			pr_info("VD1 send event, changed status: 0x%x\n",
				video_prop_status);
		atomic_set(&video_prop_change, video_prop_status);
		video_prop_status = VIDEO_PROP_CHANGE_NONE;
		wake_up_interruptible(&amvideo_prop_change_wait);
	}
	vpu_work_process();
	vpp_crc_result = vpp_crc_check(vpp_crc_en);

	cur_vd1_path_id = vd1_path_id;
	cur_vd2_path_id = vd2_path_id;

	if (debug_flag & DEBUG_FLAG_GET_COUNT)
		pr_info("count=%d pip=%d\n", get_count, get_count_pip);

	return IRQ_HANDLED;
#endif

}


#ifdef FIQ_VSYNC
void vsync_fisr(void)
{
	atomic_set(&video_inirq_flag, 1);
	vsync_fisr_in();
	atomic_set(&video_inirq_flag, 0);
}
#else
static irqreturn_t vsync_isr(int irq, void *dev_id)
{
	irqreturn_t ret;

	atomic_set(&video_inirq_flag, 1);
	ret = vsync_isr_in(irq, dev_id);
	atomic_set(&video_inirq_flag, 0);
	return ret;
}

static irqreturn_t vsync_isr_viu2(int irq, void *dev_id)
{
	return IRQ_HANDLED;
}
#endif


/*********************************************************
 * FIQ Routines
 *********************************************************/

static void vsync_fiq_up(void)
{
#ifdef FIQ_VSYNC
	request_fiq(INT_VIU_VSYNC, &vsync_fisr);
#else
	int r;
	r = request_irq(video_vsync, &vsync_isr,
		IRQF_SHARED, "vsync", (void *)video_dev_id);
	if (amvideo_meson_dev.cpu_type >= MESON_CPU_MAJOR_ID_SC2_)
		r = request_irq(video_vsync_viu2, &vsync_isr_viu2,
				IRQF_SHARED, "vsync_viu2",
				(void *)video_dev_id);

#ifdef CONFIG_MESON_TRUSTZONE
	if (num_online_cpus() > 1)
		irq_set_affinity(INT_VIU_VSYNC, cpumask_of(1));
#endif
#endif
}

static void vsync_fiq_down(void)
{
#ifdef FIQ_VSYNC
	free_fiq(INT_VIU_VSYNC, &vsync_fisr);
#else
	free_irq(video_vsync, (void *)video_dev_id);
	if (amvideo_meson_dev.cpu_type >= MESON_CPU_MAJOR_ID_SC2_)
		free_irq(video_vsync_viu2, (void *)video_dev_id);
#endif
}

int get_curren_frame_para(int *top, int *left, int *bottom, int *right)
{
	if (!cur_frame_par)
		return -1;
	*top = cur_frame_par->VPP_vd_start_lines_;
	*left = cur_frame_par->VPP_hd_start_lines_;
	*bottom = cur_frame_par->VPP_vd_end_lines_;
	*right = cur_frame_par->VPP_hd_end_lines_;
	return 0;
}

int get_current_vscale_skip_count(struct vframe_s *vf)
{
	int ret = 0;
	static struct vpp_frame_par_s frame_par;

	vpp_set_filters(
		&glayer_info[0],
		vf, &frame_par, vinfo,
		(is_dolby_vision_on() &&
		is_dolby_vision_stb_mode() &&
		for_dolby_vision_certification()),
		OP_FORCE_NOT_SWITCH_VF);
	ret = frame_par.vscale_skip_count;
	if (cur_frame_par && (process_3d_type & MODE_3D_ENABLE))
		ret |= (cur_frame_par->vpp_3d_mode<<8);
	return ret;
}

int query_video_status(int type, int *value)
{
	if (value == NULL)
		return -1;
	switch (type) {
	case 0:
		*value = trickmode_fffb;
		break;
	case 1:
		*value = trickmode_i;
		break;
	default:
		break;
	}
	return 0;
}
EXPORT_SYMBOL(query_video_status);

static void release_di_buffer(int inst)
{
	int i;

	for (i = 0; i < recycle_cnt[inst]; i++) {
		if (recycle_buf[inst][i] &&
		    (recycle_buf[inst][i]->type & VIDTYPE_DI_PW) &&
		    (recycle_buf[inst][i]->flag & VFRAME_FLAG_DI_PW_VFM)) {
			di_release_count++;
			dim_post_keep_cmd_release2(recycle_buf[inst][i]);
		}
		recycle_buf[inst][i] = NULL;
	}
	recycle_cnt[inst] = 0;
}

static void video_vf_unreg_provider(void)
{
	ulong flags;
	bool layer1_used = false;
	bool layer2_used = false;
	struct vframe_s *el_vf = NULL;
	int keeped = 0, ret = 0;
#ifdef CONFIG_AMLOGIC_MEDIA_VSYNC_RDMA
	int i;
#endif

	new_frame_count = 0;
	first_frame_toggled = 0;
	videopeek = 0;
	nopostvideostart = false;
	hold_property_changed = 0;

	atomic_inc(&video_unreg_flag);
	while (atomic_read(&video_inirq_flag) > 0)
		schedule();
	memset(&video_frame_detect, 0,
				sizeof(struct video_frame_detect_s));
	frame_detect_drop_count = 0;
	frame_detect_receive_count = 0;
	spin_lock_irqsave(&lock, flags);
	ret = update_amvideo_recycle_buffer();
	if (ret == -EAGAIN) {
	/* The currently displayed vf is not added to the queue
	 * that needs to be released. You need to release the vf
	 * data in the release queue before adding the currently
	 * displayed vf to the release queue.
	 */
		release_di_buffer(0);
		update_amvideo_recycle_buffer();
	}
#ifdef CONFIG_AMLOGIC_MEDIA_VSYNC_RDMA
	dispbuf_to_put_num = 0;
	for (i = 0; i < DISPBUF_TO_PUT_MAX; i++)
		dispbuf_to_put[i] = NULL;
	cur_rdma_buf = NULL;
#endif
	if (cur_dispbuf) {
		if ((cur_dispbuf->vf_ext) &&
		    (cur_dispbuf->type & VIDTYPE_DI_PW)) {
			struct vframe_s *tmp =
				(struct vframe_s *)cur_dispbuf->vf_ext;

			memcpy(&tmp->pic_mode, &cur_dispbuf->pic_mode,
				sizeof(struct vframe_pic_mode_s));
			vf_local_ext = *tmp;
			vf_local = *cur_dispbuf;
			vf_local.vf_ext = (void *)&vf_local_ext;
		} else {
			vf_local = *cur_dispbuf;
		}
		cur_dispbuf = &vf_local;
		cur_dispbuf->video_angle = 0;
	}

	if (cur_dispbuf2)
		need_disable_vd2 = true;
	if (is_dolby_vision_enable()) {
		if (cur_dispbuf2 == &vf_local2)
			cur_dispbuf2 = NULL;
		else if (cur_dispbuf2 != NULL) {
			vf_local2 = *cur_dispbuf2;
			el_vf = &vf_local2;
		}
		cur_dispbuf2 = NULL;
	}
	if (trickmode_fffb) {
		atomic_set(&trickmode_framedone, 0);
		to_notify_trick_wait = false;
	}

	vsync_pts_100 = 0;
	vsync_pts_112 = 0;
	vsync_pts_125 = 0;
	vsync_freerun = 0;
	vsync_pts_align = 0;
	vsync_pts_aligned = false;

	if (pip_loop) {
#ifdef CONFIG_AMLOGIC_MEDIA_VSYNC_RDMA
		pipbuf_to_put_num = 0;
		for (i = 0; i < DISPBUF_TO_PUT_MAX; i++)
			pipbuf_to_put[i] = NULL;
		pip_rdma_buf = NULL;
#endif
		if (cur_pipbuf) {
			local_pip = *cur_pipbuf;
			cur_pipbuf = &local_pip;
			cur_pipbuf->video_angle = 0;
		}
		pip_frame_count = 0;
	}
	spin_unlock_irqrestore(&lock, flags);

	if (vd_layer[0].dispbuf_mapping
		== &cur_dispbuf)
		layer1_used = true;
	if (vd_layer[1].dispbuf_mapping
		== &cur_dispbuf)
		layer2_used = true;

	if (layer1_used || !vd_layer[0].dispbuf_mapping)
		atomic_set(&primary_src_fmt, VFRAME_SIGNAL_FMT_INVALID);

	if (pip_loop) {
		vd_layer[1].disable_video =
			VIDEO_DISABLE_FORNEXT;
		safe_switch_videolayer(1, false, false);
	}

	if (!layer1_used && !layer2_used)
		cur_dispbuf = NULL;

	if (blackout | force_blackout) {
		if (layer1_used)
			safe_switch_videolayer(
				0, false, false);
		if (layer2_used)
			safe_switch_videolayer(
				1, false, false);
		try_free_keep_video(1);
	}
	if (cur_dispbuf)
		keeped = vf_keep_current(
			cur_dispbuf, el_vf);

	pr_info("video_vf_unreg_provider: vd1 used: %s, vd2 used: %s, keep_ret:%d, black_out:%d, cur_dispbuf:%p\n",
		layer1_used ? "true" : "false",
		layer2_used ? "true" : "false",
		keeped, blackout | force_blackout,
		cur_dispbuf);

	if (!nopostvideostop && (hdmi_in_onvideo == 0) && (video_start_post)) {
		tsync_avevent(VIDEO_STOP, 0);
		video_start_post = false;
	}

	if (keeped <= 0) {/*keep failed.*/
		if (keeped < 0)
			pr_info("keep frame failed, disable video now.\n");
		else
			pr_info("keep frame skip, disable video again.\n");
		if (layer1_used)
			safe_switch_videolayer(
				0, false, false);
		if (layer2_used)
			safe_switch_videolayer(
				1, false, false);
		try_free_keep_video(1);
	}

	atomic_dec(&video_unreg_flag);
	pr_info("VD1 AFBC 0x%x.\n", READ_VCBUS_REG(AFBC_ENABLE));
	enable_video_discontinue_report = 1;
	show_first_picture = false;
	show_first_frame_nosync = false;
	vframe_walk_delay = 0;
	time_setomxpts = 0;
	time_setomxpts_last = 0;
	vdin_err_crc_cnt = 0;

#ifdef PTS_LOGGING
	{
		int pattern;
		/* Print what we have right now*/
		if (pts_pattern_detected >= PTS_32_PATTERN &&
			pts_pattern_detected < PTS_MAX_NUM_PATTERNS) {
			pr_info("pattern detected = %d, pts_enter_pattern_cnt =%d, pts_exit_pattern_cnt =%d",
				pts_pattern_detected,
				pts_pattern_enter_cnt[pts_pattern_detected],
				pts_pattern_exit_cnt[pts_pattern_detected]);
		}
		/* Reset all metrics now*/
		for (pattern = 0; pattern < PTS_MAX_NUM_PATTERNS; pattern++) {
			pts_pattern[pattern] = 0;
			pts_pattern_exit_cnt[pattern] = 0;
			pts_pattern_enter_cnt[pattern] = 0;
		}
		/* Reset 4:1 data*/
		memset(&pts_41_pattern_sink[0], 0, PTS_41_PATTERN_SINK_MAX);
		pts_pattern_detected = -1;
		pre_pts_trace = 0;
		pts_escape_vsync = 0;
	}
#endif
}

static void video_vf_light_unreg_provider(int need_keep_frame)
{
	ulong flags;
	int ret = 0;
#ifdef CONFIG_AMLOGIC_MEDIA_VSYNC_RDMA
	int i;
#endif

	atomic_inc(&video_unreg_flag);
	while (atomic_read(&video_inirq_flag) > 0)
		schedule();

	spin_lock_irqsave(&lock, flags);
	ret = update_amvideo_recycle_buffer();
	if (ret == -EAGAIN) {
	/* The currently displayed vf is not added to the queue
	 * that needs to be released. You need to release the vf
	 * data in the release queue before adding the currently
	 * displayed vf to the release queue.
	 */
		release_di_buffer(0);
		update_amvideo_recycle_buffer();
	}
#ifdef CONFIG_AMLOGIC_MEDIA_VSYNC_RDMA
	dispbuf_to_put_num = 0;
	for (i = 0; i < DISPBUF_TO_PUT_MAX; i++)
		dispbuf_to_put[i] = NULL;
	cur_rdma_buf = NULL;
#endif

	if (cur_dispbuf) {
		if ((cur_dispbuf->vf_ext) &&
		    (cur_dispbuf->type & VIDTYPE_DI_PW)) {
			struct vframe_s *tmp =
				(struct vframe_s *)cur_dispbuf->vf_ext;

			memcpy(&tmp->pic_mode, &cur_dispbuf->pic_mode,
				sizeof(struct vframe_pic_mode_s));
			vf_local_ext = *tmp;
			vf_local = *cur_dispbuf;
			vf_local.vf_ext = (void *)&vf_local_ext;
		} else {
			vf_local = *cur_dispbuf;
		}
		cur_dispbuf = &vf_local;
	}
	spin_unlock_irqrestore(&lock, flags);

	if (need_keep_frame) {
		/* keep the last toggled frame*/
		if (cur_dispbuf) {
			unsigned int result;

			result = vf_keep_current(
				cur_dispbuf, NULL);
			if (result == 0)
				pr_info("%s: keep cur_disbuf failed\n",
					__func__);
		}
	}
	atomic_dec(&video_unreg_flag);
}

static int  get_display_info(void *data)
{
	s32 w, h, x, y;
	struct vdisplay_info_s  *info_para = (struct vdisplay_info_s *)data;
	const struct vinfo_s *info = get_current_vinfo();
	struct disp_info_s *layer = &glayer_info[0];

	if ((!cur_frame_par) || (!info))
		return -1;

	x = layer->layer_left;
	y = layer->layer_top;
	w = layer->layer_width;
	h = layer->layer_height;

	if ((w == 0) || (w  > info->width))
		w =  info->width;
	if ((h == 0) || (h  > info->height))
		h =  info->height;

	info_para->frame_hd_start_lines_ = cur_frame_par->VPP_hd_start_lines_;
	info_para->frame_hd_end_lines_ = cur_frame_par->VPP_hd_end_lines_;
	info_para->frame_vd_start_lines_ = cur_frame_par->VPP_vd_start_lines_;
	info_para->frame_vd_end_lines_ = cur_frame_par->VPP_vd_end_lines_;
	info_para->display_hsc_startp = cur_frame_par->VPP_hsc_startp - x;
	info_para->display_hsc_endp =
	cur_frame_par->VPP_hsc_endp + (info->width - x - w);
	info_para->display_vsc_startp = cur_frame_par->VPP_vsc_startp - y;
	info_para->display_vsc_endp =
	cur_frame_par->VPP_vsc_endp + (info->height - y - h);
	info_para->screen_vd_h_start_ =
	cur_frame_par->VPP_post_blend_vd_h_start_;
	info_para->screen_vd_h_end_ =
	cur_frame_par->VPP_post_blend_vd_h_end_;
	info_para->screen_vd_v_start_ =
	cur_frame_par->VPP_post_blend_vd_v_start_;
	info_para->screen_vd_v_end_ = cur_frame_par->VPP_post_blend_vd_v_end_;

	return 0;
}

static struct vframe_s *get_dispbuf(u8 layer_id)
{
	struct vframe_s *dispbuf = NULL;

	if (layer_id >= MAX_VD_LAYERS)
		return NULL;

	switch (glayer_info[layer_id].display_path_id) {
	case VFM_PATH_DEF:
		if ((layer_id == 0) && cur_dispbuf)
			dispbuf = cur_dispbuf;
		else if ((layer_id == 1) && cur_pipbuf)
			dispbuf = cur_pipbuf;
		break;
	case VFM_PATH_AMVIDEO:
		if (cur_dispbuf)
			dispbuf = cur_dispbuf;
		break;
	case VFM_PATH_PIP:
		if (cur_pipbuf)
			dispbuf = cur_pipbuf;
		break;
	case VFM_PATH_VIDEO_RENDER0:
		if (gvideo_recv[0] &&
		    gvideo_recv[0]->cur_buf)
			dispbuf = gvideo_recv[0]->cur_buf;
		break;
	case VFM_PATH_VIDEO_RENDER1:
		if (gvideo_recv[1] &&
		    gvideo_recv[1]->cur_buf)
			dispbuf = gvideo_recv[1]->cur_buf;
		break;
	default:
		break;
	}
	if (vd_layer[layer_id].switch_vf && vd_layer[layer_id].vf_ext)
		dispbuf = vd_layer[layer_id].vf_ext;
	return dispbuf;
}

#if ENABLE_UPDATE_HDR_FROM_USER
void init_hdr_info(void)
{
	unsigned long flags;

	spin_lock_irqsave(&omx_hdr_lock, flags);

	has_hdr_info = false;
	memset(&vf_hdr, 0, sizeof(vf_hdr));

	spin_unlock_irqrestore(&omx_hdr_lock, flags);
}
#endif
static int video_receiver_event_fun(int type, void *data, void *private_data)
{
	if (type == VFRAME_EVENT_PROVIDER_UNREG) {
		video_vf_unreg_provider();
		drop_frame_count = 0;
		receive_frame_count = 0;
		display_frame_count = 0;
		nn_scenes_value[0].maxprob = 0;
		avsync_count = 0;
		timestamp_avsync_counter_set(avsync_count);
		//init_hdr_info();
		mutex_lock(&omx_mutex);
		omx_continuous_drop_count = 0;
		omx_continuous_drop_flag = false;
		cur_disp_omx_index = 0;
		dovi_drop_flag = false;
		dovi_drop_frame_num = 0;
		video_inuse = 0;
		mutex_unlock(&omx_mutex);
#ifdef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
		if (is_dolby_vision_enable()) {
#ifdef BUILD_ERR
			dv_vf_light_unreg_provider();
#endif
#ifdef CONFIG_AMLOGIC_SET_MINFREQ_SUPPORT
			set_scaling_min_freq(min_cpufreq, 0);
#endif
		}
#endif
		update_process_hdmi_avsync_flag(false);
	} else if (type == VFRAME_EVENT_PROVIDER_RESET) {
		video_vf_light_unreg_provider(1);
		omx_cur_session = 0xffffffff;
		nn_scenes_value[0].maxprob = 0;
	} else if (type == VFRAME_EVENT_PROVIDER_LIGHT_UNREG) {
		video_vf_light_unreg_provider(0);
		nn_scenes_value[0].maxprob = 0;
	} else if (type == VFRAME_EVENT_PROVIDER_REG) {
		max_dis_hwc = 0;
		min_dis_hwc = 0;
		video_drop_vf_cnt = 0;
		enable_video_discontinue_report = 1;
		drop_frame_count = 0;
		receive_frame_count = 0;
		display_frame_count = 0;
		avsync_count = 0;
		timestamp_avsync_counter_set(avsync_count);
		mutex_lock(&omx_mutex);
		omx_run = false;
		omx_pts_set_from_hwc_count = 0;
		omx_pts_set_from_hwc_count_begin = 0;
		omx_check_previous_session = true;
		omx_need_drop_frame_num = 0;
		omx_drop_done = false;
		omx_pts_set_index = 0;
		omx_continusdrop_cnt = 0;
		omx_continuous_drop_count = 0;
		omx_continuous_drop_flag = false;
		cur_disp_omx_index = 0;
		dovi_drop_flag = false;
		dovi_drop_frame_num = 0;
		mutex_unlock(&omx_mutex);
		//init_hdr_info();
		over_sync_count = 0;
		video_inuse = 1;
/*notify di 3d mode is frame*/
/*alternative mode,passing two buffer in one frame */
		if ((process_3d_type & MODE_3D_FA) &&
		    cur_dispbuf &&
		    !cur_dispbuf->trans_fmt)
			vf_notify_receiver_by_name(
			"deinterlace",
			VFRAME_EVENT_PROVIDER_SET_3D_VFRAME_INTERLEAVE,
			(void *)1);

		video_vf_light_unreg_provider(0);
		memset(time_info, 0, sizeof(struct time_info_t) * 20);
#ifdef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
		if (is_dolby_vision_enable()) {
#ifdef BUILD_ERR
			dv_vf_light_reg_provider();
#endif
#ifdef CONFIG_AMLOGIC_SET_MINFREQ_SUPPORT
			set_scaling_min_freq(min_cpufreq, 1);
#endif
		}
#endif
		update_process_hdmi_avsync_flag(true);
	} else if (type == VFRAME_EVENT_PROVIDER_FORCE_BLACKOUT) {
		force_blackout = 1;
		if (debug_flag & DEBUG_FLAG_BASIC_INFO) {
			pr_info("%s VFRAME_EVENT_PROVIDER_FORCE_BLACKOUT\n",
			       __func__);
		}
	} else if (type == VFRAME_EVENT_PROVIDER_FR_HINT) {
#ifdef CONFIG_AM_VOUT
		if ((data != NULL) && (video_seek_flag == 0)) {
			set_vframe_rate_hint((unsigned long)data);
			omx_pts_dv_upper = DUR2PTS((unsigned long)data) * 3 / 2;
			omx_pts_dv_lower = 0 - DUR2PTS((unsigned long)data);
		}
#endif
	} else if (type == VFRAME_EVENT_PROVIDER_FR_END_HINT) {
#ifdef CONFIG_AM_VOUT
		if (video_seek_flag == 0) {
			set_vframe_rate_end_hint();
			omx_pts_dv_upper = OMX_PTS_DV_DEFAULT_UPPER;
			omx_pts_dv_lower = OMX_PTS_DV_DEFAULT_LOWER;
		}
#endif
	} else if (type == VFRAME_EVENT_PROVIDER_QUREY_DISPLAY_INFO) {
		get_display_info(data);
	} else if (type == VFRAME_EVENT_PROVIDER_PROPERTY_CHANGED) {
		vd_layer[0].property_changed = true;
		vd_layer[1].property_changed = true;
	}
	return 0;
}

static void pip_vf_unreg_provider(void)
{
	ulong flags;
	int keeped = 0, ret = 0;
	bool layer1_used = false;
	bool layer2_used = false;
	u32 enabled = 0;
#ifdef CONFIG_AMLOGIC_MEDIA_VSYNC_RDMA
	int i;
#endif

	atomic_inc(&video_unreg_flag);
	while (atomic_read(&video_inirq_flag) > 0)
		schedule();
	spin_lock_irqsave(&lock, flags);
	ret = update_pip_recycle_buffer();
	if (ret == -EAGAIN) {
	/* The currently displayed vf is not added to the queue
	 * that needs to be released. You need to release the vf
	 * data in the release queue before adding the currently
	 * displayed vf to the release queue.
	 */
		release_di_buffer(1);
		update_pip_recycle_buffer();
	}
#ifdef CONFIG_AMLOGIC_MEDIA_VSYNC_RDMA
	pipbuf_to_put_num = 0;
	for (i = 0; i < DISPBUF_TO_PUT_MAX; i++)
		pipbuf_to_put[i] = NULL;
	pip_rdma_buf = NULL;
#endif
	if (cur_pipbuf) {
		if ((cur_pipbuf->vf_ext) &&
		    (cur_pipbuf->type & VIDTYPE_DI_PW)) {
			struct vframe_s *tmp =
				(struct vframe_s *)cur_pipbuf->vf_ext;

			memcpy(&tmp->pic_mode, &cur_pipbuf->pic_mode,
				sizeof(struct vframe_pic_mode_s));
			local_pip_ext = *tmp;
			local_pip = *cur_pipbuf;
			local_pip.vf_ext = (void *)&local_pip_ext;
		} else {
			local_pip = *cur_pipbuf;
		}
		cur_pipbuf = &local_pip;
		cur_pipbuf->video_angle = 0;
	}
	pip_frame_count = 0;
	spin_unlock_irqrestore(&lock, flags);

	if (vd_layer[0].dispbuf_mapping
		== &cur_pipbuf) {
		layer1_used = true;
		enabled |= get_video_enabled();
	}
	if (vd_layer[1].dispbuf_mapping
		== &cur_pipbuf) {
		layer2_used = true;
		enabled |= get_videopip_enabled();
	}

	if (layer1_used || !vd_layer[0].dispbuf_mapping)
		atomic_set(&primary_src_fmt, VFRAME_SIGNAL_FMT_INVALID);

	if (!layer1_used && !layer2_used)
		cur_pipbuf = NULL;

	if (blackout_pip | force_blackout) {
		if (layer1_used)
			safe_switch_videolayer(
				0, false, false);
		if (layer2_used)
			safe_switch_videolayer(
				1, false, false);
		try_free_keep_videopip(1);
	}

	if (cur_pipbuf && enabled)
		keeped = vf_keep_pip_current_locked(cur_pipbuf, NULL);
	else if (cur_pipbuf)
		keeped = 0;

	pr_info("pip_vf_unreg_provider: vd1 used: %s, vd2 used: %s, keep_ret:%d, black_out:%d, cur_pipbuf:%p\n",
		layer1_used ? "true" : "false",
		layer2_used ? "true" : "false",
		keeped, blackout_pip | force_blackout,
		cur_pipbuf);

	if (keeped <= 0) {/*keep failed.*/
		if (keeped < 0)
			pr_info("keep frame failed, disable videopip now.\n");
		else
			pr_info("keep frame skip, disable videopip again.\n");
		if (layer1_used)
			safe_switch_videolayer(
				0, false, false);
		if (layer2_used)
			safe_switch_videolayer(
				1, false, false);
		try_free_keep_videopip(1);
	}

	/*disable_videopip = VIDEO_DISABLE_FORNEXT;*/
	/*DisableVideoLayer2();*/
	atomic_dec(&video_unreg_flag);
}

static void pip_vf_light_unreg_provider(int need_keep_frame)
{
	ulong flags;
	int ret = 0;
#ifdef CONFIG_AMLOGIC_MEDIA_VSYNC_RDMA
	int i;
#endif

	atomic_inc(&video_unreg_flag);
	while (atomic_read(&video_inirq_flag) > 0)
		schedule();

	spin_lock_irqsave(&lock, flags);
	ret = update_pip_recycle_buffer();
	if (ret == -EAGAIN) {
	/* The currently displayed vf is not added to the queue
	 * that needs to be released. You need to release the vf
	 * data in the release queue before adding the currently
	 * displayed vf to the release queue.
	 */
		release_di_buffer(1);
		update_pip_recycle_buffer();
	}
#ifdef CONFIG_AMLOGIC_MEDIA_VSYNC_RDMA
	pipbuf_to_put_num = 0;
	for (i = 0; i < DISPBUF_TO_PUT_MAX; i++)
		pipbuf_to_put[i] = NULL;
	pip_rdma_buf = NULL;
#endif

	if (cur_pipbuf) {
		if ((cur_pipbuf->vf_ext) &&
		    (cur_pipbuf->type & VIDTYPE_DI_PW)) {
			struct vframe_s *tmp =
				(struct vframe_s *)cur_pipbuf->vf_ext;

			memcpy(&tmp->pic_mode, &cur_pipbuf->pic_mode,
				sizeof(struct vframe_pic_mode_s));
			local_pip_ext = *tmp;
			local_pip = *cur_pipbuf;
			local_pip.vf_ext = (void *)&local_pip_ext;
		} else {
			local_pip = *cur_pipbuf;
		}
		cur_pipbuf = &local_pip;
	}
	spin_unlock_irqrestore(&lock, flags);

	if (need_keep_frame && cur_pipbuf)
		vf_keep_pip_current_locked(cur_pipbuf, NULL);

	atomic_dec(&video_unreg_flag);
}

static int pip_receiver_event_fun(
	int type, void *data, void *private_data)
{
	if (type == VFRAME_EVENT_PROVIDER_UNREG)
		pip_vf_unreg_provider();
	else if (type == VFRAME_EVENT_PROVIDER_RESET)
		pip_vf_light_unreg_provider(1);
	else if (type == VFRAME_EVENT_PROVIDER_LIGHT_UNREG)
		pip_vf_light_unreg_provider(0);
	else if (type == VFRAME_EVENT_PROVIDER_REG) {
		pip_vf_light_unreg_provider(0);
		videopip_drop_vf_cnt = 0;
	}
	return 0;
}

unsigned int get_post_canvas(void)
{
	return post_canvas;
}
EXPORT_SYMBOL(get_post_canvas);


u32 get_blackout_policy(void)
{
	return blackout | force_blackout;
}
EXPORT_SYMBOL(get_blackout_policy);

u32 set_blackout_policy(int policy)
{
	blackout = policy;
	return 0;
}
EXPORT_SYMBOL(set_blackout_policy);

u32 get_blackout_pip_policy(void)
{
	return blackout_pip | force_blackout;
}
EXPORT_SYMBOL(get_blackout_pip_policy);

u32 set_blackout_pip_policy(int policy)
{
	blackout_pip = policy;
	return 0;
}
EXPORT_SYMBOL(set_blackout_pip_policy);

u8 is_vpp_postblend(void)
{
	if (READ_VCBUS_REG(VPP_MISC + cur_dev->vpp_off) & VPP_VD1_POSTBLEND)
		return 1;
	return 0;
}
EXPORT_SYMBOL(is_vpp_postblend);

void pause_video(unsigned char pause_flag)
{
	atomic_set(&video_pause_flag, pause_flag ? 1 : 0);
}
EXPORT_SYMBOL(pause_video);

void di_unreg_notify(void)
{
	u32 sleep_time = 40;

	while (atomic_read(&video_inirq_flag) > 0)
		schedule();

	if (gvideo_recv[0] && gvideo_recv[0]->active)
		switch_vf(gvideo_recv[0], true);

	vd_layer[0].need_switch_vf = true;
	vd_layer[0].property_changed = true;

	if (vinfo) {
		sleep_time = vinfo->sync_duration_den * 1000;
		if (vinfo->sync_duration_num) {
			sleep_time /= vinfo->sync_duration_num;
			/* need two vsync */
			sleep_time = (sleep_time + 1) * 2;
		} else {
			sleep_time = 40;
		}
	}
	msleep(sleep_time);
}
EXPORT_SYMBOL(di_unreg_notify);

/*********************************************************
 * Vframe src fmt API
 *********************************************************/
#define signal_color_primaries ((vf->signal_type >> 16) & 0xff)
#define signal_transfer_characteristic ((vf->signal_type >> 8) & 0xff)
#define PREFIX_SEI_NUT 39
#define SUFFIX_SEI_NUT 40
#define SEI_ITU_T_T35 4
#define ATSC_T35_PROV_CODE    0x0031
#define DVB_T35_PROV_CODE     0x003B
#define ATSC_USER_ID_CODE     0x47413934
#define DVB_USER_ID_CODE      0x00000000
#define DM_MD_USER_TYPE_CODE  0x09

static int check_media_sei(char *sei, u32 sei_size, u32 sei_type)
{
	int ret = 0;
	char *p;
	u32 type = 0, size;
	unsigned char nal_type;
	unsigned char sei_payload_type = 0;
	unsigned char sei_payload_size = 0;
	u32 len_2094_sei = 0;
	u32 country_code;
	u32 provider_code;
	u32 user_id;
	u32 user_type_code;

	if (!sei || sei_size <= 8)
		return ret;

	p = sei;
	while (p < sei + sei_size - 8) {
		size = *p++;
		size = (size << 8) | *p++;
		size = (size << 8) | *p++;
		size = (size << 8) | *p++;
		type = *p++;
		type = (type << 8) | *p++;
		type = (type << 8) | *p++;
		type = (type << 8) | *p++;

		if (((sei_type == DV_SEI || sei_type == HDR10P) &&
			sei_type == type) ||
			(sei_type == DV_AV1_SEI &&
			sei_type == (type & 0xffff0000))) {
			ret = 1;
			break;
		} else if ((sei_type == DV_SEI) && type == HDR10P) {
			/* check DVB/ATSC as DV */
			if (p >= sei + sei_size - 12)
				break;
			nal_type = ((*p) & 0x7E) >> 1;
			if (nal_type == PREFIX_SEI_NUT) {
				sei_payload_type = *(p + 2);
				sei_payload_size = *(p + 3);
				if (sei_payload_type == SEI_ITU_T_T35 &&
				    sei_payload_size >= 8) {
					country_code = *(p + 4);
					provider_code = (*(p + 5) << 8) |
							*(p + 6);
					user_id = (*(p + 7) << 24) |
						(*(p + 8) << 16) |
						(*(p + 9) << 8) |
						(*(p + 10));
					user_type_code = *(p + 11);
					if (country_code == 0xB5 &&
					    ((provider_code ==
					    ATSC_T35_PROV_CODE &&
					    user_id == ATSC_USER_ID_CODE) ||
					    (provider_code ==
					    DVB_T35_PROV_CODE &&
					    user_id == DVB_USER_ID_CODE)) &&
					    user_type_code ==
					    DM_MD_USER_TYPE_CODE)
						len_2094_sei = sei_payload_size;
				}
				if (len_2094_sei > 0) {
					ret = 1;
					break;
				}
			}
		}
		p += size;
	}
	return ret;
}

static s32 clear_vframe_dovi_md_info(struct vframe_s *vf)
{
	if (!vf)
		return -1;

	/* invalid src fmt case */
	if (vf->src_fmt.sei_magic_code != SEI_MAGIC_CODE)
		return -1;

	vf->src_fmt.md_size = 0;
	vf->src_fmt.comp_size = 0;
	vf->src_fmt.md_buf = NULL;
	vf->src_fmt.comp_buf = NULL;
	vf->src_fmt.parse_ret_flags = 0;

	return 0;
}

s32 update_vframe_src_fmt(
	struct vframe_s *vf, void *sei,
	u32 size, bool dual_layer,
	char *prov_name, char *recv_name)
{
#ifdef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
#if PARSE_MD_IN_ADVANCE
	int src_fmt = -1;
	int ret = 0;
#endif
#endif
	int i;
	char *p;

	if (!vf)
		return -1;

	vf->src_fmt.sei_magic_code = SEI_MAGIC_CODE;
	vf->src_fmt.fmt = VFRAME_SIGNAL_FMT_INVALID;
	vf->src_fmt.sei_ptr = sei;
	vf->src_fmt.sei_size = size;
	vf->src_fmt.dual_layer = false;
	if (debug_flag & DEBUG_FLAG_OMX_DV_DROP_FRAME) {
		pr_info("===update vf %p, sei %p, size %d, dual_layer %d play_id = %d ===\n",
			vf, sei, size, dual_layer,
			vf->src_fmt.play_id);
		if (sei && size > 15) {
			p = (char *)sei;
			for (i = 0; i < size; i += 16)
				pr_info("%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
					p[i],
					p[i + 1],
					p[i + 2],
					p[i + 3],
					p[i + 4],
					p[i + 5],
					p[i + 6],
					p[i + 7],
					p[i + 8],
					p[i + 9],
					p[i + 10],
					p[i + 11],
					p[i + 12],
					p[i + 13],
					p[i + 14],
					p[i + 15]);
		}
	}

	if (vf->type & VIDTYPE_MVC) {
		vf->src_fmt.fmt = VFRAME_SIGNAL_FMT_MVC;
	} else if (sei && size) {
		if (vf->discard_dv_data) {
			if (debug_flag & DEBUG_FLAG_OMX_DV_DROP_FRAME)
				pr_info("ignore nonstandard dv\n");
		}
#ifdef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
		else if (dual_layer || check_media_sei(sei, size, DV_SEI) ||
			   check_media_sei(sei, size, DV_AV1_SEI)) {
			vf->src_fmt.fmt = VFRAME_SIGNAL_FMT_DOVI;
			vf->src_fmt.dual_layer = dual_layer;
#if PARSE_MD_IN_ADVANCE
			if (vf->src_fmt.md_buf && vf->src_fmt.comp_buf) {
				if (debug_flag & DEBUG_FLAG_OMX_DV_DROP_FRAME)
					pr_info("parse vf %p, sei %p, size %d, md_buf %p\n",
						vf, sei, size,
						vf->src_fmt.md_buf);
				ret = parse_sei_and_meta_ext
					(vf, sei, size,
					 &vf->src_fmt.comp_size,
					 &vf->src_fmt.md_size,
					 &src_fmt,
					 &vf->src_fmt.parse_ret_flags,
					 vf->src_fmt.md_buf,
					 vf->src_fmt.comp_buf);
				if (debug_flag & DEBUG_FLAG_OMX_DV_DROP_FRAME)
					pr_info("parse ret %d, %d, %d %d, %d\n",
						ret,
						src_fmt,
						vf->src_fmt.md_size,
						vf->src_fmt.comp_size,
						vf->src_fmt.parse_ret_flags);

				if (ret) {
					/* mark size -1 as parser failed */
					vf->src_fmt.md_size = -1;
					vf->src_fmt.comp_size = -1;
					if (debug_flag &
					   DEBUG_FLAG_OMX_DV_DROP_FRAME)
						pr_err("parse_sei_and_meta_ext err, vf %p\n",
						       vf);
				}
			}
#else
			clear_vframe_dovi_md_info(vf);
#endif
		}
#endif
	}

	if (vf->src_fmt.fmt == VFRAME_SIGNAL_FMT_INVALID) {
		if (((signal_transfer_characteristic == 14) ||
		     (signal_transfer_characteristic == 18)) &&
		    (signal_color_primaries == 9)) {
			vf->src_fmt.fmt = VFRAME_SIGNAL_FMT_HLG;
		} else if ((signal_transfer_characteristic == 0x30) &&
			     ((signal_color_primaries == 9) ||
			      (signal_color_primaries == 2))) {
			if (check_media_sei(sei, size, HDR10P))
				vf->src_fmt.fmt =
					VFRAME_SIGNAL_FMT_HDR10PLUS;
			else /* TODO: if need switch to HDR10 */
				vf->src_fmt.fmt =
					VFRAME_SIGNAL_FMT_HDR10;
		} else if ((signal_transfer_characteristic == 16) &&
			     ((signal_color_primaries == 9) ||
			      (signal_color_primaries == 2))) {
			vf->src_fmt.fmt =
				VFRAME_SIGNAL_FMT_HDR10;
		} else {
			vf->src_fmt.fmt = VFRAME_SIGNAL_FMT_SDR;
		}
	}

	if (vf->src_fmt.fmt != VFRAME_SIGNAL_FMT_DOVI)
		clear_vframe_dovi_md_info(vf);

	return 0;
}
EXPORT_SYMBOL(update_vframe_src_fmt);

void *get_sei_from_src_fmt(struct vframe_s *vf, u32 *sei_size)
{
	if (!vf || !sei_size)
		return NULL;

	/* invaild src fmt case */
	if (vf->src_fmt.sei_magic_code != SEI_MAGIC_CODE)
		return NULL;

	*sei_size = vf->src_fmt.sei_size;
	return vf->src_fmt.sei_ptr;
}
EXPORT_SYMBOL(get_sei_from_src_fmt);

enum vframe_signal_fmt_e get_vframe_src_fmt(
	struct vframe_s *vf)
{
	if (!vf)
		return VFRAME_SIGNAL_FMT_INVALID;

	/* invaild src fmt case */
	if (vf->src_fmt.sei_magic_code != SEI_MAGIC_CODE)
		return VFRAME_SIGNAL_FMT_INVALID;

	return vf->src_fmt.fmt;
}
EXPORT_SYMBOL(get_vframe_src_fmt);

/*return 0: no parse*/
/*return 1: dovi source and parse successed*/
/*return 2: dovi source and parse failed*/
int get_md_from_src_fmt(struct vframe_s *vf)
{
	int ret = 0;

	if (!vf)
		return 0;

	/* invaild src fmt case */
	if (vf->src_fmt.sei_magic_code != SEI_MAGIC_CODE ||
	    vf->src_fmt.fmt != VFRAME_SIGNAL_FMT_DOVI ||
	    !vf->src_fmt.md_buf ||
	    !vf->src_fmt.comp_buf) {
		ret = 0;
	} else {
		if (vf->src_fmt.md_size > 0)
			ret = 1;
		else if (vf->src_fmt.md_size == -1 &&
			 vf->src_fmt.comp_size == -1)
			ret = 2;
	}
	if (debug_flag & DEBUG_FLAG_OMX_DV_DROP_FRAME)
		pr_info("get_md_from_src_fmt ret %d\n", ret);

	return ret;
}
EXPORT_SYMBOL(get_md_from_src_fmt);

s32 clear_vframe_src_fmt(struct vframe_s *vf)
{
	if (!vf)
		return -1;

	/* invaild src fmt case */
	if (vf->src_fmt.sei_magic_code != SEI_MAGIC_CODE)
		return -1;

	vf->src_fmt.sei_magic_code = 0;
	vf->src_fmt.fmt = VFRAME_SIGNAL_FMT_INVALID;
	vf->src_fmt.sei_ptr = NULL;
	vf->src_fmt.sei_size = 0;
	vf->src_fmt.dual_layer = false;
	vf->src_fmt.md_size = 0;
	vf->src_fmt.comp_size = 0;
	vf->src_fmt.md_buf = NULL;
	vf->src_fmt.comp_buf = NULL;
	vf->src_fmt.parse_ret_flags = 0;

	return 0;
}
EXPORT_SYMBOL(clear_vframe_src_fmt);

/*********************************************************
 * Utilities
 *********************************************************/
int _video_set_disable(u32 val)
{
	struct video_layer_s *layer = &vd_layer[0];

	if (val > VIDEO_DISABLE_FORNEXT)
		return -EINVAL;

	layer->disable_video = val;

	if ((layer->disable_video ==
	     VIDEO_DISABLE_FORNEXT) &&
	    layer->dispbuf &&
	    !is_local_vf(layer->dispbuf))
		layer->disable_video = VIDEO_DISABLE_NONE;

	if (layer->disable_video != VIDEO_DISABLE_NONE) {
		pr_info("VID: VD1 off\n");
		safe_switch_videolayer(
			layer->layer_id, false, true);

		if ((layer->disable_video ==
		     VIDEO_DISABLE_FORNEXT) &&
		    layer->dispbuf &&
		    !is_local_vf(layer->dispbuf))
			layer->property_changed = true;
		/* FIXME */
		if (layer->keep_frame_id == 1)
			try_free_keep_videopip(0);
		else if (layer->keep_frame_id == 0)
			try_free_keep_video(0);
	} else {
		if (layer->dispbuf &&
		    !is_local_vf(layer->dispbuf)) {
			safe_switch_videolayer(
				layer->layer_id, true, true);
			pr_info("VID: VD1 on\n");
			layer->property_changed = true;
		}
	}
	return 0;
}

void video_set_global_output(u32 index, u32 val)
{
	if (index == 0) {
		if (val != 0)
			vd_layer[0].global_output = 1;
		else
			vd_layer[0].global_output = 0;
	} else if (index == 1) {
		if (val != 0)
			vd_layer[1].global_output = 1;
		else
			vd_layer[1].global_output = 0;
	}
	pr_info("VID: VD%d set global output as %d\n",
		index + 1, (val != 0) ? 1 : 0);
}

u32 video_get_layer_capability(void)
{
	return layer_cap;
}

#if ENABLE_UPDATE_HDR_FROM_USER
static void config_hdr_info(const struct vframe_master_display_colour_s p)
{
	struct vframe_master_display_colour_s tmp = {0};
	bool valid_hdr = false;
	unsigned long flags;

	tmp.present_flag = p.present_flag;
	if (tmp.present_flag == 1) {
		tmp = p;

		if (tmp.primaries[0][0] == 0 &&
			tmp.primaries[0][1] == 0 &&
			tmp.primaries[1][0] == 0 &&
			tmp.primaries[1][1] == 0 &&
			tmp.primaries[2][0] == 0 &&
			tmp.primaries[2][1] == 0 &&
			tmp.white_point[0] == 0 &&
			tmp.white_point[1] == 0 &&
			tmp.luminance[0] == 0 &&
			tmp.luminance[1] == 0 &&
			tmp.content_light_level.max_content == 0 &&
			tmp.content_light_level.max_pic_average == 0) {
			valid_hdr = false;
		} else {
			valid_hdr = true;
		}
	}

	spin_lock_irqsave(&omx_hdr_lock, flags);
	vf_hdr = tmp;
	has_hdr_info = valid_hdr;
	spin_unlock_irqrestore(&omx_hdr_lock, flags);

	pr_debug("has_hdr_info %d\n", has_hdr_info);
}
#endif
static void set_omx_pts(u32 *p)
{
	u32 tmp_pts = p[0];
	/*u32 vision = p[1];*/
	u32 set_from_hwc = p[2];
	u32 frame_num = p[3];
	u32 not_reset = p[4];
	u32 session = p[5];
	unsigned int try_cnt = 0x1000;
	bool updateomxpts = true;
	struct vframe_s *vf = NULL;
	u32 index;
	struct timespec mon;
	long long current_time;
	u32 index_dropped = 0;

	if ((debug_flag & DEBUG_FLAG_PRINT_DISPLAY_TIME) && set_from_hwc) {
		ktime_get_ts(&mon);
		current_time = mon.tv_sec;
		current_time = current_time * 1000 * 1000000;
		current_time += mon.tv_nsec;
		index = frame_num % 20;
		time_info[index].set_index = frame_num;
		time_info[index].hwc_time = current_time;
		pr_info("set: pts:%d, num=%d,hwc_time=%lld\n",
			tmp_pts, frame_num, current_time);
	}

	cur_omx_index = frame_num;
	mutex_lock(&omx_mutex);
	if (omx_pts_set_index < frame_num)
		omx_pts_set_index = frame_num;

	if (omx_check_previous_session) {
		if (session != omx_cur_session) {
			omx_cur_session = session;
			omx_check_previous_session = false;
		} else {
			mutex_unlock(&omx_mutex);
			pr_info("check session return: tmp_pts %d"
				"session=0x%x\n", tmp_pts, omx_cur_session);
			omx_pts_set_index = 0;
			return;
		}
	}
	if (debug_flag & DEBUG_FLAG_PTS_TRACE)
		pr_info("[set_omx_pts]tmp_pts:%d, set_from_hwc:%d,frame_num=%d, not_reset=%d\n",
			tmp_pts, set_from_hwc, frame_num, not_reset);
	if (set_from_hwc == 1) {
		if (frame_num >= cur_disp_omx_index) {
			omx_continuous_drop_flag = false;
			omx_continuous_drop_count = 0;
		} else {
			if (omx_continuous_drop_flag &&
			    (debug_flag &
			     DEBUG_FLAG_OMX_DEBUG_DROP_FRAME))
				pr_info("ignore previous rendered frame %d\n",
					frame_num);
		}
	} else {
		omx_continuous_drop_count++;
		if ((omx_continuous_drop_count >=
		     OMX_CONTINUOUS_DROP_LEVEL) &&
		    !(debug_flag &
		       DEBUG_FLAG_OMX_DISABLE_DROP_FRAME)) {
			omx_continuous_drop_flag = true;
			if (debug_flag & DEBUG_FLAG_OMX_DEBUG_DROP_FRAME)
				pr_info("countinous drop %d\n",
					omx_continuous_drop_count);
		}
	}
	if (not_reset == 0) {
		updateomxpts = set_from_hwc;
		if (!set_from_hwc) {
			omx_continusdrop_cnt++;
			if (omx_continusdrop_cnt > 1) {
				/* continus drop update omx_pts */
				updateomxpts = true;
			} else {
				struct vframe_s *vf = NULL;

				vf = vf_peek(RECEIVER_NAME);
				if (vf && (vf->omx_index > 0) &&
				    (omx_pts_set_index > vf->omx_index))
					omx_pts_set_index = vf->omx_index - 1;
			}
		} else
			omx_continusdrop_cnt = 0;

		if (updateomxpts) {
			time_setomxpts_last = time_setomxpts;
			time_setomxpts = sched_clock();
			omx_pts = tmp_pts;
			ATRACE_COUNTER("omxpts", omx_pts);
		}
	}
	/* kodi may render first frame, then drop dozens of frames */
	if (set_from_hwc == 0 && omx_run &&
	    frame_num <= 2 && not_reset == 0 &&
	    omx_pts_set_from_hwc_count > 0) {
		pr_info("reset omx_run to false.\n");
		omx_run = false;
	}
	if (set_from_hwc == 1) {
		if (!omx_run) {
			omx_need_drop_frame_num =
				frame_num > 0 ? frame_num-1 : 0;
			if (omx_need_drop_frame_num == 0)
				omx_drop_done = true;
			pr_info("omx need drop %d, dovi_drop_flag %d\n",
				omx_need_drop_frame_num, dovi_drop_flag);

#ifdef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
			if (!dovi_drop_flag && omx_need_drop_frame_num > 0) {
				vf = vf_peek(RECEIVER_NAME);
				if (is_dolby_vision_enable() &&
				    vf && is_dovi_frame(vf)) {
					if (debug_flag &
						DEBUG_FLAG_OMX_DV_DROP_FRAME)
					pr_info("will drop %d in vsync\n",
						omx_need_drop_frame_num);
					dovi_drop_flag = true;
					dovi_drop_frame_num =
						omx_need_drop_frame_num;
					if (disable_dv_drop) {
						omx_run = true;
						dovi_drop_flag = false;
						dovi_drop_frame_num = 0;
						omx_drop_done = true;
					}
				}
			}
#endif
		}
		omx_run = true;
		if (omx_pts_set_from_hwc_count < OMX_MAX_COUNT_RESET_SYSTEMTIME)
			omx_pts_set_from_hwc_count++;
		if (omx_pts_set_from_hwc_count_begin <
			OMX_MAX_COUNT_RESET_SYSTEMTIME_BEGIN)
			omx_pts_set_from_hwc_count_begin++;

	} else if (set_from_hwc == 0 && !omx_run) {
		struct vframe_s *vf = NULL;

		while (try_cnt--) {
			vf = vf_peek(RECEIVER_NAME);
#ifdef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
			if (is_dolby_vision_enable() &&
			    vf && is_dovi_frame(vf)) {
				if (debug_flag &
					DEBUG_FLAG_OMX_DV_DROP_FRAME)
					pr_info("dovi will drop %d in vsync\n",
						frame_num);
				dovi_drop_flag = true;
				dovi_drop_frame_num = frame_num;

				if (disable_dv_drop) {
					omx_run = true;
					dovi_drop_flag = false;
					dovi_drop_frame_num = 0;
					omx_drop_done = true;
				}
				break;
			}
#endif
			if (vf) {
				video_drop_vf_cnt++;
				if (debug_flag &
				    DEBUG_FLAG_PRINT_DROP_FRAME)
					pr_info("#line %d: drop frame_num=%d, omx_index=%d\n",
						__LINE__,
						frame_num,
						vf->omx_index);
				index_dropped = vf->omx_index;
				if (frame_num >= vf->omx_index) {
					vf = vf_get(RECEIVER_NAME);
					if (vf)
						vf_put(vf, RECEIVER_NAME);
				} else
					break;
			} else
				break;
		}
		if (frame_num > index_dropped)
			omx_need_drop_frame_num = frame_num;
		if (debug_flag & DEBUG_FLAG_PRINT_DROP_FRAME)
			pr_info("omx_need_drop_frame_num=%d, dropped=%d\n",
				omx_need_drop_frame_num,
				index_dropped);
	}
	mutex_unlock(&omx_mutex);
}

static int alloc_layer(u32 layer_id)
{
	int ret = -EINVAL;

	if (layer_id == 0) {
		if (layer_cap & LAYER0_BUSY) {
			ret = -EBUSY;
		} else if (layer_cap & LAYER0_AVAIL) {
			ret = 0;
			layer_cap |= LAYER0_BUSY;
		}
	} else if (layer_id == 1) {
		if (layer_cap & LAYER1_BUSY) {
			ret = -EBUSY;
		} else if (layer_cap & LAYER1_AVAIL) {
			ret = 0;
			layer_cap |= LAYER1_BUSY;
		}
	}
	return ret;
}

static int free_layer(u32 layer_id)
{
	int ret = -EINVAL;

	if (layer_id == 0) {
		if ((layer_cap & LAYER0_BUSY)
			&& (layer_cap & LAYER0_AVAIL)) {
			ret = 0;
			layer_cap &= ~LAYER0_BUSY;
		}
	} else if (layer_id == 1) {
		if ((layer_cap & LAYER1_BUSY)
			&& (layer_cap & LAYER1_AVAIL)) {
			ret = 0;
			layer_cap &= ~LAYER1_BUSY;
		}
	}
	return ret;
}

static u32 get_video_display_latency(void)
{
	u32 video_latency;
	u32 v_duration;

	v_duration = vsync_pts_inc_scale * 1000000 * 1000;
	v_duration = func_div(v_duration, vsync_pts_inc_scale_base);

	if (tsync_get_tunnel_mode())
		video_latency = v_duration;
	else if (video_inuse || (vd_layer[0].disable_video == 0))
		video_latency = 28000000;
	else
		video_latency = 0;
	pr_info("video latency =%d\n", video_latency);
	return video_latency;
}

/*********************************************************
 * /dev/amvideo APIs
 ********************************************************
 */
static int amvideo_open(struct inode *inode, struct file *file)
{
	file->private_data = NULL;
	return 0;
}

static int amvideo_poll_open(struct inode *inode, struct file *file)
{
	file->private_data = NULL;
	return 0;
}

static int amvideo_release(struct inode *inode, struct file *file)
{
	video_inuse = 0;
	file->private_data = NULL;
	return 0;
}

static int amvideo_poll_release(struct inode *inode, struct file *file)
{
	file->private_data = NULL;
	return 0;
}

static long amvideo_ioctl(struct file *file, unsigned int cmd, ulong arg)
{
	long ret = 0;
	void __user *argp = (void __user *)arg;
	struct disp_info_s *layer = &glayer_info[0];
	u32 layer_id;

	switch (cmd) {
	case AMSTREAM_IOC_GLOBAL_SET_VIDEOPIP_OUTPUT:
	case AMSTREAM_IOC_GLOBAL_GET_VIDEOPIP_OUTPUT:
	case AMSTREAM_IOC_GET_VIDEOPIP_DISABLE:
	case AMSTREAM_IOC_SET_VIDEOPIP_DISABLE:
	case AMSTREAM_IOC_GET_VIDEOPIP_AXIS:
	case AMSTREAM_IOC_SET_VIDEOPIP_AXIS:
	case AMSTREAM_IOC_GET_VIDEOPIP_CROP:
	case AMSTREAM_IOC_SET_VIDEOPIP_CROP:
	case AMSTREAM_IOC_GET_PIP_SCREEN_MODE:
	case AMSTREAM_IOC_SET_PIP_SCREEN_MODE:
	case AMSTREAM_IOC_GET_PIP_ZORDER:
	case AMSTREAM_IOC_SET_PIP_ZORDER:
	case AMSTREAM_IOC_GET_PIP_DISPLAYPATH:
	case AMSTREAM_IOC_SET_PIP_DISPLAYPATH:
		layer = &glayer_info[1];
		break;
	default:
		break;
	}

	if (file->private_data)
		layer = (struct disp_info_s *)file->private_data;

	switch (cmd) {
	case AMSTREAM_IOC_GET_VIDEO_LATENCY:
		put_user(get_video_display_latency(), (u32 __user *)argp);
		break;

	case AMSTREAM_IOC_SET_HDR_INFO:{
#if ENABLE_UPDATE_HDR_FROM_USER
			struct vframe_master_display_colour_s tmp;
			if (copy_from_user(&tmp, argp, sizeof(tmp)) == 0)
				config_hdr_info(tmp);
#endif
		}
		break;
	case AMSTREAM_IOC_SET_OMX_VPTS:{
			u32 pts[6];
			if (copy_from_user(pts, argp, sizeof(pts)) == 0)
				set_omx_pts(pts);
		}
		break;

	case AMSTREAM_IOC_GET_OMX_VPTS:
		put_user(omx_pts, (u32 __user *)argp);
		break;

	case AMSTREAM_IOC_GET_OMX_VERSION:
		put_user(omx_version, (u32 __user *)argp);
		break;

	case AMSTREAM_IOC_GET_OMX_INFO:
		put_user(omx_info, (u32 __user *)argp);
		break;

	case AMSTREAM_IOC_TRICKMODE:
		if (arg == TRICKMODE_I)
			trickmode_i = 1;
		else if (arg == TRICKMODE_FFFB)
			trickmode_fffb = 1;
		else {
			trickmode_i = 0;
			trickmode_fffb = 0;
		}
		to_notify_trick_wait = false;
		atomic_set(&trickmode_framedone, 0);
		tsync_trick_mode(trickmode_fffb);
		break;

	case AMSTREAM_IOC_TRICK_STAT:
		put_user(atomic_read(&trickmode_framedone),
			 (u32 __user *)argp);
		break;

	case AMSTREAM_IOC_GET_TRICK_VPTS:
		put_user(trickmode_vpts, (u32 __user *)argp);
		break;

	case AMSTREAM_IOC_VPAUSE:
		tsync_avevent(VIDEO_PAUSE, arg);
		break;

	case AMSTREAM_IOC_AVTHRESH:
		tsync_set_avthresh(arg);
		break;

	case AMSTREAM_IOC_SYNCTHRESH:
		tsync_set_syncthresh(arg);
		break;

	case AMSTREAM_IOC_SYNCENABLE:
		tsync_set_enable(arg);
		break;

	case AMSTREAM_IOC_SET_SYNC_ADISCON:
		tsync_set_sync_adiscont(arg);
		break;

	case AMSTREAM_IOC_SET_SYNC_VDISCON:
		tsync_set_sync_vdiscont(arg);
		break;

	case AMSTREAM_IOC_GET_SYNC_ADISCON:
		put_user(tsync_get_sync_adiscont(), (u32 __user *)argp);
		break;

	case AMSTREAM_IOC_GET_SYNC_VDISCON:
		put_user(tsync_get_sync_vdiscont(), (u32 __user *)argp);
		break;

	case AMSTREAM_IOC_GET_SYNC_ADISCON_DIFF:
		put_user(tsync_get_sync_adiscont_diff(), (u32 __user *)argp);
		break;

	case AMSTREAM_IOC_GET_SYNC_VDISCON_DIFF:
		put_user(tsync_get_sync_vdiscont_diff(), (u32 __user *)argp);
		break;

	case AMSTREAM_IOC_SET_SYNC_ADISCON_DIFF:
		tsync_set_sync_adiscont_diff(arg);
		break;

	case AMSTREAM_IOC_SET_SYNC_VDISCON_DIFF:
		tsync_set_sync_vdiscont_diff(arg);
		break;

	case AMSTREAM_IOC_VF_STATUS:{
			struct vframe_states vfsta;
			struct vframe_states states;

			video_vf_get_states(&vfsta);
			states.vf_pool_size = vfsta.vf_pool_size;
			states.buf_avail_num = vfsta.buf_avail_num;
			states.buf_free_num = vfsta.buf_free_num;
			states.buf_recycle_num = vfsta.buf_recycle_num;
			if (copy_to_user(argp, &states, sizeof(states)))
				ret = -EFAULT;
		}
		break;

	case AMSTREAM_IOC_GET_VIDEOPIP_DISABLE:
		put_user(vd_layer[1].disable_video, (u32 __user *)argp);
		break;
	case AMSTREAM_IOC_GET_VIDEO_DISABLE:
		put_user(vd_layer[0].disable_video, (u32 __user *)argp);
		break;

	case AMSTREAM_IOC_SET_VIDEOPIP_DISABLE:
		{
			u32 val;

			if (copy_from_user(&val, argp, sizeof(u32)) == 0)
				ret = _videopip_set_disable(val);
			else
				ret = -EFAULT;
		}
		break;
	case AMSTREAM_IOC_SET_VIDEO_DISABLE:
		{
			u32 val;

			if (copy_from_user(&val, argp, sizeof(u32)) == 0) {
				ret = _video_set_disable(val);
			} else
				ret = -EFAULT;
		}
		break;

	case AMSTREAM_IOC_GET_VIDEO_DISCONTINUE_REPORT:
		put_user(enable_video_discontinue_report, (u32 __user *)argp);
		break;

	case AMSTREAM_IOC_SET_VIDEO_DISCONTINUE_REPORT:
		enable_video_discontinue_report = (arg == 0) ? 0 : 1;
		break;

	case AMSTREAM_IOC_GET_VIDEOPIP_AXIS:
	case AMSTREAM_IOC_GET_VIDEO_AXIS:
		{
			int axis[4];
#ifdef CONFIG_AMLOGIC_POST_PROCESS_MANAGER_PPSCALER
			if (video_scaler_mode && (layer->layer_id == 0)) {
				axis[0] = content_left;
				axis[1] = content_top;
				axis[2] = content_w;
				axis[3] = content_h;
			} else
#endif
			{
				axis[0] = layer->layer_left;
				axis[1] = layer->layer_top;
				axis[2] = layer->layer_width;
				axis[3] = layer->layer_height;
			}

			axis[2] = axis[0] + axis[2] - 1;
			axis[3] = axis[1] + axis[3] - 1;

			if (copy_to_user(argp, &axis[0], sizeof(axis)) != 0)
				ret = -EFAULT;
		}
		break;

	case AMSTREAM_IOC_SET_VIDEOPIP_AXIS:
	case AMSTREAM_IOC_SET_VIDEO_AXIS:
		{
			int axis[4];

			if (copy_from_user(axis, argp, sizeof(axis)) == 0)
				_set_video_window(layer, axis);
			else
				ret = -EFAULT;
		}
		break;

	case AMSTREAM_IOC_GET_VIDEOPIP_CROP:
	case AMSTREAM_IOC_GET_VIDEO_CROP:
		{
			int crop[4];
			{
				crop[0] = layer->crop_top;
				crop[1] = layer->crop_left;
				crop[2] = layer->crop_bottom;
				crop[3] = layer->crop_right;
			}

			if (copy_to_user(argp, &crop[0], sizeof(crop)) != 0)
				ret = -EFAULT;
		}
		break;

	case AMSTREAM_IOC_SET_VIDEOPIP_CROP:
	case AMSTREAM_IOC_SET_VIDEO_CROP:
		{
			int crop[4];

			if (copy_from_user(crop, argp, sizeof(crop)) == 0)
				_set_video_crop(layer, crop);
			else
				ret = -EFAULT;
		}
		break;

	case AMSTREAM_IOC_GET_PIP_SCREEN_MODE:
	case AMSTREAM_IOC_GET_SCREEN_MODE:
		if (copy_to_user(argp, &layer->wide_mode, sizeof(u32)) != 0)
			ret = -EFAULT;
		break;

	case AMSTREAM_IOC_SET_PIP_SCREEN_MODE:
	case AMSTREAM_IOC_SET_SCREEN_MODE:
		{
			u32 mode;

			if (copy_from_user(&mode, argp, sizeof(u32)) == 0) {
				if (mode >= VIDEO_WIDEOPTION_MAX)
					ret = -EINVAL;
				else if (mode != layer->wide_mode) {
					u8 id = layer->layer_id;

					layer->wide_mode = mode;
					vd_layer[id].property_changed = true;
				}
			} else
				ret = -EFAULT;
		}
		break;

	case AMSTREAM_IOC_GET_BLACKOUT_POLICY:
		if (copy_to_user(argp, &blackout, sizeof(u32)) != 0)
			ret = -EFAULT;
		break;

	case AMSTREAM_IOC_SET_BLACKOUT_POLICY:{
			u32 mode;

			if (copy_from_user(&mode, argp, sizeof(u32)) == 0) {
				if (mode > 2)
					ret = -EINVAL;
				else
					blackout = mode;
			} else
				ret = -EFAULT;
		}
		break;

	case AMSTREAM_IOC_GET_BLACKOUT_PIP_POLICY:
		if (copy_to_user(argp, &blackout_pip, sizeof(u32)) != 0)
			ret = -EFAULT;
		break;

	case AMSTREAM_IOC_SET_BLACKOUT_PIP_POLICY:{
			u32 mode;

			if (copy_from_user(&mode, argp, sizeof(u32)) == 0) {
				if (mode > 2)
					ret = -EINVAL;
				else
					blackout_pip = mode;
			} else
				ret = -EFAULT;
		}
		break;

	case AMSTREAM_IOC_CLEAR_VBUF:{
			unsigned long flags;

			while (atomic_read(&video_inirq_flag) > 0 ||
			       atomic_read(&video_unreg_flag) > 0)
				schedule();
			spin_lock_irqsave(&lock, flags);
			if (cur_dispbuf != &vf_local)
				cur_dispbuf = NULL;
			spin_unlock_irqrestore(&lock, flags);
		}
		break;

	case AMSTREAM_IOC_CLEAR_VIDEO:
		if (blackout)
			safe_switch_videolayer(0, false, false);
		break;

	case AMSTREAM_IOC_CLEAR_PIP_VBUF:{
			unsigned long flags;

			while (atomic_read(&video_inirq_flag) > 0 ||
			       atomic_read(&video_unreg_flag) > 0)
				schedule();
			spin_lock_irqsave(&lock, flags);
			if (cur_pipbuf != &local_pip)
				cur_pipbuf = NULL;
			spin_unlock_irqrestore(&lock, flags);
		}
		break;

	case AMSTREAM_IOC_CLEAR_VIDEOPIP:
		safe_switch_videolayer(1, false, false);
		break;

	case AMSTREAM_IOC_SET_FREERUN_MODE:
		if (arg > FREERUN_DUR)
			ret = -EFAULT;
		else
			freerun_mode = arg;
		break;

	case AMSTREAM_IOC_GET_FREERUN_MODE:
		put_user(freerun_mode, (u32 __user *)argp);
		break;

	case AMSTREAM_IOC_DISABLE_SLOW_SYNC:
		if (arg)
			disable_slow_sync = 1;
		else
			disable_slow_sync = 0;
		break;
	/*
	 ***************************************************************
	 *3d process ioctl
	 ****************************************************************
	 */
	case AMSTREAM_IOC_SET_3D_TYPE:
		{
#ifdef TV_3D_FUNCTION_OPEN
			unsigned int set_3d =
				VFRAME_EVENT_PROVIDER_SET_3D_VFRAME_INTERLEAVE;
			unsigned int type = (unsigned int)arg;
			struct vframe_s *dispbuf = NULL;

			if (type != process_3d_type) {
				process_3d_type = type;
				if (mvc_flag)
					process_3d_type |= MODE_3D_MVC;
				vd_layer[0].property_changed = true;
				dispbuf = get_dispbuf(0);
				if ((process_3d_type & MODE_3D_FA) &&
				    dispbuf &&
				    !dispbuf->trans_fmt)
					/*notify di 3d mode is frame*/
					  /*alternative mode,passing two*/
					  /*buffer in one frame */
					vf_notify_receiver_by_name(
					"deinterlace", set_3d, (void *)1);
				else
					vf_notify_receiver_by_name(
					"deinterlace", set_3d, (void *)0);
			}
#endif
			break;
		}
	case AMSTREAM_IOC_GET_3D_TYPE:
#ifdef TV_3D_FUNCTION_OPEN
		put_user(process_3d_type, (u32 __user *)argp);

#endif
		break;
	case AMSTREAM_IOC_GET_SOURCE_VIDEO_3D_TYPE:
#ifdef TV_3D_FUNCTION_OPEN
	{
		int source_video_3d_type = VPP_3D_MODE_NULL;

		if (!cur_frame_par)
			source_video_3d_type = VPP_3D_MODE_NULL;
		else
			get_vpp_3d_mode(
				process_3d_type,
				cur_frame_par->trans_fmt,
				&source_video_3d_type);
		put_user(source_video_3d_type, (u32 __user *)argp);
	}
#endif
		break;
	case AMSTREAM_IOC_SET_VSYNC_UPINT:
		vsync_pts_inc_upint = arg;
		break;

	case AMSTREAM_IOC_GET_VSYNC_SLOW_FACTOR:
		put_user(vsync_slow_factor, (u32 __user *)argp);
		break;

	case AMSTREAM_IOC_SET_VSYNC_SLOW_FACTOR:
		vsync_slow_factor = arg;
		break;

	case AMSTREAM_IOC_GLOBAL_SET_VIDEOPIP_OUTPUT:
	case AMSTREAM_IOC_GLOBAL_SET_VIDEO_OUTPUT:
		video_set_global_output(layer->layer_id, arg ? 1 : 0);
		break;

	case AMSTREAM_IOC_GLOBAL_GET_VIDEOPIP_OUTPUT:
	case AMSTREAM_IOC_GLOBAL_GET_VIDEO_OUTPUT:
		if (layer->layer_id == 0)
			put_user(vd_layer[0].global_output, (u32 __user *)argp);
		else if (layer->layer_id == 1)
			put_user(vd_layer[1].global_output, (u32 __user *)argp);
		break;

	case AMSTREAM_IOC_GET_VIDEO_LAYER1_ON: {
			u32 vsync_duration;
			u32 video_onoff_diff = 0;

			vsync_duration = vsync_pts_inc / 90;
			video_onoff_diff =
				jiffies_to_msecs(jiffies) -
				vd_layer[0].onoff_time;

			if (vd_layer[0].onoff_state ==
			    VIDEO_ENABLE_STATE_IDLE) {
				/* wait until 5ms after next vsync */
				msleep(video_onoff_diff < vsync_duration
					? vsync_duration - video_onoff_diff + 5
					: 0);
			}
			put_user(vd_layer[0].onoff_state, (u32 __user *)argp);
			break;
		}

	case AMSTREAM_IOC_SET_TUNNEL_MODE: {
		u32 tunnelmode = 0;

		if (copy_from_user(&tunnelmode, argp, sizeof(u32)) == 0)
			tsync_set_tunnel_mode(tunnelmode);
		else
			ret = -EFAULT;
		break;
	}

	case AMSTREAM_IOC_GET_FIRST_FRAME_TOGGLED:
		put_user(first_frame_toggled, (u32 __user *)argp);
		break;

	case AMSTREAM_IOC_SET_VIDEOPEEK:
		videopeek = true;
		nopostvideostart = true;
		break;

	case AMSTREAM_IOC_SET_NO_VIDEO_STOP:
		nopostvideostop = arg;
		pr_info("nopostvideostop %lu\n", arg);
		break;

	case AMSTREAM_IOC_GET_PIP_ZORDER:
	case AMSTREAM_IOC_GET_ZORDER:
		put_user(layer->zorder, (u32 __user *)argp);
		break;

	case AMSTREAM_IOC_SET_PIP_ZORDER:
	case AMSTREAM_IOC_SET_ZORDER:{
			u32 zorder, new_prop = 0;

			if (copy_from_user(&zorder, argp, sizeof(u32)) == 0) {
				if (layer->zorder != zorder)
					new_prop = 1;
				layer->zorder = zorder;
				if ((layer->layer_id == 0) && new_prop)
					vd_layer[0].property_changed = true;
				else if ((layer->layer_id == 1) && new_prop)
					vd_layer[1].property_changed = true;
			} else {
				ret = -EFAULT;
			}
		}
		break;

	case AMSTREAM_IOC_GET_DISPLAYPATH:
	case AMSTREAM_IOC_GET_PIP_DISPLAYPATH:
		put_user(layer->display_path_id, (s32 __user *)argp);
		break;

	case AMSTREAM_IOC_SET_DISPLAYPATH:
	case AMSTREAM_IOC_SET_PIP_DISPLAYPATH:{
			u32 path_id, new_prop = 0;

			if (copy_from_user(&path_id, argp, sizeof(s32)) == 0) {
				if (layer->display_path_id != path_id) {
					new_prop = 1;
					pr_info(
						"VID: VD%d, path_id changed %d->%d\n",
						layer->layer_id + 1,
						layer->display_path_id,
						path_id);
				}
				layer->display_path_id = path_id;
				if ((layer->layer_id == 0) && new_prop)
					vd_layer[0].property_changed = true;
				else if ((layer->layer_id == 1) && new_prop)
					vd_layer[1].property_changed = true;
			} else {
				ret = -EFAULT;
			}
		}
		break;

	case AMSTREAM_IOC_QUERY_LAYER:
		mutex_lock(&video_layer_mutex);
		put_user(layer_cap, (u32 __user *)argp);
		mutex_unlock(&video_layer_mutex);
		ret = 0;
		break;

	case AMSTREAM_IOC_ALLOC_LAYER:
		if (copy_from_user(&layer_id, argp, sizeof(u32)) == 0) {
			if (layer_id >= MAX_VD_LAYERS) {
				ret = -EINVAL;
			} else {
				mutex_lock(&video_layer_mutex);
				if (file->private_data) {
					ret = -EBUSY;
				} else {
					ret = alloc_layer(layer_id);
					if (!ret)
						file->private_data =
						(void *)&glayer_info[layer_id];
				}
				mutex_unlock(&video_layer_mutex);
			}
		} else
			ret = -EFAULT;
		break;

	case AMSTREAM_IOC_FREE_LAYER:
		mutex_lock(&video_layer_mutex);
		if (!file->private_data) {
			ret = -EINVAL;
		} else {
			ret = free_layer(layer->layer_id);
			if (!ret)
				file->private_data = NULL;
		}
		mutex_unlock(&video_layer_mutex);
		break;

	default:
		return -EINVAL;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long amvideo_compat_ioctl(struct file *file, unsigned int cmd, ulong arg)
{
	long ret = 0;

	switch (cmd) {
	case AMSTREAM_IOC_SET_HDR_INFO:
	case AMSTREAM_IOC_SET_OMX_VPTS:
	case AMSTREAM_IOC_GET_OMX_VPTS:
	case AMSTREAM_IOC_GET_OMX_VERSION:
	case AMSTREAM_IOC_GET_OMX_INFO:
	case AMSTREAM_IOC_TRICK_STAT:
	case AMSTREAM_IOC_GET_TRICK_VPTS:
	case AMSTREAM_IOC_GET_SYNC_ADISCON:
	case AMSTREAM_IOC_GET_SYNC_VDISCON:
	case AMSTREAM_IOC_GET_SYNC_ADISCON_DIFF:
	case AMSTREAM_IOC_GET_SYNC_VDISCON_DIFF:
	case AMSTREAM_IOC_VF_STATUS:
	case AMSTREAM_IOC_GET_VIDEO_DISABLE:
	case AMSTREAM_IOC_GET_VIDEO_DISCONTINUE_REPORT:
	case AMSTREAM_IOC_GET_VIDEO_AXIS:
	case AMSTREAM_IOC_SET_VIDEO_AXIS:
	case AMSTREAM_IOC_GET_VIDEO_CROP:
	case AMSTREAM_IOC_SET_VIDEO_CROP:
	case AMSTREAM_IOC_GET_SCREEN_MODE:
	case AMSTREAM_IOC_SET_SCREEN_MODE:
	case AMSTREAM_IOC_GET_BLACKOUT_POLICY:
	case AMSTREAM_IOC_SET_BLACKOUT_POLICY:
	case AMSTREAM_IOC_GET_FREERUN_MODE:
	case AMSTREAM_IOC_GET_3D_TYPE:
	case AMSTREAM_IOC_GET_SOURCE_VIDEO_3D_TYPE:
	case AMSTREAM_IOC_GET_VSYNC_SLOW_FACTOR:
	case AMSTREAM_IOC_GLOBAL_GET_VIDEO_OUTPUT:
	case AMSTREAM_IOC_GET_VIDEO_LAYER1_ON:
	case AMSTREAM_IOC_GLOBAL_SET_VIDEOPIP_OUTPUT:
	case AMSTREAM_IOC_GLOBAL_GET_VIDEOPIP_OUTPUT:
	case AMSTREAM_IOC_GET_VIDEOPIP_DISABLE:
	case AMSTREAM_IOC_SET_VIDEOPIP_DISABLE:
	case AMSTREAM_IOC_GET_VIDEOPIP_AXIS:
	case AMSTREAM_IOC_SET_VIDEOPIP_AXIS:
	case AMSTREAM_IOC_GET_VIDEOPIP_CROP:
	case AMSTREAM_IOC_SET_VIDEOPIP_CROP:
	case AMSTREAM_IOC_GET_PIP_SCREEN_MODE:
	case AMSTREAM_IOC_SET_PIP_SCREEN_MODE:
	case AMSTREAM_IOC_GET_PIP_ZORDER:
	case AMSTREAM_IOC_SET_PIP_ZORDER:
	case AMSTREAM_IOC_GET_ZORDER:
	case AMSTREAM_IOC_SET_ZORDER:
	case AMSTREAM_IOC_GET_DISPLAYPATH:
	case AMSTREAM_IOC_SET_DISPLAYPATH:
	case AMSTREAM_IOC_GET_PIP_DISPLAYPATH:
	case AMSTREAM_IOC_SET_PIP_DISPLAYPATH:
	case AMSTREAM_IOC_QUERY_LAYER:
	case AMSTREAM_IOC_ALLOC_LAYER:
	case AMSTREAM_IOC_FREE_LAYER:
		arg = (unsigned long) compat_ptr(arg);
	case AMSTREAM_IOC_TRICKMODE:
	case AMSTREAM_IOC_VPAUSE:
	case AMSTREAM_IOC_AVTHRESH:
	case AMSTREAM_IOC_SYNCTHRESH:
	case AMSTREAM_IOC_SYNCENABLE:
	case AMSTREAM_IOC_SET_SYNC_ADISCON:
	case AMSTREAM_IOC_SET_SYNC_VDISCON:
	case AMSTREAM_IOC_SET_SYNC_ADISCON_DIFF:
	case AMSTREAM_IOC_SET_SYNC_VDISCON_DIFF:
	case AMSTREAM_IOC_SET_VIDEO_DISABLE:
	case AMSTREAM_IOC_SET_VIDEO_DISCONTINUE_REPORT:
	case AMSTREAM_IOC_CLEAR_VBUF:
	case AMSTREAM_IOC_CLEAR_VIDEO:
	case AMSTREAM_IOC_CLEAR_PIP_VBUF:
	case AMSTREAM_IOC_CLEAR_VIDEOPIP:
	case AMSTREAM_IOC_SET_FREERUN_MODE:
	case AMSTREAM_IOC_DISABLE_SLOW_SYNC:
	case AMSTREAM_IOC_SET_3D_TYPE:
	case AMSTREAM_IOC_SET_VSYNC_UPINT:
	case AMSTREAM_IOC_SET_VSYNC_SLOW_FACTOR:
	case AMSTREAM_IOC_GLOBAL_SET_VIDEO_OUTPUT:
	case AMSTREAM_IOC_SET_TUNNEL_MODE:
	case AMSTREAM_IOC_GET_FIRST_FRAME_TOGGLED:
	case AMSTREAM_IOC_SET_VIDEOPEEK:
		return amvideo_ioctl(file, cmd, arg);
	default:
		return -EINVAL;
	}

	return ret;
}
#endif

static unsigned int amvideo_poll(struct file *file, poll_table *wait_table)
{
	poll_wait(file, &amvideo_trick_wait, wait_table);

	if (atomic_read(&trickmode_framedone)) {
		atomic_set(&trickmode_framedone, 0);
		return POLLOUT | POLLWRNORM;
	}

	return 0;
}

static unsigned int amvideo_poll_poll(struct file *file, poll_table *wait_table)
{
	u32 val = 0;

	poll_wait(file, &amvideo_prop_change_wait, wait_table);

	val = atomic_read(&video_prop_change);
	if (debug_flag & DEBUG_FLAG_TRACE_EVENT)
		pr_info("%s event val:0x%x, called by %s\n",
			__func__, val, current->comm);
	if (val) {
		atomic_set(&status_changed, val);
		atomic_set(&video_prop_change, 0);
		return POLLIN | POLLWRNORM;
	}

	return 0;
}

static const struct file_operations amvideo_fops = {
	.owner = THIS_MODULE,
	.open = amvideo_open,
	.release = amvideo_release,
	.unlocked_ioctl = amvideo_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = amvideo_compat_ioctl,
#endif
	.poll = amvideo_poll,
};

static const struct file_operations amvideo_poll_fops = {
	.owner = THIS_MODULE,
	.open = amvideo_poll_open,
	.release = amvideo_poll_release,
	.poll = amvideo_poll_poll,
};

/*********************************************************
 * SYSFS property functions
 *********************************************************/
#define MAX_NUMBER_PARA 10
#define AMVIDEO_CLASS_NAME "video"
#define AMVIDEO_POLL_CLASS_NAME "video_poll"

static int parse_para(const char *para, int para_num, int *result)
{
	char *token = NULL;
	char *params, *params_base;
	int *out = result;
	int len = 0, count = 0;
	int res = 0;
	int ret = 0;

	if (!para)
		return 0;

	params = kstrdup(para, GFP_KERNEL);
	params_base = params;
	token = params;
	if (token) {
		len = strlen(token);
		do {
			token = strsep(&params, " ");
			if (!token)
				break;
			while (token && (isspace(*token)
					|| !isgraph(*token)) && len) {
				token++;
				len--;
			}
			if (len == 0)
				break;
			ret = kstrtoint(token, 0, &res);
			if (ret < 0)
				break;
			len = strlen(token);
			*out++ = res;
			count++;
		} while ((count < para_num) && (len > 0));
	}

	kfree(params_base);
	return count;
}

static void set_video_crop(
	struct disp_info_s *layer, const char *para)
{
	int parsed[4];

	if (likely(parse_para(para, 4, parsed) == 4))
		_set_video_crop(layer, parsed);
	amlog_mask(
		LOG_MASK_SYSFS,
		"video crop=>x0:%d,y0:%d,x1:%d,y1:%d\n ",
		parsed[0], parsed[1], parsed[2], parsed[3]);
}

static void set_video_speed_check(const char *para)
{
	int parsed[2];
	struct disp_info_s *layer = &glayer_info[0];

	if (likely(parse_para(para, 2, parsed) == 2)) {
		layer->speed_check_height = parsed[0];
		layer->speed_check_width = parsed[1];
	}
	amlog_mask(
		LOG_MASK_SYSFS,
		"video speed_check=>h:%d,w:%d\n ",
		parsed[0], parsed[1]);
}

static void set_video_window(
	struct disp_info_s *layer, const char *para)
{
	int parsed[4];

	if (likely(parse_para(para, 4, parsed) == 4))
		_set_video_window(layer, parsed);
	amlog_mask(
		LOG_MASK_SYSFS,
		"video=>x0:%d,y0:%d,x1:%d,y1:%d\n ",
		parsed[0], parsed[1], parsed[2], parsed[3]);
}

static ssize_t video_3d_scale_store(struct class *cla,
				    struct class_attribute *attr,
				    const char *buf, size_t count)
{
#ifdef TV_3D_FUNCTION_OPEN
	u32 enable;
	int r;
	struct disp_info_s *layer = &glayer_info[0];

	r = kstrtouint(buf, 0, &enable);
	if (r < 0)
		return -EINVAL;

	layer->vpp_3d_scale = enable ? true : false;
	vd_layer[0].property_changed = true;
	amlog_mask(
		LOG_MASK_SYSFS,
		"%s:%s 3d scale.\n", __func__,
		enable ? "enable" : "disable");
#endif
	return count;
}

static ssize_t video_sr_show(struct class *cla, struct class_attribute *attr,
			       char *buf)
{
	return sprintf(buf, "super_scaler:%d\n", super_scaler);
}

static ssize_t video_sr_store(struct class *cla,
		struct class_attribute *attr,
		const char *buf, size_t count)
{
	int parsed[1];

	mutex_lock(&video_module_mutex);
	if (likely(parse_para(buf, 1, parsed) == 1)) {
		if (super_scaler != (parsed[0] & 0x1)) {
			super_scaler = parsed[0] & 0x1;
			vd_layer[0].property_changed = true;
		}
	}
	mutex_unlock(&video_module_mutex);

	return strnlen(buf, count);
}

static ssize_t video_crop_show(struct class *cla, struct class_attribute *attr,
			       char *buf)
{
	u32 t, l, b, r;
	struct disp_info_s *layer = &glayer_info[0];

	t = layer->crop_top;
	l = layer->crop_left;
	b = layer->crop_bottom;
	r = layer->crop_right;
	return snprintf(buf, 40, "%d %d %d %d\n", t, l, b, r);
}

static ssize_t video_crop_store(struct class *cla,
		struct class_attribute *attr,
		const char *buf, size_t count)
{
	struct disp_info_s *layer = &glayer_info[0];

	mutex_lock(&video_module_mutex);

	set_video_crop(layer, buf);

	mutex_unlock(&video_module_mutex);

	return strnlen(buf, count);
}

static ssize_t video_state_show(struct class *cla,
		struct class_attribute *attr,
		char *buf)
{
	ssize_t len = 0;
	struct vppfilter_mode_s *vpp_filter = NULL;

	if (!cur_frame_par)
		return len;
	vpp_filter = &cur_frame_par->vpp_filter;
	len += sprintf(buf + len,
		"zoom_start_x_lines:%u.zoom_end_x_lines:%u.\n",
		vd_layer[0].start_x_lines,
		vd_layer[0].end_x_lines);
	len += sprintf(buf + len,
		"zoom_start_y_lines:%u.zoom_end_y_lines:%u.\n",
		vd_layer[0].start_y_lines,
		vd_layer[0].end_y_lines);
	len += sprintf(buf + len, "frame parameters: pic_in_height %u.\n",
		cur_frame_par->VPP_pic_in_height_);
	len += sprintf(buf + len,
		"frame parameters: VPP_line_in_length_ %u.\n",
		cur_frame_par->VPP_line_in_length_);
	len += sprintf(buf + len, "vscale_skip_count %u.\n",
		cur_frame_par->vscale_skip_count);
	len += sprintf(buf + len, "hscale_skip_count %u.\n",
		cur_frame_par->hscale_skip_count);
	len += sprintf(buf + len, "supscl_path %u.\n",
		cur_frame_par->supscl_path);
	len += sprintf(buf + len, "supsc0_enable %u.\n",
		cur_frame_par->supsc0_enable);
	len += sprintf(buf + len, "supsc1_enable %u.\n",
		cur_frame_par->supsc1_enable);
	len += sprintf(buf + len, "supsc0_hori_ratio %u.\n",
		cur_frame_par->supsc0_hori_ratio);
	len += sprintf(buf + len, "supsc1_hori_ratio %u.\n",
		cur_frame_par->supsc1_hori_ratio);
	len += sprintf(buf + len, "supsc0_vert_ratio %u.\n",
		cur_frame_par->supsc0_vert_ratio);
	len += sprintf(buf + len, "supsc1_vert_ratio %u.\n",
		cur_frame_par->supsc1_vert_ratio);
	len += sprintf(buf + len, "spsc0_h_in %u.\n",
		cur_frame_par->spsc0_h_in);
	len += sprintf(buf + len, "spsc1_h_in %u.\n",
		cur_frame_par->spsc1_h_in);
	len += sprintf(buf + len, "spsc0_w_in %u.\n",
		cur_frame_par->spsc0_w_in);
	len += sprintf(buf + len, "spsc1_w_in %u.\n",
		cur_frame_par->spsc1_w_in);
	len += sprintf(buf + len, "video_input_w %u.\n",
		cur_frame_par->video_input_w);
	len += sprintf(buf + len, "video_input_h %u.\n",
		cur_frame_par->video_input_h);
	len += sprintf(buf + len, "clk_in_pps %u.\n",
		cur_frame_par->clk_in_pps);
#ifdef TV_3D_FUNCTION_OPEN
	len += sprintf(buf + len, "vpp_2pic_mode %u.\n",
		cur_frame_par->vpp_2pic_mode);
	len += sprintf(buf + len, "vpp_3d_scale %u.\n",
		cur_frame_par->vpp_3d_scale);
	len += sprintf(buf + len, "vpp_3d_mode %u.\n",
		cur_frame_par->vpp_3d_mode);
#endif
	len +=
	    sprintf(buf + len, "hscale phase step 0x%x.\n",
		    vpp_filter->vpp_hsc_start_phase_step);
	len +=
	    sprintf(buf + len, "vscale phase step 0x%x.\n",
		    vpp_filter->vpp_vsc_start_phase_step);
	len +=
	    sprintf(buf + len, "pps pre hsc enable %d.\n",
		    vpp_filter->vpp_pre_hsc_en);
	len +=
	    sprintf(buf + len, "pps pre vsc enable %d.\n",
		    vpp_filter->vpp_pre_vsc_en);
	len +=
	    sprintf(buf + len, "hscale filter coef %d.\n",
		    vpp_filter->vpp_horz_filter);
	len +=
	    sprintf(buf + len, "vscale filter coef %d.\n",
		    vpp_filter->vpp_vert_filter);
	len +=
	    sprintf(buf + len, "vpp_vert_chroma_filter_en %d.\n",
		    vpp_filter->vpp_vert_chroma_filter_en);
	len +=
	    sprintf(buf + len, "post_blend_vd_h_start 0x%x.\n",
		    cur_frame_par->VPP_post_blend_vd_h_start_);
	len +=
	    sprintf(buf + len, "post_blend_vd_h_end 0x%x.\n",
		    cur_frame_par->VPP_post_blend_vd_h_end_);
	len +=
	    sprintf(buf + len, "post_blend_vd_v_start 0x%x.\n",
		    cur_frame_par->VPP_post_blend_vd_v_start_);
	len +=
	    sprintf(buf + len, "post_blend_vd_v_end 0x%x.\n",
		    cur_frame_par->VPP_post_blend_vd_v_end_);
	len +=
	    sprintf(buf + len, "VPP_hd_start_lines_ 0x%x.\n",
		    cur_frame_par->VPP_hd_start_lines_);
	len +=
	    sprintf(buf + len, "VPP_hd_end_lines_ 0x%x.\n",
		    cur_frame_par->VPP_hd_end_lines_);
	len +=
	    sprintf(buf + len, "VPP_vd_start_lines_ 0x%x.\n",
		    cur_frame_par->VPP_vd_start_lines_);
	len +=
	    sprintf(buf + len, "VPP_vd_end_lines_ 0x%x.\n",
		    cur_frame_par->VPP_vd_end_lines_);
	len +=
	    sprintf(buf + len, "VPP_hsc_startp 0x%x.\n",
		    cur_frame_par->VPP_hsc_startp);
	len +=
	    sprintf(buf + len, "VPP_hsc_endp 0x%x.\n",
		    cur_frame_par->VPP_hsc_endp);
	len +=
	    sprintf(buf + len, "VPP_vsc_startp 0x%x.\n",
		    cur_frame_par->VPP_vsc_startp);
	len +=
	    sprintf(buf + len, "VPP_vsc_endp 0x%x.\n",
		    cur_frame_par->VPP_vsc_endp);
	return len;
}

static ssize_t video_axis_show(struct class *cla,
		struct class_attribute *attr,
		char *buf)
{
	int x, y, w, h;
	struct disp_info_s *layer = &glayer_info[0];

#ifdef CONFIG_AMLOGIC_POST_PROCESS_MANAGER_PPSCALER
	if (video_scaler_mode) {
		x = content_left;
		y = content_top;
		w = content_w;
		h = content_h;
	} else
#endif
	{
		x = layer->layer_left;
		y = layer->layer_top;
		w = layer->layer_width;
		h = layer->layer_height;
	}
	return snprintf(buf, 40, "%d %d %d %d\n", x, y, x + w - 1, y + h - 1);
}

static ssize_t video_axis_store(struct class *cla,
		struct class_attribute *attr,
			const char *buf, size_t count)
{
	struct disp_info_s *layer = &glayer_info[0];

	mutex_lock(&video_module_mutex);

	set_video_window(layer, buf);

	mutex_unlock(&video_module_mutex);

	return strnlen(buf, count);
}

static ssize_t video_global_offset_show(struct class *cla,
			struct class_attribute *attr, char *buf)
{
	int x, y;
	struct disp_info_s *layer = &glayer_info[0];

	x = layer->global_offset_x;
	y = layer->global_offset_y;

	return snprintf(buf, 40, "%d %d\n", x, y);
}

static ssize_t video_global_offset_store(struct class *cla,
					 struct class_attribute *attr,
					 const char *buf, size_t count)
{
	int parsed[2];
	struct disp_info_s *layer = &glayer_info[0];

	mutex_lock(&video_module_mutex);

	if (likely(parse_para(buf, 2, parsed) == 2)) {
		layer->global_offset_x = parsed[0];
		layer->global_offset_y = parsed[1];
		vd_layer[0].property_changed = true;

		amlog_mask(LOG_MASK_SYSFS,
			   "video_offset=>x0:%d,y0:%d\n ",
			   parsed[0], parsed[1]);
	}

	mutex_unlock(&video_module_mutex);

	return count;
}

static ssize_t video_zoom_show(struct class *cla,
			struct class_attribute *attr,
			char *buf)
{
	u32 r;
	struct disp_info_s *layer = &glayer_info[0];

	r = layer->zoom_ratio;

	return snprintf(buf, 40, "%d\n", r);
}

static ssize_t video_zoom_store(struct class *cla,
			struct class_attribute *attr,
			const char *buf, size_t count)
{
	unsigned long r;
	int ret = 0;
	struct disp_info_s *layer = &glayer_info[0];

	ret = kstrtoul(buf, 0, (unsigned long *)&r);
	if (ret < 0)
		return -EINVAL;

	if ((r <= MAX_ZOOM_RATIO) && (r != layer->zoom_ratio)) {
		layer->zoom_ratio = r;
		vd_layer[0].property_changed = true;
	}

	return count;
}

static ssize_t video_screen_mode_show(struct class *cla,
				      struct class_attribute *attr, char *buf)
{
	struct disp_info_s *layer = &glayer_info[0];
	static const char * const wide_str[] = {
		"normal", "full stretch", "4-3", "16-9", "non-linear-V",
		"normal-noscaleup",
		"4-3 ignore", "4-3 letter box", "4-3 pan scan", "4-3 combined",
		"16-9 ignore", "16-9 letter box", "16-9 pan scan",
		"16-9 combined", "Custom AR", "AFD", "non-linear-T"
	};

	if (layer->wide_mode < ARRAY_SIZE(wide_str)) {
		return sprintf(buf, "%d:%s\n",
			layer->wide_mode,
			wide_str[layer->wide_mode]);
	} else {
		return 0;
	}
}

static ssize_t video_screen_mode_store(struct class *cla,
				       struct class_attribute *attr,
				       const char *buf, size_t count)
{
	unsigned long mode;
	int ret = 0;
	struct disp_info_s *layer = &glayer_info[0];

	ret = kstrtoul(buf, 0, (unsigned long *)&mode);
	if (ret < 0)
		return -EINVAL;

	if ((mode < VIDEO_WIDEOPTION_MAX) &&
	    (mode != layer->wide_mode)) {
		layer->wide_mode = mode;
		vd_layer[0].property_changed = true;
	}

	return count;
}

static ssize_t video_blackout_policy_show(struct class *cla,
					  struct class_attribute *attr,
					  char *buf)
{
	return sprintf(buf, "%d\n", blackout);
}

static ssize_t video_blackout_policy_store(struct class *cla,
					   struct class_attribute *attr,
					   const char *buf, size_t count)
{
	int r;

	r = kstrtoint(buf, 0, &blackout);
	if (r < 0)
		return -EINVAL;

	if (debug_flag & DEBUG_FLAG_BASIC_INFO)
		pr_info("%s(%d)\n", __func__, blackout);

	return count;
}

static ssize_t video_seek_flag_show(struct class *cla,
					  struct class_attribute *attr,
					  char *buf)
{
	return sprintf(buf, "%d\n", video_seek_flag);
}

static ssize_t video_seek_flag_store(struct class *cla,
					   struct class_attribute *attr,
					   const char *buf, size_t count)
{
	int r;

	r = kstrtoint(buf, 0, &video_seek_flag);
	if (r < 0)
		return -EINVAL;

	return count;
}

static ssize_t video_videodelay_store(struct class *class,
				struct class_attribute *attr,
				const char *buf, size_t count)
{
	int r;
	int value;

	r = kstrtoint(buf, 0, &value);
	if (r < 0)
		return -EINVAL;

	video_delay_val = value * 90;

	return count;
}

static ssize_t video_videodelay_show(struct class *cla,
				struct class_attribute *attr,
				char *buf)
{
	return sprintf(buf, "%d(%dms)\n", video_delay_val,
						video_delay_val / 90);
}

#ifdef PTS_TRACE_DEBUG
static ssize_t pts_trace_show(struct class *cla,
			struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%d %d %d %d %d %d %d %d\n"
				"%d %d %d %d %d %d %d %d\n"
				"%0x %0x %0x %0x %0x %0x %0x %0x\n"
				"%0x %0x %0x %0x %0x %0x %0x %0x\n"
				"%0x %0x %0x %0x %0x %0x %0x %0x\n"
				"%0x %0x %0x %0x %0x %0x %0x %0x\n",
		pts_trace_his[0], pts_trace_his[1], pts_trace_his[2],
		pts_trace_his[3], pts_trace_his[4], pts_trace_his[5],
		pts_trace_his[6], pts_trace_his[7], pts_trace_his[8],
		pts_trace_his[9], pts_trace_his[10], pts_trace_his[11],
		pts_trace_his[12], pts_trace_his[13], pts_trace_his[14],
		pts_trace_his[15],
		pts_his[0], pts_his[1], pts_his[2], pts_his[3],
		pts_his[4], pts_his[5], pts_his[6], pts_his[7],
		pts_his[8], pts_his[9], pts_his[10], pts_his[11],
		pts_his[12], pts_his[13], pts_his[14], pts_his[15],
		scr_his[0], scr_his[1], scr_his[2], scr_his[3],
		scr_his[4], scr_his[5], scr_his[6], scr_his[7],
		scr_his[8], scr_his[9], scr_his[10], scr_his[11],
		scr_his[12], scr_his[13], scr_his[14], scr_his[15]);
}
#endif

static ssize_t video_brightness_show(struct class *cla,
				     struct class_attribute *attr, char *buf)
{
	s32 val = (READ_VCBUS_REG(VPP_VADJ1_Y + cur_dev->vpp_off) >> 8) &
			0x1ff;

	val = (val << 23) >> 23;

	return sprintf(buf, "%d\n", val);
}

static ssize_t video_brightness_store(struct class *cla,
				      struct class_attribute *attr,
				      const char *buf, size_t count)
{
	int r;
	int val;

	r = kstrtoint(buf, 0, &val);
	if ((r < 0) || (val < -255) || (val > 255))
		return -EINVAL;

	if (get_cpu_type() <= MESON_CPU_MAJOR_ID_GXTVBB)
		WRITE_VCBUS_REG_BITS(VPP_VADJ1_Y +
			cur_dev->vpp_off, val, 8, 9);
	else
		WRITE_VCBUS_REG_BITS(VPP_VADJ1_Y +
			cur_dev->vpp_off, val << 1, 8, 10);

	WRITE_VCBUS_REG(VPP_VADJ_CTRL + cur_dev->vpp_off, VPP_VADJ1_EN);

	return count;
}

static ssize_t video_contrast_show(struct class *cla,
				   struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n",
		       (int)(READ_VCBUS_REG(VPP_VADJ1_Y + cur_dev->vpp_off) &
			     0xff) - 0x80);
}

static ssize_t video_contrast_store(struct class *cla,
				    struct class_attribute *attr,
				    const char *buf, size_t count)
{
	int r;
	int val;

	r = kstrtoint(buf, 0, &val);
	if ((r < 0) || (val < -127) || (val > 127))
		return -EINVAL;

	val += 0x80;

	WRITE_VCBUS_REG_BITS(VPP_VADJ1_Y + cur_dev->vpp_off, val, 0, 8);
	WRITE_VCBUS_REG(VPP_VADJ_CTRL + cur_dev->vpp_off, VPP_VADJ1_EN);

	return count;
}

static ssize_t vpp_brightness_show(struct class *cla,
				   struct class_attribute *attr, char *buf)
{
	s32 val = (READ_VCBUS_REG(VPP_VADJ2_Y +
			cur_dev->vpp_off) >> 8) & 0x1ff;

	val = (val << 23) >> 23;

	return sprintf(buf, "%d\n", val);
}

static ssize_t vpp_brightness_store(struct class *cla,
				    struct class_attribute *attr,
				    const char *buf, size_t count)
{
	int r;
	int val;

	r = kstrtoint(buf, 0, &val);
	if ((r < 0) || (val < -255) || (val > 255))
		return -EINVAL;

	if (get_cpu_type() <= MESON_CPU_MAJOR_ID_GXTVBB)
		WRITE_VCBUS_REG_BITS(VPP_VADJ2_Y +
			cur_dev->vpp_off, val, 8, 9);
	else
		WRITE_VCBUS_REG_BITS(VPP_VADJ2_Y +
			cur_dev->vpp_off, val << 1, 8, 10);

	WRITE_VCBUS_REG(VPP_VADJ_CTRL + cur_dev->vpp_off, VPP_VADJ2_EN);

	return count;
}

static ssize_t vpp_contrast_show(struct class *cla,
				 struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n",
		       (int)(READ_VCBUS_REG(VPP_VADJ2_Y + cur_dev->vpp_off) &
			     0xff) - 0x80);
}

static ssize_t vpp_contrast_store(struct class *cla,
			struct class_attribute *attr, const char *buf,
			size_t count)
{
	int r;
	int val;

	r = kstrtoint(buf, 0, &val);
	if ((r < 0) || (val < -127) || (val > 127))
		return -EINVAL;

	val += 0x80;

	WRITE_VCBUS_REG_BITS(VPP_VADJ2_Y + cur_dev->vpp_off, val, 0, 8);
	WRITE_VCBUS_REG(VPP_VADJ_CTRL + cur_dev->vpp_off, VPP_VADJ2_EN);

	return count;
}

static ssize_t video_saturation_show(struct class *cla,
				     struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n",
		READ_VCBUS_REG(VPP_VADJ1_Y + cur_dev->vpp_off) & 0xff);
}

static ssize_t video_saturation_store(struct class *cla,
				      struct class_attribute *attr,
				      const char *buf, size_t count)
{
	int r;
	int val;

	r = kstrtoint(buf, 0, &val);
	if ((r < 0) || (val < -127) || (val > 127))
		return -EINVAL;

	WRITE_VCBUS_REG_BITS(VPP_VADJ1_Y + cur_dev->vpp_off, val, 0, 8);
	WRITE_VCBUS_REG(VPP_VADJ_CTRL + cur_dev->vpp_off, VPP_VADJ1_EN);

	return count;
}

static ssize_t vpp_saturation_hue_show(struct class *cla,
				       struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "0x%x\n", READ_VCBUS_REG(VPP_VADJ2_MA_MB));
}

static ssize_t vpp_saturation_hue_store(struct class *cla,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	int r;
	s32 mab = 0;
	s16 mc = 0, md = 0;

	r = kstrtoint(buf, 0, &mab);
	if ((r < 0) || (mab & 0xfc00fc00))
		return -EINVAL;

	WRITE_VCBUS_REG(VPP_VADJ2_MA_MB, mab);
	mc = (s16) ((mab << 22) >> 22);	/* mc = -mb */
	mc = 0 - mc;
	if (mc > 511)
		mc = 511;
	if (mc < -512)
		mc = -512;
	md = (s16) ((mab << 6) >> 22);	/* md =  ma; */
	mab = ((mc & 0x3ff) << 16) | (md & 0x3ff);
	WRITE_VCBUS_REG(VPP_VADJ2_MC_MD, mab);
	/* WRITE_MPEG_REG(VPP_VADJ_CTRL, 1); */
	WRITE_VCBUS_REG_BITS(VPP_VADJ_CTRL + cur_dev->vpp_off, 1, 2, 1);
#ifdef PQ_DEBUG_EN
	pr_info("\n[amvideo..] set vpp_saturation OK!!!\n");
#endif
	return count;
}

/* [   24] 1/enable, 0/disable */
/* [23:16] Y */
/* [15: 8] Cb */
/* [ 7: 0] Cr */
static ssize_t video_test_screen_show(struct class *cla,
				      struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "0x%x\n", test_screen);
}

/* [24]    Flag: enable/disable auto background color */
/* [23:16] Y */
/* [15: 8] Cb */
/* [ 7: 0] Cr */
static ssize_t video_background_show(struct class *cla,
				      struct class_attribute *attr,
				      char *buf)
{
	return sprintf(buf, "channel_bg(0x%x) no_channel_bg(0x%x)\n",
		       vd_layer[0].video_en_bg_color,
		       vd_layer[0].video_dis_bg_color);
}

static ssize_t video_rgb_screen_show(struct class *cla,
				      struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "0x%x\n", rgb_screen);
}

static ssize_t enable_hdmi_delay_check_show(struct class *cla,
				      struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", enable_hdmi_delay_normal_check);
}

static ssize_t enable_hdmi_delay_check_store(struct class *cla,
				      struct class_attribute *attr,
				      const char *buf, size_t count)
{
	int r;
	int value;

	r = kstrtoint(buf, 0, &value);
	if (r < 0)
		return -EINVAL;

	enable_hdmi_delay_normal_check = value > 0 ? true : false;
	return count;
}

static ssize_t hdmi_delay_debug_show(struct class *cla,
				      struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", hdmin_delay_count_debug);
}
#define SCALE 6

static short R_Cr[] = { -11484, -11394, -11305, -11215, -11125,
-11036, -10946, -10856, -10766, -10677, -10587, -10497, -10407,
-10318, -10228, -10138, -10049, -9959, -9869, -9779, -9690, -9600,
-9510, -9420, -9331, -9241, -9151, -9062, -8972, -8882, -8792, -8703,
-8613, -8523, -8433, -8344, -8254, -8164, -8075, -7985, -7895, -7805,
-7716, -7626, -7536, -7446, -7357, -7267, -7177, -7088, -6998, -6908,
-6818, -6729, -6639, -6549, -6459, -6370, -6280, -6190, -6101, -6011,
-5921, -5831, -5742, -5652, -5562, -5472, -5383, -5293, -5203, -5113,
-5024, -4934, -4844, -4755, -4665, -4575, -4485, -4396, -4306, -4216,
-4126, -4037, -3947, -3857, -3768, -3678, -3588, -3498, -3409, -3319,
-3229, -3139, -3050, -2960, -2870, -2781, -2691, -2601, -2511, -2422,
-2332, -2242, -2152, -2063, -1973, -1883, -1794, -1704, -1614, -1524,
-1435, -1345, -1255, -1165, -1076, -986, -896, -807, -717, -627, -537,
-448, -358, -268, -178, -89, 0, 90, 179, 269, 359, 449, 538, 628, 718,
808, 897, 987, 1077, 1166, 1256, 1346, 1436, 1525, 1615, 1705, 1795,
1884, 1974, 2064, 2153, 2243, 2333, 2423, 2512, 2602, 2692, 2782,
2871, 2961, 3051, 3140, 3230, 3320, 3410, 3499, 3589, 3679, 3769,
3858, 3948, 4038, 4127, 4217, 4307, 4397, 4486, 4576, 4666, 4756,
4845, 4935, 5025, 5114, 5204, 5294, 5384, 5473, 5563, 5653, 5743,
5832, 5922, 6012, 6102, 6191, 6281, 6371, 6460, 6550, 6640, 6730,
6819, 6909, 6999, 7089, 7178, 7268, 7358, 7447, 7537, 7627, 7717,
7806, 7896, 7986, 8076, 8165, 8255, 8345, 8434, 8524, 8614, 8704,
8793, 8883, 8973, 9063, 9152, 9242, 9332, 9421, 9511, 9601, 9691,
9780, 9870, 9960, 10050, 10139, 10229, 10319, 10408, 10498, 10588,
10678, 10767, 10857, 10947, 11037, 11126, 11216, 11306, 11395 };

static short G_Cb[] = { 2819, 2797, 2775, 2753, 2731, 2709, 2687,
2665, 2643, 2621, 2599, 2577, 2555, 2533, 2511, 2489, 2467, 2445,
2423, 2401, 2379, 2357, 2335, 2313, 2291, 2269, 2247, 2225, 2202,
2180, 2158, 2136, 2114, 2092, 2070, 2048, 2026, 2004, 1982, 1960,
1938, 1916, 1894, 1872, 1850, 1828, 1806, 1784, 1762, 1740, 1718,
1696, 1674, 1652, 1630, 1608, 1586, 1564, 1542, 1520, 1498, 1476,
1454, 1432, 1410, 1388, 1366, 1344, 1321, 1299, 1277, 1255, 1233,
1211, 1189, 1167, 1145, 1123, 1101, 1079, 1057, 1035, 1013, 991, 969,
947, 925, 903, 881, 859, 837, 815, 793, 771, 749, 727, 705, 683, 661,
639, 617, 595, 573, 551, 529, 507, 485, 463, 440, 418, 396, 374, 352,
330, 308, 286, 264, 242, 220, 198, 176, 154, 132, 110, 88, 66, 44, 22,
0, -21, -43, -65, -87, -109, -131, -153, -175, -197, -219, -241, -263,
-285, -307, -329, -351, -373, -395, -417, -439, -462, -484, -506,
-528, -550, -572, -594, -616, -638, -660, -682, -704, -726, -748,
-770, -792, -814, -836, -858, -880, -902, -924, -946, -968, -990,
-1012, -1034, -1056, -1078, -1100, -1122, -1144, -1166, -1188, -1210,
-1232, -1254, -1276, -1298, -1320, -1343, -1365, -1387, -1409, -1431,
-1453, -1475, -1497, -1519, -1541, -1563, -1585, -1607, -1629, -1651,
-1673, -1695, -1717, -1739, -1761, -1783, -1805, -1827, -1849, -1871,
-1893, -1915, -1937, -1959, -1981, -2003, -2025, -2047, -2069, -2091,
-2113, -2135, -2157, -2179, -2201, -2224, -2246, -2268, -2290, -2312,
-2334, -2356, -2378, -2400, -2422, -2444, -2466, -2488, -2510, -2532,
-2554, -2576, -2598, -2620, -2642, -2664, -2686, -2708, -2730, -2752,
-2774, -2796 };

static short G_Cr[] = { 5850, 5805, 5759, 5713, 5667, 5622, 5576,
5530, 5485, 5439, 5393, 5347, 5302, 5256, 5210, 5165, 5119, 5073,
5028, 4982, 4936, 4890, 4845, 4799, 4753, 4708, 4662, 4616, 4570,
4525, 4479, 4433, 4388, 4342, 4296, 4251, 4205, 4159, 4113, 4068,
4022, 3976, 3931, 3885, 3839, 3794, 3748, 3702, 3656, 3611, 3565,
3519, 3474, 3428, 3382, 3336, 3291, 3245, 3199, 3154, 3108, 3062,
3017, 2971, 2925, 2879, 2834, 2788, 2742, 2697, 2651, 2605, 2559,
2514, 2468, 2422, 2377, 2331, 2285, 2240, 2194, 2148, 2102, 2057,
2011, 1965, 1920, 1874, 1828, 1782, 1737, 1691, 1645, 1600, 1554,
1508, 1463, 1417, 1371, 1325, 1280, 1234, 1188, 1143, 1097, 1051,
1006, 960, 914, 868, 823, 777, 731, 686, 640, 594, 548, 503, 457, 411,
366, 320, 274, 229, 183, 137, 91, 46, 0, -45, -90, -136, -182, -228,
-273, -319, -365, -410, -456, -502, -547, -593, -639, -685, -730,
-776, -822, -867, -913, -959, -1005, -1050, -1096, -1142, -1187,
-1233, -1279, -1324, -1370, -1416, -1462, -1507, -1553, -1599, -1644,
-1690, -1736, -1781, -1827, -1873, -1919, -1964, -2010, -2056, -2101,
-2147, -2193, -2239, -2284, -2330, -2376, -2421, -2467, -2513, -2558,
-2604, -2650, -2696, -2741, -2787, -2833, -2878, -2924, -2970, -3016,
-3061, -3107, -3153, -3198, -3244, -3290, -3335, -3381, -3427, -3473,
-3518, -3564, -3610, -3655, -3701, -3747, -3793, -3838, -3884, -3930,
-3975, -4021, -4067, -4112, -4158, -4204, -4250, -4295, -4341, -4387,
-4432, -4478, -4524, -4569, -4615, -4661, -4707, -4752, -4798, -4844,
-4889, -4935, -4981, -5027, -5072, -5118, -5164, -5209, -5255, -5301,
-5346, -5392, -5438, -5484, -5529, -5575, -5621, -5666, -5712, -5758,
-5804 };

static short B_Cb[] = { -14515, -14402, -14288, -14175, -14062,
-13948, -13835, -13721, -13608, -13495, -13381, -13268, -13154,
-13041, -12928, -12814, -12701, -12587, -12474, -12360, -12247,
-12134, -12020, -11907, -11793, -11680, -11567, -11453, -11340,
-11226, -11113, -11000, -10886, -10773, -10659, -10546, -10433,
-10319, -10206, -10092, -9979, -9865, -9752, -9639, -9525, -9412,
-9298, -9185, -9072, -8958, -8845, -8731, -8618, -8505, -8391, -8278,
-8164, -8051, -7938, -7824, -7711, -7597, -7484, -7371, -7257, -7144,
-7030, -6917, -6803, -6690, -6577, -6463, -6350, -6236, -6123, -6010,
-5896, -5783, -5669, -5556, -5443, -5329, -5216, -5102, -4989, -4876,
-4762, -4649, -4535, -4422, -4309, -4195, -4082, -3968, -3855, -3741,
-3628, -3515, -3401, -3288, -3174, -3061, -2948, -2834, -2721, -2607,
-2494, -2381, -2267, -2154, -2040, -1927, -1814, -1700, -1587, -1473,
-1360, -1246, -1133, -1020, -906, -793, -679, -566, -453, -339, -226,
-112, 0, 113, 227, 340, 454, 567, 680, 794, 907, 1021, 1134, 1247,
1361, 1474, 1588, 1701, 1815, 1928, 2041, 2155, 2268, 2382, 2495,
2608, 2722, 2835, 2949, 3062, 3175, 3289, 3402, 3516, 3629, 3742,
3856, 3969, 4083, 4196, 4310, 4423, 4536, 4650, 4763, 4877, 4990,
5103, 5217, 5330, 5444, 5557, 5670, 5784, 5897, 6011, 6124, 6237,
6351, 6464, 6578, 6691, 6804, 6918, 7031, 7145, 7258, 7372, 7485,
7598, 7712, 7825, 7939, 8052, 8165, 8279, 8392, 8506, 8619, 8732,
8846, 8959, 9073, 9186, 9299, 9413, 9526, 9640, 9753, 9866, 9980,
10093, 10207, 10320, 10434, 10547, 10660, 10774, 10887, 11001, 11114,
11227, 11341, 11454, 11568, 11681, 11794, 11908, 12021, 12135, 12248,
12361, 12475, 12588, 12702, 12815, 12929, 13042, 13155, 13269, 13382,
13496, 13609, 13722, 13836, 13949, 14063, 14176, 14289, 14403
};

static u32 yuv2rgb(u32 yuv)
{
	int y = (yuv >> 16) & 0xff;
	int cb = (yuv >> 8) & 0xff;
	int cr = yuv & 0xff;
	int r, g, b;

	r = y + ((R_Cr[cr]) >> SCALE);
	g = y + ((G_Cb[cb] + G_Cr[cr]) >> SCALE);
	b = y + ((B_Cb[cb]) >> SCALE);

	r = r - 16;
	if (r < 0)
		r = 0;
	r = r*1164/1000;
	g = g - 16;
	if (g < 0)
		g = 0;
	g = g*1164/1000;
	b = b - 16;
	if (b < 0)
		b = 0;
	b = b*1164/1000;

	r = (r <= 0) ? 0 : (r >= 255) ? 255 : r;
	g = (g <= 0) ? 0 : (g >= 255) ? 255 : g;
	b = (b <= 0) ? 0 : (b >= 255) ? 255 : b;

	return  (r << 16) | (g << 8) | b;
}
/* 8bit convert to 10bit */
static u32 eight2ten(u32 yuv)
{
	int y = (yuv >> 16) & 0xff;
	int cb = (yuv >> 8) & 0xff;
	int cr = yuv & 0xff;
	u32 data32;

	/* txlx need check vd1 path bit width by s2u registers */
	if (get_cpu_type() == MESON_CPU_MAJOR_ID_TXLX) {
		data32 = READ_VCBUS_REG(0x1d94) & 0xffff;
		if ((data32 == 0x2000) ||
			(data32 == 0x800))
			return  ((y << 20)<<2) | ((cb << 10)<<2) | (cr<<2);
		else
			return  (y << 20) | (cb << 10) | cr;
	} else
		return  (y << 20) | (cb << 10) | cr;
}

static u32 rgb2yuv(u32 rgb)
{
	int r = (rgb >> 16) & 0xff;
	int g = (rgb >> 8) & 0xff;
	int b = rgb & 0xff;
	int y, u, v;

	y = ((47*r + 157*g + 16*b + 128) >> 8) + 16;
	u = ((-26*r - 87*g + 113*b + 128) >> 8) + 128;
	v = ((112*r - 102*g - 10*b + 128) >> 8) + 128;

	return  (y << 16) | (u << 8) | v;
}

static ssize_t video_test_screen_store(struct class *cla,
				       struct class_attribute *attr,
				       const char *buf, size_t count)
{
	int r;
	unsigned int data = 0x0;

	r = kstrtoint(buf, 0, &test_screen);
	if (r < 0)
		return -EINVAL;
#if 0/*no use now*/
	/* vdin0 pre post blend enable or disabled */
	data = READ_VCBUS_REG(VPP_MISC);
	if (test_screen & 0x01000000)
		data |= VPP_VD1_PREBLEND;
	else
		data &= (~VPP_VD1_PREBLEND);

	if (test_screen & 0x02000000)
		data |= VPP_VD1_POSTBLEND;
	else
		data &= (~VPP_VD1_POSTBLEND);
#endif

#if DEBUG_TMP
	if (test_screen & 0x04000000)
		data |= VPP_VD2_PREBLEND;
	else
		data &= (~VPP_VD2_PREBLEND);

	if (test_screen & 0x08000000)
		data |= VPP_VD2_POSTBLEND;
	else
		data &= (~VPP_VD2_POSTBLEND);
#endif

	/* show test screen  YUV blend*/
	/* force as black 0x108080 for dolbyvision stb ipt blend */
	if (!legacy_vpp) {
		if (is_dolby_vision_enable() &&
			is_dolby_vision_stb_mode())
			WRITE_VCBUS_REG(
				VPP_POST_BLEND_BLEND_DUMMY_DATA,
				0x00108080);
		else
		WRITE_VCBUS_REG(
			VPP_POST_BLEND_BLEND_DUMMY_DATA,
			test_screen & 0x00ffffff);
	}
	else if (is_meson_gxm_cpu() ||
		(get_cpu_type() == MESON_CPU_MAJOR_ID_TXLX))
		/* bit width change to 10bit in gxm, 10/12 in txlx*/
		WRITE_VCBUS_REG(VPP_DUMMY_DATA1,
			eight2ten(test_screen & 0x00ffffff));
	else if (get_cpu_type() == MESON_CPU_MAJOR_ID_GXTVBB)
		WRITE_VCBUS_REG(VPP_DUMMY_DATA1,
			yuv2rgb(test_screen & 0x00ffffff));
	else if (get_cpu_type() < MESON_CPU_MAJOR_ID_GXTVBB)
		if (READ_VCBUS_REG(VIU_OSD1_BLK0_CFG_W0) & 0x80)
			WRITE_VCBUS_REG(VPP_DUMMY_DATA1,
				test_screen & 0x00ffffff);
		else /* RGB blend */
			WRITE_VCBUS_REG(VPP_DUMMY_DATA1,
				yuv2rgb(test_screen & 0x00ffffff));
	else
		WRITE_VCBUS_REG(VPP_DUMMY_DATA1,
			test_screen & 0x00ffffff);
#if 0/*no use*/
	WRITE_VCBUS_REG(VPP_MISC, data);
#endif
	if (debug_flag & DEBUG_FLAG_BASIC_INFO) {
		pr_info("%s write(VPP_MISC,%x) write(VPP_DUMMY_DATA1, %x)\n",
		       __func__, data, test_screen & 0x00ffffff);
	}
	return count;
}

/* [24]    Flag: enable/disable auto background color */
/* [23:16] Y */
/* [15: 8] Cb */
/* [ 7: 0] Cr */
static ssize_t video_background_store(struct class *cla,
				       struct class_attribute *attr,
				       const char *buf, size_t count)
{
	int parsed[2];

	if (likely(parse_para(buf, 2, parsed) == 2)) {
		pr_info("video bg color, channel(0x%x) no_channel(0x%x)\n",
			 parsed[0], parsed[1]);
		vd_layer[0].video_en_bg_color = parsed[0];
		vd_layer[0].video_dis_bg_color = parsed[1];
	} else {
		pr_err("video_background: wrong input params\n");
		return -EINVAL;
	}

	return count;
}

static ssize_t video_rgb_screen_store(struct class *cla,
				       struct class_attribute *attr,
				       const char *buf, size_t count)
{
	int r;
	u32 yuv_eight;

	/* unsigned data = 0x0; */
	r = kstrtoint(buf, 0, &rgb_screen);
	if (r < 0)
		return -EINVAL;

#if DEBUG_TMP
	/* vdin0 pre post blend enable or disabled */

	data = READ_VCBUS_REG(VPP_MISC);
	if (rgb_screen & 0x01000000)
		data |= VPP_VD1_PREBLEND;
	else
		data &= (~VPP_VD1_PREBLEND);

	if (rgb_screen & 0x02000000)
		data |= VPP_VD1_POSTBLEND;
	else
		data &= (~VPP_VD1_POSTBLEND);

	if (test_screen & 0x04000000)
		data |= VPP_VD2_PREBLEND;
	else
		data &= (~VPP_VD2_PREBLEND);

	if (test_screen & 0x08000000)
		data |= VPP_VD2_POSTBLEND;
	else
		data &= (~VPP_VD2_POSTBLEND);
#endif
	/* show test screen  YUV blend*/
	yuv_eight = rgb2yuv(rgb_screen & 0x00ffffff);
	if (!legacy_vpp) {
		WRITE_VCBUS_REG(
			VPP_POST_BLEND_BLEND_DUMMY_DATA,
			yuv_eight & 0x00ffffff);
	} else if (is_meson_gxtvbb_cpu()) {
		WRITE_VCBUS_REG(VPP_DUMMY_DATA1,
			rgb_screen & 0x00ffffff);
	} else if (cpu_after_eq(MESON_CPU_MAJOR_ID_TXL)) {
		WRITE_VCBUS_REG(VPP_DUMMY_DATA1,
			eight2ten(yuv_eight & 0x00ffffff));
	}
	/* WRITE_VCBUS_REG(VPP_MISC, data); */

	if (debug_flag & DEBUG_FLAG_BASIC_INFO) {
		pr_info("%s write(VPP_DUMMY_DATA1, %x)\n",
		       __func__, rgb_screen & 0x00ffffff);
	}
	return count;
}



static ssize_t video_nonlinear_factor_show(struct class *cla,
					   struct class_attribute *attr,
					   char *buf)
{
	u32 factor;
	struct disp_info_s *layer = &glayer_info[0];

	factor = vpp_get_nonlinear_factor(layer);

	return sprintf(buf, "%d\n", factor);
}

static ssize_t video_nonlinear_factor_store(struct class *cla,
					    struct class_attribute *attr,
					    const char *buf, size_t count)
{
	int r;
	u32 factor;
	struct disp_info_s *layer = &glayer_info[0];

	r = kstrtoint(buf, 0, &factor);
	if (r < 0)
		return -EINVAL;

	if (vpp_set_nonlinear_factor(layer, factor) == 0)
		vd_layer[0].property_changed = true;

	return count;
}

static ssize_t video_nonlinear_t_factor_show(struct class *cla,
					   struct class_attribute *attr,
					   char *buf)
{
	u32 factor;
	struct disp_info_s *layer = &glayer_info[0];

	factor = vpp_get_nonlinear_t_factor(layer);

	return sprintf(buf, "%d\n", factor);
}

static ssize_t video_nonlinear_t_factor_store(struct class *cla,
					    struct class_attribute *attr,
					    const char *buf, size_t count)
{
	int r;
	u32 factor;
	struct disp_info_s *layer = &glayer_info[0];

	r = kstrtoint(buf, 0, &factor);
	if (r < 0)
		return -EINVAL;

	if (vpp_set_nonlinear_t_factor(layer, factor) == 0)
		vd_layer[0].property_changed = true;

	return count;
}

static ssize_t video_disable_show(struct class *cla,
				  struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n",
		vd_layer[0].disable_video);
}

static ssize_t video_disable_store(struct class *cla,
				   struct class_attribute *attr,
				   const char *buf, size_t count)
{
	int r;
	int val;

	if (debug_flag & DEBUG_FLAG_BASIC_INFO)
		pr_info("%s(%s)\n", __func__, buf);

	r = kstrtoint(buf, 0, &val);
	if (r < 0)
		return -EINVAL;

	if (_video_set_disable(val) < 0)
		return -EINVAL;

	return count;
}

static ssize_t video_global_output_show(struct class *cla,
				struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", vd_layer[0].global_output);
}

static ssize_t video_global_output_store(struct class *cla,
				struct class_attribute *attr,
				const char *buf, size_t count)
{
	int r;

	r = kstrtoint(buf, 0, &vd_layer[0].global_output);
	if (r < 0)
		return -EINVAL;

	pr_info("%s(%d)\n", __func__, vd_layer[0].global_output);
	return count;
}

static ssize_t video_hold_show(struct class *cla,
				  struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", hold_video);
}

static ssize_t video_hold_store(struct class *cla,
				   struct class_attribute *attr,
				   const char *buf, size_t count)
{
	int r;
	unsigned int value;

	cur_width = 0;
	cur_height = 0;
	if (debug_flag & DEBUG_FLAG_BASIC_INFO)
		pr_info("%s(%s)\n", __func__, buf);

	r = kstrtoint(buf, 0, &value);
	if (r < 0)
		return -EINVAL;

	while (atomic_read(&video_inirq_flag) > 0)
		schedule();

	if (value == 0 && hold_video == 1)
		hold_property_changed = 1;

	hold_video = value;
	return count;
}

static ssize_t video_freerun_mode_show(struct class *cla,
				       struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", freerun_mode);
}

static ssize_t video_freerun_mode_store(struct class *cla,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	int r;

	r = kstrtoint(buf, 0, &freerun_mode);
	if (r < 0)
		return -EINVAL;

	if (debug_flag)
		pr_info("%s(%d)\n", __func__, freerun_mode);

	return count;
}

static ssize_t video_speed_check_show(struct class *cla,
				      struct class_attribute *attr, char *buf)
{
	u32 h, w;
	struct disp_info_s *layer = &glayer_info[0];

	h = layer->speed_check_height;
	w = layer->speed_check_width;

	return snprintf(buf, 40, "%d %d\n", h, w);
}

static ssize_t video_speed_check_store(struct class *cla,
				       struct class_attribute *attr,
				       const char *buf, size_t count)
{
	set_video_speed_check(buf);
	return strnlen(buf, count);
}

static ssize_t threedim_mode_store(struct class *cla,
				   struct class_attribute *attr,
				   const char *buf, size_t len)
{
#ifdef TV_3D_FUNCTION_OPEN

	u32 type;
	int r;
	struct vframe_s *dispbuf = NULL;

	r = kstrtoint(buf, 0, &type);
	if (r < 0)
		return -EINVAL;

	if (type != process_3d_type) {
		process_3d_type = type;
		if (mvc_flag)
			process_3d_type |= MODE_3D_MVC;
		vd_layer[0].property_changed = true;

		dispbuf = get_dispbuf(0);
		if ((process_3d_type & MODE_3D_FA) &&
		    dispbuf && !dispbuf->trans_fmt)
			/*notify di 3d mode is frame alternative mode,1*/
			/*passing two buffer in one frame */
			vf_notify_receiver_by_name(
			"deinterlace",
			VFRAME_EVENT_PROVIDER_SET_3D_VFRAME_INTERLEAVE,
			(void *)1);
		else
			vf_notify_receiver_by_name(
			"deinterlace",
			VFRAME_EVENT_PROVIDER_SET_3D_VFRAME_INTERLEAVE,
			(void *)0);
	}
#endif
	return len;
}

static ssize_t threedim_mode_show(struct class *cla,
				  struct class_attribute *attr, char *buf)
{
#ifdef TV_3D_FUNCTION_OPEN
	return sprintf(buf, "process type 0x%x,trans fmt %u.\n",
		       process_3d_type, video_3d_format);
#else
	return 0;
#endif
}

static ssize_t frame_addr_show(struct class *cla, struct class_attribute *attr,
			       char *buf)
{
	struct canvas_s canvas;
	u32 addr[3];
	struct vframe_s *dispbuf = NULL;
	unsigned int canvas0Addr;

	dispbuf = get_dispbuf(0);
	if (dispbuf) {
		canvas0Addr = get_layer_display_canvas(0);
		canvas_read(canvas0Addr & 0xff, &canvas);
		addr[0] = canvas.addr;
		canvas_read((canvas0Addr >> 8) & 0xff, &canvas);
		addr[1] = canvas.addr;
		canvas_read((canvas0Addr >> 16) & 0xff, &canvas);
		addr[2] = canvas.addr;

		return sprintf(buf, "0x%x-0x%x-0x%x\n", addr[0], addr[1],
			       addr[2]);
	}

	return sprintf(buf, "NA\n");
}

static ssize_t hdmin_delay_start_show(struct class *class,
			struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", hdmin_delay_start);
}

static ssize_t hdmin_delay_start_store(struct class *class,
			struct class_attribute *attr,
			const char *buf, size_t count)
{
	int r;
	int value;
	unsigned long flags;

	r = kstrtoint(buf, 0, &value);
	if (r < 0)
		return -EINVAL;

	spin_lock_irqsave(&hdmi_avsync_lock, flags);
	hdmin_delay_start = value;
	hdmin_delay_start_time = -1;
	pr_info("[%s] hdmin_delay_start:%d\n", __func__, value);
	spin_unlock_irqrestore(&hdmi_avsync_lock, flags);

	return count;
}

static ssize_t hdmin_delay_max_ms_show(struct class *class,
			struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", hdmin_delay_max_ms);
}

static ssize_t hdmin_delay_max_ms_store(struct class *class,
			struct class_attribute *attr,
			const char *buf, size_t count)
{
	int r;
	int value;

	r = kstrtoint(buf, 0, &value);
	if (r < 0)
		return -EINVAL;
	hdmin_delay_max_ms = value;
	pr_info("[%s] hdmin_delay_max_ms:%d\n", __func__, value);
	return count;
}

static ssize_t hdmin_delay_duration_show(struct class *class,
			struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", hdmin_delay_duration);
}

static ssize_t hdmin_delay_duration_store(struct class *class,
			struct class_attribute *attr,
			const char *buf, size_t count)
{
	int r;
	int value;
	unsigned long flags;

	r = kstrtoint(buf, 0, &value);
	if (r < 0)
		return -EINVAL;

	if (value < 0) { /*not support*/
		pr_info("[%s] invalid delay: %d\n", __func__, value);
		return -EINVAL;
	}

	spin_lock_irqsave(&hdmi_avsync_lock, flags);
	last_required_total_delay = value;
	spin_unlock_irqrestore(&hdmi_avsync_lock, flags);

	pr_info("[%s]current delay %d, total require %d\n",
		__func__, vframe_walk_delay, last_required_total_delay);
	return count;
}

static ssize_t vframe_walk_delay_show(struct class *class,
			struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", vframe_walk_delay);
}

static ssize_t last_required_total_delay_show(struct class *class,
			struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", last_required_total_delay);
}

static ssize_t frame_canvas_width_show(struct class *cla,
				       struct class_attribute *attr, char *buf)
{
	struct vframe_s *dispbuf = NULL;
	struct canvas_s canvas;
	u32 width[3];
	unsigned int canvas0Addr;

	dispbuf = get_dispbuf(0);
	if (dispbuf) {
		canvas0Addr = get_layer_display_canvas(0);
		canvas_read(canvas0Addr & 0xff, &canvas);
		width[0] = canvas.width;
		canvas_read((canvas0Addr >> 8) & 0xff, &canvas);
		width[1] = canvas.width;
		canvas_read((canvas0Addr >> 16) & 0xff, &canvas);
		width[2] = canvas.width;

		return sprintf(buf, "%d-%d-%d\n",
			width[0], width[1], width[2]);
	}

	return sprintf(buf, "NA\n");
}

static ssize_t frame_canvas_height_show(struct class *cla,
			struct class_attribute *attr, char *buf)
{
	struct vframe_s *dispbuf = NULL;
	struct canvas_s canvas;
	u32 height[3];
	unsigned int canvas0Addr;

	dispbuf = get_dispbuf(0);
	if (dispbuf) {
		canvas0Addr = get_layer_display_canvas(0);
		canvas_read(canvas0Addr & 0xff, &canvas);
		height[0] = canvas.height;
		canvas_read((canvas0Addr >> 8) & 0xff, &canvas);
		height[1] = canvas.height;
		canvas_read((canvas0Addr >> 16) & 0xff, &canvas);
		height[2] = canvas.height;

		return sprintf(buf, "%d-%d-%d\n", height[0], height[1],
			       height[2]);
	}

	return sprintf(buf, "NA\n");
}

static ssize_t frame_width_show(struct class *cla,
			struct class_attribute *attr,
			char *buf)
{
	struct vframe_s *dispbuf = NULL;

	dispbuf = get_dispbuf(0);

	if ((hold_video == 1) &&
	    ((glayer_info[0].display_path_id ==
	     VFM_PATH_AMVIDEO) ||
	     (glayer_info[0].display_path_id ==
	     VFM_PATH_DEF)))
		return sprintf(buf, "%d\n", cur_width);
	if (dispbuf) {
		if (dispbuf->type & VIDTYPE_COMPRESS)
			return sprintf(buf, "%d\n", dispbuf->compWidth);
		else
			return sprintf(buf, "%d\n", dispbuf->width);
	}

	return sprintf(buf, "NA\n");
}

static ssize_t frame_height_show(struct class *cla,
				 struct class_attribute *attr, char *buf)
{
	struct vframe_s *dispbuf = NULL;

	if ((hold_video == 1) &&
	    ((glayer_info[0].display_path_id ==
	     VFM_PATH_AMVIDEO) ||
	     (glayer_info[0].display_path_id ==
	     VFM_PATH_DEF)))
		return sprintf(buf, "%d\n", cur_height);

	dispbuf = get_dispbuf(0);
	if (dispbuf) {
		if (dispbuf->type & VIDTYPE_COMPRESS)
			return sprintf(buf, "%d\n", dispbuf->compHeight);
		else
			return sprintf(buf, "%d\n", dispbuf->height);
	}
	return sprintf(buf, "NA\n");
}

static ssize_t frame_format_show(struct class *cla,
				 struct class_attribute *attr, char *buf)
{
	struct vframe_s *dispbuf = NULL;
	ssize_t ret = 0;

	dispbuf = get_dispbuf(0);
	if (dispbuf) {
		if ((dispbuf->type & VIDTYPE_TYPEMASK) ==
		    VIDTYPE_INTERLACE_TOP)
			ret = sprintf(buf, "interlace-top\n");
		else if ((dispbuf->type & VIDTYPE_TYPEMASK) ==
			 VIDTYPE_INTERLACE_BOTTOM)
			ret = sprintf(buf, "interlace-bottom\n");
		else
			ret = sprintf(buf, "progressive\n");

		if (dispbuf->type & VIDTYPE_COMPRESS)
			ret += sprintf(buf + ret, "Compressed\n");

		return ret;
	}

	return sprintf(buf, "NA\n");
}

static ssize_t frame_aspect_ratio_show(struct class *cla,
			struct class_attribute *attr, char *buf)
{
	struct vframe_s *dispbuf = NULL;

	dispbuf = get_dispbuf(0);
	if (dispbuf) {
		u32 ar = (dispbuf->ratio_control &
			DISP_RATIO_ASPECT_RATIO_MASK) >>
			DISP_RATIO_ASPECT_RATIO_BIT;

		if (ar)
			return sprintf(buf, "0x%x\n", ar);
		else
			return sprintf(buf, "0x%x\n",
				       (dispbuf->width << 8) /
				       dispbuf->height);
	}

	return sprintf(buf, "NA\n");
}

static ssize_t frame_rate_show(struct class *cla, struct class_attribute *attr,
			       char *buf)
{
	u32 cnt = frame_count - last_frame_count;
	u32 time = jiffies;
	u32 tmp = time;
	u32 rate = 0;
	u32 vsync_rate;
	ssize_t ret = 0;

	time -= last_frame_time;
	last_frame_time = tmp;
	last_frame_count = frame_count;
	if (time == 0)
		return 0;
	rate = 100 * cnt * HZ / time;
	vsync_rate = 100 * vsync_count * HZ / time;
	if (vinfo->sync_duration_den > 0) {
		ret =
		    sprintf(buf,
		"VF.fps=%d.%02d panel fps %d, dur/is: %d,v/s=%d.%02d,inc=%d\n",
				rate / 100, rate % 100,
				vinfo->sync_duration_num /
				vinfo->sync_duration_den,
				time, vsync_rate / 100, vsync_rate % 100,
				vsync_pts_inc);
	}
	if ((debugflags & DEBUG_FLAG_CALC_PTS_INC) && time > HZ * 10
	    && vsync_rate > 0) {
		if ((vsync_rate * vsync_pts_inc / 100) != 90000)
			vsync_pts_inc = 90000 * 100 / (vsync_rate);
	}
	vsync_count = 0;
	return ret;
}

static ssize_t vframe_states_show(struct class *cla,
				  struct class_attribute *attr, char *buf)
{
	int ret = 0;
	struct vframe_states states;
	unsigned long flags;
	struct vframe_s *vf;

	if (video_vf_get_states(&states) == 0) {
		ret += sprintf(buf + ret, "vframe_pool_size=%d\n",
			states.vf_pool_size);
		ret += sprintf(buf + ret, "vframe buf_free_num=%d\n",
			states.buf_free_num);
		ret += sprintf(buf + ret, "vframe buf_recycle_num=%d\n",
			states.buf_recycle_num);
		ret += sprintf(buf + ret, "vframe buf_avail_num=%d\n",
			states.buf_avail_num);

		spin_lock_irqsave(&lock, flags);

		vf = video_vf_peek();
		if (vf) {
			ret += sprintf(buf + ret,
				"vframe ready frame delayed =%dms\n",
				(int)(jiffies_64 -
				vf->ready_jiffies64) * 1000 /
				HZ);
			ret += sprintf(buf + ret,
				"vf index=%d\n", vf->index);
			ret += sprintf(buf + ret,
				"vf->pts=%d\n", vf->pts);
			ret += sprintf(buf + ret,
				"cur vpts=%d\n",
				timestamp_vpts_get());
			ret += sprintf(buf + ret,
				"vf type=%d\n",
				vf->type);
			ret += sprintf(buf + ret,
				"vf type_original=%d\n",
				vf->type_original);
			if (vf->type & VIDTYPE_COMPRESS) {
				ret += sprintf(buf + ret,
					"vf compHeadAddr=%x\n",
						vf->compHeadAddr);
				ret += sprintf(buf + ret,
					"vf compBodyAddr =%x\n",
						vf->compBodyAddr);
			} else {
				ret += sprintf(buf + ret,
					"vf canvas0Addr=%x\n",
						vf->canvas0Addr);
				ret += sprintf(buf + ret,
					"vf canvas1Addr=%x\n",
						vf->canvas1Addr);
				ret += sprintf(buf + ret,
					"vf canvas0Addr.y.addr=%x(%d)\n",
					canvas_get_addr(
					canvasY(vf->canvas0Addr)),
					canvas_get_addr(
					canvasY(vf->canvas0Addr)));
				ret += sprintf(buf + ret,
					"vf canvas0Adr.uv.adr=%x(%d)\n",
					canvas_get_addr(
					canvasUV(vf->canvas0Addr)),
					canvas_get_addr(
					canvasUV(vf->canvas0Addr)));
			}
			if (vf->vf_ext)
				ret += sprintf(buf + ret,
					"vf_ext=%p\n",
					vf->vf_ext);
		}
		spin_unlock_irqrestore(&lock, flags);

	} else
		ret += sprintf(buf + ret, "vframe no states\n");

	return ret;
}

#ifdef CONFIG_AM_VOUT
static ssize_t device_resolution_show(struct class *cla,
		struct class_attribute *attr, char *buf)
{
	const struct vinfo_s *info = get_current_vinfo();

	if (info != NULL)
		return sprintf(buf, "%dx%d\n", info->width, info->height);
	else
		return sprintf(buf, "0x0\n");
}
#endif

static ssize_t video_filename_show(struct class *cla,
				   struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", file_name);
}

static ssize_t video_filename_store(struct class *cla,
				    struct class_attribute *attr,
				    const char *buf, size_t count)
{
	size_t r;

	/* check input buf to mitigate buffer overflow issue */
	if (strlen(buf) >= sizeof(file_name)) {
		memcpy(file_name, buf, sizeof(file_name));
		file_name[sizeof(file_name)-1] = '\0';
		r = 1;
	} else
		r = sscanf(buf, "%s", file_name);
	if (r != 1)
		return -EINVAL;

	return r;
}

static ssize_t video_debugflags_show(struct class *cla,
				     struct class_attribute *attr, char *buf)
{
	int len = 0;

	len += sprintf(buf + len, "value=%d\n", debugflags);
	len += sprintf(buf + len, "bit0:playing as fast!\n");
	len += sprintf(buf + len,
		"bit1:enable calc pts inc in frame rate show\n");
	return len;
}

static ssize_t video_debugflags_store(struct class *cla,
				      struct class_attribute *attr,
				      const char *buf, size_t count)
{
	int r;
	int value = -1;

/*
 *	r = sscanf(buf, "%d", &value);
 *	if (r == 1) {
 *		debugflags = value;
 *		seted = 1;
 *	} else {
 *		r = sscanf(buf, "0x%x", &value);
 *		if (r == 1) {
 *			debugflags = value;
 *			seted = 1;
 *		}
 *	}
 */

	r = kstrtoint(buf, 0, &value);
	if (r < 0)
		return -EINVAL;

	debugflags = value;

	pr_info("debugflags changed to %d(%x)\n", debugflags,
	       debugflags);
	return count;

}

static ssize_t trickmode_duration_show(struct class *cla,
				       struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "trickmode frame duration %d\n",
		       trickmode_duration / 9000);
}

static ssize_t trickmode_duration_store(struct class *cla,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	int r;
	u32 s_value;

	r = kstrtoint(buf, 0, &s_value);
	if (r < 0)
		return -EINVAL;

	trickmode_duration = s_value * 9000;

	return count;
}

static ssize_t video_vsync_pts_inc_upint_show(struct class *cla,
					      struct class_attribute *attr,
					      char *buf)
{
	if (vsync_pts_inc_upint)
		return sprintf(buf,
		"%d,freerun %d,1.25xInc %d,1.12xInc %d,inc+10 %d,1xInc %d\n",
		vsync_pts_inc_upint, vsync_freerun,
		vsync_pts_125, vsync_pts_112, vsync_pts_101,
		vsync_pts_100);
	else
		return sprintf(buf, "%d\n", vsync_pts_inc_upint);
}

static ssize_t video_vsync_pts_inc_upint_store(struct class *cla,
					       struct class_attribute *attr,
					       const char *buf, size_t count)
{
	int r;

	r = kstrtoint(buf, 0, &vsync_pts_inc_upint);
	if (r < 0)
		return -EINVAL;

	if (debug_flag)
		pr_info("%s(%d)\n", __func__, vsync_pts_inc_upint);

	return count;
}

static ssize_t slowsync_repeat_enable_show(struct class *cla,
					   struct class_attribute *attr,
					   char *buf)
{
	return sprintf(buf, "slowsync repeate enable = %d\n",
		       slowsync_repeat_enable);
}

static ssize_t slowsync_repeat_enable_store(struct class *cla,
					    struct class_attribute *attr,
					    const char *buf, size_t count)
{
	int r;

	r = kstrtoint(buf, 0, &slowsync_repeat_enable);
	if (r < 0)
		return -EINVAL;

	if (debug_flag)
		pr_info("%s(%d)\n", __func__, slowsync_repeat_enable);

	return count;
}

static ssize_t video_vsync_slow_factor_show(struct class *cla,
					    struct class_attribute *attr,
					    char *buf)
{
	return sprintf(buf, "%d\n", vsync_slow_factor);
}

static ssize_t video_vsync_slow_factor_store(struct class *cla,
					     struct class_attribute *attr,
					     const char *buf, size_t count)
{
	int r;

	r = kstrtoint(buf, 0, &vsync_slow_factor);
	if (r < 0)
		return -EINVAL;

	if (debug_flag)
		pr_info("%s(%d)\n", __func__, vsync_slow_factor);

	return count;
}

static ssize_t vframe_ready_cnt_show(struct class *cla,
				struct class_attribute *attr, char *buf)
{
	int ret = -1;
	struct vframe_states states;

	ret = video_vf_get_states(&states);

	return snprintf(buf, 10, "%d\n", (ret == 0) ?
		states.buf_avail_num : 0);
}

static ssize_t fps_info_show(struct class *cla, struct class_attribute *attr,
			     char *buf)
{
	u32 cnt = frame_count - last_frame_count;
	u32 time = jiffies;
	u32 input_fps = 0;
	u32 tmp = time;

	time -= last_frame_time;
	last_frame_time = tmp;
	last_frame_count = frame_count;
	if (time != 0)
		output_fps = cnt * HZ / time;
	if (cur_dispbuf && cur_dispbuf->duration > 0) {
		input_fps = 96000 / cur_dispbuf->duration;
		if (output_fps > input_fps)
			output_fps = input_fps;
	} else
		input_fps = output_fps;
	return sprintf(buf, "input_fps:0x%x output_fps:0x%x drop_fps:0x%x\n",
		       input_fps, output_fps, input_fps - output_fps);
}

static ssize_t video_layer1_state_show(struct class *cla,
			struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", vd_layer[0].enabled);
}

void set_video_angle(u32 s_value)
{
	struct disp_info_s *layer = &glayer_info[0];

	if ((s_value <= 3) && (layer->angle != s_value)) {
		layer->angle = s_value;
		pr_info("video angle:%d\n", layer->angle);
	}
}
EXPORT_SYMBOL(set_video_angle);

static ssize_t video_angle_show(struct class *cla, struct class_attribute *attr,
				char *buf)
{
	struct disp_info_s *layer = &glayer_info[0];

	return snprintf(buf, 40, "%d\n", layer->angle);
}

static ssize_t video_angle_store(struct class *cla,
				 struct class_attribute *attr, const char *buf,
				 size_t count)
{
	int r;
	u32 s_value;

	r = kstrtoint(buf, 0, &s_value);
	if (r < 0)
		return -EINVAL;

	set_video_angle(s_value);
	return strnlen(buf, count);
}

static ssize_t show_first_frame_nosync_show(struct class *cla,
					    struct class_attribute *attr,
					    char *buf)
{
	return sprintf(buf, "%d\n", show_first_frame_nosync ? 1 : 0);
}

static ssize_t show_first_frame_nosync_store(struct class *cla,
					     struct class_attribute *attr,
					     const char *buf, size_t count)
{
	int r;
	int value;

	r = kstrtoint(buf, 0, &value);
	if (r < 0)
		return -EINVAL;

	if (value == 0)
		show_first_frame_nosync = false;
	else
		show_first_frame_nosync = true;

	return count;
}

static ssize_t show_first_picture_store(struct class *cla,
				   struct class_attribute *attr,
				   const char *buf, size_t count)
{
	int r;
	int value;

	r = kstrtoint(buf, 0, &value);
	if (r < 0)
		return -EINVAL;

	if (value == 0)
		show_first_picture = false;
	else
		show_first_picture = true;

	return count;
}

static ssize_t video_free_keep_buffer_store(struct class *cla,
				   struct class_attribute *attr,
				   const char *buf, size_t count)
{
	int r;
	int val;

	if (debug_flag & DEBUG_FLAG_BASIC_INFO)
		pr_info("%s(%s)\n", __func__, buf);
	r = kstrtoint(buf, 0, &val);
	if (r < 0)
		return -EINVAL;
	if (val == 1) {
		if (vd_layer[0].keep_frame_id == 1)
			try_free_keep_videopip(1);
		else if (vd_layer[0].keep_frame_id == 0)
			try_free_keep_video(1);
	}
	return count;
}


static ssize_t free_cma_buffer_store(struct class *cla,
				   struct class_attribute *attr,
				   const char *buf, size_t count)
{
	int r;
	int val;

	r = kstrtoint(buf, 0, &val);
	if (r < 0)
		return -EINVAL;
	if (val == 1) {
		pr_info("start to free cma buffer\n");
#if DEBUG_TMP
		vh265_free_cmabuf();
		vh264_4k_free_cmabuf();
		vdec_free_cmabuf();
#endif
	}
	return count;
}

static ssize_t pic_mode_info_show(struct class *cla,
			struct class_attribute *attr, char *buf)
{
	int ret = 0;
	struct vframe_s *dispbuf = NULL;

	dispbuf = get_dispbuf(0);
	if (dispbuf) {
		u32 adapted_mode = (dispbuf->ratio_control
			& DISP_RATIO_ADAPTED_PICMODE) ? 1 : 0;
		u32 info_frame = (dispbuf->ratio_control
			& DISP_RATIO_INFOFRAME_AVAIL) ? 1 : 0;

		ret += sprintf(buf + ret, "ratio_control=0x%x\n",
			dispbuf->ratio_control);
		ret += sprintf(buf + ret, "adapted_mode=%d\n",
			adapted_mode);
		ret += sprintf(buf + ret, "info_frame=%d\n",
			info_frame);
		ret += sprintf(buf + ret,
			"hs=%d, he=%d, vs=%d, ve=%d\n",
			dispbuf->pic_mode.hs,
			dispbuf->pic_mode.he,
			dispbuf->pic_mode.vs,
			dispbuf->pic_mode.ve);
		ret += sprintf(buf + ret, "screen_mode=%d\n",
			dispbuf->pic_mode.screen_mode);
		ret += sprintf(buf + ret, "custom_ar=%d\n",
			dispbuf->pic_mode.custom_ar);
		ret += sprintf(buf + ret, "AFD_enable=%d\n",
			dispbuf->pic_mode.AFD_enable);
		return ret;
	}
	return sprintf(buf, "NA\n");
}

static ssize_t src_fmt_show(
	struct class *cla, struct class_attribute *attr, char *buf)
{
	int ret = 0;
	struct vframe_s *dispbuf = NULL;
	enum vframe_signal_fmt_e fmt;
	void *sei_ptr;
	u32 sei_size = 0;

	dispbuf = get_dispbuf(0);
	ret += sprintf(buf + ret, "vd1 dispbuf: %p\n", dispbuf);
	if (dispbuf) {
		fmt = get_vframe_src_fmt(dispbuf);
		if (fmt != VFRAME_SIGNAL_FMT_INVALID) {
			sei_ptr = get_sei_from_src_fmt(dispbuf, &sei_size);
			ret += sprintf(buf + ret, "fmt = %s\n",
				src_fmt_str[fmt]);
			ret += sprintf(buf + ret, "sei: %p, size: %d\n",
				sei_ptr, sei_size);
		} else {
			ret += sprintf(buf + ret, "src_fmt is invaild\n");
		}
	}
	dispbuf = get_dispbuf(1);
	ret += sprintf(buf + ret, "vd2 dispbuf: %p\n", dispbuf);
	if (dispbuf) {
		fmt = get_vframe_src_fmt(dispbuf);
		if (fmt != VFRAME_SIGNAL_FMT_INVALID) {
			sei_size = 0;
			sei_ptr = get_sei_from_src_fmt(dispbuf, &sei_size);
			ret += sprintf(buf + ret, "fmt=0x%s\n",
				src_fmt_str[fmt]);
			ret += sprintf(buf + ret, "sei: %p, size: %d\n",
				sei_ptr, sei_size);
		} else {
			ret += sprintf(buf + ret, "src_fmt is invaild\n");
		}
	}
	return ret;
}

static ssize_t cur_aipq_sp_show(struct class *cla,
			struct class_attribute *attr, char *buf)
{
	ssize_t count = 0;

	if (!cur_dispbuf)
		return 0;

	count += sprintf(buf + count, "%d:", scene_prob[0]);
	count += sprintf(buf + count, "%d;", scene_prob[1]);
	count += sprintf(buf + count, "\n");

	return count;
}

static ssize_t video_inuse_show(struct class *class,
			struct class_attribute *attr, char *buf)
{
	size_t r;

	mutex_lock(&video_inuse_mutex);
	if (video_inuse == 0) {
		r = sprintf(buf, "%d\n", video_inuse);
		video_inuse = 1;
		pr_info("video_inuse return 0,set 1\n");
	} else {
		r = sprintf(buf, "%d\n", video_inuse);
		pr_info("video_inuse = %d\n", video_inuse);
	}
	mutex_unlock(&video_inuse_mutex);
	return r;
}

static ssize_t video_inuse_store(struct class *class,
			struct class_attribute *attr,
			const char *buf, size_t count)
{
	size_t r;
	int val;

	mutex_lock(&video_inuse_mutex);
	r = kstrtoint(buf, 0, &val);
	pr_info("set video_inuse val:%d\n", val);
	video_inuse = val;
	mutex_unlock(&video_inuse_mutex);
	if (r != 1)
		return -EINVAL;

	return count;
}

static ssize_t video_zorder_show(
	struct class *cla,
	struct class_attribute *attr,
	char *buf)
{
	struct disp_info_s *layer = &glayer_info[0];

	return sprintf(buf, "%d\n", layer->zorder);
}

static ssize_t video_zorder_store(
	struct class *cla,
	struct class_attribute *attr,
	const char *buf, size_t count)
{
	int zorder;
	int ret = 0;
	struct disp_info_s *layer = &glayer_info[0];

	ret = kstrtoint(buf, 0, &zorder);
	if (ret < 0)
		return -EINVAL;

	if (zorder != layer->zorder) {
		layer->zorder = zorder;
		vd_layer[0].property_changed = true;
	}
	return count;
}

static ssize_t black_threshold_show(
	struct class *cla,
	struct class_attribute *attr,
	char *buf)
{
	return sprintf(buf, "width: %d, height: %d\n",
		black_threshold_width,
		black_threshold_height);
}

static ssize_t black_threshold_store(
	struct class *cla,
	struct class_attribute *attr,
	const char *buf, size_t count)
{
	int parsed[2];

	if (likely(parse_para(buf, 2, parsed) == 2)) {
		black_threshold_width = parsed[0];
		black_threshold_height = parsed[1];
	}
	return strnlen(buf, count);
}

static ssize_t get_di_count_show(struct class *cla,
				 struct class_attribute *attr,
				 char *buf)
{
	return sprintf(buf, "%d\n", get_di_count);
}

static ssize_t get_di_count_store(struct class *cla,
				  struct class_attribute *attr,
				  const char *buf, size_t count)
{
	int r;

	r = kstrtoint(buf, 0, &get_di_count);
	if (r < 0)
		return -EINVAL;

	return count;
}

static ssize_t put_di_count_show(struct class *cla,
				 struct class_attribute *attr,
				 char *buf)
{
	return sprintf(buf, "%d\n", put_di_count);
}

static ssize_t put_di_count_store(struct class *cla,
				  struct class_attribute *attr,
				  const char *buf, size_t count)
{
	int r;

	r = kstrtoint(buf, 0, &put_di_count);
	if (r < 0)
		return -EINVAL;

	return count;
}

static ssize_t di_release_count_show(struct class *cla,
				     struct class_attribute *attr,
				     char *buf)
{
	return sprintf(buf, "%d\n", di_release_count);
}

static ssize_t di_release_count_store(struct class *cla,
				      struct class_attribute *attr,
				      const char *buf, size_t count)
{
	int r;

	r = kstrtoint(buf, 0, &di_release_count);
	if (r < 0)
		return -EINVAL;

	return count;
}

static int free_alloced_hist_test_buffer(void)
{
	if (hist_buffer_addr) {
		codec_mm_free_for_dma("hist-test", hist_buffer_addr);
		hist_buffer_addr = 0;
	}
	return 0;
}

static int alloc_hist_test_buffer(u32 size)
{
	int ret = -ENOMEM;
	int flags = CODEC_MM_FLAGS_DMA |
		CODEC_MM_FLAGS_FOR_VDECODER;

	if (!hist_buffer_addr) {
		hist_buffer_addr = codec_mm_alloc_for_dma(
			"hist-test",
			PAGE_ALIGN(size)/PAGE_SIZE, 0, flags);
	}
	if (hist_buffer_addr)
		ret = 0;
	return ret;
}

static ssize_t hist_test_show(
	struct class *cla,
	struct class_attribute *attr,
	char *buf)
{
#define VI_HIST_MAX_MIN (0x2e03)
#define VI_HIST_SPL_VAL (0x2e04)
#define VI_HIST_SPL_PIX_CNT (0x2e05)
#define VI_HIST_CHROMA_SUM (0x2e06)
	ssize_t len = 0;
	u32 hist_result[4];

	if (hist_test_flag) {
		hist_result[0] = READ_VCBUS_REG(VI_HIST_MAX_MIN);
		hist_result[1] = READ_VCBUS_REG(VI_HIST_SPL_VAL);
		hist_result[2] = READ_VCBUS_REG(VI_HIST_SPL_PIX_CNT);
		hist_result[3] = READ_VCBUS_REG(VI_HIST_CHROMA_SUM);

		len +=
			sprintf(buf + len, "\n======time %d =====\n",
			hist_print_count + 1);
		len +=
			sprintf(buf + len, "hist_max_min: 0x%08x\n",
			hist_result[0]);
		len +=
			sprintf(buf + len, "hist_spl_val: 0x%08x\n",
			hist_result[1]);
		len +=
			sprintf(buf + len, "hist_spl_pix_cnt: 0x%08x\n",
			hist_result[2]);
		len +=
			sprintf(buf + len, "hist_chroma_sum: 0x%08x\n",
			hist_result[3]);
		msleep(50);
		hist_print_count++;
	} else {
		len +=
			sprintf(buf + len, "no hist data\n");
	}
	return len;
}

static ssize_t hist_test_store(
	struct class *cla,
	struct class_attribute *attr,
	const char *buf, size_t count)
{
#define VI_HIST_CTRL (0x2e00)
#define VI_HIST_H_START_END (0x2e01)
#define VI_HIST_V_START_END (0x2e02)
#define VI_HIST_PIC_SIZE (0x2e28)
#define VIU_EOTF_CTL (0x31d0)
#define XVYCC_LUT_CTL (0x3165)
#define XVYCC_INV_LUT_CTL (0x3164)
#define XVYCC_VD1_RGB_CTRST (0x3170)
	int parsed[3];
	int frame_width = 0, frame_height = 0;
	int pat_val = 0, canvas_width;
	u32 hist_dst_w, hist_dst_h;
	const struct vinfo_s *ginfo = get_current_vinfo();
	struct disp_info_s *layer = &glayer_info[0];

	if (likely(parse_para(buf, 3, parsed) == 3)) {
		frame_width = parsed[0];
		frame_height = parsed[1];
		pat_val = parsed[2];
	}

	if (cur_dispbuf
		&& (cur_dispbuf != &vf_local)
		&& (cur_dispbuf != &hist_test_vf))
		pat_val = 0;
	if (!frame_width || !frame_height)
		pat_val = 0;

	if (legacy_vpp)
		pat_val = 0;

	if (pat_val > 0 && pat_val <= 0x3fffffff) {
		if (!hist_test_flag) {
			memset(&hist_test_vf, 0, sizeof(hist_test_vf));
			canvas_width = (frame_width + 31) & (~31);
			if (!alloc_hist_test_buffer(
				canvas_width * frame_height * 3)) {
				hist_test_vf.canvas0Addr =
					LAYER1_CANVAS_BASE_INDEX + 5;
				hist_test_vf.canvas1Addr =
					LAYER1_CANVAS_BASE_INDEX + 5;
				canvas_config(
					LAYER1_CANVAS_BASE_INDEX + 5,
					(unsigned int)hist_buffer_addr,
					canvas_width * 3,
					frame_height,
					CANVAS_ADDR_NOWRAP,
					CANVAS_BLKMODE_LINEAR);
				hist_test_vf.width = frame_width;
				hist_test_vf.height = frame_height;
				hist_test_vf.type = VIDTYPE_VIU_444 |
					VIDTYPE_VIU_SINGLE_PLANE |
					VIDTYPE_VIU_FIELD | VIDTYPE_PIC;
				/* indicate the vframe is a full range frame */
				hist_test_vf.signal_type =
					/* HD default 709 limit */
					  (1 << 29) /* video available */
					| (5 << 26) /* unspecified */
					| (1 << 25) /* full */
					| (1 << 24) /* color available */
					| (1 << 16) /* bt709 */
					| (1 << 8)  /* bt709 */
					| (1 << 0); /* bt709 */
				hist_test_vf.duration_pulldown = 0;
				hist_test_vf.index = 0;
				hist_test_vf.pts = 0;
				hist_test_vf.pts_us64 = 0;
				hist_test_vf.ratio_control = 0;
				hist_test_flag = true;
				WRITE_VCBUS_REG(VIU_EOTF_CTL, 0);
				WRITE_VCBUS_REG(XVYCC_LUT_CTL, 0);
				WRITE_VCBUS_REG(XVYCC_INV_LUT_CTL, 0);
				WRITE_VCBUS_REG(VPP_VADJ_CTRL, 0);
				WRITE_VCBUS_REG(VPP_GAINOFF_CTRL0, 0);
				WRITE_VCBUS_REG(VPP_VE_ENABLE_CTRL, 0);
				WRITE_VCBUS_REG(XVYCC_VD1_RGB_CTRST, 0);
				if (ginfo) {
					if (ginfo->width >
						(layer->layer_width
						+ layer->layer_left))
						hist_dst_w =
						layer->layer_width
						+ layer->layer_left;
					else
						hist_dst_w =
						ginfo->width;
					if (ginfo->field_height >
						(layer->layer_height
						+ layer->layer_top))
						hist_dst_h =
						layer->layer_height
						+ layer->layer_top;
					else
						hist_dst_h =
						ginfo->field_height;
					WRITE_VCBUS_REG(
					VI_HIST_H_START_END,
					hist_dst_w & 0xfff);
					WRITE_VCBUS_REG(
					VI_HIST_V_START_END,
					(hist_dst_h - 2) & 0xfff);
					WRITE_VCBUS_REG(
					VI_HIST_PIC_SIZE,
					(ginfo->width & 0xfff) |
					(ginfo->field_height << 16));
					WRITE_VCBUS_REG(VI_HIST_CTRL, 0x3803);
				} else {
					WRITE_VCBUS_REG(
					VI_HIST_H_START_END, 0);
					WRITE_VCBUS_REG(
					VI_HIST_V_START_END, 0);
					WRITE_VCBUS_REG(
					VI_HIST_PIC_SIZE,
					1080 | 1920);
					WRITE_VCBUS_REG(VI_HIST_CTRL, 0x3801);
				}
			}
		}
		WRITE_VCBUS_REG(VPP_VD1_CLIP_MISC0, pat_val);
		WRITE_VCBUS_REG(VPP_VD1_CLIP_MISC1, pat_val);
		WRITE_VCBUS_REG(DOLBY_PATH_CTRL, 0x3f);
		msleep(50);
		hist_print_count = 0;
	} else if (hist_test_flag) {
		hist_test_flag = false;
		msleep(50);
		free_alloced_hist_test_buffer();
		WRITE_VCBUS_REG(VPP_VD1_CLIP_MISC0, 0x3fffffff);
		WRITE_VCBUS_REG(VPP_VD1_CLIP_MISC1, 0);
		WRITE_VCBUS_REG(DOLBY_PATH_CTRL, 0xf);
		safe_switch_videolayer(0, false, false);
	}
	return strnlen(buf, count);
}

static ssize_t video_ipt_show(
	struct class *cla,
	struct class_attribute *attr,
	char *buf)
{
	return sprintf(buf, "ipt enable: %d\n",
		ipt_enable);
}

static ssize_t video_ipt_store(
	struct class *cla,
	struct class_attribute *attr,
	const char *buf, size_t count)
{
	int ret;
	u32 enable;

	ret = kstrtoint(buf, 0, &enable);
	if (ret < 0)
		return -EINVAL;
	ipt_enable = enable;
	set_video_ipt(VFM_PATH_AMVIDEO, enable);
	return count;
}

static ssize_t videopip_ipt_show(
	struct class *cla,
	struct class_attribute *attr,
	char *buf)
{
	return sprintf(buf, "piip ipt enable: %d\n",
		pip_ipt_enable);
}

static ssize_t videopip_ipt_store(
	struct class *cla,
	struct class_attribute *attr,
	const char *buf, size_t count)
{
	int ret;
	u32 enable;

	ret = kstrtoint(buf, 0, &enable);
	if (ret < 0)
		return -EINVAL;
	pip_ipt_enable = enable;
	set_video_ipt(VFM_PATH_PIP, enable);
	return count;
}

int _videopip_set_disable(u32 val)
{
	struct video_layer_s *layer = &vd_layer[1];

	if (val > VIDEO_DISABLE_FORNEXT)
		return -EINVAL;

	layer->disable_video = val;

	if ((layer->disable_video ==
	     VIDEO_DISABLE_FORNEXT) &&
	    layer->dispbuf &&
	    !is_local_vf(layer->dispbuf))
		layer->disable_video = VIDEO_DISABLE_NONE;

	if (layer->disable_video != VIDEO_DISABLE_NONE) {
		pr_info("VID: VD2 off\n");
		safe_switch_videolayer(
			layer->layer_id, false, true);

		if ((layer->disable_video ==
		     VIDEO_DISABLE_FORNEXT) &&
		    layer->dispbuf &&
		    !is_local_vf(layer->dispbuf))
			layer->property_changed = true;
		/* FIXME */
		if (layer->keep_frame_id == 1)
			try_free_keep_videopip(0);
		else if (vd_layer[0].keep_frame_id == 0)
			try_free_keep_video(0);
	} else {
		if (layer->dispbuf &&
		    !is_local_vf(layer->dispbuf)) {
			safe_switch_videolayer(
				layer->layer_id, true, true);
			pr_info("VID: VD2 on\n");
			layer->property_changed = true;
		}
	}
	return 0;
}

static ssize_t videopip_blackout_policy_show(struct class *cla,
					  struct class_attribute *attr,
					  char *buf)
{
	return sprintf(buf, "%d\n", blackout_pip);
}

static ssize_t videopip_blackout_policy_store(struct class *cla,
					   struct class_attribute *attr,
					   const char *buf, size_t count)
{
	int r;

	r = kstrtoint(buf, 0, &blackout_pip);
	if (r < 0)
		return -EINVAL;

	return count;
}

static ssize_t videopip_axis_show(
	struct class *cla,
	struct class_attribute *attr,
	char *buf)
{
	int x0, y0, x1, y1;
	struct disp_info_s *layer = &glayer_info[1];

	x0 = layer->layer_left;
	y0 = layer->layer_top;
	x1 = layer->layer_width + x0 - 1;
	y1 = layer->layer_height + y0 - 1;
	return snprintf(buf, 40, "%d %d %d %d\n", x0, y0, x1, y1);
}

static ssize_t videopip_axis_store(
	struct class *cla,
	struct class_attribute *attr,
	const char *buf, size_t count)
{
	struct disp_info_s *layer = &glayer_info[1];

	mutex_lock(&video_module_mutex);

	set_video_window(layer, buf);

	mutex_unlock(&video_module_mutex);

	return strnlen(buf, count);
}

static ssize_t videopip_crop_show(
	struct class *cla,
	struct class_attribute *attr,
	char *buf)
{
	u32 t, l, b, r;
	struct disp_info_s *layer = &glayer_info[1];

	t = layer->crop_top;
	l = layer->crop_left;
	b = layer->crop_bottom;
	r = layer->crop_right;
	return snprintf(buf, 40, "%d %d %d %d\n", t, l, b, r);
}

static ssize_t videopip_crop_store(
	struct class *cla,
	struct class_attribute *attr,
	const char *buf, size_t count)
{
	struct disp_info_s *layer = &glayer_info[1];

	mutex_lock(&video_module_mutex);

	set_video_crop(layer, buf);

	mutex_unlock(&video_module_mutex);

	return strnlen(buf, count);
}

static ssize_t videopip_disable_show(
	struct class *cla, struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", vd_layer[1].disable_video);
}

static ssize_t videopip_disable_store(
	struct class *cla,
	struct class_attribute *attr,
	const char *buf, size_t count)
{
	int r;
	int val;

	if (debug_flag & DEBUG_FLAG_BASIC_INFO)
		pr_info("%s(%s)\n", __func__, buf);

	r = kstrtoint(buf, 0, &val);
	if (r < 0)
		return -EINVAL;

	if (_videopip_set_disable(val) < 0)
		return -EINVAL;

	return count;
}

static ssize_t videopip_screen_mode_show(
	struct class *cla,
	struct class_attribute *attr,
	char *buf)
{
	struct disp_info_s *layer = &glayer_info[1];
	static const char * const wide_str[] = {
		"normal", "full stretch", "4-3", "16-9", "non-linear",
		"normal-noscaleup",
		"4-3 ignore", "4-3 letter box", "4-3 pan scan", "4-3 combined",
		"16-9 ignore", "16-9 letter box", "16-9 pan scan",
		"16-9 combined", "Custom AR", "AFD"
	};

	if (layer->wide_mode < ARRAY_SIZE(wide_str)) {
		return sprintf(buf, "%d:%s\n",
			layer->wide_mode,
			wide_str[layer->wide_mode]);
	} else
		return 0;
}

static ssize_t videopip_screen_mode_store(
	struct class *cla,
	struct class_attribute *attr,
	const char *buf, size_t count)
{
	unsigned long mode;
	int ret = 0;
	struct disp_info_s *layer = &glayer_info[1];

	ret = kstrtoul(buf, 0, (unsigned long *)&mode);
	if (ret < 0)
		return -EINVAL;

	if ((mode < VIDEO_WIDEOPTION_MAX)
		&& (mode != layer->wide_mode)) {
		layer->wide_mode = mode;
		vd_layer[1].property_changed = true;
	}
	return count;
}

static ssize_t videopip_loop_show(
	struct class *cla, struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", pip_loop);
}

static ssize_t videopip_loop_store(
	struct class *cla,
	struct class_attribute *attr,
	const char *buf, size_t count)
{
	int r;
	int val;

	r = kstrtoint(buf, 0, &val);
	if (r < 0)
		return -EINVAL;

	pip_loop = val;
	return count;
}

static ssize_t videopip_global_output_show(
	struct class *cla,
	struct class_attribute *attr,
	char *buf)
{
	return sprintf(buf, "%d\n",
		vd_layer[1].global_output);
}

static ssize_t videopip_global_output_store(
	struct class *cla,
	struct class_attribute *attr,
	const char *buf, size_t count)
{
	int r;

	r = kstrtoint(buf, 0, &vd_layer[1].global_output);
	if (r < 0)
		return -EINVAL;

	pr_info("%s(%d)\n", __func__, vd_layer[1].global_output);
	return count;
}

static ssize_t videopip_zorder_show(
	struct class *cla,
	struct class_attribute *attr,
	char *buf)
{
	struct disp_info_s *layer = &glayer_info[1];

	return sprintf(buf, "%d\n", layer->zorder);
}

static ssize_t videopip_zorder_store(
	struct class *cla,
	struct class_attribute *attr,
	const char *buf, size_t count)
{
	int zorder;
	int ret = 0;
	struct disp_info_s *layer = &glayer_info[1];

	ret = kstrtoint(buf, 0, &zorder);
	if (ret < 0)
		return -EINVAL;

	if (zorder != layer->zorder) {
		layer->zorder = zorder;
		vd_layer[1].property_changed = true;
	}
	return count;
}

static ssize_t videopip_state_show(
	struct class *cla,
	struct class_attribute *attr,
	char *buf)
{
	ssize_t len = 0;
	struct vppfilter_mode_s *vpp_filter = NULL;

	if (!curpip_frame_par)
		return len;
	vpp_filter = &curpip_frame_par->vpp_filter;
	len += sprintf(buf + len,
		"zoom2_start_x_lines:%u.zoom2_end_x_lines:%u.\n",
		vd_layer[1].start_x_lines, vd_layer[1].end_x_lines);
	len += sprintf(buf + len,
		"zoom2_start_y_lines:%u.zoom2_end_y_lines:%u.\n",
		vd_layer[1].start_y_lines, vd_layer[1].end_x_lines);
	len += sprintf(buf + len, "frame parameters: pic_in_height %u.\n",
		curpip_frame_par->VPP_pic_in_height_);
	len += sprintf(buf + len,
		"frame parameters: VPP_line_in_length_ %u.\n",
		curpip_frame_par->VPP_line_in_length_);
	len += sprintf(buf + len, "vscale_skip_count %u.\n",
		curpip_frame_par->vscale_skip_count);
	len += sprintf(buf + len, "hscale_skip_count %u.\n",
		curpip_frame_par->hscale_skip_count);
	len += sprintf(buf + len, "supscl_path %u.\n",
		curpip_frame_par->supscl_path);
	len += sprintf(buf + len, "supsc0_enable %u.\n",
		curpip_frame_par->supsc0_enable);
	len += sprintf(buf + len, "supsc1_enable %u.\n",
		curpip_frame_par->supsc1_enable);
	len += sprintf(buf + len, "supsc0_hori_ratio %u.\n",
		curpip_frame_par->supsc0_hori_ratio);
	len += sprintf(buf + len, "supsc1_hori_ratio %u.\n",
		curpip_frame_par->supsc1_hori_ratio);
	len += sprintf(buf + len, "supsc0_vert_ratio %u.\n",
		curpip_frame_par->supsc0_vert_ratio);
	len += sprintf(buf + len, "supsc1_vert_ratio %u.\n",
		curpip_frame_par->supsc1_vert_ratio);
	len += sprintf(buf + len, "spsc0_h_in %u.\n",
		curpip_frame_par->spsc0_h_in);
	len += sprintf(buf + len, "spsc1_h_in %u.\n",
		curpip_frame_par->spsc1_h_in);
	len += sprintf(buf + len, "spsc0_w_in %u.\n",
		curpip_frame_par->spsc0_w_in);
	len += sprintf(buf + len, "spsc1_w_in %u.\n",
		curpip_frame_par->spsc1_w_in);
	len += sprintf(buf + len, "video_input_w %u.\n",
		curpip_frame_par->video_input_w);
	len += sprintf(buf + len, "video_input_h %u.\n",
		curpip_frame_par->video_input_h);
	len += sprintf(buf + len, "clk_in_pps %u.\n",
		curpip_frame_par->clk_in_pps);
	len +=
		sprintf(buf + len, "hscale phase step 0x%x.\n",
		vpp_filter->vpp_hsc_start_phase_step);
	len +=
		sprintf(buf + len, "vscale phase step 0x%x.\n",
		vpp_filter->vpp_vsc_start_phase_step);
	len +=
		sprintf(buf + len, "pps pre hsc enable %d.\n",
		vpp_filter->vpp_pre_hsc_en);
	len +=
		sprintf(buf + len, "pps pre vsc enable %d.\n",
		vpp_filter->vpp_pre_vsc_en);
	len +=
		sprintf(buf + len, "hscale filter coef %d.\n",
		vpp_filter->vpp_horz_filter);
	len +=
		sprintf(buf + len, "vscale filter coef %d.\n",
		vpp_filter->vpp_vert_filter);
	len +=
		sprintf(buf + len, "vpp_vert_chroma_filter_en %d.\n",
		vpp_filter->vpp_vert_chroma_filter_en);
	len +=
		sprintf(buf + len, "post_blend_vd_h_start 0x%x.\n",
		curpip_frame_par->VPP_post_blend_vd_h_start_);
	len +=
		sprintf(buf + len, "post_blend_vd_h_end 0x%x.\n",
		curpip_frame_par->VPP_post_blend_vd_h_end_);
	len +=
		sprintf(buf + len, "post_blend_vd_v_start 0x%x.\n",
		curpip_frame_par->VPP_post_blend_vd_v_start_);
	len +=
		sprintf(buf + len, "post_blend_vd_v_end 0x%x.\n",
		curpip_frame_par->VPP_post_blend_vd_v_end_);
	len +=
		sprintf(buf + len, "VPP_hd_start_lines_ 0x%x.\n",
		curpip_frame_par->VPP_hd_start_lines_);
	len +=
		sprintf(buf + len, "VPP_hd_end_lines_ 0x%x.\n",
		curpip_frame_par->VPP_hd_end_lines_);
	len +=
		sprintf(buf + len, "VPP_vd_start_lines_ 0x%x.\n",
		curpip_frame_par->VPP_vd_start_lines_);
	len +=
		sprintf(buf + len, "VPP_vd_end_lines_ 0x%x.\n",
		curpip_frame_par->VPP_vd_end_lines_);
	len +=
		sprintf(buf + len, "VPP_hsc_startp 0x%x.\n",
		curpip_frame_par->VPP_hsc_startp);
	len +=
		sprintf(buf + len, "VPP_hsc_endp 0x%x.\n",
		curpip_frame_par->VPP_hsc_endp);
	len +=
		sprintf(buf + len, "VPP_vsc_startp 0x%x.\n",
		curpip_frame_par->VPP_vsc_startp);
	len +=
		sprintf(buf + len, "VPP_vsc_endp 0x%x.\n",
		curpip_frame_par->VPP_vsc_endp);
	return len;
}

static ssize_t performance_debug_show(
	struct class *cla,
	struct class_attribute *attr,
	char *buf)
{
	return sprintf(buf, "performance_debug: %d\n",
		performance_debug);
}

static ssize_t performance_debug_store(
	struct class *cla,
	struct class_attribute *attr,
	const char *buf, size_t count)
{
	int ret;
	u32 enable;

	ret = kstrtoint(buf, 0, &enable);
	if (ret < 0)
		return -EINVAL;
	performance_debug = enable;
	return count;
}

static ssize_t over_sync_count_show(
	struct class *cla,
	struct class_attribute *attr,
	char *buf)
{
	return sprintf(buf, "over_sync_count: %d\n",
		over_sync_count);
}

static ssize_t min_cpufreq_show(
	struct class *cla,
	struct class_attribute *attr,
	char *buf)
{
	return sprintf(buf, "mincpufreq: %d\n", min_cpufreq);
}

static ssize_t min_cpufreq_store(
	struct class *cla,
	struct class_attribute *attr,
	const char *buf, size_t count)
{
	int ret;
	u32 freq;

	ret = kstrtoint(buf, 0, &freq);
	if (ret < 0)
		return -EINVAL;
	min_cpufreq = freq;
	return count;
}

s32 set_video_path_select(const char *recv_name, u8 layer_id)
{
	u32 new_path_id;
	struct disp_info_s *layer_info;
	struct video_layer_s *layer;

	if (!recv_name ||
	    (layer_id >= MAX_VD_LAYERS))
		return -1;

	layer_info = &glayer_info[layer_id];
	layer = &vd_layer[layer_id];
	new_path_id = layer_info->display_path_id;
	if (!strcmp(recv_name, "default"))
		new_path_id = VFM_PATH_DEF;
	else if (!strcmp(recv_name, RECEIVER_NAME))
		new_path_id = VFM_PATH_AMVIDEO;
	else if (!strcmp(recv_name, RECEIVERPIP_NAME))
		new_path_id = VFM_PATH_PIP;
	else if (!strcmp(recv_name, "video_render.0"))
		new_path_id = VFM_PATH_VIDEO_RENDER0;
	else if (!strcmp(recv_name, "video_render.1"))
		new_path_id = VFM_PATH_VIDEO_RENDER1;
	else if (!strcmp(recv_name, "auto"))
		new_path_id = VFM_PATH_AUTO;
	else if (!strcmp(recv_name, "invalid"))
		new_path_id = VFM_PATH_INVAILD;
	if (layer_info->display_path_id != new_path_id) {
		pr_info("VID: store VD%d path_id changed %d->%d\n",
			layer_id, layer_info->display_path_id, new_path_id);
		layer_info->display_path_id = new_path_id;
		layer->property_changed = true;
		if (new_path_id == VFM_PATH_AUTO)
			layer_info->sideband_type = -1;
	}
	return 0;
}
EXPORT_SYMBOL(set_video_path_select);

s32 set_sideband_type(s32 type, u8 layer_id)
{
	struct disp_info_s *layer_info;

	if (layer_id >= MAX_VD_LAYERS)
		return -1;

	layer_info = &glayer_info[layer_id];
	pr_info("VID: sideband_type %d changed to %d\n",
		layer_info->sideband_type, type);
	layer_info->sideband_type = type;

	return 0;
}
EXPORT_SYMBOL(set_sideband_type);

static ssize_t path_select_show(
	struct class *cla,
	struct class_attribute *attr,
	char *buf)
{
	return snprintf(buf, 40, "vd1: %d vd2: %d\n",
		glayer_info[0].display_path_id,
		glayer_info[1].display_path_id);
}

static ssize_t path_select_store(
	struct class *cla,
	struct class_attribute *attr,
	const char *buf, size_t count)
{
	int parsed[2];

	if (likely(parse_para(buf, 2, parsed) == 2)) {
		if (glayer_info[0].display_path_id != parsed[0]) {
			pr_info("VID: store VD1 path_id changed %d->%d\n",
				glayer_info[0].display_path_id, parsed[0]);
			glayer_info[0].display_path_id = parsed[0];
			vd_layer[0].property_changed = true;
		}
		if (glayer_info[1].display_path_id != parsed[1]) {
			pr_info("VID: store VD2 path_id changed %d->%d\n",
				glayer_info[1].display_path_id, parsed[1]);
			glayer_info[1].display_path_id = parsed[1];
			vd_layer[1].property_changed = true;
		}
	}
	return strnlen(buf, count);
}

static ssize_t vpp_crc_show(
	struct class *cla,
	struct class_attribute *attr,
	char *buf)
{
	return snprintf(buf, 64, "vpp_crc_en: %d vpp_crc_result: %x\n\n",
		vpp_crc_en,
		vpp_crc_result);
}

static ssize_t vpp_crc_store(
	struct class *cla,
	struct class_attribute *attr,
	const char *buf, size_t count)
{
	int ret;

	ret = kstrtoint(buf, 0, &vpp_crc_en);
	if (ret < 0)
		return -EINVAL;
	return count;
}

static ssize_t vpp_crc_viu2_show(
	struct class *cla,
	struct class_attribute *attr,
	char *buf)
{
	int vpp_crc_viu2_result = 0;

	if (amvideo_meson_dev.cpu_type >= MESON_CPU_MAJOR_ID_SC2_)
		vpp_crc_viu2_result = vpp_crc_viu2_check(vpp_crc_viu2_en);
	return snprintf(buf, 64, "crc_viu2_en: %d crc_vui2_result: %x\n\n",
		vpp_crc_viu2_en,
		vpp_crc_viu2_result);
}

static ssize_t vpp_crc_viu2_store(
	struct class *cla,
	struct class_attribute *attr,
	const char *buf, size_t count)
{
	int ret;

	ret = kstrtoint(buf, 0, &vpp_crc_viu2_en);
	if (ret < 0)
		return -EINVAL;
	if (amvideo_meson_dev.cpu_type >= MESON_CPU_MAJOR_ID_SC2_)
		enable_vpp_crc_viu2(vpp_crc_viu2_en);
	return count;
}

static ssize_t pip_alpha_store(
	struct class *cla,
	struct class_attribute *attr,
	const char *buf, size_t count)
{
	int i = 0;
	int param_num;
	int parsed[66];
	int layer_id, win_num, win_en = 0;
	struct pip_alpha_scpxn_s alpha_win;

	if (likely(parse_para(buf, 2, parsed) >= 2)) {
		layer_id = parsed[0];
		win_num = parsed[1];
		param_num = win_num * 4 + 2;
		if (likely(parse_para(buf, param_num, parsed) == param_num)) {
			for (i = 0; i < win_num; i++) {
				alpha_win.scpxn_bgn_h[i] = parsed[2 + i * 4];
				alpha_win.scpxn_end_h[i] = parsed[3 + i * 4];
				alpha_win.scpxn_bgn_v[i] = parsed[4 + i * 4];
				alpha_win.scpxn_end_v[i] = parsed[5 + i * 4];
				win_en |= 1 << i;
			}
			pr_info("layer_id=%d, win_num=%d, win_en=%d\n",
				layer_id, win_num, win_en);
			set_alpha(layer_id, win_en, &alpha_win);
		}
	}

	return strnlen(buf, count);
}

static ssize_t film_grain_show(
	struct class *cla,
	struct class_attribute *attr,
	char *buf)
{
	return snprintf(buf, 40, "fgrain_enable vd1: %d vd2: %d\n",
		glayer_info[0].fgrain_enable,
		glayer_info[1].fgrain_enable);
}

static ssize_t film_grain_store(
	struct class *cla,
	struct class_attribute *attr,
	const char *buf, size_t count)
{
	int parsed[2];

	if (likely(parse_para(buf, 2, parsed) == 2)) {
		glayer_info[0].fgrain_enable = parsed[0];
		glayer_info[1].fgrain_enable = parsed[1];
	}
	return strnlen(buf, count);
}

/*
 * default setting scenes is 23
 */
static ssize_t pq_default_show(struct class *cla,
			       struct class_attribute *attr, char *buf)
{
	ssize_t count;
	int i = 0;
	char end[4] = "\n";
	char temp[20] = "default scene pq:";

	count = sprintf(buf, "%s", temp);
	while (i < SCENES_VALUE)
		count += sprintf(buf + count, " %d",
				 vpp_scenes[AI_SCENES_MAX - 1].pq_values[i++]);
	count += sprintf(buf + count, " %s", end);
	return count;
}

static ssize_t pq_default_store(struct class *cla,
				struct class_attribute *attr,
				const char *buf, size_t count)
{
	int i = 0;
	int parsed[SCENES_VALUE];

	if (likely(parse_para(buf, SCENES_VALUE, parsed) == SCENES_VALUE)) {
		vpp_scenes[AI_SCENES_MAX - 1].pq_scenes = DEFAUT_SETTING;
		while (i < SCENES_VALUE) {
			vpp_scenes[AI_SCENES_MAX - 1].pq_values[i] = parsed[i];
			i++;
		}
	}
	i = 0;
	pr_info("the default scene pq param: ");
	while (i < SCENES_VALUE)
		pr_info("%d ", vpp_scenes[AI_SCENES_MAX - 1].pq_values[i++]);
	pr_info("\n");

	return strnlen(buf, count);
}

static ssize_t pq_data_store(struct class *cla,
			     struct class_attribute *attr,
			     const char *buf, size_t count)

{
	int parsed[4];
	ssize_t buflen;
	int ret;

	buflen = strlen(buf);
	if (buflen <= 0)
		return 0;

	ret = parse_para(buf, 4, parsed);
	if (ret == 4 && parsed[0]) {
		if (parsed[1] < AI_SCENES_MAX && parsed[1] >= 0 &&
		    parsed[2] < SCENES_VALUE && parsed[2] >= 0 &&
		    parsed[3] >= 0) {
			vpp_scenes[parsed[1]].pq_scenes = parsed[1];
			vpp_scenes[parsed[1]].pq_values[parsed[2]] = parsed[3];

		} else {
			pr_info("the 2nd param: 0~23,the 3rd param: 0~9,the 4th param: >=0\n");
		}
	} else if (ret == 3 && parsed[0] == 0) {
		if (parsed[1] < AI_SCENES_MAX && parsed[1] >= 0 &&
		    parsed[2] < SCENES_VALUE && parsed[2] >= 0)
			pr_info("pq value: %d\n",
				vpp_scenes[parsed[1]].pq_values[parsed[2]]);
		else
			pr_info("the 2nd param: 0~23,the 3rd param: 0~9\n");
	} else {
		if (parsed[0] == 0)
			pr_info("1st param 0 is to read pq value,need set 3 param\n");
		else
			pr_info("1st param 1 is to set pq value,need set 4 param\n");
	}
	return strnlen(buf, count);
}

static ssize_t hscaler_8tap_enable_show(
	struct class *cla,
	struct class_attribute *attr,
	char *buf)
{
	return snprintf(buf, 64, "hscaler_8tap_en: %d\n\n",
		hscaler_8tap_enable[0]);
}

static ssize_t hscaler_8tap_enable_store(
		struct class *cla,
		struct class_attribute *attr,
		const char *buf, size_t count)
{
	int ret;
	int hscaler_8tap_en;

	ret = kstrtoint(buf, 0, &hscaler_8tap_en);
	if (ret < 0)
		return -EINVAL;

	if (amvideo_meson_dev.has_hscaler_8tap[0] &&
	    (hscaler_8tap_en != hscaler_8tap_enable[0])) {
		hscaler_8tap_enable[0] = hscaler_8tap_en;
	}
	return count;
}

static ssize_t pre_hscaler_ntap_enable_show(
	struct class *cla,
	struct class_attribute *attr,
	char *buf)
{
	if (pre_hscaler_ntap_set[0] == 0xff)
		return snprintf(buf, 64, "pre_hscaler_ntap_en(0xff):%d\n",
			pre_hscaler_ntap_enable[0]);
	else
		return snprintf(buf, 64, "pre_hscaler_ntap_en: %d\n",
			pre_hscaler_ntap_set[0]);
}

static ssize_t pre_hscaler_ntap_enable_store(
	struct class *cla,
	struct class_attribute *attr,
	const char *buf, size_t count)
{
	int ret;
	int pre_hscaler_ntap_en;

	ret = kstrtoint(buf, 0, &pre_hscaler_ntap_en);
	if (ret < 0)
		return -EINVAL;

	if (pre_hscaler_ntap_en != pre_hscaler_ntap_set[0])
		pre_hscaler_ntap_set[0] = pre_hscaler_ntap_en;
	return count;
}

static ssize_t pip_pre_hscaler_ntap_enable_show(
	struct class *cla,
	struct class_attribute *attr,
	char *buf)
{
	if (pre_hscaler_ntap_set[1] == 0xff)
		return snprintf(buf, 64, "pip_pre_hscaler_ntap_en(0xff):%d\n",
			pre_hscaler_ntap_enable[1]);
	else
		return snprintf(buf, 64, "pip_pre_hscaler_ntap_en: %d\n",
			pre_hscaler_ntap_set[1]);
}

static ssize_t pip_pre_hscaler_ntap_enable_store(
	struct class *cla,
	struct class_attribute *attr,
	const char *buf, size_t count)
{
	int ret;
	int pre_hscaler_ntap_en;

	ret = kstrtoint(buf, 0, &pre_hscaler_ntap_en);
	if (ret < 0)
		return -EINVAL;

	if (pre_hscaler_ntap_en != pre_hscaler_ntap_set[1])
		pre_hscaler_ntap_set[1] = pre_hscaler_ntap_en;
	return count;
}

static ssize_t pre_vscaler_ntap_enable_show(
	struct class *cla,
	struct class_attribute *attr,
	char *buf)
{
	if (pre_vscaler_ntap_set[0] == 0xff)
		return snprintf(buf, 64, "pre_vscaler_ntap_en(0xff):%d\n",
			pre_vscaler_ntap_enable[0]);
	else
		return snprintf(buf, 64, "pre_vscaler_ntap_en: %d\n",
			pre_vscaler_ntap_set[0]);
}

static ssize_t pre_vscaler_ntap_enable_store(
	struct class *cla,
	struct class_attribute *attr,
	const char *buf, size_t count)
{
	int ret;
	int pre_vscaler_ntap_en;

	ret = kstrtoint(buf, 0, &pre_vscaler_ntap_en);
	if (ret < 0)
		return -EINVAL;

	if (pre_vscaler_ntap_en != pre_vscaler_ntap_set[0])
		pre_vscaler_ntap_set[0] = pre_vscaler_ntap_en;
	return count;
}

static ssize_t primary_src_fmt_show(
	struct class *cla, struct class_attribute *attr, char *buf)
{
	int ret = 0;
	enum vframe_signal_fmt_e fmt;

	fmt = atomic_read(&cur_primary_src_fmt);
	if (fmt != VFRAME_SIGNAL_FMT_INVALID)
		ret += sprintf(buf + ret, "src_fmt = %s\n",
			src_fmt_str[fmt]);
	else
		ret += sprintf(buf + ret, "src_fmt = invaild\n");
	return ret;
}

static ssize_t status_changed_show(
	struct class *cla, struct class_attribute *attr, char *buf)
{
	u32 status = 0;

	status = atomic_read(&status_changed);
	return sprintf(buf, "0x%x\n", status);
}

static ssize_t cur_ai_scenes_show(struct class *cla,
				  struct class_attribute *attr, char *buf)
{
	ssize_t count;
	int i = 0;

	if (!cur_dispbuf)
		return 0;

	if (nn_scenes_value[0].maxprob == 0)
		return 0;
	if (vd_layer[0].disable_video == 1 ||
	    vd_layer[0].global_output == 0)
		return 0;
	count = 0;
	while (i < AI_PQ_TOP) {
		count += sprintf(buf + count, "%d:",
			nn_scenes_value[i].maxclass);
		count += sprintf(buf + count, "%d;",
			nn_scenes_value[i].maxprob);
		i++;
	}
	count += sprintf(buf + count, "\n");
	return count;
}

static ssize_t force_switch_vf_show(
	struct class *cla,
	struct class_attribute *attr,
	char *buf)
{
	return sprintf(buf, "%d\n", force_switch_vf_mode);
}

static ssize_t force_switch_vf_store(
	struct class *cla,
	struct class_attribute *attr,
	const char *buf,
	size_t count)
{
	int ret;
	int force;

	ret = kstrtoint(buf, 0, &force);
	if (ret < 0)
		return -EINVAL;

	if ((force >= 0) && (force <= 2) &&
	    (force_switch_vf_mode != force)) {
		force_switch_vf_mode = force;
		vd_layer[0].property_changed = true;
		vd_layer[1].property_changed = true;
	}
	return count;
}

static ssize_t force_property_change_store(
	struct class *cla,
	struct class_attribute *attr,
	const char *buf,
	size_t count)
{
	int ret;
	int force;

	ret = kstrtoint(buf, 0, &force);
	if (ret < 0)
		return -EINVAL;

	if (force) {
		vd_layer[0].property_changed = true;
		vd_layer[1].property_changed = true;
	}
	return count;
}

static ssize_t probe_en_store(
	struct class *cla,
	struct class_attribute *attr,
	const char *buf, size_t count)
{
	int ret;
	int probe_en;

	ret = kstrtoint(buf, 0, &probe_en);
	if (ret < 0)
		return -EINVAL;

	vpp_probe_en_set(probe_en);
	return count;
}

static ssize_t mirror_axis_show(struct class *cla,
				struct class_attribute *attr,
				char *buf)
{
	return snprintf(buf, 40, "%d (1: H_MIRROR 2: V_MIRROR)\n", mirror);
}

static ssize_t mirror_axis_store(struct class *cla,
				 struct class_attribute *attr,
				 const char *buf, size_t count)
{
	int res = 0;
	int ret = 0;

	ret = kstrtoint(buf, 0, &res);
	if (ret) {
		pr_err("kstrtoint err\n");
		return -EINVAL;
	}
	pr_info("mirror: %d->%d (1: H_MIRROR 2: V_MIRROR)\n", mirror, res);
	mirror = res;

	return count;
}

static ssize_t pps_coefs_store(struct class *cla,
				    struct class_attribute *attr,
				    const char *buf, size_t count)
{
	int parsed[3];
	int layer_id = 0, bit9_mode = 0, coef_type = 0;

	if (likely(parse_para(buf, 3, parsed) == 3)) {
		layer_id = parsed[0];
		bit9_mode = parsed[1];
		coef_type = parsed[2];
	}

	dump_pps_coefs_info(layer_id, bit9_mode, coef_type);
	return strnlen(buf, count);
}

static ssize_t load_pps_coefs_store(struct class *cla,
				struct class_attribute *attr,
				 const char *buf, size_t count)
{
	int res = 0;
	int ret = 0;

	ret = kstrtoint(buf, 0, &res);
	if (ret) {
		pr_err("kstrtoint err\n");
		return -EINVAL;
	}
	load_pps_coef = res;
	return count;
}

static ssize_t vd1_vd2_mux_show(struct class *cla,
				struct class_attribute *attr,
				char *buf)
{
	return snprintf(buf, 40, "vd1_vd2_mux:%d(for t5d revb)\n", vd1_vd2_mux);
}

static ssize_t vd1_vd2_mux_store(struct class *cla,
				 struct class_attribute *attr,
				 const char *buf, size_t count)
{
	int res = 0;
	int ret = 0;

	ret = kstrtoint(buf, 0, &res);
	if (ret) {
		pr_err("kstrtoint err\n");
		return -EINVAL;
	}
	vd1_vd2_mux = res;
	vd_layer[0].vd1_vd2_mux = res;
	if (vd1_vd2_mux)
		di_used_vd1_afbc(true);
	else
		di_used_vd1_afbc(false);
	return count;
}

static struct class_attribute amvideo_class_attrs[] = {
	__ATTR(axis,
	       0664,
	       video_axis_show,
	       video_axis_store),
	__ATTR(crop,
	       0644,
	       video_crop_show,
	       video_crop_store),
	__ATTR(sr,
	       0644,
	       video_sr_show,
	       video_sr_store),
	__ATTR(global_offset,
	       0644,
	       video_global_offset_show,
	       video_global_offset_store),
	__ATTR(screen_mode,
	       0664,
	       video_screen_mode_show,
	       video_screen_mode_store),
	__ATTR(blackout_policy,
	       0664,
	       video_blackout_policy_show,
	       video_blackout_policy_store),
	__ATTR(blackout_pip_policy,
			0664,
			videopip_blackout_policy_show,
			videopip_blackout_policy_store),
	__ATTR(video_seek_flag,
	       0664,
	       video_seek_flag_show,
	       video_seek_flag_store),
	__ATTR(video_delay_val,
		   0664,
		   video_videodelay_show,
		   video_videodelay_store),
	__ATTR(disable_video,
	       0664,
	       video_disable_show,
	       video_disable_store),
	 __ATTR(video_global_output,
	       0664,
	       video_global_output_show,
	       video_global_output_store),
	__ATTR(hold_video,
	       0664,
	       video_hold_show,
	       video_hold_store),
	__ATTR(zoom,
	       0664,
	       video_zoom_show,
	       video_zoom_store),
	__ATTR(brightness,
	       0644,
	       video_brightness_show,
	       video_brightness_store),
	__ATTR(contrast,
	       0644,
	       video_contrast_show,
	       video_contrast_store),
	__ATTR(vpp_brightness,
	       0644,
	       vpp_brightness_show,
	       vpp_brightness_store),
	__ATTR(vpp_contrast,
	       0644,
	       vpp_contrast_show,
	       vpp_contrast_store),
	__ATTR(saturation,
	       0644,
	       video_saturation_show,
	       video_saturation_store),
	__ATTR(vpp_saturation_hue,
	       0644,
	       vpp_saturation_hue_show,
	       vpp_saturation_hue_store),
	__ATTR(video_background,
	       0644,
	       video_background_show,
	       video_background_store),
	__ATTR(test_screen,
	       0644,
	       video_test_screen_show,
	       video_test_screen_store),
	__ATTR(rgb_screen,
	       0644,
	       video_rgb_screen_show,
	       video_rgb_screen_store),
	__ATTR(file_name,
	       0644,
	       video_filename_show,
	       video_filename_store),
	__ATTR(debugflags,
	       0644,
	       video_debugflags_show,
	       video_debugflags_store),
	__ATTR(trickmode_duration,
	       0644,
	       trickmode_duration_show,
	       trickmode_duration_store),
	__ATTR(nonlinear_factor,
	       0644,
	       video_nonlinear_factor_show,
	       video_nonlinear_factor_store),
	__ATTR(nonlinear_t_factor,
	       0644,
	       video_nonlinear_t_factor_show,
	       video_nonlinear_t_factor_store),
	__ATTR(freerun_mode,
	       0644,
	       video_freerun_mode_show,
	       video_freerun_mode_store),
	__ATTR(video_speed_check_h_w,
	       0644,
	       video_speed_check_show,
	       video_speed_check_store),
	__ATTR(threedim_mode,
	       0644,
	       threedim_mode_show,
	       threedim_mode_store),
	__ATTR(vsync_pts_inc_upint,
	       0644,
	       video_vsync_pts_inc_upint_show,
	       video_vsync_pts_inc_upint_store),
	__ATTR(vsync_slow_factor,
	       0644,
	       video_vsync_slow_factor_show,
	       video_vsync_slow_factor_store),
	__ATTR(angle,
	       0644,
	       video_angle_show,
	       video_angle_store),
	__ATTR(stereo_scaler,
	       0644, NULL,
	       video_3d_scale_store),
	__ATTR(show_first_frame_nosync,
	       0644,
	       show_first_frame_nosync_show,
	       show_first_frame_nosync_store),
	__ATTR(show_first_picture,
	       0664, NULL,
	       show_first_picture_store),
	__ATTR(slowsync_repeat_enable,
	       0644,
	       slowsync_repeat_enable_show,
	       slowsync_repeat_enable_store),
	__ATTR(free_keep_buffer,
	       0664, NULL,
	       video_free_keep_buffer_store),
	__ATTR(hdmin_delay_start,
	       0664,
	       hdmin_delay_start_show,
	       hdmin_delay_start_store),
	__ATTR(hdmin_delay_duration,
	       0664,
	       hdmin_delay_duration_show,
	       hdmin_delay_duration_store),
	__ATTR(hdmin_delay_max_ms,
	       0664,
	       hdmin_delay_max_ms_show,
	       hdmin_delay_max_ms_store),
	__ATTR(vframe_walk_delay,
	       0664,
	       vframe_walk_delay_show, NULL),
	__ATTR(last_required_total_delay,
	       0664,
	       last_required_total_delay_show, NULL),
	__ATTR(free_cma_buffer,
	       0664, NULL,
	       free_cma_buffer_store),
#ifdef CONFIG_AM_VOUT
	__ATTR_RO(device_resolution),
#endif
#ifdef PTS_TRACE_DEBUG
	__ATTR_RO(pts_trace),
#endif
	__ATTR(video_inuse,
	       0664,
	       video_inuse_show,
	       video_inuse_store),
	 __ATTR(video_zorder,
	       0664,
	       video_zorder_show,
	       video_zorder_store),
	 __ATTR(black_threshold,
	       0664,
	       black_threshold_show,
	       black_threshold_store),
	 __ATTR(get_di_count,
		0664,
		get_di_count_show,
		get_di_count_store),
	 __ATTR(put_di_count,
		0664,
		put_di_count_show,
		put_di_count_store),
	 __ATTR(di_release_count,
		0664,
		di_release_count_show,
		di_release_count_store),
	 __ATTR(hist_test,
	       0664,
	       hist_test_show,
	       hist_test_store),
	__ATTR(video_ipt,
		  0664,
		  video_ipt_show,
		  video_ipt_store),
	__ATTR_RO(frame_addr),
	__ATTR_RO(frame_canvas_width),
	__ATTR_RO(frame_canvas_height),
	__ATTR_RO(frame_width),
	__ATTR_RO(frame_height),
	__ATTR_RO(frame_format),
	__ATTR_RO(frame_aspect_ratio),
	__ATTR_RO(frame_rate),
	__ATTR_RO(vframe_states),
	__ATTR_RO(video_state),
	__ATTR_RO(fps_info),
	__ATTR_RO(vframe_ready_cnt),
	__ATTR_RO(video_layer1_state),
	__ATTR_RO(pic_mode_info),
	__ATTR_RO(src_fmt),
	__ATTR_RO(cur_aipq_sp),
	__ATTR(axis_pip,
	       0664,
	       videopip_axis_show,
	       videopip_axis_store),
	__ATTR(crop_pip,
	       0664,
	       videopip_crop_show,
	       videopip_crop_store),
	__ATTR(disable_videopip,
	       0664,
	       videopip_disable_show,
	       videopip_disable_store),
	__ATTR(screen_mode_pip,
	       0664,
	       videopip_screen_mode_show,
	       videopip_screen_mode_store),
	__ATTR(videopip_loop,
	       0664,
	       videopip_loop_show,
	       videopip_loop_store),
	 __ATTR(pip_global_output,
	       0664,
	       videopip_global_output_show,
	       videopip_global_output_store),
	 __ATTR(videopip_zorder,
	       0664,
	       videopip_zorder_show,
	       videopip_zorder_store),
	__ATTR_RO(videopip_state),
	__ATTR(videopip_ipt,
		  0664,
		  videopip_ipt_show,
		  videopip_ipt_store),
	__ATTR(path_select,
	       0664,
	       path_select_show,
	       path_select_store),
	__ATTR(vpp_crc,
	       0664,
	       vpp_crc_show,
	       vpp_crc_store),
	__ATTR(vpp_crc_viu2,
	       0664,
	       vpp_crc_viu2_show,
	       vpp_crc_viu2_store),
	__ATTR(pip_alpha,
	       0220,
	       NULL,
	       pip_alpha_store),
	__ATTR(film_grain,
	       0664,
	       film_grain_show,
	       film_grain_store),
	__ATTR(pq_default,
	       0664,
	       pq_default_show,
	       pq_default_store),
	__ATTR(hscaler_8tap_en,
	       0664,
	       hscaler_8tap_enable_show,
	       hscaler_8tap_enable_store),
	__ATTR(pre_hscaler_ntap_en,
	       0664,
	       pre_hscaler_ntap_enable_show,
	       pre_hscaler_ntap_enable_store),
	__ATTR(pip_pre_hscaler_ntap_en,
	       0664,
	       pip_pre_hscaler_ntap_enable_show,
	       pip_pre_hscaler_ntap_enable_store),
	__ATTR(pre_vscaler_ntap_en,
	       0664,
	       pre_vscaler_ntap_enable_show,
	       pre_vscaler_ntap_enable_store),
	__ATTR_WO(pq_data),
	__ATTR_RO(cur_ai_scenes),
	__ATTR(performance_debug,
	       0664,
	       performance_debug_show,
	       performance_debug_store),
	__ATTR(force_switch_vf,
	       0644,
	       force_switch_vf_show,
	       force_switch_vf_store),
	__ATTR(force_property_change,
	       0644, NULL,
	       force_property_change_store),
	__ATTR(probe_en,
	       0644,
	       NULL,
	       probe_en_store),
	__ATTR(mirror,
	       0664,
	       mirror_axis_show,
	       mirror_axis_store),
	__ATTR(min_cpufreq,
	       0664,
	       min_cpufreq_show,
	       min_cpufreq_store),
	__ATTR(over_sync_count,
	       0664,
	       over_sync_count_show,
	       NULL),
	__ATTR(pps_coefs,
	       0664,
	       NULL,
	       pps_coefs_store),
	__ATTR(load_pps_coefs,
	       0664,
	       NULL,
	       load_pps_coefs_store),
	__ATTR(enable_hdmi_delay_normal_check,
	       0664,
	       enable_hdmi_delay_check_show,
	       enable_hdmi_delay_check_store),
	__ATTR(hdmin_delay_count_debug,
	       0664,
	       hdmi_delay_debug_show,
	       NULL),
	__ATTR(vd1_vd2_mux,
	       0664,
	       vd1_vd2_mux_show,
	       vd1_vd2_mux_store),
	__ATTR_NULL
};

static struct class_attribute amvideo_poll_class_attrs[] = {
	__ATTR_RO(frame_width),
	__ATTR_RO(frame_height),
	__ATTR_RO(vframe_states),
	__ATTR_RO(video_state),
	__ATTR_RO(primary_src_fmt),
	__ATTR_RO(status_changed),
	__ATTR_NULL
};

#ifdef CONFIG_PM
static int amvideo_class_suspend(struct device *dev, pm_message_t state)
{
#if 0

	pm_state.event = state.event;

	if (state.event == PM_EVENT_SUSPEND) {
		pm_state.vpp_misc =
			READ_VCBUS_REG(VPP_MISC + cur_dev->vpp_off);

		DisableVideoLayer_NoDelay();

		msleep(50);
		/* #if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8 */
		if ((get_cpu_type() >= MESON_CPU_MAJOR_ID_M8)
		    && !is_meson_mtvd_cpu()) {
			vpu_delay_work_flag = 0;
		}
		/* #endif */

	}
#endif
	return 0;
}

static int amvideo_class_resume(struct device *dev)
{
#if 0
#define VPP_MISC_VIDEO_BITS_MASK \
	((VPP_VD2_ALPHA_MASK << VPP_VD2_ALPHA_BIT) | \
	VPP_VD2_PREBLEND | VPP_VD1_PREBLEND |\
	VPP_VD2_POSTBLEND | VPP_VD1_POSTBLEND | VPP_POSTBLEND_EN)\

	if (pm_state.event == PM_EVENT_SUSPEND) {
		/* #if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8 */
		if ((get_cpu_type() >= MESON_CPU_MAJOR_ID_M8)
		    && !is_meson_mtvd_cpu()) {
			switch_vpu_mem_pd_vmod(VPU_VIU_VD1,
					       pm_state.mem_pd_vd1);
			switch_vpu_mem_pd_vmod(VPU_VIU_VD2,
					       pm_state.mem_pd_vd2);
			switch_vpu_mem_pd_vmod(VPU_DI_POST,
					       pm_state.mem_pd_di_post);
		}
		/* #endif */
		WRITE_VCBUS_REG(VPP_MISC + cur_dev->vpp_off,
			(READ_VCBUS_REG(VPP_MISC + cur_dev->vpp_off) &
			(~VPP_MISC_VIDEO_BITS_MASK)) |
			(pm_state.vpp_misc & VPP_MISC_VIDEO_BITS_MASK));
		WRITE_VCBUS_REG(VPP_MISC +
			cur_dev->vpp_off, pm_state.vpp_misc);

		pm_state.event = -1;
		if (debug_flag & DEBUG_FLAG_BASIC_INFO) {
			pr_info("%s write(VPP_MISC,%x)\n", __func__,
			       pm_state.vpp_misc);
		}
	}
#ifdef CONFIG_SCREEN_ON_EARLY
	if (power_key_pressed) {
		vout_pll_resume_early();
		osd_resume_early();
		resume_vout_early();
		power_key_pressed = 0;
	}
#endif
#endif
	return 0;
}
#endif

static struct class amvideo_class = {
	.name = AMVIDEO_CLASS_NAME,
	.class_attrs = amvideo_class_attrs,
#ifdef CONFIG_PM
	.suspend = amvideo_class_suspend,
	.resume = amvideo_class_resume,
#endif
};

static struct class amvideo_poll_class = {
	.name = AMVIDEO_POLL_CLASS_NAME,
	.class_attrs = amvideo_poll_class_attrs,
};

#ifdef TV_REVERSE
static int __init vpp_axis_reverse(char *str)
{
	unsigned char *ptr = str;

	pr_info("%s: bootargs is %s\n", __func__, str);
	if (strstr(ptr, "1"))
		reverse = true;
	else
		reverse = false;

	return 0;
}

__setup("video_reverse=", vpp_axis_reverse);
#endif

struct vframe_s *get_cur_dispbuf(void)
{
	return cur_dispbuf;
}

#ifdef CONFIG_AM_VOUT
int vout_notify_callback(struct notifier_block *block, unsigned long cmd,
			 void *para)
{
	const struct vinfo_s *info;
	ulong flags;

	switch (cmd) {
	case VOUT_EVENT_MODE_CHANGE:
		info = get_current_vinfo();
		spin_lock_irqsave(&lock, flags);
		vinfo = info;
		/* pre-calculate vsync_pts_inc in 90k unit */
		vsync_pts_inc = 90000 * vinfo->sync_duration_den /
				vinfo->sync_duration_num;
		vsync_pts_inc_scale = vinfo->sync_duration_den;
		vsync_pts_inc_scale_base = vinfo->sync_duration_num;
		spin_unlock_irqrestore(&lock, flags);
		if (vinfo->name)
			strncpy(new_vmode, vinfo->name, sizeof(new_vmode) - 1);
#ifdef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
		pr_info("DOLBY: vout_notify_callback: VOUT_EVENT_MODE_CHANGE\n");
		/* force send hdmi pkt in dv code */
		/* to workaround pkt cleaned during hotplug */
		if (is_dolby_vision_enable())
			dolby_vision_set_toggle_flag(2);
		else
			set_cur_hdr_policy(0xff);
#endif
		break;
	case VOUT_EVENT_OSD_PREBLEND_ENABLE:
		break;
	case VOUT_EVENT_OSD_DISP_AXIS:
		break;
	}
	return 0;
}

static struct notifier_block vout_notifier = {
	.notifier_call = vout_notify_callback,
};

static void vout_hook(void)
{
	vout_register_client(&vout_notifier);

	vinfo = get_current_vinfo();

	if (!vinfo) {
#if DEBUG_TMP
		set_current_vmode(VMODE_720P);
#endif
		vinfo = get_current_vinfo();
	}

	if (vinfo) {
		vsync_pts_inc = 90000 * vinfo->sync_duration_den /
			vinfo->sync_duration_num;
		vsync_pts_inc_scale = vinfo->sync_duration_den;
		vsync_pts_inc_scale_base = vinfo->sync_duration_num;
		if (vinfo->name) {
			strncpy(old_vmode, vinfo->name, sizeof(old_vmode) - 1);
			strncpy(new_vmode, vinfo->name, sizeof(new_vmode) - 1);
		}
	}
#ifdef CONFIG_AM_VIDEO_LOG
	if (vinfo) {
		amlog_mask(LOG_MASK_VINFO, "vinfo = %p\n", vinfo);
		amlog_mask(LOG_MASK_VINFO, "display platform %s:\n",
			   vinfo->name);
		amlog_mask(LOG_MASK_VINFO, "\tresolution %d x %d\n",
			   vinfo->width, vinfo->height);
		amlog_mask(LOG_MASK_VINFO, "\taspect ratio %d : %d\n",
			   vinfo->aspect_ratio_num, vinfo->aspect_ratio_den);
		amlog_mask(LOG_MASK_VINFO, "\tsync duration %d : %d\n",
			   vinfo->sync_duration_num, vinfo->sync_duration_den);
	}
#endif
}
#endif

static int amvideo_notify_callback(
	struct notifier_block *block,
	unsigned long cmd,
	void *para)
{
	u32 *p, val;
	static struct vd_signal_info_s vd_signal;

	switch (cmd) {
	case AMVIDEO_UPDATE_OSD_MODE:
		p = (u32 *)para;
		if (!update_osd_vpp_misc)
			osd_vpp_misc_mask = p[1];
		val = osd_vpp_misc
			& (~osd_vpp_misc_mask);
		val |= (p[0] & osd_vpp_misc_mask);
		osd_vpp_misc = val;
		if (!update_osd_vpp_misc)
			update_osd_vpp_misc = true;
		break;
	case AMVIDEO_UPDATE_PREBLEND_MODE:
		p = (u32 *)para;
		osd_preblend_en = p[0];
		break;
	case AMVIDEO_UPDATE_SIGNAL_MODE:
		memcpy(&vd_signal, para,
			sizeof(struct vd_signal_info_s));
		vd_signal_notifier_call_chain
			(VIDEO_SIGNAL_TYPE_CHANGED,
			&vd_signal);
		break;
	default:
		break;
	}
	return 0;
}

static struct notifier_block amvideo_notifier = {
	.notifier_call = amvideo_notify_callback,
};

static RAW_NOTIFIER_HEAD(amvideo_notifier_list);
int amvideo_register_client(struct notifier_block *nb)
{
	return raw_notifier_chain_register(&amvideo_notifier_list, nb);
}
EXPORT_SYMBOL(amvideo_register_client);

int amvideo_unregister_client(struct notifier_block *nb)
{
	return raw_notifier_chain_unregister(&amvideo_notifier_list, nb);
}
EXPORT_SYMBOL(amvideo_unregister_client);

int amvideo_notifier_call_chain(unsigned long val, void *v)
{
	return raw_notifier_call_chain(&amvideo_notifier_list, val, v);
}
EXPORT_SYMBOL_GPL(amvideo_notifier_call_chain);

/*********************************************************/
struct device *get_video_device(void)
{
	return amvideo_dev;
}

static struct mconfig video_configs[] = {
	MC_PI32("pause_one_3d_fl_frame", &pause_one_3d_fl_frame),
	MC_PI32("debug_flag", &debug_flag),
	MC_PU32("force_3d_scaler", &force_3d_scaler),
	MC_PU32("video_3d_format", &video_3d_format),
	MC_PI32("vsync_enter_line_max", &vsync_enter_line_max),
	MC_PI32("vsync_exit_line_max", &vsync_exit_line_max),
#ifdef CONFIG_AMLOGIC_MEDIA_VSYNC_RDMA
	MC_PI32("vsync_rdma_line_max", &vsync_rdma_line_max),
#endif
	MC_PU32("underflow", &underflow),
	MC_PU32("next_peek_underflow", &next_peek_underflow),
	MC_PU32("smooth_sync_enable", &smooth_sync_enable),
	MC_PU32("hdmi_in_onvideo", &hdmi_in_onvideo),
	MC_PU32("smooth_sync_enable", &smooth_sync_enable),
	MC_PU32("hdmi_in_onvideo", &hdmi_in_onvideo),
	MC_PU32("new_frame_count", &new_frame_count),
	MC_PU32("omx_pts", &omx_pts),
	MC_PU32("omx_pts_set_index", &omx_pts_set_index),
	MC_PBOOL("omx_run", &omx_run),
	MC_PU32("omx_version", &omx_version),
	MC_PU32("omx_info", &omx_info),
	MC_PI32("omx_need_drop_frame_num", &omx_need_drop_frame_num),
	MC_PBOOL("omx_drop_done", &omx_drop_done),
	MC_PI32("omx_pts_interval_upper", &omx_pts_interval_upper),
	MC_PI32("omx_pts_interval_lower", &omx_pts_interval_lower),
	MC_PBOOL("bypass_pps", &bypass_pps),
	MC_PU32("process_3d_type", &process_3d_type),
	MC_PU32("omx_pts", &omx_pts),
	MC_PU32("framepacking_support", &framepacking_support),
	MC_PU32("framepacking_width", &framepacking_width),
	MC_PU32("framepacking_height", &framepacking_height),
	MC_PU32("framepacking_blank", &framepacking_blank),
	MC_PU32("video_seek_flag", &video_seek_flag),
	MC_PU32("slowsync_repeat_enable", &slowsync_repeat_enable),
	MC_PU32("toggle_count", &toggle_count),
	MC_PBOOL("show_first_frame_nosync", &show_first_frame_nosync),
#ifdef TV_REVERSE
	MC_PBOOL("reverse", &reverse),
#endif
};

#ifdef CONFIG_AMLOGIC_LEGACY_EARLY_SUSPEND
static void video_early_suspend(struct early_suspend *h)
{
	safe_switch_videolayer(0, false, false);
	safe_switch_videolayer(1, false, false);
	video_suspend = true;
	pr_info("video_early_suspend ok\n");
}

static void video_late_resume(struct early_suspend *h)
{
	video_suspend_cycle = 0;
	video_suspend = false;
	log_out = 1;
	pr_info("video_late_resume ok\n");
};

static struct early_suspend video_early_suspend_handler = {
	.suspend = video_early_suspend,
	.resume = video_late_resume,
};
#endif

static struct amvideo_device_data_s amvideo = {
	.cpu_type = MESON_CPU_MAJOR_ID_COMPATIBALE,
	.sr_reg_offt = 0xff,
	.sr_reg_offt2 = 0xff,
	.layer_support[0] = 0xff,
	.layer_support[1] = 0xff,
	.afbc_support[0] = 0xff,
	.afbc_support[1] = 0xff,
	.pps_support[0] = 0xff,
	.pps_support[1] = 0xff,
	.alpha_support[0] = 0xff,
	.alpha_support[1] = 0xff,
	.dv_support = 1,
	.sr0_support = 0xff,
	.sr1_support = 0xff,
	.core_v_disable_width_max[0] = 0xff,
	.core_v_disable_width_max[1] = 0xff,
	.core_v_enable_width_max[0] = 0xff,
	.core_v_enable_width_max[1] = 0xff,
	.supscl_path = 0xff,
	.fgrain_support[0] = 0,
	.fgrain_support[1] = 0,
	.has_hscaler_8tap[0] = 0,
	.has_hscaler_8tap[1] = 0,
	.has_pre_hscaler_ntap[0] = 0,
	.has_pre_hscaler_ntap[1] = 0,
	.has_pre_vscaler_ntap[0] = 0,
	.has_pre_vscaler_ntap[1] = 0,
	.src_width_max[0] = 0xff,
	.src_width_max[1] = 0xff,
	.src_height_max[0] = 0xff,
	.src_height_max[1] = 0xff,
	.ofifo_size = 0xff,
	.afbc_conv_lbuf_len = 0x100,
};

static struct amvideo_device_data_s amvideo_tm2_revb = {
	.cpu_type = MESON_CPU_MAJOR_ID_TM2_REVB,
	.sr_reg_offt = 0x1e00,
	.sr_reg_offt2 = 0x1f80,
	.layer_support[0] = 1,
	.layer_support[1] = 1,
	.afbc_support[0] = 1,
	.afbc_support[1] = 1,
	.pps_support[0] = 1,
	.pps_support[1] = 1,
	.alpha_support[0] = 0,
	.alpha_support[1] = 0,
	.dv_support = 1,
	.sr0_support = 1,
	.sr1_support = 1,
	.core_v_disable_width_max[0] = 2048,
	.core_v_disable_width_max[1] = 4096,
	.core_v_enable_width_max[0] = 1024,
	.core_v_enable_width_max[1] = 2048,
	.supscl_path = CORE0_PPS_CORE1,
	.fgrain_support[0] = 1,
	.fgrain_support[1] = 1,
	.has_hscaler_8tap[0] = 0,
	.has_hscaler_8tap[1] = 0,
	.has_pre_hscaler_ntap[0] = 0,
	.has_pre_hscaler_ntap[1] = 0,
	.has_pre_vscaler_ntap[0] = 0,
	.has_pre_vscaler_ntap[1] = 0,
	.src_width_max[0] = 4096,
	.src_width_max[1] = 4096,
	.src_height_max[0] = 2160,
	.src_height_max[1] = 2160,
	.ofifo_size = 0x1000,
	.afbc_conv_lbuf_len = 0x100,
};

static struct amvideo_device_data_s amvideo_sc2 = {
	.cpu_type = MESON_CPU_MAJOR_ID_SC2_,
	.sr_reg_offt = 0x1e00,
	.sr_reg_offt2 = 0x1f80,
	.layer_support[0] = 1,
	.layer_support[1] = 1,
	.afbc_support[0] = 1,
	.afbc_support[1] = 1,
	.pps_support[0] = 1,
	.pps_support[1] = 1,
	.alpha_support[0] = 1,
	.alpha_support[1] = 1,
	.dv_support = 1,
	.sr0_support = 1,
	.sr1_support = 0,
	.core_v_disable_width_max[0] = 4096,
	.core_v_disable_width_max[1] = 4096,
	.core_v_enable_width_max[0] = 2048,
	.core_v_enable_width_max[1] = 2048,
	.supscl_path = CORE0_BEFORE_PPS,
	.fgrain_support[0] = 1,
	.fgrain_support[1] = 1,
	.has_hscaler_8tap[0] = 1,
	.has_hscaler_8tap[1] = 1,
	.has_pre_hscaler_ntap[0] = 1,
	.has_pre_hscaler_ntap[1] = 1,
	.has_pre_vscaler_ntap[0] = 0,
	.has_pre_vscaler_ntap[1] = 0,
	.src_width_max[0] = 4096,
	.src_width_max[1] = 4096,
	.src_height_max[0] = 2160,
	.src_height_max[1] = 2160,
	.ofifo_size = 0x1000,
	.afbc_conv_lbuf_len = 0x100,
};

static struct amvideo_device_data_s amvideo_t5 = {
	.cpu_type = MESON_CPU_MAJOR_ID_T5_,
	.sr_reg_offt = 0x1e00,
	.sr_reg_offt2 = 0x1f80,
	.layer_support[0] = 1,
	.layer_support[1] = 1,
	.afbc_support[0] = 1,
	.afbc_support[1] = 0,
	.pps_support[0] = 1,
	.pps_support[1] = 0,
	.alpha_support[0] = 0,
	.alpha_support[1] = 0,
	.dv_support = 0,
	.sr0_support = 1,
	.sr1_support = 1,
	.core_v_disable_width_max[0] = 2048,
	.core_v_disable_width_max[1] = 4096,
	.core_v_enable_width_max[0] = 1024,
	.core_v_enable_width_max[1] = 2048,
	.supscl_path = CORE0_PPS_CORE1,
	.fgrain_support[0] = 0,
	.fgrain_support[1] = 0,
	.has_hscaler_8tap[0] = 1,
	.has_hscaler_8tap[1] = 0,
	.has_pre_hscaler_ntap[0] = 1,
	.has_pre_hscaler_ntap[1] = 0,
	.has_pre_vscaler_ntap[0] = 1,
	.has_pre_vscaler_ntap[1] = 0,
	.src_width_max[0] = 4096,
	.src_width_max[1] = 4096,
	.src_height_max[0] = 2160,
	.src_height_max[1] = 2160,
	.ofifo_size = 0x1000,
	.afbc_conv_lbuf_len = 0x100,
};

static struct amvideo_device_data_s amvideo_t5d = {
	.cpu_type = MESON_CPU_MAJOR_ID_T5D_,
	.sr_reg_offt = 0x1e00,
	.sr_reg_offt2 = 0x1f80,
	.layer_support[0] = 1,
	.layer_support[1] = 1,
	.afbc_support[0] = 1,
	.afbc_support[1] = 0,
	.pps_support[0] = 1,
	.pps_support[1] = 1,
	.alpha_support[0] = 0,
	.alpha_support[1] = 0,
	.dv_support = 0,
	.sr0_support = 0,
	.sr1_support = 1,
	.core_v_disable_width_max[0] = 1024,
	.core_v_disable_width_max[1] = 2048,
	.core_v_enable_width_max[0] = 1024,
	.core_v_enable_width_max[1] = 1024,
	.supscl_path = PPS_CORE1_CM,
	.fgrain_support[0] = 0,
	.fgrain_support[1] = 0,
	.has_hscaler_8tap[0] = 1,
	.has_hscaler_8tap[1] = 0,
	.has_pre_hscaler_ntap[0] = 1,
	.has_pre_hscaler_ntap[1] = 0,
	.has_pre_vscaler_ntap[0] = 1,
	.has_pre_vscaler_ntap[1] = 0,
	.src_width_max[0] = 2048,
	.src_width_max[1] = 2048,
	.src_height_max[0] = 1080,
	.src_height_max[1] = 1080,
	.ofifo_size = 0x780,
	.afbc_conv_lbuf_len = 0x80,
};

static struct amvideo_device_data_s amvideo_t5d_revb = {
	.cpu_type = MESON_CPU_MAJOR_ID_T5D_REVB_,
	.sr_reg_offt = 0x1e00,
	.sr_reg_offt2 = 0x1f80,
	.layer_support[0] = 1,
	.layer_support[1] = 1,
	.afbc_support[0] = 1,
	.afbc_support[1] = 0,
	.pps_support[0] = 1,
	.pps_support[1] = 1,
	.alpha_support[0] = 0,
	.alpha_support[1] = 0,
	.dv_support = 0,
	.sr0_support = 0,
	.sr1_support = 1,
	.core_v_disable_width_max[0] = 1024,
	.core_v_disable_width_max[1] = 2048,
	.core_v_enable_width_max[0] = 1024,
	.core_v_enable_width_max[1] = 1024,
	.supscl_path = PPS_CORE1_CM,
	.fgrain_support[0] = 0,
	.fgrain_support[1] = 0,
	.has_hscaler_8tap[0] = 1,
	.has_hscaler_8tap[1] = 0,
	.has_pre_hscaler_ntap[0] = 1,
	.has_pre_hscaler_ntap[1] = 0,
	.has_pre_vscaler_ntap[0] = 1,
	.has_pre_vscaler_ntap[1] = 0,
	.src_width_max[0] = 2048,
	.src_width_max[1] = 2048,
	.src_height_max[0] = 1080,
	.src_height_max[1] = 1080,
	.ofifo_size = 0x77f,
	.afbc_conv_lbuf_len = 0x80,
};

static const struct of_device_id amlogic_amvideom_dt_match[] = {
	{
		.compatible = "amlogic, amvideom",
		.data = &amvideo,
	},
	{
		.compatible = "amlogic, amvideom-tm2-revb",
		.data = &amvideo_tm2_revb,
	},
	{
		.compatible = "amlogic, amvideom-sc2",
		.data = &amvideo_sc2,
	},
	{
		.compatible = "amlogic, amvideom-t5",
		.data = &amvideo_t5,
	},
	{
		.compatible = "amlogic, amvideom-t5d",
		.data = &amvideo_t5d,
	},
	{
		.compatible = "amlogic, amvideom-t5d-revb",
		.data = &amvideo_t5d_revb,
	},
	{}
};

bool is_meson_tm2_revb(void)
{
	if (amvideo_meson_dev.cpu_type ==
		MESON_CPU_MAJOR_ID_TM2_REVB)
		return true;
	else
		return false;
}

bool is_meson_sc2_cpu(void)
{
	if (amvideo_meson_dev.cpu_type ==
		MESON_CPU_MAJOR_ID_SC2_)
		return true;
	else
		return false;
}

bool is_meson_t5_cpu(void)
{
	if (amvideo_meson_dev.cpu_type ==
		MESON_CPU_MAJOR_ID_T5_)
		return true;
	else
		return false;
}

bool video_is_meson_t5d_revb_cpu(void)
{
	if (amvideo_meson_dev.cpu_type ==
		MESON_CPU_MAJOR_ID_T5D_REVB_)
		return true;
	else
		return false;
}

bool has_hscaler_8tap(u8 layer_id)
{
	if (amvideo_meson_dev.has_hscaler_8tap[layer_id])
		return true;
	else
		return false;
}

bool has_pre_hscaler_ntap(u8 layer_id)
{
	if (amvideo_meson_dev.has_pre_hscaler_ntap[layer_id])
		return true;
	else
		return false;
}

bool has_pre_vscaler_ntap(u8 layer_id)
{
	if (amvideo_meson_dev.has_pre_vscaler_ntap[layer_id])
		return true;
	else
		return false;
}

static void video_cap_set(struct amvideo_device_data_s *p_amvideo)
{
	if (p_amvideo->cpu_type ==
		MESON_CPU_MAJOR_ID_COMPATIBALE) {
		if (legacy_vpp)
			layer_cap =
				LAYER1_AFBC |
				LAYER1_AVAIL |
				LAYER0_AFBC |
				LAYER0_SCALER |
				LAYER0_AVAIL;
		else if (is_meson_tl1_cpu()) {
			layer_cap =
				LAYER1_AVAIL |
				LAYER0_AFBC |
				LAYER0_SCALER |
				LAYER0_AVAIL;
		} else if (is_meson_tm2_cpu()) {
			layer_cap =
				LAYER1_SCALER |
				LAYER1_AVAIL |
				LAYER0_AFBC |
				LAYER0_SCALER |
				LAYER0_AVAIL;
		} else {
			/* g12a, g12b, sm1 */
			layer_cap =
				LAYER1_AFBC |
				LAYER1_SCALER |
				LAYER1_AVAIL |
				LAYER0_AFBC |
				LAYER0_SCALER |
				LAYER0_AVAIL;
		}
	} else {
		if (p_amvideo->layer_support[0])
			layer_cap |= LAYER0_AVAIL;
		if (p_amvideo->layer_support[1])
			layer_cap |= LAYER1_AVAIL;
		if (p_amvideo->afbc_support[0])
			layer_cap |= LAYER0_AFBC;
		if (p_amvideo->afbc_support[1])
			layer_cap |= LAYER1_AFBC;
		if (p_amvideo->pps_support[0])
			layer_cap |= LAYER0_SCALER;
		if (p_amvideo->pps_support[1])
			layer_cap |= LAYER1_SCALER;
		if (p_amvideo->alpha_support[0])
			layer_cap |= LAYER0_ALPHA;
		if (p_amvideo->alpha_support[1])
			layer_cap |= LAYER1_ALPHA;
	}
}

static int amvideom_probe(struct platform_device *pdev)
{
	int ret = 0;
	int i, j;

	if (pdev->dev.of_node) {
		const struct of_device_id *match;
		struct amvideo_device_data_s *amvideo_meson;
		struct device_node	*of_node = pdev->dev.of_node;

		match = of_match_node(amlogic_amvideom_dt_match, of_node);
		if (match) {
			amvideo_meson =
				(struct amvideo_device_data_s *)match->data;
			if (amvideo_meson)
				memcpy(&amvideo_meson_dev, amvideo_meson,
				       sizeof(struct amvideo_device_data_s));
			else {
				pr_err("%s data NOT match\n", __func__);
				return -ENODEV;
			}
		} else {
			pr_err("%s NOT match\n", __func__);
			return -ENODEV;
		}
	}

	video_early_init(&amvideo_meson_dev);
	video_cap_set(&amvideo_meson_dev);
	video_hw_init();
	video_suspend = false;
	video_suspend_cycle = 0;
	log_out = 1;

	safe_switch_videolayer(0, false, false);
	safe_switch_videolayer(1, false, false);

	/* get interrupt resource */
	video_vsync = platform_get_irq_byname(pdev, "vsync");
	if (video_vsync  == -ENXIO) {
		pr_info("cannot get amvideom irq resource\n");

		return video_vsync;
	}
	pr_info("amvideom vsync irq: %d, cpu_type:%d\n", video_vsync,
		amvideo_meson_dev.cpu_type);

	if (amvideo_meson_dev.cpu_type == MESON_CPU_MAJOR_ID_SC2_) {
		/* get interrupt resource */
		video_vsync_viu2 = platform_get_irq_byname(pdev, "vsync_viu2");
		if (video_vsync_viu2  == -ENXIO)
			pr_info("cannot get amvideom viu2 irq resource\n");
		else
			pr_info("amvideom vsync viu2 irq: %d\n",
				video_vsync_viu2);
	}

#ifdef CONFIG_AMLOGIC_LEGACY_EARLY_SUSPEND
	register_early_suspend(&video_early_suspend_handler);
#endif
	video_keeper_init();
	for (i = 0; i < AI_SCENES_MAX; i++) {
		vpp_scenes[i].pq_scenes = i;
		for (j = 0; j < SCENES_VALUE; j++)
			vpp_scenes[i].pq_values[j] = vpp_pq_data[i][j];
	}
	return ret;
}

static int amvideom_remove(struct platform_device *pdev)
{
#ifdef CONFIG_AMLOGIC_LEGACY_EARLY_SUSPEND
	unregister_early_suspend(&video_early_suspend_handler);
#endif
	video_keeper_exit();
	return 0;
}

static struct platform_driver amvideom_driver = {
	.probe = amvideom_probe,
	.remove = amvideom_remove,
	.driver = {
		.name = "amvideom",
		.of_match_table = amlogic_amvideom_dt_match,
	}
};

static int __init video_init(void)
{
	int r = 0;
	/*
	 *#ifdef CONFIG_ARCH_MESON1
	 *ulong clk = clk_get_rate(clk_get_sys("clk_other_pll", NULL));
	 *#elif !defined(CONFIG_ARCH_MESON3) && !defined(CONFIG_ARCH_MESON6)
	 *ulong clk = clk_get_rate(clk_get_sys("clk_misc_pll", NULL));
	 *#endif
	 */

#ifdef CONFIG_ARCH_MESON1
	no to here ulong clk =
		clk_get_rate(clk_get_sys("clk_other_pll", NULL));
#elif defined(CONFIG_ARCH_MESON2)
	not to here ulong clk =
		clk_get_rate(clk_get_sys("clk_misc_pll", NULL));
#endif
	/* #if !defined(CONFIG_ARCH_MESON3) && !defined(CONFIG_ARCH_MESON6) */
#if 0				/* MESON_CPU_TYPE <= MESON_CPU_TYPE_MESON2 */
	/* MALI clock settings */
	if ((clk <= 750000000) && (clk >= 600000000)) {
		WRITE_VCBUS_REG(HHI_MALI_CLK_CNTL,
		(2 << 9) |	/* select misc pll as clock source */
		(1 << 8) |	/* enable clock gating */
		(2 << 0));	/* Misc clk / 3 */
	} else {
		WRITE_VCBUS_REG(HHI_MALI_CLK_CNTL,
		(3 << 9) |	/* select DDR clock as clock source */
		(1 << 8) |	/* enable clock gating */
		(1 << 0));	/* DDR clk / 2 */
	}
#endif


	if (platform_driver_register(&amvideom_driver)) {
		pr_info("failed to amvideom driver!\n");
		return -ENODEV;
	}

	cur_dispbuf = NULL;
	cur_dispbuf2 = NULL;
	amvideo_register_client(&amvideo_notifier);

#ifdef FIQ_VSYNC
	/* enable fiq bridge */
	vsync_fiq_bridge.handle = vsync_bridge_isr;
	vsync_fiq_bridge.key = (u32) vsync_bridge_isr;
	vsync_fiq_bridge.name = "vsync_bridge_isr";

	r = register_fiq_bridge_handle(&vsync_fiq_bridge);

	if (r) {
		amlog_level(LOG_LEVEL_ERROR,
			    "video fiq bridge register error.\n");
		r = -ENOENT;
		goto err0;
	}
#endif

    /* sysfs node creation */
	r = class_register(&amvideo_poll_class);
	if (r) {
		amlog_level(LOG_LEVEL_ERROR, "create video_poll class fail.\n");
#ifdef FIQ_VSYNC
		free_irq(BRIDGE_IRQ, (void *)video_dev_id);
#else
		free_irq(INT_VIU_VSYNC, (void *)video_dev_id);
#endif
		goto err1;
	}

	r = class_register(&amvideo_class);
	if (r) {
		amlog_level(LOG_LEVEL_ERROR, "create video class fail.\n");
#ifdef FIQ_VSYNC
		free_irq(BRIDGE_IRQ, (void *)video_dev_id);
#else
		free_irq(INT_VIU_VSYNC, (void *)video_dev_id);
#endif
		goto err1;
	}

	/* create video device */
	r = register_chrdev(AMVIDEO_MAJOR, "amvideo", &amvideo_fops);
	if (r < 0) {
		amlog_level(LOG_LEVEL_ERROR,
			    "Can't register major for amvideo device\n");
		goto err2;
	}

	r = register_chrdev(0, "amvideo_poll", &amvideo_poll_fops);
	if (r < 0) {
		amlog_level(LOG_LEVEL_ERROR,
			"Can't register major for amvideo_poll device\n");
		goto err3;
	}

	amvideo_poll_major = r;

	amvideo_dev = device_create(&amvideo_class, NULL,
		MKDEV(AMVIDEO_MAJOR, 0), NULL, DEVICE_NAME);

	if (IS_ERR(amvideo_dev)) {
		amlog_level(LOG_LEVEL_ERROR, "Can't create amvideo device\n");
		goto err4;
	}

	amvideo_poll_dev = device_create(&amvideo_poll_class, NULL,
		MKDEV(amvideo_poll_major, 0), NULL, "amvideo_poll");

	if (IS_ERR(amvideo_poll_dev)) {
		amlog_level(LOG_LEVEL_ERROR,
			"Can't create amvideo_poll device\n");
		goto err5;
	}

	init_waitqueue_head(&amvideo_trick_wait);
	init_waitqueue_head(&amvideo_prop_change_wait);

#ifdef CONFIG_AM_VOUT
	vout_hook();
#endif

#ifdef CONFIG_AMLOGIC_MEDIA_VSYNC_RDMA
	memset(dispbuf_to_put, 0, sizeof(dispbuf_to_put));
	memset(pipbuf_to_put, 0, sizeof(pipbuf_to_put));
#endif
	memset(recycle_buf, 0, sizeof(recycle_buf));
	memset(recycle_cnt, 0, sizeof(recycle_cnt));

	gvideo_recv[0] = create_video_receiver(
		"video_render.0", VFM_PATH_VIDEO_RENDER0);
	gvideo_recv[1] = create_video_receiver(
		"video_render.1", VFM_PATH_VIDEO_RENDER1);

	vsync_fiq_up();

	vf_receiver_init(
		&video_vf_recv, RECEIVER_NAME,
		&video_vf_receiver, NULL);
	vf_reg_receiver(&video_vf_recv);

	vf_receiver_init(
		&videopip_vf_recv, RECEIVERPIP_NAME,
		&videopip_vf_receiver, NULL);
	vf_reg_receiver(&videopip_vf_recv);

	REG_PATH_CONFIGS("media.video", video_configs);
	video_debugfs_init();
	return 0;
 err5:
	device_destroy(&amvideo_class, MKDEV(AMVIDEO_MAJOR, 0));
 err4:
	unregister_chrdev(amvideo_poll_major, "amvideo_poll");
 err3:
	unregister_chrdev(AMVIDEO_MAJOR, DEVICE_NAME);

 err2:
#ifdef FIQ_VSYNC
	unregister_fiq_bridge_handle(&vsync_fiq_bridge);
#endif
	class_unregister(&amvideo_class);
 err1:
	class_unregister(&amvideo_poll_class);
#ifdef FIQ_VSYNC
 err0:
#endif
	amvideo_unregister_client(&amvideo_notifier);
	platform_driver_unregister(&amvideom_driver);

	return r;
}


static void __exit video_exit(void)
{
	video_debugfs_exit();
	vf_unreg_receiver(&video_vf_recv);
	vf_unreg_receiver(&videopip_vf_recv);
	safe_switch_videolayer(0, false, false);
	safe_switch_videolayer(1, false, false);
#ifdef CONFIG_AMLOGIC_MEDIA_SECURITY
	secure_unregister(VIDEO_MODULE);
#endif
	vsync_fiq_down();

	if (gvideo_recv[0])
		destroy_video_receiver(gvideo_recv[0]);
	gvideo_recv[0] = NULL;
	if (gvideo_recv[1])
		destroy_video_receiver(gvideo_recv[1]);
	gvideo_recv[1] = NULL;

	device_destroy(&amvideo_class, MKDEV(AMVIDEO_MAJOR, 0));
	device_destroy(&amvideo_poll_class, MKDEV(amvideo_poll_major, 0));

	unregister_chrdev(AMVIDEO_MAJOR, DEVICE_NAME);
	unregister_chrdev(amvideo_poll_major, "amvideo_poll");

#ifdef FIQ_VSYNC
	unregister_fiq_bridge_handle(&vsync_fiq_bridge);
#endif

	class_unregister(&amvideo_class);
	class_unregister(&amvideo_poll_class);
	amvideo_unregister_client(&amvideo_notifier);
}



MODULE_PARM_DESC(debug_flag, "\n debug_flag\n");
module_param(debug_flag, uint, 0664);

#ifdef TV_3D_FUNCTION_OPEN
MODULE_PARM_DESC(force_3d_scaler, "\n force_3d_scaler\n");
module_param(force_3d_scaler, uint, 0664);

MODULE_PARM_DESC(video_3d_format, "\n video_3d_format\n");
module_param(video_3d_format, uint, 0664);

#endif

MODULE_PARM_DESC(vsync_enter_line_max, "\n vsync_enter_line_max\n");
module_param(vsync_enter_line_max, uint, 0664);

MODULE_PARM_DESC(vsync_exit_line_max, "\n vsync_exit_line_max\n");
module_param(vsync_exit_line_max, uint, 0664);

#ifdef CONFIG_AMLOGIC_MEDIA_VSYNC_RDMA
MODULE_PARM_DESC(vsync_rdma_line_max, "\n vsync_rdma_line_max\n");
module_param(vsync_rdma_line_max, uint, 0664);
#endif

module_param(underflow, uint, 0664);
MODULE_PARM_DESC(underflow, "\n Underflow count\n");

module_param(next_peek_underflow, uint, 0664);
MODULE_PARM_DESC(skip, "\n Underflow count\n");

module_param(step_enable, uint, 0664);
MODULE_PARM_DESC(step_enable, "\n step_enable\n");

module_param(step_flag, uint, 0664);
MODULE_PARM_DESC(step_flag, "\n step_flag\n");

/*arch_initcall(video_early_init);*/

module_init(video_init);
module_exit(video_exit);

MODULE_PARM_DESC(smooth_sync_enable, "\n smooth_sync_enable\n");
module_param(smooth_sync_enable, uint, 0664);

MODULE_PARM_DESC(hdmi_in_onvideo, "\n hdmi_in_onvideo\n");
module_param(hdmi_in_onvideo, uint, 0664);

MODULE_PARM_DESC(vsync_count, "\n vsync_count\n");
module_param(vsync_count, uint, 0664);

MODULE_PARM_DESC(new_frame_count, "\n new_frame_count\n");
module_param(new_frame_count, uint, 0664);

MODULE_PARM_DESC(first_frame_toggled, "\n first_frame_toggled\n");
module_param(first_frame_toggled, uint, 0664);

MODULE_PARM_DESC(omx_pts, "\n omx_pts\n");
module_param(omx_pts, uint, 0664);

MODULE_PARM_DESC(omx_run, "\n omx_run\n");
module_param(omx_run, bool, 0664);

MODULE_PARM_DESC(omx_pts_set_index, "\n omx_pts_set_index\n");
module_param(omx_pts_set_index, uint, 0664);

MODULE_PARM_DESC(omx_version, "\n omx_version\n");
module_param(omx_version, uint, 0664);

MODULE_PARM_DESC(omx_info, "\n omx_info\n");
module_param(omx_info, uint, 0664);

MODULE_PARM_DESC(omx_need_drop_frame_num, "\n omx_need_drop_frame_num\n");
module_param(omx_need_drop_frame_num, int, 0664);

MODULE_PARM_DESC(omx_drop_done, "\n omx_drop_done\n");
module_param(omx_drop_done, bool, 0664);

MODULE_PARM_DESC(omx_pts_interval_upper, "\n omx_pts_interval\n");
module_param(omx_pts_interval_upper, int, 0664);

MODULE_PARM_DESC(omx_pts_interval_lower, "\n omx_pts_interval\n");
module_param(omx_pts_interval_lower, int, 0664);

MODULE_PARM_DESC(omx_pts_dv_upper, "\n omx_pts_dv_upper\n");
module_param(omx_pts_dv_upper, int, 0664);

MODULE_PARM_DESC(omx_pts_dv_lower, "\n omx_pts_dv_lower\n");
module_param(omx_pts_dv_lower, int, 0664);

MODULE_PARM_DESC(drop_frame_count, "\n drop_frame_count\n");
module_param(drop_frame_count, int, 0664);

MODULE_PARM_DESC(receive_frame_count, "\n receive_frame_count\n");
module_param(receive_frame_count, int, 0664);

MODULE_PARM_DESC(display_frame_count, "\n display_frame_count\n");
module_param(display_frame_count, int, 0664);

module_param(frame_detect_time, uint, 0664);
MODULE_PARM_DESC(frame_detect_time, "\n frame_detect_time\n");

module_param(frame_detect_flag, uint, 0664);
MODULE_PARM_DESC(frame_detect_flag, "\n frame_detect_flag\n");

module_param(frame_detect_fps, uint, 0664);
MODULE_PARM_DESC(frame_detect_fps, "\n frame_detect_fps\n");

module_param(frame_detect_receive_count, uint, 0664);
MODULE_PARM_DESC(frame_detect_receive_count, "\n frame_detect_receive_count\n");

module_param(frame_detect_drop_count, uint, 0664);
MODULE_PARM_DESC(frame_detect_drop_count, "\n frame_detect_drop_count\n");

MODULE_PARM_DESC(bypass_pps, "\n pps_bypass\n");
module_param(bypass_pps, bool, 0664);

MODULE_PARM_DESC(process_3d_type, "\n process_3d_type\n");
module_param(process_3d_type, uint, 0664);

MODULE_PARM_DESC(framepacking_support, "\n framepacking_support\n");
module_param(framepacking_support, uint, 0664);

MODULE_PARM_DESC(framepacking_width, "\n framepacking_width\n");
module_param(framepacking_width, uint, 0664);

MODULE_PARM_DESC(framepacking_height, "\n framepacking_height\n");
module_param(framepacking_height, uint, 0664);

MODULE_PARM_DESC(framepacking_blank, "\n framepacking_blank\n");
module_param(framepacking_blank, uint, 0664);

module_param(reverse, bool, 0644);
MODULE_PARM_DESC(reverse, "reverse /disable reverse");

MODULE_PARM_DESC(toggle_count, "\n toggle count\n");
module_param(toggle_count, uint, 0664);

MODULE_PARM_DESC(stop_update, "\n stop_update\n");
module_param(stop_update, uint, 0664);

MODULE_PARM_DESC(pts_rollback_threshold, "\n pts_rollback_threshold\n");
module_param(pts_rollback_threshold, uint, 0664);

MODULE_PARM_DESC(OMX_MAX_COUNT_RESET_SYSTEMTIME, "\n OMX_MAX_COUNT_RESET_SYSTEMTIME\n");
module_param(OMX_MAX_COUNT_RESET_SYSTEMTIME, int, 0664);

MODULE_PARM_DESC(ai_pq_disable, "\n ai_pq_disable\n");
module_param(ai_pq_disable, uint, 0664);

MODULE_PARM_DESC(ai_pq_debug, "\n ai_pq_debug\n");
module_param(ai_pq_debug, uint, 0664);

MODULE_PARM_DESC(ai_pq_value, "\n ai_pq_value\n");
module_param(ai_pq_value, uint, 0664);

MODULE_PARM_DESC(ai_pq_policy, "\n ai_pq_policy\n");
module_param(ai_pq_policy, uint, 0664);

MODULE_DESCRIPTION("AMLOGIC video output driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tim Yao <timyao@amlogic.com>");
