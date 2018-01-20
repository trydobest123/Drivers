#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <asm/uaccess.h>
#include <plat/gpio-cfg.h>
#include <mach/regs-gpio.h>
#include <mach/gpio-samsung.h>

static struct cdev *oled_cdev = NULL;
static struct class *oled_class = NULL;
static struct device  *oled_dev = NULL;
static struct spi_device *spi_oled_device = NULL;

static dev_t dev_num = 0;
static int unsigned oled_cd_pin = 0;
static unsigned char *write_buf = NULL;

#define OLED_CMD_INIT 0x10000000
#define OLED_CMD_CLEAR_ALL 0x10000001
#define OLED_CMD_CLEAR_PAGE 0x10000010
#define OLED_CMD_SET_POS 0x10000011

static struct spi_device_id oled_id[2] = {
	{.name  = "mini2440_spi_oled"},
	{.name  = "jz2440_spi_oled"},
};

static void OLEDSetPos(int page,int col);
static void OLEDSetCD(unsigned char val);
static void OLEDWriteCmd(unsigned char cmd);
static void OLEDClearOnePage(unsigned int page);
static void OLEDClearScreen(unsigned char dat);
static void OLEDInit(void);

static void s3c2410_gpio_setpin(unsigned int pin, unsigned int to)
{
	/* do this via gpiolib until all users removed */

	gpio_request(pin, "temporary");
	gpio_set_value(pin, to);
	gpio_free(pin);
} 

static void OLEDSetCD(unsigned char val)
{
	s3c2410_gpio_setpin(oled_cd_pin,val);
}

static void OLEDWriteCmd(unsigned char cmd)
{
	/* 发送命令 */
	OLEDSetCD(0);
	spi_write(spi_oled_device,&cmd,1);
	OLEDSetCD(1);
	//printk("cmd = 0x%x\n",cmd);
}

static void OLEDWriteDat(unsigned char dat)
{
	/* 数据输入 */
	OLEDSetCD(1);
	spi_write(spi_oled_device,&dat,1);
	OLEDSetCD(1);
}

static void OLEDInit(void)
{
	OLEDWriteCmd(0xAE); /*display off*/
	OLEDWriteCmd(0x00); /*set lower column address*/
	OLEDWriteCmd(0x10); /*set higher column address*/
	OLEDWriteCmd(0x40); /*set display start line*/
	OLEDWriteCmd(0xB0); /*set page address*/
	OLEDWriteCmd(0x81); /*contract control*/
	OLEDWriteCmd(0x66); /*128*/
	OLEDWriteCmd(0xA1); /*set segment remap*/
	OLEDWriteCmd(0xA6); /*normal / reverse*/
	OLEDWriteCmd(0xA8); /*multiplex ratio*/
	OLEDWriteCmd(0x3F); /*duty = 1/64*/
	OLEDWriteCmd(0xC8); /*Com scan direction*/
	OLEDWriteCmd(0xD3); /*set display offset*/
	OLEDWriteCmd(0x00);
	OLEDWriteCmd(0xD5); /*set osc division*/
	OLEDWriteCmd(0x80);
	OLEDWriteCmd(0xD9); /*set pre-charge period*/
	OLEDWriteCmd(0x1f);
	OLEDWriteCmd(0xDA); /*set COM pins*/
	OLEDWriteCmd(0x12);
	OLEDWriteCmd(0xdb); /*set vcomh*/
	OLEDWriteCmd(0x30);
	OLEDWriteCmd(0x8d); /*set charge pump enable*/
	OLEDWriteCmd(0x14);
	
	/* set display mode is page mode  */
	OLEDWriteCmd(0x20);
	OLEDWriteCmd(0x02);
	OLEDClearScreen(0x0);
	OLEDWriteCmd(0xAF); /* display ON */
}

static  void OLEDClearOnePage(unsigned int page)
{	
	int i = 0;
	OLEDSetPos(page,0);
	
	for(i = 0; i < 128; i++)
	{
		OLEDWriteDat(0x0);	
	}
}

static void OLEDClearScreen(unsigned char dat)
{
	int i = 0,j = 0;
	for(j = 0; j < 8; j++)
	{
		OLEDSetPos(j,0);
		for(i = 0; i < 128; i++)
		{
			OLEDWriteDat(dat);
		}
	}
}

