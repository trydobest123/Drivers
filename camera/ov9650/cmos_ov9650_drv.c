#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/wait.h>
#include <linux/i2c.h>
#include <linux/poll.h>
#include <linux/gfp.h>
#include <linux/sched.h>
#include <linux/irqreturn.h>
#include <linux/interrupt.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-ioctl.h>
#include <mach/regs-gpio.h>
#include <mach/hardware.h>
#include <mach/leds-gpio.h>
#include <mach/irqs.h>
#include <asm/io.h>
#include <plat/gpio-fns.h>

#define SIO_C		S3C2410_GPE(14)
#define SIO_D	S3C2410_GPE(15)

#define State(x)	s3c2410_gpio_getpin(x)
#define High(x)	do{s3c2410_gpio_setpin(x,1); smp_mb();}while(0)
#define Low(x)	do{s3c2410_gpio_setpin(x,0); smp_mb();}while(0)

#define WAIT_STABLE()	do{udelay(10);}while(0)
#define WAIT_CYCLE()		do{udelay(90);}while(0)

#define CFG_READ(x)		do{s3c2410_gpio_cfgpin(x,S3C2410_GPIO_INPUT);smp_mb();}while(0)
#define CFG_WRITE(x)		do{s3c2410_gpio_cfgpin(x,S3C2410_GPIO_OUTPUT);smp_mb();}while(0)

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

