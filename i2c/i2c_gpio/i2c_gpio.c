#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <asm/io.h>

/*GPE14 = SCL*/
/*GPE15 = SDA*/
static volatile unsigned int *gpecon = NULL;
static volatile unsigned int *gpedat  = NULL;
static volatile unsigned int *gpepull  = NULL;
static unsigned char name_buf[] = "Ma Yi Ming I Love You";

static void __inline  i2c_set_dat(unsigned char dat)
{		
		 
		if(dat)
			*gpedat |= (1 << 15);
		else
			*gpedat &= ~(1 << 15);
}

static unsigned char __inline i2c_get_dat(void)
{			
		if(*gpedat & (1<<15))
			return 1;
		else
			return 0;
}

static void __inline set_gpio_out(void)
{
		 *gpecon  &= ~(0x3 << 30);
		 *gpecon  |= (1<<30);
}

static void __inline set_gpio_in(void)
{
	 *gpecon  &= ~(0x3 << 30);
}

static void __inline i2c_set_clk(unsigned char dat)
{		
		if(dat)
			*gpedat |= (1 << 14);
		else
			*gpedat &= ~(1 << 14);
}

static void __inline i2c_reset(void)
{
		i2c_set_clk(1);
		i2c_set_dat(1);
}

static void __inline i2c_start(void)
{		
		set_gpio_out();
		i2c_set_dat(1);
		udelay(50);
		i2c_set_clk(1);
		udelay(50);
		i2c_set_dat(0);
		udelay(50);
}

static void __inline i2c_stop(void)
{		
		set_gpio_out();
		i2c_set_dat(0);
		udelay(50);
		i2c_set_clk(1);
		udelay(50);
		i2c_set_dat(1);
		udelay(50);
}

static void __inline i2c_wait_ack(void)
{
		set_gpio_in();
		i2c_set_clk(1);
		udelay(50);
		while(i2c_get_dat());
		i2c_set_clk(0); //拉低时钟线
		udelay(50);
}

static void i2c_write_byte(char byte)
{
		int i;

		set_gpio_out();
		for(i = 0; i < 8; i++)
		{
			i2c_set_clk(0);
			udelay(50);
			i2c_set_dat(byte&0x80);
			udelay(50);
			i2c_set_clk(1);
			udelay(50);
			byte<<=1;
		}
		
		i2c_set_clk(0);
		udelay(50);
		i2c_set_dat(1); //释放总线、
		udelay(50);
}

static char i2c_read_byte(void)
{
		int i;
		char byte = 0;
		set_gpio_in();
		i2c_set_clk(0);		
		for(i = 0; i < 8; i++)
		{
			byte<<=1;
			i2c_set_clk(1);
			udelay(50);
			byte|=i2c_get_dat();
			udelay(50);
			i2c_set_clk(0);
			udelay(50);
		}
		
		return byte;
}

/* byte write */
static void at24cx_write_byte(unsigned int addr,char val)
{
		i2c_start();
		i2c_write_byte(0xa0);
		i2c_wait_ack();
		i2c_write_byte(addr);
		i2c_wait_ack();
		i2c_write_byte(val);
		i2c_wait_ack();
		i2c_stop();
}

/*random read*/
static char at24cx_read_byte(unsigned char addr)
{
		char dat;
		
		i2c_start();
		i2c_write_byte(0xa0);
		i2c_wait_ack();
		i2c_write_byte(addr);
		i2c_wait_ack();

		i2c_start();
		i2c_write_byte(0xa1);
		i2c_wait_ack();
		dat = i2c_read_byte();
		i2c_stop();

		return dat;
}

static int at24cx_init(void)
{
		unsigned int dat;
		int i;
		
		  gpecon = ioremap(0x56000040,4);
		  gpedat = gpecon + 1;
		  gpepull = gpedat + 1;
		
		*gpecon  &= ~ (0x3 << 28);
		*gpecon  |= 0x1 << 28; //输出
		
		*gpepull = 0x3fff; //禁止上拉

		i2c_reset();

		for(i=0; i<strlen(name_buf);i++)
		{
				at24cx_write_byte(i,name_buf[i]);	
				mdelay(10);
		}
		
		for(i=0; i<strlen(name_buf);i++)
		{
				dat = at24cx_read_byte(i);
				printk("%c",dat);
				mdelay(10);
		}
		
		printk("\n");
		
		return 0;
}

static void at24cx_exit(void)
{
		iounmap(gpecon);
		iounmap(gpedat);
		iounmap(gpepull);
}

module_init(at24cx_init);
module_exit(at24cx_exit);
MODULE_LICENSE("GPL");

