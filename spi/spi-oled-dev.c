#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <mach/gpio-samsung.h>
#include <mach/regs-gpio.h>
#include <plat/gpio-cfg.h>

static const struct spi_board_info jz2440_spi_info[] = {
	{
	.modalias 		 = "jz2440_spi_oled",
	.max_speed_hz = 10000000,
	.chip_select       = S3C2410_GPF(1),
	.platform_data	  = (const void *)S3C2410_GPG(4),
	.mode 		 = SPI_MODE_0,
	.bus_num 	 = 1,
	},
	{
	.modalias 		 = "jz2440_spi_flash",
	.max_speed_hz = 80000000,
	.chip_select     = S3C2410_GPG(2),
	.mode 		 = SPI_MODE_0,
	.bus_num 	 = 1,
	},
};

static int spi_dev_init(void)
{
	int ret = 0;
	
	ret = spi_register_board_info(jz2440_spi_info, ARRAY_SIZE(jz2440_spi_info));
	if(ret)
	{
		printk("register spi board info fail.\n");
		return -ENODEV;
	}
	
	return 0;
}

module_init(spi_dev_init);
MODULE_LICENSE("GPL");

