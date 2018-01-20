#include <linux/fs.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/cdev.h>

#include <asm/delay.h>
#include <asm/io.h>

#define    DS18B20_ReadROM        0x33    //读ROM
#define    DS18B20_MatchROM       0x55    //匹配ROM
#define    DS18B20_SkipROM        0xCC    //跳过ROM
#define    DS18B20_SearchROM      0xF0    //搜索ROM
#define    DS18B20_AlarmROM       0xEC    //报警搜索

//定义DS18B20存储器操作命令
#define    DS18B20_WriteSCR       0x4E    //写暂存存储器
#define    DS18B20_ReadSCR        0xBE    //读暂存存储器
#define    DS18B20_CopySCR        0x48    //复制暂存存储器
#define    DS18B20_ConvertTemp    0x44    //温度变换
#define    DS18B20_RecallEP       0xB8    //重新调出
#define    DS18B20_ReadPower      0xB4    //读电源

static volatile unsigned int *gpbcon = NULL;
static volatile unsigned int *gpbdat  = NULL;
static volatile unsigned int *gpbup    = NULL;
static unsigned int dev_id = 0;

static struct cdev *ds_cdev = NULL;
static struct class *ds_class = NULL;
static struct device *ds_class_dev  = NULL;

/* gpio的初始化 */
static void gpb_init(void)
{
	/* gipo 映射 */
	gpbcon = ioremap(0x56000010,12);
	gpbdat  = gpbcon+1;
	gpbup   = gpbdat +1;
	
	/* 设置gpb5上拉 */
	*gpbup &= ~(1<<5);
}

/* 输入 */
static void gpb5_in(void)
{
	*gpbcon &= ~(0x3 << 10);
}

/* 输出 */
static void gpb5_out(void)
{
	*gpbcon &= ~(0x3 << 10);
	*gpbcon |= (0x1 << 10);
}

static void gpb5_data_out(char dat)
{
	if(dat)
	{
		*gpbdat |= 1<<5; 
	}
	else
	{
		*gpbdat &= ~(1<<5); 
	}
}

static char gpb5_data_in(void)
{
	return ((*gpbdat & (1<<5)) == (1 << 5));
}

static int ds18b20_reset(void)
{
	gpb5_out();
	gpb5_data_out(0);
	udelay(500);
	gpb5_data_out(1);
	
	udelay(40);
	gpb5_in();

	if(gpb5_data_in())
	{
		return -1;
	}
	else 
	{
		udelay(70);
		if(gpb5_data_in())
		{	
			return -1;
		}
		else
			return 0;
	}
}

static int ds18b20_check(void)
{
	if(ds18b20_reset())
	{
		printk("none device dected in gpb5!\n ");
		return -1;
	}
	else
	{
		printk("one device has dected in gpb5.\n ");
		return 0;
	}
}

static void one_wire_write(unsigned char bit)
{
	/* 写1 */	
	if(bit)
	{
		gpb5_out();
	  	gpb5_data_out(0);
	  	udelay(20);
	  	gpb5_data_out(1);
		udelay(20);
	}
	/* 写0 */
	else
	{
	  	gpb5_out();
	  	gpb5_data_out(0);
	  	udelay(40);
	  	gpb5_data_out(1);
	}
}

static bool one_wire_read(void)
{
	/* 读1 */
	gpb5_out();
	gpb5_data_out(0);
	udelay(10);
	gpb5_data_out(1);
	gpb5_in();
	udelay(3);
	/* 读1 */
	if(gpb5_data_in())
		return true;
	else
		return false;
}

static void ds18b20_write_byte(unsigned char dat)
{
	unsigned char tmp = dat;
	unsigned int i;

	for(i = 0; i < 8; i++)
	{
		if(tmp & 0x01)
		{
			one_wire_write(1);
		}
		else
		{
			one_wire_write(0);
		}

		tmp >>=1;
		udelay(20);
	}
}


static unsigned char ds18b20_read_byte(void)
{
	unsigned char tmp = 0;
	unsigned i = 0;

	for(i = 0; i < 8; i++)
	{
		tmp >>= 1;
		if(one_wire_read() == true)
		{
			tmp |= 0x80;
		}

		udelay(20);
	}
	
	return tmp;
}

static void ds18b20_read_rom(unsigned char buf[])
{	
	unsigned int i = 0;
	
	ds18b20_reset();
	udelay(500);
	ds18b20_write_byte(DS18B20_ReadROM);
	udelay(10);
	
	for(i = 0; i < 8;i++)
	{
		buf[i] = ds18b20_read_byte();
	}
}

static void ds18b20_put_info(void)
{	
	unsigned char code_buf[8] = {0};
	unsigned int i = 0;
	
	ds18b20_read_rom(code_buf);
	
	printk("serial code:");
	for(i = 0; i < 8; i++)
	{
		printk("%02x ",code_buf[7 -i]);
	}
	printk("\n");
}

static void ds18b20_convert_temp(unsigned char  *temp)
{
	ds18b20_reset();
	udelay(500);
	ds18b20_write_byte(DS18B20_SkipROM);
	udelay(10);
	ds18b20_write_byte(DS18B20_ConvertTemp);
	udelay(50);

	ds18b20_reset();
	udelay(500);
	ds18b20_write_byte(DS18B20_SkipROM);
	udelay(10);
	ds18b20_write_byte(DS18B20_ReadSCR);
	udelay(10);
	
	temp[0]  = ds18b20_read_byte();//温度的低八位
	temp[1]  = ds18b20_read_byte();//温度的高八位
}

static int ds18b20_open (struct inode *inode, struct file *filp)
{
	return 0;
}

static ssize_t ds18b20_read (struct file *filp, char __user *buf, size_t size, loff_t *offset)
{	
	unsigned char tmp_buf[2] = {0};
	
	ds18b20_convert_temp(tmp_buf);
	copy_to_user(buf,tmp_buf,size);

	return size;
}

static int ds18b20_close(struct inode *inode, struct file *filp)
{
	ds18b20_reset();
	return 0;
}

static struct file_operations ds18b20_fops = {
	.owner = THIS_MODULE,
	.open   = ds18b20_open,
	.read    = ds18b20_read,
	.release   = ds18b20_close,
};

/*GPB5 I/O*/
static int ds18b20_init(void)
{
	int ret = 0;
	
	gpb_init();
	if(ds18b20_check())
		return -ENODEV;

	ds18b20_put_info();
	
	ret = alloc_chrdev_region(&dev_id,0,1,"ds18b20");
	if(ret)
	{
		printk("alloc dev_id fail.\n");
		return -EIO;
	}
	
	ds_cdev = cdev_alloc();
	if(!ds_cdev)
	{	
		printk("alloc cdev fail.\n");
		return -ENODEV;
	}
	
	cdev_init(ds_cdev,&ds18b20_fops);
	cdev_add(ds_cdev,dev_id,1);
	
	ds_class = class_create(THIS_MODULE,"Dallas");
	ds_class_dev = device_create(ds_class,NULL,dev_id,NULL,"ds18b20");
	
	return 0;
}

static void ds18b20_exit(void)
{
	iounmap(gpbcon);
	cdev_del(ds_cdev);
	unregister_chrdev_region(dev_id,1);
	device_unregister(ds_class_dev);
	class_destroy(ds_class);
}

module_init(ds18b20_init);
module_exit(ds18b20_exit);
MODULE_LICENSE("GPL");

