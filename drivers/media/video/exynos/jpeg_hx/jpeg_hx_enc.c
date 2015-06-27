/* linux/drivers/media/video/exynos/jpeg_hx/jpeg_hx_dev.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 * http://www.samsung.com/
 *
 * Core file for Samsung Jpeg hx Interface driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/signal.h>
#include <linux/ioport.h>
#include <linux/kmod.h>
#include <linux/vmalloc.h>
#include <linux/time.h>
#include <linux/clk.h>
#include <linux/semaphore.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>

#include <asm/page.h>

#include <mach/irqs.h>

#include <media/v4l2-ioctl.h>

#include "jpeg_hx_core.h"
#include "jpeg_hx_dev.h"

#include "jpeg_hx_mem.h"
#include "jpeg_hx_regs.h"
#include "regs_jpeg_hx.h"

static struct jpeg_fmt formats[] = {
	{
		.name		= "JPEG compressed format",
		.fourcc		= V4L2_PIX_FMT_JPEG_444,
		.depth		= {8},
		.color		= JPEG_444,
		.memplanes	= 1,
		.types		= M2M_CAPTURE,
	}, {
		.name		= "JPEG compressed format",
		.fourcc		= V4L2_PIX_FMT_JPEG_422,
		.depth		= {8},
		.color		= JPEG_422,
		.memplanes	= 1,
		.types		= M2M_CAPTURE,
	}, {
		.name		= "JPEG compressed format",
		.fourcc		= V4L2_PIX_FMT_JPEG_420,
		.depth		= {8},
		.color		= JPEG_420,
		.memplanes	= 1,
		.types		= M2M_CAPTURE,
	}, {
		.name		= "JPEG compressed format",
		.fourcc		= V4L2_PIX_FMT_JPEG_GRAY,
		.depth		= {8},
		.color		= JPEG_GRAY,
		.memplanes	= 1,
		.types		= M2M_CAPTURE,
	}, {
		.name		= "RGB565",
		.fourcc		= V4L2_PIX_FMT_RGB565X,
		.depth		= {16},
		.color		= RGB_565,
		.memplanes	= 1,
		.types		= M2M_OUTPUT,
	}, {
		.name		= "ARGB8888",
		.fourcc		= V4L2_PIX_FMT_BGR32,
		.depth		= {32},
		.color		= ARGB_8888,
		.memplanes	= 1,
		.types		= M2M_OUTPUT,
	}, {
		.name		= "ARGB8888",
		.fourcc		= V4L2_PIX_FMT_RGB32,
		.depth		= {32},
		.color		= ARGB_8888,
		.memplanes	= 1,
		.types		= M2M_OUTPUT,
	}, {
		.name		= "YUV 4:2:2 packed, CbYCrY",
		.fourcc		= V4L2_PIX_FMT_UYVY,
		.depth		= {16},
		.color		= CBYCRY_422_1P,
		.memplanes	= 1,
		.types		= M2M_OUTPUT,
	}, {
		.name		= "YUV 4:2:2 packed, YCbYCr",
		.fourcc		= V4L2_PIX_FMT_YUYV,
		.depth		= {16},
		.color		= YCBYCR_422_1P,
		.memplanes	= 1,
		.types		= M2M_OUTPUT,
	}, {
		.name		= "YUV 4:2:0 planar, Y/CbCr",
		.fourcc		= V4L2_PIX_FMT_NV12,
		.depth		= {12},
		.color		= YCBCR_420_2P,
		.memplanes	= 1,
		.types		= M2M_OUTPUT,
	}, {
		.name		= "YUV 4:2:0 planar, Y/CrCb",
		.fourcc		= V4L2_PIX_FMT_NV21,
		.depth		= {12},
		.color		= YCRCB_420_2P,
		.memplanes	= 1,
		.types		= M2M_OUTPUT,
	},
};

static struct jpeg_fmt *find_format(struct v4l2_format *f)
{
	struct jpeg_fmt *fmt;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(formats); ++i) {
		fmt = &formats[i];
		if (fmt->fourcc == f->fmt.pix_mp.pixelformat)
			break;
	}

	return (i == ARRAY_SIZE(formats)) ? NULL : fmt;
}

static int jpeg_enc_vidioc_querycap(struct file *file, void *priv,
			   struct v4l2_capability *cap)
{
	struct jpeg_ctx *ctx = file->private_data;
	struct jpeg_dev *dev = ctx->dev;

	strncpy(cap->driver, dev->plat_dev->name, sizeof(cap->driver) - 1);
	strncpy(cap->card, dev->plat_dev->name, sizeof(cap->card) - 1);
	cap->bus_info[0] = 0;
	cap->version = KERNEL_VERSION(1, 0, 0);
	cap->capabilities = V4L2_CAP_STREAMING |
		V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_VIDEO_OUTPUT |
		V4L2_CAP_VIDEO_CAPTURE_MPLANE | V4L2_CAP_VIDEO_OUTPUT_MPLANE;
	return 0;
}

static int jpeg_enc_vidioc_enum_fmt(struct file *file, void *priv,
				struct v4l2_fmtdesc *f)
{
	struct jpeg_fmt *fmt;

	if (f->index >= ARRAY_SIZE(formats))
		return -EINVAL;

	fmt = &formats[f->index];
	strncpy(f->description, fmt->name, sizeof(f->description) - 1);
	f->pixelformat = fmt->fourcc;

	return 0;
}

static int jpeg_enc_vidioc_g_fmt(struct file *file, void *priv,
			     struct v4l2_format *f)
{
	struct jpeg_ctx *ctx = priv;
	struct v4l2_pix_format_mplane *pixm;
	struct jpeg_enc_param *enc_param = &ctx->param.enc_param;
	struct jpeg_frame *frame;

	frame = ctx_get_frame(ctx, f->type);
	if (IS_ERR(frame))
		return PTR_ERR(frame);

	pixm = &f->fmt.pix_mp;
	pixm->field	= V4L2_FIELD_NONE;

	if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		pixm->pixelformat =
			frame->pixelformat;
		pixm->num_planes =
			enc_param->in_plane;
		pixm->width =
			enc_param->in_width;
		pixm->height =
			enc_param->in_height;
	} else if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		pixm->pixelformat =
			frame->pixelformat;
		pixm->num_planes =
			enc_param->out_plane;
		pixm->width =
			enc_param->out_width;
		pixm->height =
			enc_param->out_height;
	} else {
		v4l2_err(&ctx->dev->v4l2_dev,
			"Wrong buffer/video queue type (%d)\n", f->type);
	}

	return 0;
}

static int jpeg_enc_vidioc_try_fmt(struct file *file, void *priv,
			       struct v4l2_format *f)
{
	struct jpeg_fmt *fmt;
	struct v4l2_pix_format_mplane *pix = &f->fmt.pix_mp;
	struct jpeg_ctx *ctx = priv;
	int i;

	if (f->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE &&
		f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;

	fmt = find_format(f);

	if (!fmt) {
		v4l2_err(&ctx->dev->v4l2_dev,
			 "Fourcc format (0x%08x) invalid.\n",
			 f->fmt.pix.pixelformat);
		return -EINVAL;
	}

	if (pix->field == V4L2_FIELD_ANY)
		pix->field = V4L2_FIELD_NONE;
	else if (V4L2_FIELD_NONE != pix->field)
		return -EINVAL;


	pix->num_planes = fmt->memplanes;

	for (i = 0; i < pix->num_planes; ++i) {
		int bpl = pix->plane_fmt[i].bytesperline;

		jpeg_dbg("[%d] bpl: %d, depth: %d, w: %d, h: %d",
		    i, bpl, fmt->depth[i], pix->width, pix->height);

		if (!bpl || (bpl * 8 / fmt->depth[i]) > pix->width)
			bpl = (pix->width * fmt->depth[i]) >> 3;

		if (!pix->plane_fmt[i].sizeimage)
			pix->plane_fmt[i].sizeimage = pix->height * bpl;

		pix->plane_fmt[i].bytesperline = bpl;

		jpeg_dbg("[%d]: bpl: %d, sizeimage: %d",
		    i, pix->plane_fmt[i].bytesperline,
		    pix->plane_fmt[i].sizeimage);
	}

	if (f->fmt.pix.height > MAX_JPEG_HEIGHT)
		f->fmt.pix.height = MAX_JPEG_HEIGHT;

	if (f->fmt.pix.width > MAX_JPEG_WIDTH)
		f->fmt.pix.width = MAX_JPEG_WIDTH;

	return 0;
}

static int jpeg_enc_vidioc_s_fmt_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct jpeg_ctx *ctx = priv;
	struct vb2_queue *vq;
	struct v4l2_pix_format_mplane *pix;
	struct jpeg_fmt *fmt;
	struct jpeg_frame *frame;
	int ret;
	int i;

	ret = jpeg_enc_vidioc_try_fmt(file, priv, f);
	if (ret)
		return ret;

	vq = v4l2_m2m_get_vq(ctx->m2m_ctx, f->type);
	if (!vq)
		return -EINVAL;

	if (vb2_is_busy(vq)) {
		v4l2_err(&ctx->dev->v4l2_dev, "queue (%d) busy\n", f->type);
		return -EBUSY;
	}

	pix = &f->fmt.pix_mp;
	fmt = find_format(f);

	frame = ctx_get_frame(ctx, f->type);
	if (IS_ERR(frame))
		return PTR_ERR(frame);

	frame->jpeg_fmt = fmt;
	if (!frame->jpeg_fmt) {
		v4l2_err(&ctx->dev->v4l2_dev,
				"not supported format values\n");
		return -EINVAL;
	}

	for (i = 0; i < fmt->memplanes; i++)
		ctx->payload[i] =
			pix->plane_fmt[i].bytesperline * pix->height;

	frame->width = pix->width;
	frame->height = pix->height;
	frame->pixelformat = pix->pixelformat;

	ctx->param.enc_param.out_width = pix->height;
	ctx->param.enc_param.out_height = pix->width;
	ctx->param.enc_param.out_plane = fmt->memplanes;
	ctx->param.enc_param.out_depth = fmt->depth[0];
	ctx->param.enc_param.out_fmt = fmt->color;

	return 0;
}

static int jpeg_enc_vidioc_s_fmt_out(struct file *file, void *priv,
			struct v4l2_format *f)
{
	struct jpeg_ctx *ctx = priv;
	struct vb2_queue *vq;
	struct v4l2_pix_format_mplane *pix;
	struct jpeg_fmt *fmt;
	struct jpeg_frame *frame;
	int ret;
	int i;

	ret = jpeg_enc_vidioc_try_fmt(file, priv, f);
	if (ret)
		return ret;

	vq = v4l2_m2m_get_vq(ctx->m2m_ctx, f->type);
	if (!vq)
		return -EINVAL;

	if (vb2_is_busy(vq)) {
		v4l2_err(&ctx->dev->v4l2_dev, "queue (%d) busy\n", f->type);
		return -EBUSY;
	}

	/* TODO: width & height has to be multiple of two */
	pix = &f->fmt.pix_mp;
	fmt = find_format(f);

	frame = ctx_get_frame(ctx, f->type);
	if (IS_ERR(frame))
		return PTR_ERR(frame);

	frame->jpeg_fmt = fmt;
	if (!frame->jpeg_fmt) {
		v4l2_err(&ctx->dev->v4l2_dev,
				"not supported format values\n");
		return -EINVAL;
	}

	for (i = 0; i < fmt->memplanes; i++) {
		ctx->payload[i] =
			pix->plane_fmt[i].bytesperline * pix->height;
		ctx->param.enc_param.in_depth[i] = fmt->depth[i];
	}

	frame->width = pix->width;
	frame->height = pix->height;
	frame->pixelformat = pix->pixelformat;

	ctx->param.enc_param.in_width = pix->width;
	ctx->param.enc_param.in_height = pix->height;
	ctx->param.enc_param.in_plane = fmt->memplanes;
	ctx->param.enc_param.in_fmt = fmt->color;

	return 0;
}

