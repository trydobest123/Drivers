#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/wait.h>
#include <linux/i2c.h>
#include <linux/poll.h>
#include <linux/gfp.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/irqreturn.h>
#include <linux/interrupt.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-vmalloc.h>
#include <mach/irqs.h>
#include <asm/io.h>

#define CMOS_OV7740_QUEUE_DISCONNECTED		(1 << 0)
#define CMOS_OV7740_QUEUE_DROP_CORRUPTED	(1 << 1)

#define  buf_limit  4

struct camif_buf_addr{
	unsigned int order;
	unsigned int phy_addr;
	unsigned int vir_addr;
};

struct camera_format{
	unsigned char name[64];
	unsigned int fourcc;
	unsigned int depth;
};

struct cmos_ov7740_reg{
	unsigned char addr;
	unsigned char value;
};

enum cmos_ov7740_buffer_state {
	CMOS_OV7740_BUF_STATE_IDLE	= 0,
	CMOS_OV7740_BUF_STATE_QUEUED	= 1,
	CMOS_OV7740_BUF_STATE_ACTIVE	= 2,
	CMOS_OV7740_BUF_STATE_READY	= 3,
	CMOS_OV7740_BUF_STATE_DONE	= 4,
	CMOS_OV7740_BUF_STATE_ERROR	= 5,
}buf_state;

struct cmos_ov7740_video_queue {
	unsigned int flags;
	struct list_head irqqueue;
}cmos_ov7740_queue;

struct cmos_ov7740_video_buffer {
	struct vb2_buffer buf;
	struct list_head queue;
	enum cmos_ov7740_buffer_state state;
	unsigned int error;
	void *mem;
	unsigned int length;
	unsigned int bytesused;
};

/* camif gpio*/
static volatile unsigned int *gpjcon = NULL;
static volatile unsigned int *gpjdat = NULL;
static volatile unsigned int *gpjup = NULL;

/* camif controler */
static volatile unsigned int *cisrcfmt;
static volatile unsigned int *ciwdofst;
static volatile unsigned int *cigctrl;
static volatile unsigned int *ciprclrsa1;
static volatile unsigned int *ciprclrsa2;
static volatile unsigned int *ciprclrsa3;
static volatile unsigned int *ciprclrsa4;
static volatile unsigned int *ciprtrgfmt;
static volatile unsigned int *ciprctrl;
static volatile unsigned int *ciprscpreratio;
static volatile unsigned int *ciprscpredst;
static volatile unsigned int *ciprscctrl;
static volatile unsigned int *ciprtarea;
static volatile unsigned int *ciimgcpt;

static struct i2c_client *cmos_ov7740_client = NULL;
static struct video_device *cmos_ov7740_video    = NULL; 
static struct vb2_queue *cmos_ov7740_vb_queue = NULL;
struct v4l2_format *cur_v4l2_fmt = NULL;
static struct camif_buf_addr cmos_ov7740_addr;
static unsigned int state;
static unsigned int target_size;
static unsigned int bytes_per_line;
static unsigned int target_height,target_width;
static unsigned int src_width, src_height;
static DECLARE_WAIT_QUEUE_HEAD(camif_done);

static int cmos_ov7740_queue_setup(struct vb2_queue *vq, const struct v4l2_format *fmt,
				unsigned int *nbuffers, unsigned int *nplanes,
				unsigned int sizes[], void *alloc_ctxs[]);
static int cmos_ov7740_buffer_prepare(struct vb2_buffer *vb); 
 static void cmos_ov7740_buffer_queue(struct vb2_buffer *vb);
static int cmos_ov7740_start_streaming(struct vb2_queue *vq, unsigned int count);
static int cmos_ov7740_stop_streaming(struct vb2_queue *vq);

