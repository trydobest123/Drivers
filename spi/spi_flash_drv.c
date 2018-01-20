#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/spi/spi.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>

static int spi_flash_probe(struct spi_device *spi);
static int spi_flash_remove(struct spi_device *spi);
static void SPIFlashWriteStaEnable(void);
static void SPIFlashWriteEnable(void);
static void SPIFlashStaRegUnPro(void);
static unsigned char SPIFlashReadBusyBit(void);
static void SPIFlashReadStaReg(unsigned char *sta1,unsigned char *sta2);
static void SPIFlashWaitBusy(void);
static void SPIFlashWriteStaReg(unsigned char reg1,unsigned char reg2);
static void SPIFlashPro(void);
static void SPIFlashUnPro(void);
static void SPIFlashRead(unsigned int addr,unsigned char *buf,int len);
static void SPIFlashEraseSect(unsigned int sector);
static void SPIFlashReadId(unsigned char *mid,unsigned char *pid);


static struct spi_device *spi_flash_device = NULL;
static struct mtd_info *mtd_spi_flash = NULL;

static const struct spi_device_id spi_flash_table[] = {
	{.name = "mini2440_spi_flash",},
	{.name = "jz2440_spi_flash",},
};


static struct spi_driver spi_flash = {
	.driver = {
		.name = "spi_flash",
	},
	.id_table = spi_flash_table,
	.probe    = spi_flash_probe,
	.remove   = spi_flash_remove,
};

static void SPIFlashWriteStaEnable(void)
{
	unsigned char cmd = 0x50;
	
	spi_write(spi_flash_device,&cmd,1);
}

static void SPIFlashWriteEnable()
{
	unsigned char cmd = 0x06;
	
	spi_write(spi_flash_device,&cmd,1);
}

static void SPIFlashStaRegUnPro(void)
{
	unsigned char sta1,sta2;
	SPIFlashReadStaReg(&sta1,&sta2);

	sta1 &= ~(1 << 7);
	sta2 &= ~(1 << 0);

	SPIFlashWriteStaReg(sta1,sta2);
}

static unsigned char SPIFlashReadBusyBit(void)
{	
	unsigned char sta1,sta2;
	
	SPIFlashReadStaReg(&sta1,&sta2);
	if(sta1 & 0x1)
		return 1;
	else
		return 0;
}

/* sta1:¼Ä´æÆ÷1µÄ×´Ì¬£¬sta2:¼Ä´æÆ÷2µÄ×´Ì¬ */
static void SPIFlashReadStaReg(unsigned char *sta1,unsigned char *sta2)
{
	unsigned char cmd1 = 0x05;
	unsigned char cmd2 = 0x35;

	spi_write_then_read(spi_flash_device,&cmd1 ,1, sta1,1);
	spi_write_then_read(spi_flash_device,&cmd2 ,1, sta2,1);
}

static void SPIFlashWaitBusy(void)
{
	while(SPIFlashReadBusyBit())
	{	
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ/100);
	}
}	

/* ×´Ì¬¼Ä´æÆ÷½âËø */
static void SPIFlashWriteStaReg(unsigned char reg1,unsigned char reg2)
{
	unsigned char tx[3];

	tx[0] = 0x01;
	tx[1] = reg1;
	tx[2] = reg2;
	
	/* Ð´Ê¹ÄÜ*/
	SPIFlashWriteStaEnable();
	spi_write(spi_flash_device,tx,sizeof(tx));
	
	SPIFlashWaitBusy();
}

/*´æ´¢Æ÷ÉÏËø */
static void SPIFlashPro(void)
{
	unsigned char sta1,sta2;
	SPIFlashReadStaReg(&sta1,&sta2);

	sta1 |= (1 << 4) | (1 << 3); 
	sta2 &= ~(1 << 6);

	SPIFlashWriteStaReg(sta1,sta2);
}

/* ´æ´¢Æ÷½âËø */
static void SPIFlashUnPro(void)
{
	unsigned char sta1,sta2;

	SPIFlashReadStaReg(&sta1,&sta2);
	sta1 &= ~(7 << 2); 
	sta2 &= ~(1 << 6);

	SPIFlashWriteStaReg(sta1,sta2);

}

static void SPIFlashRead(unsigned int addr,unsigned char *buf,int len)
{
	unsigned char tx[4];
	
	tx[0] = 0x03;
	tx[1] = addr >> 16;
	tx[2] = (addr >> 8) & 0xff;
	tx[3] = addr & 0xff;
	
	struct spi_transfer x[2] = {
			{
			   .tx_buf	= tx,
			   .len		= 4,
			},
			{
			   .rx_buf = buf,
			   .len = len,
			},
		};
		
	struct spi_message	m;

	spi_message_init(&m);
	spi_message_add_tail(x, &m);
	spi_sync(spi_flash_device, &m);
	
	spi_write_then_read(spi_flash_device,tx,sizeof(tx),buf,len);
}

