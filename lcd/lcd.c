#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>

struct s3clcdreg {
	unsigned long lcdcon1  ;
	unsigned long lcdcon2  ;
	unsigned long lcdcon3  ;
	unsigned long lcdcon4  ;
	unsigned long lcdcon5  ;
	unsigned long lcdsaddr1;
	unsigned long lcdsaddr2;
	unsigned long lcdsaddr3;
	unsigned long redlut;	 
	unsigned long greenlut ;
	unsigned long bluelut ;
	unsigned long	reserved[9]; 
	unsigned long dithmode;	
	unsigned long tpal; 			
	unsigned long lcdintpnd;
	unsigned long lcdsrcpnd;
	unsigned long lcdintmsk;
	unsigned long tconsel  ;
};

static volatile struct s3clcdreg *lcd_reg = NULL;
static struct fb_info *s3c_fbinfo = NULL;
static unsigned int map_size;
static unsigned int pseudopalette[16];
static struct clk *lcd_clk ;

static volatile unsigned long *gpbcon;
static volatile unsigned long *gpbdat;
static volatile unsigned long *gpccon;
static volatile unsigned long *gpdcon;
static volatile unsigned long *gpgcon;

/* from pxafb.c */
static inline unsigned int chan_to_field(unsigned int chan,
					 struct fb_bitfield *bf)
{
	chan &= 0xffff;
	chan >>= 16 - bf->length;
	return chan << bf->offset;
}
 
/*调色板设置函数*/
static int s3cfb_setcolreg(unsigned regno, unsigned red, unsigned green,
			      unsigned blue, unsigned transp,
			      struct fb_info *info) 
{
	unsigned int val;

	if(regno > 16)
		return 1;

	val  = chan_to_field(red,   &info->var.red);
	val |= chan_to_field(green, &info->var.green);
	val |= chan_to_field(blue,  &info->var.blue);

	pseudopalette[regno] = val;

	return 0;
}

static struct fb_ops s3c_fbinfo_ops = {
	.owner = THIS_MODULE,
	.fb_setcolreg	= s3cfb_setcolreg,
	.fb_fillrect 	= cfb_fillrect,
	.fb_copyarea   = cfb_copyarea,
	.fb_imageblit   = cfb_imageblit,
};

static __init int S3CLcdInit(void)
{
	dma_addr_t dma_addr;
	
	/* 首先进行寄存器地址的映射 */
	lcd_reg = ioremap(0X4D000000,sizeof(struct s3clcdreg));
	gpbcon = ioremap(0x56000010, 8);
	gpbdat = gpbcon+1;
	gpccon = ioremap(0x56000020, 4);
	gpdcon = ioremap(0x56000030, 4);
	gpgcon = ioremap(0x56000060, 4);

      *gpccon  = 0xaaaaaaaa;   /* GPIO管脚用于VD[7:0],LCDVF[2:0],VM,VFRAME,VLINE,VCLK,LEND */
	*gpdcon  = 0xaaaaaaaa;   /* GPIO管脚用于VD[23:8] */
	
	*gpbcon &= ~(3);  /* GPB0设置为输出引脚 */
	*gpbcon |= 1;
	*gpbdat &= ~1;     /* 输出低电平 */

	*gpgcon |= (3<<8); /* GPG4用作LCD_PWREN */
	
	/* 分配一个fb_info结构体 */ 
	s3c_fbinfo = framebuffer_alloc(0,NULL);
	if(!s3c_fbinfo)
	{
		printk("alloc frambuffer fail.\n");
		return -1;
	}

	/* 进行固定参数的设置 */	
	strcpy(s3c_fbinfo->fix.id,"s3c_lcd");
	s3c_fbinfo->fix.smem_len  = 272*480*16/8;
	s3c_fbinfo->fix.type  		= FB_TYPE_PACKED_PIXELS;
	s3c_fbinfo->fix.visual		= FB_VISUAL_TRUECOLOR;
	s3c_fbinfo->fix.line_length	= 480 * 2;
	
	/* 进行可变参数的设置 */
	s3c_fbinfo->var.xres		  = 480; 
	s3c_fbinfo->var.yres 		  = 272; 
	s3c_fbinfo->var.xres_virtual = 480;
	s3c_fbinfo->var.yres_virtual = 272;
	s3c_fbinfo->var.bits_per_pixel = 16;
	
	s3c_fbinfo->var.red.offset 	   = 11;
	s3c_fbinfo->var.green.offset   = 5; 
	s3c_fbinfo->var.blue.offset     = 0;
	
	s3c_fbinfo->var.red.length 	    =5;
	s3c_fbinfo->var.green.length  = 6; 
	s3c_fbinfo->var.blue.length    = 5;
	
	s3c_fbinfo->var.activate	   = FB_ACTIVATE_NOW;	
	s3c_fbinfo->screen_size 	   = 272 * 480 * 16 / 8;
	s3c_fbinfo->pseudo_palette   = pseudopalette;
	
	/* 设置操作函数 */
	s3c_fbinfo->fbops = &s3c_fbinfo_ops;

	/* 打开时钟 */
	lcd_clk = clk_get(NULL, "lcd");
	clk_prepare_enable(lcd_clk);
		
	/*时钟频率为10MHZ,显示格式为16Bpp,禁止LCD信号*/
	lcd_reg->lcdcon1 = (0x4 << 8) | (0x3<<5) | (0xc << 1) | (0x0);
	lcd_reg->lcdcon2 = (0x1<< 24) | (271 << 14) | (1<<6) | (9 << 0);
	lcd_reg->lcdcon3 = (4<< 19) | (479 << 8) | (4 << 0);
	lcd_reg->lcdcon4 = 40;
	lcd_reg->lcdcon5 = (0x1<< 11) | (0x1 << 9) | (0x1<<8) | 0x1;
	
	s3c_fbinfo->screen_base = dma_alloc_wc(NULL,PAGE_ALIGN(s3c_fbinfo->screen_size),&dma_addr ,GFP_KERNEL);
	s3c_fbinfo->fix.smem_start = dma_addr;

	lcd_reg->lcdsaddr1 =	(s3c_fbinfo->fix.smem_start & ~(3 << 30))  >> 1;
	lcd_reg->lcdsaddr2 =  ((s3c_fbinfo->fix.smem_start + s3c_fbinfo->fix.smem_len) >> 1) & 0x1fffff;
	lcd_reg->lcdsaddr3 =  (480*16/16);	
	
	lcd_reg->lcdcon1 |= (1<<0);
	lcd_reg->lcdcon5 |= (1<<3);
	
	/* 打开背光 */
	*gpbdat |= 1;
	
	/*注册fb_info*/
	register_framebuffer(s3c_fbinfo);
	
	return 0;
}

static __exit void S3CLcdExit(void)
{	
	/* 关闭背光 */
	*gpbdat &= ~(1 << 0);
	iounmap(lcd_reg);
	iounmap(gpbcon);
	iounmap(gpccon);
	iounmap(gpdcon);
	iounmap(gpgcon);
	
	dma_free_wc(NULL,map_size , s3c_fbinfo->screen_base, s3c_fbinfo->fix.smem_start);
	unregister_framebuffer(s3c_fbinfo);
}

module_init(S3CLcdInit);
module_exit(S3CLcdExit);

MODULE_LICENSE("GPL");