static struct vb2_ops cmos_ov7740_video_qops = {
	.queue_setup		= cmos_ov7740_queue_setup,
	.buf_prepare		= cmos_ov7740_buffer_prepare, 
	.buf_queue		= cmos_ov7740_buffer_queue,
	.start_streaming	= cmos_ov7740_start_streaming,
	.stop_streaming	= cmos_ov7740_stop_streaming,
};

static struct cmos_ov7740_reg ov7740_reg_set[] =
{
	{0x12, 0x80},{0x47, 0x02},{0x17, 0x27},{0x04, 0x40},
	{0x1B, 0x81},{0x29, 0x17},{0x5F, 0x03},{0x3A, 0x09},
	{0x33, 0x44},{0x68, 0x1A},{0x14, 0x38},{0x5F, 0x04},
	{0x64, 0x00},{0x67, 0x90},{0x27, 0x80},{0x45, 0x41},
	{0x4B, 0x40},{0x36, 0x2f},{0x11, 0x01},{0x36, 0x3f},
	{0x0c, 0x12},{0x12, 0x00},{0x17, 0x25},{0x18, 0xa0},
	{0x1a, 0xf0},{0x31, 0xa0},{0x32, 0xf0},{0x85, 0x08},
	{0x86, 0x02},{0x87, 0x01},{0xd5, 0x10},{0x0d, 0x34},
	{0x19, 0x03},{0x2b, 0xf8},{0x2c, 0x01},{0x53, 0x00},
	{0x89, 0x30},{0x8d, 0x30},{0x8f, 0x85},{0x93, 0x30},
	{0x95, 0x85},{0x99, 0x30},{0x9b, 0x85},{0xac, 0x6E},
	{0xbe, 0xff},{0xbf, 0x00},{0x38, 0x14},{0xe9, 0x00},
	{0x3D, 0x08},{0x3E, 0x80},{0x3F, 0x40},{0x40, 0x7F},
	{0x41, 0x6A},{0x42, 0x29},{0x49, 0x64},{0x4A, 0xA1},
	{0x4E, 0x13},{0x4D, 0x50},{0x44, 0x58},{0x4C, 0x1A},
	{0x4E, 0x14},{0x38, 0x11},{0x84, 0x70}
};

static struct camera_format cmos_ov7740_format[] = {
	{
		.name     = "RGB24 Packed",
		.fourcc   = V4L2_PIX_FMT_RGB24,
		.depth    = 32,
	},
	{
		.name     = "RGB565",
		.fourcc   = V4L2_PIX_FMT_RGB565, /* gggbbbbb rrrrrggg */
		.depth    = 16,
	},
};

static void camera_ov7740_init(void)
{
	int mid;
	int i;
	
	mid   =  i2c_smbus_read_byte_data(cmos_ov7740_client,0xa) << 8;
	mid |= i2c_smbus_read_byte_data(cmos_ov7740_client,0xb);

	printk("MID = 0x%04x\n",mid);

	for(i = 0; i < ARRAY_SIZE(ov7740_reg_set);i++)
	{
		i2c_smbus_write_byte_data(cmos_ov7740_client,ov7740_reg_set[i].addr,ov7740_reg_set[i].value);
		mdelay(10);
	}
}

static void cmos_ov7740_reset(void)
{
	*cigctrl |= 1<<30;
	mdelay(10);
	*cigctrl &= ~(1<<30);
	mdelay(10);
	*cigctrl |= 1<<30;
	mdelay(10);
}

static void cmos_ov7740_clk_init(void)
{
	struct clk *camif_clk;
	struct clk *camif_upll_clk;

	/* 使能CAMIF的时钟源 */
	camif_clk = clk_get(NULL, "camif");
	if(!camif_clk){
		printk( "failed to get camif clock\n");
		return;
	}
	clk_enable(camif_clk);

	/* 使能并设置CAMCLK = 24MHz */
	camif_upll_clk = clk_get(NULL, "camif-upll");
	clk_set_rate(camif_upll_clk, 24000000);
	mdelay(100);
}

