#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/gfp.h>
#include <linux/sched.h>
#include <linux/irqreturn.h>
#include <linux/interrupt.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-ioctl.h>
#include <mach/irqs.h>
#include <asm/io.h>

struct cmos_ov7740_reg{
	unsigned char addr;
	unsigned char value;
};

struct camif_addr{
	unsigned int order;
	unsigned int phy_addr;
	unsigned int vir_addr;
};


struct camera_format{
	unsigned char name[64];
	unsigned int fourcc;
	unsigned int depth;
};

struct camif_buf_addr{
	unsigned int order;
	unsigned int phy_addr;
	unsigned int vir_addr;
};

/*GPE14 = SCL*/
/*GPE15 = SDA*/
static volatile unsigned int *gpecon = NULL;
static volatile unsigned int *gpedat  = NULL;
static volatile unsigned int *gpepull  = NULL;

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

static struct video_device *cmos_ov7740_video    = NULL; 
static unsigned char cam_event = 0;
static unsigned char state = 0;
static unsigned int buf_size;
static unsigned int bytes_per_line;
static unsigned int target_height,target_width;
static unsigned int src_width, src_height;
static struct camif_buf_addr imag_buf;

static DECLARE_WAIT_QUEUE_HEAD(camif_wait_queue);

static struct camera_format cmos_ov7740_format[] = {
	{
		.name     = "RGB24 Packed",
		.fourcc   = V4L2_PIX_FMT_RGB24,
		.depth    = 24,
	},
	{
		.name     = "RGB565",
		.fourcc   = V4L2_PIX_FMT_RGB565, /* gggbbbbb rrrrrggg */
		.depth    = 16,
	},
};

static struct cmos_ov7740_reg ov7740_reg_set[] =
{
	{0x12, 0x80},{0x47, 0x02},{0x17, 0x27},
	{0x04, 0x40},{0x1B, 0x81},{0x29, 0x17},
	{0x5F, 0x03},{0x3A, 0x09},{0x33, 0x44},
	{0x68, 0x1A},{0x14, 0x38},{0x5F, 0x04},
	{0x64, 0x00},{0x67, 0x90},{0x27, 0x80},
	{0x45, 0x41},{0x4B, 0x40},{0x36, 0x2f},
	{0x11, 0x01},{0x36, 0x3f},{0x0c, 0x12},
	{0x12, 0x00},{0x17, 0x25},{0x18, 0xa0},
	{0x1a, 0xf0},{0x31, 0xa0},{0x32, 0xf0},
	{0x85, 0x08},{0x86, 0x02},{0x87, 0x01},
	{0xd5, 0x10},{0x0d, 0x34},{0x19, 0x03},
	{0x2b, 0xf8},{0x2c, 0x01},{0x53, 0x00},
	{0x89, 0x30},{0x8d, 0x30},{0x8f, 0x85},
	{0x93, 0x30},{0x95, 0x85},{0x99, 0x30},
	{0x9b, 0x85},{0xac, 0x6E},{0xbe, 0xff},
	{0xbf, 0x00},{0x38, 0x14},{0xe9, 0x00},
	{0x3D, 0x08},	{0x3E,0x80},{0x3F,0x40},
	{0x40, 0x7F},{0x41, 0x6A},{0x42, 0x29},
	{0x49, 0x64},{0x4A, 0xA1},{0x4E, 0x13},
	{0x4D, 0x50},{0x44, 0x58},{0x4C, 0x1A},
	{0x4E, 0x14},{0x38, 0x11},{0x84, 0x70}
};

static void sccb_set_dat(unsigned char dat)
{		
	 
	if(dat)
		*gpedat |= (1 << 15);
	else
		*gpedat &= ~(1 << 15);
}

static unsigned char sccb_get_dat(void)
{			
	if(*gpedat & (1<<15))
		return 1;
	else
		return 0;
}

static void set_gpio_out(void)
{
	 *gpecon  &= ~(0x3 << 30);
	 *gpecon  |= (1<<30);
}

static void set_gpio_in(void)
{
	 *gpecon  &= ~(0x3 << 30);
}

static void sccb_set_clk(unsigned char dat)
{		
	if(dat)
		*gpedat |= (1 << 14);
	else
		*gpedat &= ~(1 << 14);
}


static void sccb_reset(void)
{
	set_gpio_out();
	sccb_set_clk(1);
	sccb_set_dat(1);
}

static void sccb_start(void)
{		
	set_gpio_out(); 
	sccb_set_dat(1);
	udelay(5);
	sccb_set_clk(1);
	udelay(5);
	sccb_set_dat(0);		
}