static int jpeg_enc_m2m_reqbufs(struct file *file, void *priv,
			  struct v4l2_requestbuffers *reqbufs)
{
	struct jpeg_ctx *ctx = priv;

	return v4l2_m2m_reqbufs(file, ctx->m2m_ctx, reqbufs);
}

static int jpeg_enc_m2m_querybuf(struct file *file, void *priv,
			   struct v4l2_buffer *buf)
{
	struct jpeg_ctx *ctx = priv;
	return v4l2_m2m_querybuf(file, ctx->m2m_ctx, buf);
}

static int jpeg_enc_m2m_qbuf(struct file *file, void *priv,
			  struct v4l2_buffer *buf)
{
	struct jpeg_ctx *ctx = priv;

	return v4l2_m2m_qbuf(file, ctx->m2m_ctx, buf);
}

static int jpeg_enc_m2m_dqbuf(struct file *file, void *priv,
			   struct v4l2_buffer *buf)
{
	struct jpeg_ctx *ctx = priv;
	return v4l2_m2m_dqbuf(file, ctx->m2m_ctx, buf);
}

static int jpeg_enc_m2m_streamon(struct file *file, void *priv,
			   enum v4l2_buf_type type)
{
	struct jpeg_ctx *ctx = priv;
	return v4l2_m2m_streamon(file, ctx->m2m_ctx, type);
}

