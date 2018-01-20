#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/completion.h>
#include <linux/spi/spi.h>
#include <linux/platform_device.h>
#include <mach/regs-gpio.h>
#include <mach/gpio-samsung.h>
#include <plat/gpio-cfg.h>

#define SPCON  0x0
#define SPSTA  0x1
#define SPPIN  0x2
#define SPPRE  0x3
#define SPTDAT 0x4
#define SPRDAT 0x5


struct s3c_spi_info{
	unsigned int *io_base;
	unsigned int irq_num;
	unsigned int bus_num;
	unsigned int count;
	struct clk *clk;
	struct completion done;
	struct spi_transfer *transfer;
	struct platform_device *pdev;
};

static struct spi_master *s3c2440_spi0 = NULL;
static struct spi_master *s3c2440_spi1 = NULL;

static int s3c2440_spi_setup(struct spi_device *spi)
{
	int cpol,cpha;
	unsigned int hz,div;
	struct clk *pclk;
	struct s3c_spi_info *info;
	
	info = spi_master_get_devdata(spi->master);
	hz = spi->max_speed_hz;
	
	/* 设置模式 */
	switch(spi->mode)
	{
		case SPI_MODE_0:
		{
			cpol = 0;
			cpha = 0;
			break;
		}
		case SPI_MODE_1:
		{
			cpol = 0;
			cpha = 1;		
			break;
		}
		case SPI_MODE_2:
		{
			cpol = 1;
			cpha = 0;		
			break;
		}
		case SPI_MODE_3:
		{
			cpol = 1;
			cpha = 1;		
			break;
		}
		default:
		{
			cpol = 0;
			cpha = 0;		
			break;
		}
	}

	/* 寄存器的设置 */
	writeb(((1 << 5)|( 1 << 4)|(1 << 3)|(cpol << 2)|(cpha << 1)),info->io_base + SPCON);
	
	/* 设置时钟 */
	pclk = clk_get(NULL,"pclk");
	div = DIV_ROUND_UP(clk_get_rate(pclk), hz * 2) - 1;

	if (div > 255)
		div = 255;

	writeb(div,info->io_base+SPPRE);
	
	return 0;
}

static int s3c2440_spi_transfer(struct spi_device *spi,struct spi_message *mesg)
{
	struct spi_master *master = spi->master;
	struct s3c_spi_info *info = spi_master_get_devdata(master);
	struct spi_transfer	*t = NULL;
	
	/* 更新设备的状态 */
	master->setup(spi);
	/*选中芯片*/
	gpio_set_value(spi->chip_select,0);
	/* 发送数据 */
	list_for_each_entry (t, &mesg->transfers, transfer_list) {	
		init_completion(&info->done);
		info->transfer = t;
		info->count = 0;
		/* 发送 */
		if(t->tx_buf)
		{	
			writeb(((unsigned char *)t->tx_buf)[info->count],info->io_base+SPTDAT);
			if(wait_for_completion_timeout(&info->done,2 * HZ) == 0)
			{
				printk("transfer timeout.\n");
				break;
			}
		}
		/* 接收 */
		else if(t->rx_buf)
		{	
			writeb(0xff,info->io_base+SPTDAT);
			if(wait_for_completion_timeout(&info->done,2 * HZ) == 0)
			{
				printk("receive timeout.\n");
				break;
			}
		}
	};

	/* 取消 */
	gpio_set_value(spi->chip_select,1);

	/* 唤醒等待的进程 */
	mesg->status = 0;
	mesg->complete(mesg->context);
	return 0;
}

static irqreturn_t s3c2440_spi_handle(int irq, void *dev_id)
{	
	struct spi_master *master = (struct spi_master *)dev_id;
	struct s3c_spi_info *info = spi_master_get_devdata(master);
	struct spi_transfer *transfer = info->transfer;

	if(!transfer)
		return IRQ_HANDLED;
	
	if(transfer->tx_buf)
	{	
		info->count++;
		/*没有发完*/
		if(info->count < transfer->len)
		{
			writeb(((unsigned char *)transfer->tx_buf)[info->count],info->io_base+SPTDAT);
		}
		else
			complete(&info->done);
	}
	else if(transfer->rx_buf)
	{
		((unsigned char *)transfer->rx_buf)[info->count] = readb(info->io_base+SPRDAT);
		info->count++;
		if(info->count < transfer->len)
			writeb(0xff,info->io_base+SPTDAT);
		else
			complete(&info->done);
	}
	
	return IRQ_HANDLED;
}