static void sccb_stop(void)
{		
	set_gpio_out();
	sccb_set_dat(0);
	udelay(5);
	sccb_set_clk(1);
	udelay(5);
	sccb_set_dat(1);	
	udelay(5);
}

static void sccb_wait_ack(void)
{
	set_gpio_in();
	sccb_set_clk(1);
	while(sccb_get_dat());
	udelay(5);
	sccb_set_clk(0);
	udelay(5);
}

static void sccb_write_byte(unsigned char byte)
{
	 int i;
	 
	 set_gpio_out();
	 
	 for(i = 0; i < 8; i++)
	{
		sccb_set_clk(0);	
		udelay(5);
	 	sccb_set_dat(byte&0x80);
	 	udelay(5);
		sccb_set_clk(1);
		udelay(5);
		byte <<= 1;
	 }

	 sccb_set_clk(0);	
	udelay(5);	
	sccb_set_dat(1); //释放总线
	udelay(5);
}

static unsigned char  sccb_read_byte(void)
{
	int i;
	unsigned char dat = 0; 
	
	set_gpio_in();
	sccb_set_clk(0);
	for(i = 0; i < 8; i++)
	{
		dat<<=1;
		udelay(5);
		sccb_set_clk(1);
		dat |=sccb_get_dat();
		udelay(5);
		sccb_set_clk(0);
	}
	
	return dat;
}

/* cmos ov7740 write reg*/
static void cmos_ov7740_write_byte(unsigned char addr,unsigned char dat)
{
	sccb_start();
	sccb_write_byte(0x42);
	sccb_wait_ack();
	sccb_write_byte(addr);
	sccb_wait_ack();
	sccb_write_byte(dat);
	sccb_stop();
}

static unsigned char cmos_ov7740_read_byte(unsigned char addr)
{
	unsigned char dat = 0;
	
	sccb_start();
	sccb_write_byte(0x42);
	sccb_wait_ack();
	sccb_write_byte(addr);
	sccb_wait_ack();
	sccb_stop();
	
	sccb_start();
	sccb_write_byte(0x43);
	sccb_wait_ack();
	dat = sccb_read_byte();
	sccb_stop();
	
	return dat;
}