static int jpeg_enc_m2m_streamoff(struct file *file, void *priv,
			    enum v4l2_buf_type type)
{
	struct jpeg_ctx *ctx = priv;
	return v4l2_m2m_streamoff(file, ctx->m2m_ctx, type);
}

static int vidioc_enc_s_jpegcomp(struct file *file, void *priv,
			struct v4l2_jpegcompression *jpegcomp)
{
	struct jpeg_ctx *ctx = priv;

	ctx->param.enc_param.quality = jpegcomp->quality;
	return 0;
}

static int vidioc_enc_g_jpegcomp(struct file *file, void *priv,
			struct v4l2_jpegcompression *jpegcomp)
{
	struct jpeg_ctx *ctx = priv;

	jpegcomp->quality = ctx->param.enc_param.quality;
	return 0;
}

static int jpeg_enc_vidioc_s_ctrl(struct file *file, void *priv,
			struct v4l2_control *ctrl)
{
	struct jpeg_ctx *ctx = priv;

	switch (ctrl->id) {
	case V4L2_CID_CACHEABLE:
		v4l2_err(&ctx->dev->v4l2_dev,
		"Invalid control : 'cacheable' set is not available\n");
		break;
	default:
		v4l2_err(&ctx->dev->v4l2_dev, "Invalid control\n");
		break;
	}

