#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>
#include <asm/io.h>

static struct map_info *s3c_map = NULL;
static struct mtd_info *s3c_mtd = NULL;

static const char * const probe_types[] = {
	"cfi_probe", "jedec_probe", "qinfo_probe", "map_rom", NULL };

static struct mtd_partition s3c_nor_partitions[] = {
	[0] = {
        .name   = "bootloader",
        .size   = 0x00040000,
	 .offset	= 0,
	},
	[1] = {
        .name   = "params",
        .offset = MTDPART_OFS_APPEND,
        .size   = MTDPART_SIZ_FULL,
	}
};

static int __init s3c_nor_init(void)
{	
	char **probe_name = probe_types;
	
	/* 分配一个 map_info 对象 */
	s3c_map = kzalloc(sizeof(struct map_info), GFP_KERNEL);
	if(!s3c_map){
		return -ENOMEM;
	};
	
	/* 设置map_info对象 */
	s3c_map->name        = "s3c_nor";
	s3c_map->size   	      = 0x200000; //容量2MBytes Bytes
	s3c_map->phys          = 0x0;
	s3c_map->bankwidth  = 2;
	s3c_map->virt            = ioremap(s3c_map->phys,s3c_map->size);
	
	/* 进行一些简单的初始化 */
	simple_map_init(s3c_map);
	
	for(;*probe_name;probe_name++)
	{
		/* 选择相应的接口识别函数,构造MTD结构体 */
		s3c_mtd = do_map_probe(*probe_name,s3c_map);
		
		if(s3c_mtd)
		{
			printk("probe_type:%s\n",*probe_name);
			break;
		}
	}

	if(!s3c_mtd)
		return -ENOMEM;
	
	/* 添加分区*/ 
	mtd_device_parse_register(s3c_mtd,NULL,NULL,s3c_nor_partitions,2);
	
	return 0;
}

static void __exit s3c_nor_exit(void)
{
	mtd_device_unregister(s3c_mtd);
	iounmap(s3c_map->virt);
	kfree(s3c_map);
}

module_init(s3c_nor_init);
module_exit(s3c_nor_exit);

MODULE_LICENSE("GPL");