struct cmos_ov9650_reg{
	unsigned char addr;
	unsigned char value;
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

static volatile unsigned int *gpgcon = NULL;
static volatile unsigned int *gpgdat = NULL;
static volatile unsigned int *gpgup = NULL;

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

static struct i2c_client *cmos_ov9650_client = NULL;
static struct video_device *cmos_ov9650_video    = NULL; 
static unsigned char cam_event = 0;
static unsigned char state = 0;
static unsigned int buf_size;
static unsigned int bytes_per_line;
static unsigned int target_height,target_width;
static unsigned int src_width, src_height;
static struct camif_buf_addr imag_buf;

static DECLARE_WAIT_QUEUE_HEAD(camif_wait_queue);

static struct cmos_ov9650_reg ov9650_reg_set[] = {
	/* OV9650 intialization parameter table for SXGA application */        
    {0x12, 0x80}, {0x39, 0x43}, {0x38, 0x12}, {0x37, 0x00}, {0x0e, 0x20},
    {0x1e, 0x1c}, {0x01, 0x80}, {0x02, 0x80}, {0x00, 0x00}, {0x10, 0xf0},
    {0x04, 0x00}, {0x0c, 0x00}, {0x0d, 0x00}, {0x11, 0x80}, {0x12, 0x00},
    {0x14, 0x2e}, {0x15, 0x00}, {0x18, 0xbd}, {0x17, 0x1d}, {0x32, 0xbf}, 
    {0x03, 0x12}, {0x1a, 0x81}, {0x19, 0x01}, {0x3f, 0xa6}, {0x41, 0x02},
    {0x42, 0x08}, {0x1b, 0x00}, {0x16, 0x06}, {0x33, 0xc0}, {0x34, 0xbf},
    {0xa8, 0x80}, {0x96, 0x04}, {0x3a, 0x00}, {0x8e, 0x00}, {0x3c, 0x77}, 
    {0x8b, 0x06}, {0x35, 0x91}, {0x94, 0x88}, {0x95, 0x88}, {0x40, 0xc1}, 
    {0x29, 0x3f}, {0x0f, 0x42}, {0x13, 0xe5}, {0x3d, 0x92}, {0x69, 0x80}, 
    {0x5c, 0x96}, {0x5d, 0x96}, {0x5e, 0x10}, {0x59, 0xeb}, {0x5a, 0x9c},
    {0x5b, 0x55}, {0x43, 0xf0}, {0x44, 0x10}, {0x45, 0x55}, {0x46, 0x86},
    {0x47, 0x64}, {0x48, 0x86}, {0x5f, 0xe0}, {0x60, 0x8c}, {0x61, 0x20},
    {0xa5, 0xd9}, {0xa4, 0x74}, {0x8d, 0x02}, {0x13, 0xe7}, {0x4f, 0x3a}, 
    {0x50, 0x3d}, {0x51, 0x03}, {0x52, 0x12}, {0x53, 0x26}, {0x54, 0x38},
    {0x55, 0x40}, {0x56, 0x40}, {0x57, 0x40}, {0x58, 0x0d}, {0x8c, 0x23},
    {0x3e, 0x02}, {0xa9, 0xb8}, {0xaa, 0x92}, {0xab, 0x0a}, {0x8f, 0xdf},
    {0x90, 0x00}, {0x91, 0x00}, {0x9f, 0x00}, {0x3a, 0x0c}, {0x24, 0x70},
    {0x25, 0x64}, {0x26, 0xc3}, {0x2a, 0x12}, {0x2b, 0x46}, {0x3b, 0x19},
    {0x6c, 0x40}, {0x6d, 0x30}, {0x6e, 0x4b}, {0x6f, 0x60},  
    {0x70, 0x70}, {0x71, 0x70}, {0x72, 0x70}, {0x73, 0x70},
    {0x74, 0x60}, {0x75, 0x60}, {0x76, 0x50}, {0x77, 0x48},
    {0x78, 0x3a}, {0x79, 0x2e}, {0x7a, 0x28}, {0x7b, 0x22},
    {0x7c, 0x04}, {0x7d, 0x07}, {0x7e, 0x10}, {0x7f, 0x28},
    {0x80, 0x36}, {0x81, 0x44}, {0x82, 0x52}, {0x83, 0x60},
    {0x84, 0x6c}, {0x85, 0x78}, {0x86, 0x8c}, {0x87, 0x9e},
    {0x88, 0xbb}, {0x89, 0xd2}, {0x8a, 0xe6},
    {0x6a, 0x41}, {0x66, 0x00},
    {0x3e, 0x00}, {0x3f, 0xa4}
};

static struct camera_format cmos_ov9650_format[] = {
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

static void __inline__ sccb_start(void)
{
	CFG_WRITE(SIO_D);

	Low(SIO_D);
	WAIT_STABLE();
}

static void __inline__ sccb_write_byte(u8 data)
{
	int i;

	CFG_WRITE(SIO_D);
	WAIT_STABLE();

	/* write 8-bits octet. */
	for (i=0;i<8;i++)
	{
		Low(SIO_C);
		WAIT_STABLE();

		if (data & 0x80)
		{
			High(SIO_D);
		}
		else
		{
			Low(SIO_D);
		}
		data = data<<1;
		WAIT_CYCLE();
		
		High(SIO_C);
		WAIT_CYCLE();
	}
	
	/* write byte done, wait the Don't care bit now. */
	{
		Low(SIO_C);
		High(SIO_D);
		CFG_READ(SIO_D);
		WAIT_CYCLE();
		
		High(SIO_C);
		WAIT_CYCLE();
	}
}

static u8 __inline__ sccb_read_byte(void)
{
	int i;
	u8 data;

	CFG_READ(SIO_D);
	WAIT_STABLE();
	
	Low(SIO_C);
	WAIT_CYCLE();

	data = 0;
	for (i=0;i<8;i++)
	{
		High(SIO_C);
		WAIT_STABLE();
		
		data = data<<1;
		data |= State(SIO_D)?1:0;
		WAIT_CYCLE();
		
		Low(SIO_C);
		WAIT_CYCLE();
	}
	
	/* read byte down, write the NA bit now.*/
	{
		CFG_WRITE(SIO_D);
		High(SIO_D);
		WAIT_CYCLE();
		
		High(SIO_C);
		WAIT_CYCLE();
	}
	
	return data;
}

static void __inline__ sccb_stop(void)
{
	Low(SIO_C);
	WAIT_STABLE();
	
	CFG_WRITE(SIO_D);
	Low(SIO_D);
	WAIT_CYCLE();
	
	High(SIO_C);
	WAIT_STABLE();
	
	High(SIO_D);
	WAIT_CYCLE();
	
	CFG_READ(SIO_D);
}

void sccb_write(u8 IdAddr, u8 SubAddr, u8 data)
{
	sccb_start();
	sccb_write_byte(IdAddr);
	sccb_write_byte(SubAddr);
	sccb_write_byte(data);
	sccb_stop();
}

u8 sccb_read(u8 IdAddr, u8 SubAddr)
{
	u8 data;

	sccb_start();
	sccb_write_byte(IdAddr);
	sccb_write_byte(SubAddr);                
	sccb_stop();

	sccb_start();
	sccb_write_byte(IdAddr|0x01);
	data = sccb_read_byte();
	sccb_stop();
	
	return data;
}

#if 0
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
	udelay(90);
	sccb_set_clk(1);
	udelay(90);
	sccb_set_dat(0);		
}

static void sccb_stop(void)
{		
	set_gpio_out();
	sccb_set_dat(0);
	udelay(90);
	sccb_set_clk(1);
	udelay(90);
	sccb_set_dat(1);	
	udelay(90);
}

static void sccb_wait_ack(void)
{
	set_gpio_in();
	sccb_set_clk(1);
	udelay(90);
	while(sccb_get_dat());
	udelay(90);
	sccb_set_clk(0);
	udelay(90);
}

static void sccb_write_byte(unsigned char byte)
{
	 int i;
	 
	 set_gpio_out();
	 udelay(10);
	 for(i = 0; i < 8; i++)
	{
		sccb_set_clk(0);	
		udelay(10);
	 	sccb_set_dat(byte&0x80);
	 	udelay(90);
		sccb_set_clk(1);
		udelay(90);
		byte <<= 1;
	 }


	sccb_set_clk(0);
	sccb_set_dat(0);
	set_gpio_in();
	udelay(50);
	
	//sccb_set_clk(1);
	//udelay(50);
}

static unsigned char  sccb_read_byte(void)
{
	int i;
	unsigned char dat = 0; 
	
	set_gpio_in();
	udelay(10);
	sccb_set_clk(0);
	udelay(90);
	for(i = 0; i < 8; i++)
	{
		dat<<=1;
		sccb_set_clk(1);
		udelay(10);
		dat |=sccb_get_dat();
		udelay(90);
		sccb_set_clk(0);
		udelay(90);
	}
	
	return dat;
}

/* cmos ov9650 write reg*/
static void cmos_ov9650_write_byte(unsigned char addr,unsigned char dat)
{
	sccb_start();
	sccb_write_byte(0x60);
	sccb_wait_ack();
	sccb_write_byte(addr);
	sccb_wait_ack();
	sccb_write_byte(dat);
	sccb_stop();
}

static unsigned char cmos_ov9650_read_byte(unsigned char addr)
{
	unsigned char dat = 0;
	sccb_start();
	sccb_write_byte(0x60);
	sccb_wait_ack();
	sccb_write_byte(addr);
	sccb_wait_ack();
	sccb_stop();
	
	sccb_start();
	sccb_write_byte(0x61);
	sccb_wait_ack();
	dat = sccb_read_byte();
	sccb_stop();
	
	return dat;
}
#endif 

static void camera_ov9650_init(void)
{
	int pid,mid;
	int i;
	
	pid   = sccb_read(0x60,0xa) << 8;
	pid |= sccb_read(0x60,0xb);
	printk("PID = 0x%04x\n",pid);

	mid   = sccb_read(0x60,0x1c) << 8;
	mid |= sccb_read(0x60,0x1d);
	printk("MID = 0x%04x\n",mid);

	printk("size = %d\n",ARRAY_SIZE(ov9650_reg_set));

	for(i = 0; i < ARRAY_SIZE(ov9650_reg_set);i++)
	{		
		sccb_write(0x60,ov9650_reg_set[i].addr,ov9650_reg_set[i].value);
		printk("%d\n",i);
	}
}

/* 复位时序:0=>1=>0 */
static void cmos_ov9650_reset(void)
{
	*cigctrl &=~ (1<<30);
	mdelay(10);
	*cigctrl |=  (1<<30);
	mdelay(10);
	*cigctrl &= ~(1<<30);
	mdelay(10);
}

static void cmos_ov9650_clk_init(void)
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
 	  gpjcon = ioremap(0x560000d0, 12);
 	  gpjdat = gpjcon+1;
 	  gpjup = gpjdat+1;