	return 0;
}

static const struct v4l2_ioctl_ops jpeg_enc_ioctl_ops = {
	.vidioc_querycap		= jpeg_enc_vidioc_querycap,

	.vidioc_enum_fmt_vid_cap_mplane	= jpeg_enc_vidioc_enum_fmt,
	.vidioc_enum_fmt_vid_out_mplane	= jpeg_enc_vidioc_enum_fmt,

	.vidioc_g_fmt_vid_cap_mplane	= jpeg_enc_vidioc_g_fmt,
	.vidioc_g_fmt_vid_out_mplane	= jpeg_enc_vidioc_g_fmt,

	.vidioc_try_fmt_vid_cap_mplane		= jpeg_enc_vidioc_try_fmt,
	.vidioc_try_fmt_vid_out_mplane		= jpeg_enc_vidioc_try_fmt,
	.vidioc_s_fmt_vid_cap_mplane		= jpeg_enc_vidioc_s_fmt_cap,
	.vidioc_s_fmt_vid_out_mplane		= jpeg_enc_vidioc_s_fmt_out,

	.vidioc_reqbufs		= jpeg_enc_m2m_reqbufs,
	.vidioc_querybuf		= jpeg_enc_m2m_querybuf,
	.vidioc_qbuf			= jpeg_enc_m2m_qbuf,
	.vidioc_dqbuf			= jpeg_enc_m2m_dqbuf,
	.vidioc_streamon		= jpeg_enc_m2m_streamon,
	.vidioc_streamoff		= jpeg_enc_m2m_streamoff,
	.vidioc_g_jpegcomp		= vidioc_enc_g_jpegcomp,
	.vidioc_s_jpegcomp		= vidioc_enc_s_jpegcomp,
	.vidioc_s_ctrl			= jpeg_enc_vidioc_s_ctrl,
};
const struct v4l2_ioctl_ops *get_jpeg_hx_enc_v4l2_ioctl_ops(void)
{
	return &jpeg_enc_ioctl_ops;
}