static void OLEDSetPos(int page,int col)
{
	char uper   = (col >> 4) | 0x10;
	char lower =  col & 0xf;
	
	OLEDWriteCmd(0xb0 | page);
	OLEDWriteCmd(lower);
	OLEDWriteCmd(uper);
}

static int oled_open(struct inode *inode, struct file *filp)
{
	/* oled init */
	OLEDInit();
	return 0;
}

static ssize_t oled_write (struct file *filp, const char __user *buf, size_t count, loff_t *offset)
{
	if(count > 4096)
	{
		printk("write bytes too len.\n");
		return -EINVAL;
	}
	
	copy_from_user(write_buf,buf,count);
	
	/* 发送数据 */
	OLEDSetCD(1);
	spi_write(spi_oled_device,write_buf,count);
	
	memset(write_buf,0,count);
	
	return 0;
}

static long oled_ioctl (struct file *filp, unsigned int cmd, unsigned long arg)
{
	/* arg :高八位为页地址，低八位为列地址*/
	unsigned char page = arg >> 8;
	unsigned char col  = arg  & 0xff;
	
	switch(cmd)
	{
		case OLED_CMD_INIT:
		{
			OLEDInit();
			break;
		}
		case OLED_CMD_CLEAR_ALL:
		{
			OLEDClearScreen(0x0);
			break;
		}
		case OLED_CMD_CLEAR_PAGE:
		{
			OLEDClearOnePage(page);
			break;
		}
		case OLED_CMD_SET_POS:
		{
			OLEDSetPos(page,col);
			break;
		}
	}
	return 0;
}

static int oled_close(struct inode *inode, struct file *filp)
{
	return 0;
}


const struct file_operations oled_fops = {
	.owner =	THIS_MODULE,
	.open   = oled_open,
	.write   = oled_write,
	.unlocked_ioctl = oled_ioctl, 
	.release = oled_close,
};

static int jz2440_oled_dect(struct spi_device *spi)
{	
	spi_oled_device = spi;
	
	/* 引脚的初始化 */
	oled_cd_pin = (unsigned int)spi->dev.platform_data;
	
	s3c_gpio_cfgpin(oled_cd_pin,S3C2410_GPIO_OUTPUT);
	s3c_gpio_cfgpin(spi->chip_select,S3C2410_GPIO_OUTPUT);

	write_buf = kzalloc(4096,GFP_KERNEL) ;
	if(!write_buf)
	{
		printk("alloc memory fail.\n");
		return -ENOMEM;
	}
	
	alloc_chrdev_region(&dev_num,0,1,"oled");
	
	oled_cdev = cdev_alloc();
	if(!oled_cdev)
		return -ENODEV;

	oled_cdev->owner = THIS_MODULE;
	cdev_init(oled_cdev,&oled_fops);
	cdev_add(oled_cdev,dev_num,1);	

	oled_class = class_create(THIS_MODULE,"spi_oled");
	oled_dev   = device_create(oled_class,NULL,dev_num,NULL,"oled_dev");

	OLEDInit();
	OLEDClearScreen(0xff);
	
	return 0;
}

static int jz2440_oled_remove(struct spi_device *spi)
{
	printk("spi oled device remove.\n");
	device_destroy(oled_class, dev_num);
	class_destroy(oled_class);
	cdev_del(oled_cdev);
	unregister_chrdev_region(dev_num, 1);
	kfree(write_buf);
	return 0;
}

static struct spi_driver oled_driver = {
	.driver  = {
		.name = "jz2440_oled",
		.owner = THIS_MODULE,
	},
	
	.id_table  = oled_id,
	.probe     = jz2440_oled_dect,
	.remove  = jz2440_oled_remove,
};

static int __init oled_drv_init(void)
{
	int ret = 0;
	
	ret = spi_register_driver(&oled_driver);
	if(ret)
	{
		printk("register spi driver fail.\n");
		return -EIO;
	}
	
	return 0;
}

static void __exit oled_drv_exit(void)
{
	spi_unregister_driver(&oled_driver);
}

module_init(oled_drv_init);
module_exit(oled_drv_exit);
MODULE_LICENSE("GPL");