static void camif_gpio_init(void)
{
 	  gpjcon = ioremap(0x560000d0, 4);
 	  gpjdat = gpjcon+1;
 	  gpjup = gpjdat+1;

 	*gpjcon = 0x2aaaaaa;
 	*gpjdat  =0x0;
 	*gpjup   = 0x0; //使能上拉

}

static void camif_reg_init(void)
{
	cisrcfmt = ioremap(0x4f000000,4);
	ciwdofst = ioremap(0x4f000004,4);	
	cigctrl  = ioremap(0x4f000008,4);
	ciprclrsa1 = ioremap(0x4f00006c,4);	
	ciprclrsa2 = ioremap(0x4f000070,4);
	ciprclrsa3 = ioremap(0x4f000074,4);	
	ciprclrsa4 = ioremap(0x4f000078,4);
	ciprtrgfmt = ioremap(0x4f00007c,4);	
	ciprctrl = ioremap(0x4f000080,4);
	ciprscpreratio = ioremap(0x4f000084,4);	
	ciprscpredst = ioremap(0x4f000088,4);
	ciprscctrl = ioremap(0x4f00008c,4);	
	ciprtarea = ioremap(0x4f000090,4);
	ciimgcpt = ioremap(0x4f0000a0,4);	
}

static void camif_gpio_exit(void)
{
	iounmap(gpjcon);
	iounmap(gpjcon);
	iounmap(gpjup);
}

static void camif_reg_exit(void)
{
	iounmap(cisrcfmt);
	iounmap(ciwdofst);
	iounmap(cigctrl);
	iounmap(ciprclrsa1);
	iounmap(ciprclrsa2);
	iounmap(ciprclrsa3);
	iounmap(ciprclrsa4);
	iounmap(ciprtrgfmt);
	iounmap(ciprctrl);
	iounmap(ciprscpreratio);
	iounmap(ciprscpredst);
	iounmap(ciprscctrl);
	iounmap(ciprtarea);
	iounmap(ciimgcpt);
}

static void camif_caculate_bust_size(unsigned int h_size,unsigned int *main_burst,unsigned int *remain_burst)
{	
	/* 先以16byte传输 */
	unsigned int tmp;
	tmp = (h_size / 4)  %16;
	switch(tmp)
	{
		case 0:
			*main_burst = 16;
			*remain_burst = 16;
			break;
		case 2:
			*main_burst = 16;
			*remain_burst = 2;
			break;
		case 4:
			*main_burst = 16;
			*remain_burst = 4;
			break;

		case 8:
			*main_burst = 16;
			*remain_burst = 8;
			break;
		default :
		{
			tmp = (h_size/4) % 8;
			switch(tmp)
			{
				case 0:
					*main_burst = 8;
					*remain_burst = 8;
					break;

				case 2:
					*main_burst = 8;
					*remain_burst = 2;
					break;
				
				case 4:
					*main_burst = 8;
					*remain_burst = 4;
					break;
				default:
				{
					tmp = (h_size/4) %4;
					switch(tmp)
					{
						case 0:
							*main_burst = 4;
							*remain_burst = 4;
							break;
						case 2:
							*main_burst = 4;
							*remain_burst = 2;
							break;
						default:
						{
							*main_burst = 2;
							*remain_burst = 2;
							break;
						}
					}
					break;
				}
			}					
			break;
		}
	}
}

static int camif_get_scaler_info(unsigned int src,unsigned int dst,unsigned int *shift,unsigned int *pre_ratio)
{
	if( src >= 64*dst ){
		return -EINVAL; /* Out Of Vertical Scale Range */ 
	}
	else if (src >= 32*dst) { *pre_ratio = 32; *shift = 5; }
	else if (src>= 16*dst) { *pre_ratio = 16; *shift = 4; }
	else if (src >= 8*dst) { *pre_ratio = 8; *shift = 3; }
	else if (src >= 4*dst) { *pre_ratio = 4; *shift = 2; }
	else if (src >= 2*dst) { *pre_ratio = 2; *shift = 1; }
	else { *pre_ratio = 1; *shift = 0; }

	return 0;
}