static void camera_ov7740_init(void)
{
	int mid;
	int i;
	
	mid   =  cmos_ov7740_read_byte(0xa) << 8;
	mid |= cmos_ov7740_read_byte(0xb);

	printk("MID = 0x%04x\n",mid);

	for(i = 0; i < ARRAY_SIZE(ov7740_reg_set);i++)
	{
		cmos_ov7740_write_byte(ov7740_reg_set[i].addr,ov7740_reg_set[i].value);
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
	if(!camif_clk)
	{
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
	   gpecon = ioremap(0x56000040,4);
	   gpedat = gpecon + 1;
	   gpepull = gpedat + 1;
		
 	  gpjcon = ioremap(0x560000d0, 4);
 	  gpjdat = gpjcon+1;
 	  gpjup = gpjdat+1;
 
 	*gpecon  &= ~ (0x3 << 28);
 	*gpecon  |= 0x1 << 28; //输出
 	*gpepull = 0x3fff; //禁止上拉
 
 	*gpjcon = 0x0;
 	*gpjcon = 0x2aaaaaa;
 	*gpjup   = 0x0; //禁止上拉

	mdelay(20);
 	sccb_reset();
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

static void camif_gpio_exit(void)
{
	iounmap(gpecon);
	iounmap(gpedat);
	iounmap(gpepull);
	iounmap(gpjcon);
	iounmap(gpjcon);
	iounmap(gpjup);	
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

static int cmos_ov7740_open(struct file *filp)
{
	return 0;
}

static int cmos_ov7740_release(struct file *filp)
{	
	int order;
	
	state = 1; //已经关闭
	order = get_order(buf_size);
	free_pages(imag_buf.vir_addr,order);		
	
	return 0;
}

static ssize_t cmos_ov7740_read(struct file *filp, char __user *buf, size_t count, loff_t *ofs)
{
	int size;

	wait_event_interruptible(camif_wait_queue,cam_event);
	cam_event = 0;
	
	size = min_t(size_t, buf_size, count);
	if(copy_to_user(buf, (void *)imag_buf.vir_addr, size))
		return -EFAULT;

	return  size;
}

/* 异步阻塞 */
static unsigned int cmos_ov7740_poll(struct file *filp, struct poll_table_struct *pt)
{	
	unsigned int mask = 0;
	
	poll_wait(filp,&camif_wait_queue,pt);	
	if(cam_event)
	{
		cam_event = 0;
		mask |= POLLIN | POLLRDNORM;
	}
	
	return mask;
}

static void cmos_ov7740_video_release(struct video_device *vdev)
{	
	int order;
	
	if(!state)
	{
		order = get_order(buf_size);
		free_pages(imag_buf.vir_addr,order);		
	}	

	printk("cmos_ov7740 release\n");
}

/* 查询性能 */
static int cmos_ov7740_querycap(struct file *file, void *fh, struct v4l2_capability *cap)
{
	memset(cap, 0, sizeof *cap);
	strcpy(cap->driver, "cmos_ov7740");
	strcpy(cap->card, "cmos_ov7740");
	cap->version = 2;

	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_READWRITE;

	return 0;
}

/* 列举格式 */
static int cmos_ov7740_enum_fmt(struct file *file, void *fh,struct v4l2_fmtdesc *f)
{
	struct camera_format *fmt;

	if(f->index >ARRAY_SIZE(cmos_ov7740_format))
		return -EINVAL;
		
	memset(f, 0, sizeof(*f));
	fmt = &cmos_ov7740_format[f->index];
	strlcpy(f->description, fmt->name,sizeof(f->description));
	f->pixelformat = fmt->fourcc;
	
	return 0;
}

/* 获得格式 */
static int cmos_ov7740_get_fmt(struct file *file, void *fh,struct v4l2_format *f)
{	
	
	return 0;
}

/* 检查某种格式是否支持 */
static int cmos_ov7740_try_fmt (struct file *file, void *fh, struct v4l2_format *f)
{
	int i;
	int support = 0;
	
	if(f->type !=V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	for(i = 0; i < ARRAY_SIZE(cmos_ov7740_format); i++)
	{
		if(f->fmt.pix.pixelformat == cmos_ov7740_format[i].fourcc)
		{
			support = 1;
			break;
		}
	}
		
	return support ? 0 : -EINVAL;
}

/* 设置格式 */
static int cmos_ov7740_set_fmt(struct file *file, void *fh,struct v4l2_format *f)
{
	int ret = 0 ;

	printk("set_fmt.\n");
	/*先查询是否支持这种格式*/
	ret = cmos_ov7740_try_fmt(file,fh,f);
	if(ret<0)
	{
		printk("can't support image format.\n");
		return -EINVAL;
	}

	target_width  = f->fmt.pix.width;
	target_height = f->fmt.pix.height;
	
	if(f->fmt.pix.pixelformat == V4L2_PIX_FMT_RGB565)
	{
		f->fmt.pix.bytesperline = (f->fmt.pix.width * 16) >> 3;
		*ciprscctrl &= ~(1<<30);
	}

	if(f->fmt.pix.pixelformat == V4L2_PIX_FMT_RGB24)
	{
		f->fmt.pix.bytesperline = (f->fmt.pix.width * 32) >> 3;
		*ciprscctrl |= (1<<30);
	}

	f->fmt.pix.sizeimage =  f->fmt.pix.bytesperline  * f->fmt.pix.height;
	buf_size = f->fmt.pix.sizeimage;
	bytes_per_line = f->fmt.pix.bytesperline;

	*ciprtrgfmt |= (target_width<<16) |(0<<14)|(target_height<<0);
	
	return 0;
}

/* 申请缓存 */
static int cmos_ov7740_request_buffer(struct file *file, void *fh, struct v4l2_requestbuffers *b)
{	
	int order;

	order = get_order(buf_size);
	
	imag_buf.order = order;
	imag_buf.vir_addr = __get_dma_pages(GFP_KERNEL,imag_buf.order);	
	if(!imag_buf.vir_addr)
	{
		printk("get dma page error.\n");
		return -ENOMEM;
	}
	
	imag_buf.phy_addr = __virt_to_phys(imag_buf.vir_addr);
	
	*ciprclrsa1 =imag_buf.phy_addr;
	*ciprclrsa2 =imag_buf.phy_addr;
	*ciprclrsa3 =imag_buf.phy_addr;
	*ciprclrsa4 = imag_buf.phy_addr;
		
	return 0;

}

/* 打开摄像头 */
static int cmos_ov7740_stream_on(struct file *file, void *fh, enum v4l2_buf_type i)
{
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
		return -EINVAL;
	}
	
	pre_dst_width  = src_width/pre_hor_ratio;
	main_hor_ratio = ( src_width<<8)/( target_width << h_shift);

	pre_dst_height = src_height /pre_ver_ratio;
	main_ver_ratio = ( src_height << 8) /( target_height << v_shift);
	sh_factor = 10 - ( h_shift + v_shift);

	*cisrcfmt |= (0<<30)|(0<<29)|(src_width<<16)|(0x2<<14)|(src_height<<0);
	*ciwdofst |=(1<<30)|(0xf<<12);
	*ciwdofst |= (1<<31)|(0<<16)|(0<<0);
	*cigctrl |= (1<<29)|(0<<27)|(0<<26)|(0<<25)|(0<<24);
	*ciprctrl    |= (main_burst<<19)  | (remain_burst<<14)|(0<<2);
	*ciprscpreratio = (sh_factor<<28) | (pre_hor_ratio<<16) | (pre_ver_ratio<<0);
	*ciprscpredst    = (pre_dst_width<<16) | (pre_dst_height<<0);
	*ciprscctrl   |= (1<<31)|(scale_up_down<<28)|(main_hor_ratio<<16) | (main_ver_ratio<<0);
	*ciprtarea   = target_width * target_height;

	*ciprscctrl  |=  (1<<15); 
	*ciimgcpt  = (1<<31)|(1<<29);
	
	return 0;
}

/* 关闭摄像头 */
static int cmos_ov7740_stream_off(struct file *file, void *fh, enum v4l2_buf_type i)
{
	*ciprscctrl  &= ~(1<<15); 
	*ciimgcpt  &= ~((1<<31)|(1<<29));
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

static irqreturn_t s3c2440_irq_cam_c(int irq, void *devid)
{	
	
	return IRQ_HANDLED;
}

static irqreturn_t s3c2440_irq_cam_p(int irq, void *devid)
{

	cam_event = 1; //有数据可读了
	wake_up_interruptible(&camif_wait_queue);
	return IRQ_HANDLED;
}

static struct v4l2_file_operations cmos_ov7740_fops = {
	.owner		    = THIS_MODULE,
	.open		    = cmos_ov7740_open,
	.release	    	    = cmos_ov7740_release,
	.read		    = cmos_ov7740_read,
	.poll		   	    = cmos_ov7740_poll,
	.unlocked_ioctl   = video_ioctl2,
};

static struct v4l2_ioctl_ops cmos_ov7740_ioctl = {
	.vidioc_querycap 		    = cmos_ov7740_querycap,
	.vidioc_try_fmt_vid_cap      = cmos_ov7740_try_fmt,
	.vidioc_enum_fmt_vid_cap = cmos_ov7740_enum_fmt,
	.vidioc_g_fmt_vid_cap        = cmos_ov7740_get_fmt,
	.vidioc_s_fmt_vid_cap         = cmos_ov7740_set_fmt,
	.vidioc_reqbufs                   = cmos_ov7740_request_buffer,
	.vidioc_streamon                = cmos_ov7740_stream_on,
	.vidioc_streamoff                = cmos_ov7740_stream_off,
};
	
static int cmos_ov7740_init(void)
{
		int ret;
		
		/* 初始化GPIO */
		camif_gpio_init();
		/* 初始化寄存器 */
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
			
		cmos_ov7740_video->fops         = &cmos_ov7740_fops;
		cmos_ov7740_video->ioctl_ops = &cmos_ov7740_ioctl;
		cmos_ov7740_video->release    = cmos_ov7740_video_release;

		/* 申请中断 */
		ret = request_irq(IRQ_S3C2440_CAM_C,s3c2440_irq_cam_c,0,"camif_codec",NULL);
		if(ret)
		{
			printk("request camera codec irq error.\n");
			return -EIO;
		}

		ret = request_irq(IRQ_S3C2440_CAM_P,s3c2440_irq_cam_p,0,"camif_preview",NULL);
		if(ret)
		{
			printk("request camera preview irq error.\n");
			return -EIO;
		}

		ret = video_register_device(cmos_ov7740_video, VFL_TYPE_GRABBER, -1);
		
		return ret;
}

static void cmos_ov7740_exit(void)
{
	camif_gpio_exit();
	camif_reg_exit();
	free_irq(IRQ_S3C2440_CAM_C,NULL);
	free_irq(IRQ_S3C2440_CAM_P,NULL);
	video_unregister_device(cmos_ov7740_video);
	video_device_release(cmos_ov7740_video);
}

module_init(cmos_ov7740_init);
module_exit(cmos_ov7740_exit);
MODULE_LICENSE("GPL");