static void SPIFlashPageProgram(unsigned int address,unsigned char *buf,unsigned int len)
{

	unsigned char tmp_buf[4];
	
	tmp_buf[0] = 0x02;
	tmp_buf[1] = address >> 16;
	tmp_buf[2] = (address >> 8) & 0xff;
	tmp_buf[3] = address & 0xff;

	struct spi_transfer x[] = {
		{
			.tx_buf = tmp_buf,
		    .len   = 4,
		},
		{
			.tx_buf = buf,
			.len    = len,
		},
	};
		
	struct spi_message	m;

	spi_message_init(&m);
	spi_message_add_tail(x, &m);
	spi_sync(spi_flash_device, &m); 
	SPIFlashWaitBusy();
}

static void SPIFlashEraseSect(unsigned int sector)
{
	unsigned char tx[4];

	tx[0] = 0x20;
	tx[1] = sector >> 16;
	tx[2] = (sector >> 8) & 0xff;
	tx[3] = sector & 0xff;

	SPIFlashWriteEnable();
	spi_write(spi_flash_device,tx,sizeof(tx));
	SPIFlashWaitBusy(); 
}

static void SPIFlashReadId(unsigned char *mid,unsigned char *pid)
{	
	unsigned char tx[4];
	unsigned char rx[2];

	tx[0] = 0x90;
	tx[1] = 0x0;
	tx[2] = 0x0;
	tx[3] = 0x0;
	
	spi_write_then_read(spi_flash_device,tx,sizeof(tx),rx,sizeof(rx));

	*mid = rx[0];
	*pid = rx[1];
}

static void SPIFlashInit(void)
{
	SPIFlashStaRegUnPro();
	SPIFlashUnPro();
}

static int spi_flash_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	unsigned int addr = instr->addr;
	unsigned int sect = addr;
	unsigned int i = 0;
	
	if ((addr & (mtd_spi_flash->erasesize - 1)) || (instr->len & (mtd_spi_flash->erasesize - 1)))
    {
        printk("addr/len is not aligned\n");
        return -EINVAL;
    }

    for(i = 0; i < instr->len; i += 4096,sect += 4096)
    	SPIFlashEraseSect(sect);
    
	instr->state = MTD_ERASE_DONE;
	mtd_erase_callback(instr);
	return 0;
}

static int spi_flash_read_data(struct mtd_info *mtd, loff_t from, size_t len,
		      size_t *retlen, u_char *buf)
{	
    SPIFlashRead(from,buf,len);
	*retlen = len;
	return 0;
}

static int spi_flash_write(struct mtd_info *mtd, loff_t to, size_t len,
		       size_t *retlen, const u_char *buf)
{
	unsigned int addr = to;
	unsigned int wlen = 0;
	
	if ((addr & (mtd_spi_flash->erasesize - 1)) || (len & (mtd_spi_flash->erasesize - 1)))
    {
        printk("addr/len is not aligned\n");
        return -EINVAL;
    }

    for(wlen = 0; wlen < len; wlen +=256)
    {
		SPIFlashPageProgram(addr,(unsigned char *)buf,len);
		addr += 256;
		buf += 256;
    }
    
	*retlen = len;
	
	return 0;
}

static struct mtd_partition spi_flash_partitions[] = {
	{
		.name = "Bios",
		.offset = 0,
		.size = SZ_512K,
	},
	{
		.name   = "FileSystem",
		.offset = SZ_512K,
		.size   = SZ_512K, 
	},
	{
		.name = "uImage",
		.offset = SZ_1M,
		.size = SZ_1M,
	}
};

static int spi_flash_probe(struct spi_device *spi)
{
	unsigned char MID,DID;
	
	spi_flash_device = spi;
	gpio_direction_output(spi->chip_select,1);
	
	/*¶Á ID*/
	SPIFlashReadId(&MID,&DID);	
	printk("MID = 0x%x,DID = 0x%x\n",MID,DID);
	
	/* È¥µôSPI Flash µÄÐ´¼Ä´æÆ÷±£»¤ */
	SPIFlashInit();

	/* ¹¹Ôìmtd_spi_flash½á¹¹ */
	mtd_spi_flash = kzalloc(sizeof(*mtd_spi_flash),GFP_KERNEL);
	if(!mtd_spi_flash)
		return -ENOMEM;

	mtd_spi_flash->name = "spi_flash";
	mtd_spi_flash->type = MTD_NORFLASH;
	mtd_spi_flash->flags = MTD_CAP_NORFLASH;
	mtd_spi_flash->size =  SZ_2M;
	mtd_spi_flash->writesize = 256 ; //Ò³¶ÔÆë
	mtd_spi_flash->writebufsize = 64;
	mtd_spi_flash->erasesize = 4096;

	mtd_spi_flash->owner = THIS_MODULE;
	mtd_spi_flash->_erase = spi_flash_erase;
	mtd_spi_flash->_read = spi_flash_read_data;
	mtd_spi_flash->_write = spi_flash_write;

	mtd_device_register(mtd_spi_flash,spi_flash_partitions,3);
	
	return 0;
}

static int spi_flash_remove(struct spi_device *spi)
{
	mtd_device_unregister(mtd_spi_flash);
	kfree(mtd_spi_flash);
	return 0;
}

static int __init spi_flash_init(void)
{
	spi_register_driver(&spi_flash);
	return 0;
}

static void __exit spi_flash_exit(void)
{
	spi_unregister_driver(&spi_flash);
}

module_init(spi_flash_init);
module_exit(spi_flash_exit);
MODULE_LICENSE("GPL");