static void camera_interface_reset(void)
{
	*cisrcfmt |= 1<<31;
	mdelay(10);
	*cigctrl |= 1<< 31;
	mdelay(10);
	*cigctrl &= ~(1<< 31);
	mdelay(10);
}

static int cmos_ov7740_stream_on(struct file *file, void *fh, enum v4l2_buf_type i)
{
	/* 打开视频传输 */
	return vb2_streamon(cmos_ov7740_vb_queue, i);
}

static int cmos_ov7740_stream_off(struct file *file, void *fh, enum v4l2_buf_type i)
{
	/* 关闭视频传输 */
	return vb2_streamoff(cmos_ov7740_vb_queue, i);
}

static irqreturn_t s3c2440_irq_cam_c(int irq, void *devid)
{	
	
	return IRQ_HANDLED;
}

static irqreturn_t s3c2440_irq_cam_p(int irq, void *devid)
{
	struct cmos_ov7740_video_buffer *cmos_buf;
	unsigned int maxlen;
	unsigned int nbytes;	
	unsigned char *mem;
	
	/* 取出数据 */
	if (!list_empty(&cmos_ov7740_queue.irqqueue))
	{
		cmos_buf = list_first_entry(&cmos_ov7740_queue.irqqueue, struct cmos_ov7740_video_buffer,queue);
		list_del(&cmos_buf->queue);
	}		  
	else
	{
		cmos_buf = NULL;
		return IRQ_NONE;
	}				
	
	mem = (unsigned char *)cmos_buf->mem + cmos_buf->bytesused;
	maxlen = cmos_buf->length - cmos_buf->bytesused;
	nbytes = min(target_size,maxlen);
	cmos_buf->bytesused += nbytes;
	memcpy(mem, (unsigned char *)cmos_ov7740_addr.vir_addr, nbytes);
	
	cmos_buf->state = cmos_buf->error ? VB2_BUF_STATE_ERROR : CMOS_OV7740_BUF_STATE_DONE;
	vb2_set_plane_payload(&cmos_buf->buf, 0, cmos_buf->bytesused);
	vb2_buffer_done(&cmos_buf->buf, VB2_BUF_STATE_DONE); //唤醒等待的进程
	
	return IRQ_HANDLED;
}

static int cmos_ov7740_querycap(struct file *file, void *fh, struct v4l2_capability *cap)
{
	//printk("%s,%d\n",__FUNCTION__,__LINE__);
	strlcpy(cap->driver, "cmos_video", sizeof cap->driver);
	strlcpy(cap->card, "cmos_video", sizeof cap->card);
	cap->version = LINUX_VERSION_CODE;
	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	return 0;
}

static int cmos_ov7740_enum_fmt(struct file *file, void *fh,struct v4l2_fmtdesc *fmt)
{
	//printk("%s,%d\n",__FUNCTION__,__LINE__);
	struct camera_format *format;	
	enum v4l2_buf_type type = fmt->type;
	unsigned int  index = fmt->index;
	if(fmt->index >= ARRAY_SIZE(cmos_ov7740_format))
		return -EIO;
		
	memset(fmt, 0, sizeof(*fmt));
	fmt->index = index;
	fmt->type = type;
	format = &cmos_ov7740_format[fmt->index];
	strlcpy(fmt->description, format->name,sizeof(fmt->description));
	fmt->description[sizeof fmt->description - 1] = 0;
	fmt->pixelformat = format->fourcc;

	return 0;
}

static int cmos_ov7740_get_fmt(struct file *file, void *fh,struct v4l2_format *fmt)
{
	//printk("%s,%d\n",__FUNCTION__,__LINE__);
	fmt->fmt.pix.pixelformat = cur_v4l2_fmt->fmt.pix.pixelformat;
	fmt->fmt.pix.field = V4L2_FIELD_NONE;
	fmt->fmt.pix.bytesperline =cur_v4l2_fmt->fmt.pix.bytesperline;
	fmt->fmt.pix.sizeimage = target_size;
	fmt->fmt.pix.priv = 0;
	return 0;
}