static void spi_controler_init(struct spi_master *master)
{	
	struct s3c_spi_info *info;
	
	info = spi_master_get_devdata(master);
	/* 使能时钟 */
	info->clk = clk_get(NULL,"spi");
	clk_prepare_enable(info->clk);
	
	if(info->bus_num)
	{
		/**
     	SPIMI GPG5
     	SPICLK GPG7
 		SPIMO GPG6
     	*/
     	s3c_gpio_cfgpin(S3C2410_GPG(5), S3C2410_GPG5_SPIMISO1);
     	s3c_gpio_cfgpin(S3C2410_GPG(6), S3C2410_GPG6_SPIMOSI1);
     	s3c_gpio_cfgpin(S3C2410_GPG(7), S3C2410_GPG7_SPICLK1);
	}
	else
	{
		/**
		GPE13 SPICLK0
		GPE12 SPIMOSI0
		GPE11 SPIMISO0
		*/
		s3c_gpio_cfgpin(S3C2410_GPE(13), S3C2410_GPE13_SPICLK0);
     	s3c_gpio_cfgpin(S3C2410_GPE(12), S3C2410_GPE12_SPIMOSI0);
     	s3c_gpio_cfgpin(S3C2410_GPE(11), S3C2410_GPE11_SPIMISO0);
	}	

}

static struct spi_master* spi_create_master(unsigned int bus_num)
{
	unsigned int irq_num = bus_num ? IRQ_SPI1 : IRQ_SPI0;
	unsigned int reg_base = bus_num ? 0x59000020 : 0x59000000;
	struct platform_device *s3c2440_spi;
	struct spi_master *master;
	struct s3c_spi_info *info;

	printk("bus_num = %d,reg_base = 0x%x\n",bus_num,reg_base);
	s3c2440_spi = platform_device_alloc("s3c_spi",bus_num);
	if(!s3c2440_spi)
	{	
		printk("platform alloc error\n");
		return NULL;
	}
	
	platform_device_add(s3c2440_spi);
	master = spi_alloc_master(&s3c2440_spi->dev,sizeof(struct s3c_spi_info));
	if(!master)
		return NULL;
		
	master->bus_num = bus_num;
	master->num_chipselect = 0xffff;
	master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH; 
	master->setup     = s3c2440_spi_setup;
	master->transfer  = s3c2440_spi_transfer;
	
	info = (struct s3c_spi_info *)spi_master_get_devdata(master);
	info->io_base = (unsigned int *)ioremap(reg_base,0x18);
	info->irq_num = irq_num;
	info->bus_num = bus_num;
	info->pdev    = s3c2440_spi;

	spi_controler_init(master);
	if(request_irq(irq_num,s3c2440_spi_handle,0,"s3c2440_spi",master))
	{
		printk("request irq error.\n");
		return NULL; 
	}
		
	return master;
	
}

static int __init spi_master_init(void)
{
	s3c2440_spi0 = spi_create_master(0);
	s3c2440_spi1 = spi_create_master(1);

	if(!s3c2440_spi0 || !s3c2440_spi1)
		return -ENOMEM;
	
	spi_register_master(s3c2440_spi0);
	spi_register_master(s3c2440_spi1);
	
	return 0;
}

static void spi_master_destory(struct spi_master * master)
{
	struct s3c_spi_info *info;
	info = spi_master_get_devdata(master);
	platform_device_del(info->pdev);
	platform_device_put(info->pdev);
	free_irq(info->irq_num,master);
	iounmap(info->io_base);
	clk_disable(info->clk);
	clk_put(info->clk);
	spi_unregister_master(master);
	spi_master_put(master);
	kfree(master);
}

static void __exit spi_master_exit(void)
{
	spi_master_destory(s3c2440_spi0);
	spi_master_destory(s3c2440_spi1);
}

module_init(spi_master_init);
module_exit(spi_master_exit);
MODULE_LICENSE("GPL");

