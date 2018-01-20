#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/clk.h>

#include <linux/mtd/nand.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>

struct nand_regs{
	unsigned long nfconf;
	unsigned long nfcont;
	unsigned long nfcmd; 
	unsigned long nfaddr;
	unsigned long nfdata;
	unsigned long nfeccd0 ;
	unsigned long nfeccd1 ;
	unsigned long nfseccd; 
	unsigned long nfstat; 
	unsigned long nfestat0;
	unsigned long nfestat1;
	unsigned long nfmecc0; 
	unsigned long nfmecc1; 
	unsigned long nfsecc;
	unsigned long nfsblk;
	unsigned long nfblk;
};

static struct nand_regs *s3c_nand_reg = NULL;
static struct nand_chip *s3c_nand_chip = NULL;
static struct mtd_info *s3c_mtd = NULL;
static struct clk *nand_clk =NULL;

static struct mtd_partition s3c_nand_partitions[] = {
	[0] = {
        .name   = "bootloader",
        .size   = 0x00040000,
		.offset	= 0,
	},
	[1] = {
        .name   = "params",
        .offset = MTDPART_OFS_APPEND,
        .size   = 0x00020000,
	},
	[2] = {
        .name   = "kernel",
        .offset = MTDPART_OFS_APPEND,
        .size   = 0x00200000,
	},
	[3] = {
        .name   = "root",
        .offset = MTDPART_OFS_APPEND,
        .size   = MTDPART_SIZ_FULL,
	}
};

/* �����λΪ0ʱ��˵��оƬ����æµ״̬ */
static int s3c_nand_ready(struct mtd_info *mtd)
{
	return (s3c_nand_reg->nfstat & 0x1);
}

/* Ƭѡ���� */
static void s3c_nand_select_chip(struct mtd_info *mtd, int chip)
{	
	/* ѡ�� */
	if(chip != -1)
	{
		s3c_nand_reg->nfcont &= ~(1<<1); 
	}
	/*ȡ��Ƭѡ*/
	else
	{
		s3c_nand_reg->nfcont |= (1<<1);
	}
}

static void s3c_nand_cmd_ctr(struct mtd_info *mtd, int dat, unsigned int ctrl)
{	
	if (dat  == NAND_CMD_NONE)
		return;
	
	/* �������� */
	if(ctrl & NAND_CLE)
	{
		s3c_nand_reg->nfcmd= dat;
	}
	/* ���͵�ַ */
	else
	{
		s3c_nand_reg->nfaddr = dat;
	}
}

static int __init s3c_nand_init(void)
{	
	/* ����һ��NandChip���� */
	s3c_nand_chip = kzalloc(sizeof(struct nand_chip),GFP_KERNEL);
	/* ���мĴ�����ַ��ӳ�� */
	s3c_nand_reg  = ioremap(0x4E000000,sizeof(struct nand_regs));
	
	/* ��ʼ��NandChip */
	s3c_nand_chip->IO_ADDR_R  = &s3c_nand_reg->nfdata;
	s3c_nand_chip->IO_ADDR_W = &s3c_nand_reg->nfdata;
	s3c_nand_chip->select_chip   = s3c_nand_select_chip;
	s3c_nand_chip->dev_ready    = s3c_nand_ready;
	s3c_nand_chip->cmd_ctrl	    = s3c_nand_cmd_ctr;
	s3c_nand_chip->ecc.mode	    = NAND_ECC_SOFT;
	s3c_nand_chip->ecc.algo	    = NAND_ECC_HAMMING;
	
	/*nand controler init*/
	nand_clk = clk_get(NULL,"nand");
	clk_prepare_enable(nand_clk);
	
#define TACLS    0
#define TWRPH0   1
#define TWRPH1   0

	/* ����Nand Flash ��оƬ�ֲ����ò��� */
	s3c_nand_reg->nfconf = (TACLS<<12) | (TWRPH0<<8) | (TWRPH1<<4);
	/* ȡ��Ƭѡ��ʹ��nand������ */
	s3c_nand_reg->nfcont = (1<<1) | (1<<0);

	/* ����mtd_info���� */
	s3c_mtd = nand_to_mtd(s3c_nand_chip);
	s3c_mtd->owner = THIS_MODULE;
	
	nand_scan(s3c_mtd,1);

	/* ��ӷ��� */
	mtd_device_parse_register(s3c_mtd, NULL, NULL,s3c_nand_partitions, 4);
	
	return 0;
}

static void __exit s3c_nand_exit(void)
{
	mtd_device_unregister(nand_to_mtd(s3c_nand_chip));
	clk_disable_unprepare(nand_clk);
	iounmap(s3c_nand_reg);
	kfree(s3c_nand_chip);
}

module_init(s3c_nand_init);
module_exit(s3c_nand_exit);

MODULE_LICENSE("GPL");