	gpgcon = ioremap(0x56000060,12);
	gpgdat  = gpgcon + 1;
	gpgup = gpgdat + 1;

	gpecon = ioremap(0x56000040,4);
	gpedat = gpecon + 1;
	gpepull = gpedat + 1;
	   
 	*gpjcon = 0x2aaaaaa;
 	*gpjdat  =0x0;
 	*gpjup   = 0x0; //使能上拉

	*gpecon  &= ~ (0x3 << 28);
 	*gpecon  |= 0x1 << 28; //输出
 	*gpepull = 0x3fff; //禁止上拉
 	
 	*gpgcon |= (1<<22);

 	

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
	iounmap(gpecon);
	iounmap(gpedat);
	iounmap(gpepull);
	iounmap(gpgcon);
	iounmap(gpgdat);
	iounmap(gpgup);
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

static int cmos_ov9650_open(struct file *filp)
{
	return 0;
}

static int cmos_ov9650_release(struct file *filp)
{	
	int order;
	
	state = 1; //已经关闭
	order = get_order(buf_size);
	free_pages(imag_buf.vir_addr,order);		
	
	return 0;
}

static ssize_t cmos_ov9650_read(struct file *filp, char __user *buf, size_t count, loff_t *ofs)
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
static unsigned int cmos_ov9650_poll(struct file *filp, struct poll_table_struct *pt)
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

static void cmos_ov9650_video_release(struct video_device *vdev)
{	
	int order;
	
	if(!state)
	{
		order = get_order(buf_size);
		free_pages(imag_buf.vir_addr,order);		
	}	

	printk("cmos_ov9650 release\n");
}

/* 查询性能 */
static int cmos_ov9650_querycap(struct file *file, void *fh, struct v4l2_capability *cap)
{
	memset(cap, 0, sizeof *cap);
	strcpy(cap->driver, "cmos_ov9650");
	strcpy(cap->card,"cmos_ov9650");
	cap->version = 2;

	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_READWRITE;

	return 0;
}

/* 列举格式 */
static int cmos_ov9650_enum_fmt(struct file *file, void *fh,struct v4l2_fmtdesc *f)
{
	struct camera_format *fmt;

	if(f->index >ARRAY_SIZE(cmos_ov9650_format))
		return -EINVAL;
		
	memset(f, 0, sizeof(*f));
	fmt = &cmos_ov9650_format[f->index];
	strlcpy(f->description, fmt->name,sizeof(f->description));
	f->pixelformat = fmt->fourcc;
	
	return 0;
}

/* 获得格式 */
static int cmos_ov9650_get_fmt(struct file *file, void *fh,struct v4l2_format *f)
{	
	
	return 0;
}

/* 检查某种格式是否支持 */
static int cmos_ov9650_try_fmt (struct file *file, void *fh, struct v4l2_format *f)
{
	int i;
	int support = 0;
	
	if(f->type !=V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	for(i = 0; i < ARRAY_SIZE(cmos_ov9650_format); i++)
	{
		if(f->fmt.pix.pixelformat == cmos_ov9650_format[i].fourcc)
		{
			support = 1;
			break;
		}
	}
		
	return support ? 0 : -EINVAL;
}

/* 设置格式 */
static int cmos_ov9650_set_fmt(struct file *file, void *fh,struct v4l2_format *f)
{
	int ret = 0 ;

	/*先查询是否支持这种格式*/
	ret = cmos_ov9650_try_fmt(file,fh,f);
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

	f->fmt.pix.sizeimage =  f->fmt.pix.bytesperline * f->fmt.pix.height;
	buf_size = f->fmt.pix.sizeimage;
	bytes_per_line = f->fmt.pix.bytesperline;

	*ciprtrgfmt |= (target_width<<16) |(0<<14)|(target_height<<0);
	
	return 0;
}

/* 申请缓存 */
static int cmos_ov9650_request_buffer(struct file *file, void *fh, struct v4l2_requestbuffers *b)
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

/* 打开摄像头 */
static int cmos_ov9650_stream_on(struct file *file, void *fh, enum v4l2_buf_type i)
{
	unsigned int main_burst,remain_burst;
	unsigned int h_shift,v_shift,pre_hor_ratio,pre_ver_ratio;
	unsigned int pre_dst_width,main_hor_ratio;
	unsigned int pre_dst_height,main_ver_ratio,sh_factor;
	unsigned int scale_up_down;
	int ret;
	
	src_width   = 1280;
	src_height  = 1024;

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

/* 关闭摄像头 */
static int cmos_ov9650_stream_off(struct file *file, void *fh, enum v4l2_buf_type i)
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

static struct v4l2_file_operations cmos_ov9650_fops = {
	.owner		    = THIS_MODULE,
	.open		    = cmos_ov9650_open,
	.release	    	    = cmos_ov9650_release,
	.read		    = cmos_ov9650_read,
	.poll		   	    = cmos_ov9650_poll,
	.unlocked_ioctl   = video_ioctl2,
};

static struct v4l2_ioctl_ops cmos_ov9650_ioctl = {
	.vidioc_querycap 		    = cmos_ov9650_querycap,
	.vidioc_try_fmt_vid_cap      = cmos_ov9650_try_fmt,
	.vidioc_enum_fmt_vid_cap = cmos_ov9650_enum_fmt,
	.vidioc_g_fmt_vid_cap        = cmos_ov9650_get_fmt,
	.vidioc_s_fmt_vid_cap         = cmos_ov9650_set_fmt,
	.vidioc_reqbufs                   = cmos_ov9650_request_buffer,
	.vidioc_streamon                = cmos_ov9650_stream_on,
	.vidioc_streamoff                = cmos_ov9650_stream_off,
};

static void cmos_ov9650_power_up(void)
{
	*gpgdat &= ~(1<<11);
	mdelay(100);
}

static void cmos_ov9650_power_down(void)
{
	*gpgdat  |= (1<<11);
	mdelay(10);
}

static int cmos_ov9650_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
		int ret;

		cmos_ov9650_client = client;
		
		/* 初始化GPIO */
		camif_gpio_init();
		/* 映射寄存器 */
		camif_reg_init(); 
		/* 获得时钟 */
		cmos_ov9650_clk_init();
		/* 控制器复位 */
		camera_interface_reset();
		/* 摄像头复位 */
		cmos_ov9650_reset();
		/* 摄像头上电 */
		cmos_ov9650_power_up();
		//CFG_WRITE(SIO_C);
		//CFG_WRITE(SIO_D);

		//High(SIO_C);
		//High(SIO_D);
		//WAIT_STABLE();
		/* 摄像头初始化 */
		camera_ov9650_init();
		
		cmos_ov9650_video  = video_device_alloc();
		if(!cmos_ov9650_video)
			return -ENOMEM;
			
		cmos_ov9650_video->fops         = &cmos_ov9650_fops;
		cmos_ov9650_video->ioctl_ops = &cmos_ov9650_ioctl;
		cmos_ov9650_video->release    = cmos_ov9650_video_release;

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

		ret = video_register_device(cmos_ov9650_video, VFL_TYPE_GRABBER, -1);
		
		return ret;
}

static int  cmos_ov9650_i2c_remove(struct i2c_client *client)
{
	camif_gpio_exit();
	camif_reg_exit();
	cmos_ov9650_power_down();
	free_irq(IRQ_S3C2440_CAM_C,NULL);
	free_irq(IRQ_S3C2440_CAM_P,NULL);
	video_unregister_device(cmos_ov9650_video);
	video_device_release(cmos_ov9650_video);

	return 0;
}

static struct i2c_device_id cmos_ov9650_id[] = {
	{.name = "cmos_ov9650"},
	{.name = "cmos_ov7740"},
	{},
};

static struct i2c_driver cmos_ov9650_driver = {
	.driver = {
		.name = "cmos_ov9650",
	},
	
	.probe        = cmos_ov9650_i2c_probe,
	.remove     = cmos_ov9650_i2c_remove,
	.id_table 	  = cmos_ov9650_id,
};

static int cmos_ov9650_drv_init(void)
{	
	return  i2c_add_driver(&cmos_ov9650_driver);
}

static void cmos_ov9650_drv_exit(void)
{
	i2c_del_driver(&cmos_ov9650_driver);
}

module_init(cmos_ov9650_drv_init);
module_exit(cmos_ov9650_drv_exit);
MODULE_LICENSE("GPL");


