#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>

static struct i2c_board_info cmos_ov9650_info = {
	.type = "cmos_ov9650",
	.addr = 0x30,
};

static struct i2c_client *coms_ov9650_client = NULL;

static int cmos_ov9650_dev_init(void)
{	
	struct i2c_adapter *adapter;
	
	adapter = i2c_get_adapter(0);
	if(!adapter)
	{
		printk("no i2c adapter.\n");
		return -ENODEV;
	}
	
	coms_ov9650_client = i2c_new_device(adapter,&cmos_ov9650_info);
	i2c_put_adapter(adapter);
	
	if(!coms_ov9650_client)
		return -ENODEV;
	
	return 0;
}

static void cmos_ov9650_dev_exit(void)
{
	i2c_unregister_device(coms_ov9650_client);
}

module_init(cmos_ov9650_dev_init);
module_exit(cmos_ov9650_dev_exit);
MODULE_LICENSE("GPL");