static int cmos_ov7740_try_fmt(struct file *file, void *fh,struct v4l2_format *fmt)
{
	struct camera_format *format;
	int i;

	//printk("%s,%d\n",__FUNCTION__,__LINE__);
	if(fmt->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
		
	for (i = 0; i < ARRAY_SIZE(cmos_ov7740_format); i++)
	{
		format = &cmos_ov7740_format[i];
		if (format->fourcc == fmt->fmt.pix.pixelformat)
			break;
	}

	if (format == NULL || format->fourcc != fmt->fmt.pix.pixelformat) 
	{
		printk("Unsupported format 0x%08x.\n",fmt->fmt.pix.pixelformat);
		return -EINVAL;
	}

	return 0;
}

static int cmos_ov7740_set_fmt(struct file *file, void *fh,struct v4l2_format *fmt)
{
	//printk("%s,%d\n",__FUNCTION__,__LINE__);
	
	if(cmos_ov7740_try_fmt(file,fh,fmt))
		return -EINVAL;

	target_height = fmt->fmt.pix.height;
	target_width  = fmt->fmt.pix.width;

	/* 进行硬件的设置 */
	if(fmt->fmt.pix.pixelformat == V4L2_PIX_FMT_RGB565)
	{
		fmt->fmt.pix.bytesperline = (fmt->fmt.pix.width * 16) >> 3;
		*ciprscctrl &= ~(1<<30);
	}

	if(fmt->fmt.pix.pixelformat == V4L2_PIX_FMT_RGB24)
	{
		fmt->fmt.pix.bytesperline = (fmt->fmt.pix.width * 32) >> 3;
		*ciprscctrl |= (1<<30);
	}

	bytes_per_line = fmt->fmt.pix.bytesperline;
	target_size = bytes_per_line * target_height;
	fmt->fmt.pix.sizeimage = target_size;
	cur_v4l2_fmt = fmt;

	printk("target_size = %d\n",target_size);
	*ciprtrgfmt |= (target_width<<16) |(0<<14)|(target_height<<0);
	
	return 0;
}

/*
 *为摄像头分配缓存以存储运行中产生的数据
 *并为APP读取缓冲区分配空间
 */
static int cmos_ov7740_request_buffer(struct file *file, void *fh, struct v4l2_requestbuffers *req)
{
	unsigned int order; 
	//printk("%s,%d\n",__FUNCTION__,__LINE__);
	
	order = get_order(target_size);
	cmos_ov7740_addr.order = order;
	cmos_ov7740_addr.vir_addr =  __get_dma_pages(GFP_KERNEL,cmos_ov7740_addr.order);
	if(cmos_ov7740_addr.vir_addr == (unsigned int)NULL)
	{
		printk("request virtual mem error.\n");
		return -ENOMEM;
	}

	cmos_ov7740_addr.phy_addr =  __virt_to_phys(cmos_ov7740_addr.vir_addr);
	
	*ciprclrsa1  = cmos_ov7740_addr.phy_addr;
	*ciprclrsa2  = cmos_ov7740_addr.phy_addr;
	*ciprclrsa3  = cmos_ov7740_addr.phy_addr;
	*ciprclrsa4  = cmos_ov7740_addr.phy_addr;
	
	return vb2_reqbufs(cmos_ov7740_vb_queue, req);
}

static int cmos_ov7740_query_buffer(struct file *file, void *fh, struct v4l2_buffer *b)
{
	//printk("%s,%d\n",__FUNCTION__,__LINE__);
	return vb2_querybuf(cmos_ov7740_vb_queue, b);
}

static int cmos_ov7740_queue_buffer(struct file *file, void *fh, struct v4l2_buffer *b)
{
	//printk("%s,%d\n",__FUNCTION__,__LINE__);
	return vb2_qbuf(cmos_ov7740_vb_queue, b);
}

static int cmos_ov7740_dequeue_buffer(struct file *file, void *fh, struct v4l2_buffer *b)
{
	int ret;
	//printk("%s,%d\n",__FUNCTION__,__LINE__);
	ret = vb2_dqbuf(cmos_ov7740_vb_queue, b,file->f_flags & O_NONBLOCK);
	printk("dequeue buffer index = %d\n",b->index);
	return ret;
}

static int cmos_ov7740_queue_setup(struct vb2_queue *vq, const struct v4l2_format *fmt,
				unsigned int *nbuffers, unsigned int *nplanes,
				unsigned int sizes[], void *alloc_ctxs[])
{
	unsigned int size = target_size;
	//printk("%s,%d\n",__FUNCTION__,__LINE__);
	if (0 == *nbuffers)
		*nbuffers = 32;

	while (size * *nbuffers > buf_limit * 1024 * 1024)
		(*nbuffers)--;

	*nplanes = 1;

	sizes[0] = size;
	
	return 0;
}

/*  每次提交queue_buffer 前的准备工作 */
static int cmos_ov7740_buffer_prepare(struct vb2_buffer *vb)
{	
	//printk("%s,%d\n",__FUNCTION__,__LINE__);
	struct cmos_ov7740_video_buffer *cmos_buffer;
	cmos_buffer = container_of(vb, struct cmos_ov7740_video_buffer, buf);
	cmos_buffer->error = 0;
	cmos_buffer->buf.vb2_queue = vb->vb2_queue;
	cmos_buffer->state = CMOS_OV7740_BUF_STATE_QUEUED;
	cmos_buffer->mem = vb2_plane_vaddr(vb, 0);
	cmos_buffer->length = vb2_plane_size(vb, 0);

	if (vb->v4l2_buf.type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
		cmos_buffer->bytesused = 0;
	else
		cmos_buffer->bytesused = vb2_get_plane_payload(vb, 0);
		
	return 0;
}
 
 static void cmos_ov7740_buffer_queue(struct vb2_buffer *vb)
{	
	//printk("%s,%d\n",__FUNCTION__,__LINE__);
	printk("queue buffer index = %d\n",vb->v4l2_buf.index);
	struct cmos_ov7740_video_buffer *cmos_buf;
	cmos_buf = container_of(vb,struct cmos_ov7740_video_buffer,buf);
	cmos_buf->buf = *vb;
	if (!(cmos_ov7740_queue.flags & CMOS_OV7740_QUEUE_DISCONNECTED)) 
	{
		list_add_tail(&cmos_buf->queue, &cmos_ov7740_queue.irqqueue);
	}
	else
	{
		cmos_buf->state = CMOS_OV7740_BUF_STATE_ERROR;
		vb2_buffer_done(&cmos_buf->buf, VB2_BUF_STATE_ERROR);
	}
}

/* 打开摄像头 */
static int cmos_ov7740_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	//printk("%s,%d\n",__FUNCTION__,__LINE__);
	unsigned int main_burst,remain_burst;
	unsigned int h_shift,v_shift,pre_hor_ratio,pre_ver_ratio;
	unsigned int pre_dst_width,main_hor_ratio;
	unsigned int pre_dst_height,main_ver_ratio,sh_factor;
	unsigned int scale_up_down;
	int ret;
	
	src_width   = 640;
	src_height  = 480;

	/* 缩放比例的设置 */
	camif_caculate_bust_size(bytes_per_line,&main_burst,&remain_burst);
	ret = camif_get_scaler_info(src_width,target_width,&h_shift,&pre_hor_ratio);
	ret = camif_get_scaler_info(src_height,target_height,&v_shift,&pre_ver_ratio);
	
	if(ret){
		printk("get scaler info error.\n");
		return -EFAULT;
	}
	pre_dst_width  = src_width/pre_hor_ratio;
	main_hor_ratio = ( src_width<<8)/( target_width << h_shift);

	pre_dst_height = src_height /pre_ver_ratio;
	main_ver_ratio = ( src_height << 8) /( target_height << v_shift);
	sh_factor = 10 - ( h_shift + v_shift);

	scale_up_down = (target_height>=src_width) ? 1:0;

	*cisrcfmt |= (0<<30)|(0<<29)|(src_width<<16)|(0x2<<14)|(src_height<<0);
	*ciwdofst |=(1<<30)|(0xf<<12);
	*ciwdofst |= (1<<31)|(0<<16)|(0<<0);
	*cigctrl |= (1<<29)|(0<<27)|(0<<26)|(0<<25)|(0<<24);	
	*ciprctrl    |= (main_burst<<19)  | (remain_burst<<14)|(0<<2);
	*ciprscpreratio = (sh_factor<<28) | (pre_hor_ratio<<16) | (pre_ver_ratio<<0);
	*ciprscpredst    = (pre_dst_width<<16) | (pre_dst_height<<0);
	*ciprscctrl   |= (1<<31)|(scale_up_down<<28)|(main_hor_ratio<<16) | (main_ver_ratio<<0);
	*ciprtarea   = target_width * target_height;
	
	/*preview scaler start*/
	*ciprscctrl  |=  (1<<15); 
	*ciimgcpt  = (1<<31)|(1<<29);
	return 0;
}

static int cmos_ov7740_stop_streaming(struct vb2_queue *vq)
{
	//printk("%s,%d\n",__FUNCTION__,__LINE__);
	*ciprscctrl  &= ~(1<<15); 
	*ciimgcpt  &= ~((1<<31)|(1<<29));

	vq->streaming = 0;
	return 0;
}

static void cmos_queue_init(struct vb2_queue *queue)
{
	queue->type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
	queue->io_modes = VB2_MMAP | VB2_USERPTR | VB2_READ,
	queue->buf_struct_size = sizeof(struct cmos_ov7740_video_buffer);
	queue->ops = &cmos_ov7740_video_qops,
	queue->mem_ops = &vb2_vmalloc_memops,
	
	INIT_LIST_HEAD(&cmos_ov7740_queue.irqqueue);
	vb2_queue_init(queue);
}

static int cmos_ov7740_open(struct file *filp)
{
	return 0;
}

static int cmos_ov7740_mmap (struct file *filp, struct vm_area_struct *vma)
{
	return vb2_mmap(cmos_ov7740_vb_queue, vma);
}

static unsigned int cmos_ov7740_poll (struct file *filp, poll_table *wait)
{
	return vb2_poll(cmos_ov7740_vb_queue,filp,wait);
}

static int cmos_ov7740_release(struct file *filp)
{	
	if(!state)
	{
		free_pages(cmos_ov7740_addr.vir_addr,cmos_ov7740_addr.order);
		state = 1;
	}

	cmos_ov7740_stop_streaming(cmos_ov7740_vb_queue);
	
	return 0;
}

static void cmos_ov7740_video_release(struct video_device *vdev)
{	
	if(!state)
	{
		free_pages(cmos_ov7740_addr.vir_addr,cmos_ov7740_addr.order);		
		state = 1;
	}
	printk("cmos_ov7740 release\n");
}

static struct v4l2_file_operations cmos_ov7740_fops = {
	.owner		    = THIS_MODULE,
	.open		    = cmos_ov7740_open,
	.release	    	    = cmos_ov7740_release,
	.mmap		    = cmos_ov7740_mmap,
	.poll 		    = cmos_ov7740_poll,
	.unlocked_ioctl   = video_ioctl2,
};


static struct v4l2_ioctl_ops cmos_ov7740_ioctl = {
	.vidioc_querycap 		    = 	cmos_ov7740_querycap,
	.vidioc_try_fmt_vid_cap      = cmos_ov7740_try_fmt,
	.vidioc_enum_fmt_vid_cap = cmos_ov7740_enum_fmt,
	.vidioc_g_fmt_vid_cap        = cmos_ov7740_get_fmt,
	.vidioc_s_fmt_vid_cap         = cmos_ov7740_set_fmt,
	.vidioc_reqbufs                   = cmos_ov7740_request_buffer,
	.vidioc_querybuf                 = cmos_ov7740_query_buffer,
	.vidioc_qbuf			     = cmos_ov7740_queue_buffer,
	.vidioc_dqbuf			     = cmos_ov7740_dequeue_buffer,
	.vidioc_streamon                = cmos_ov7740_stream_on,
	.vidioc_streamoff                = cmos_ov7740_stream_off,
};

static int cmos_ov7740_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{		
		int ret;

		cmos_ov7740_client = client;
		
		/* 初始化GPIO */
		camif_gpio_init();
		/* 映射寄存器 */
		camif_reg_init(); 
		/* 获得时钟 */
		cmos_ov7740_clk_init();
		/* 控制器复位 */
		camera_interface_reset();
		/* 摄像头复位 */
		cmos_ov7740_reset();
		/* 摄像头初始化 */
		camera_ov7740_init();
		
		cmos_ov7740_video  = video_device_alloc();
		if(!cmos_ov7740_video)
			return -ENOMEM;

		cmos_ov7740_vb_queue = kzalloc(sizeof(struct vb2_queue),GFP_KERNEL);
		if(!cmos_ov7740_vb_queue)
		{
			printk("cmos_ov7740_queue alloc failed\n");
			return -ENOMEM;
		}

		cmos_queue_init(cmos_ov7740_vb_queue);
		cmos_ov7740_video->fops         = &cmos_ov7740_fops;
		cmos_ov7740_video->ioctl_ops = &cmos_ov7740_ioctl;
		cmos_ov7740_video->release    = cmos_ov7740_video_release;

		/* 申请中断 */
		ret = request_irq(IRQ_S3C2440_CAM_C,s3c2440_irq_cam_c,0,"s3c_cam_c",NULL);
		if(ret)
		{
			printk("request camera codec irq error.\n");
			return -EIO;
		}

		ret = request_irq(IRQ_S3C2440_CAM_P,s3c2440_irq_cam_p,0,"s3c_cam_p",NULL);
		if(ret)
		{
			printk("request camera preview irq error.\n");
			return -EIO;
		}

		ret = video_register_device(cmos_ov7740_video, VFL_TYPE_GRABBER, -1);
		
		return ret;
}

static int  cmos_ov7740_i2c_remove(struct i2c_client *client)
{
	camif_gpio_exit();
	camif_reg_exit();
	free_irq(IRQ_S3C2440_CAM_C,NULL);
	free_irq(IRQ_S3C2440_CAM_P,NULL);
	kfree(cmos_ov7740_vb_queue);
	video_unregister_device(cmos_ov7740_video);
	video_device_release(cmos_ov7740_video);

	return 0;
}

static struct i2c_device_id cmos_ov7740_id[] = {
	{.name = "cmos_ov9650"},
	{.name = "cmos_ov7740"},
	{},
};

static struct i2c_driver cmos_ov7740_driver = {
	.driver = {
		.name = "cmos_ov7740",
	},
	.probe        = cmos_ov7740_i2c_probe,
	.remove     = cmos_ov7740_i2c_remove,
	.id_table 	  = cmos_ov7740_id,
};

static int cmos_ov7740_drv_init(void)
{	
	return  i2c_add_driver(&cmos_ov7740_driver);
}

static void cmos_ov7740_drv_exit(void)
{
	i2c_del_driver(&cmos_ov7740_driver);
}

module_init(cmos_ov7740_drv_init);
module_exit(cmos_ov7740_drv_exit);
MODULE_LICENSE("GPL");


