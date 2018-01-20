#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>

static struct i2c_board_info ov7740_info = {
	.type = "cmos_ov7740",
	.addr = 0x21,
};

static struct i2c_client *ov7740_client = NULL;

static int ov7740_dev_init(void)
{	
	struct i2c_adapter *adapter;
	
	adapter = i2c_get_adapter(0);
	if(!adapter)
	{
		printk("no i2c adapter.\n");
		return -ENODEV;
	}
	
	ov7740_client = i2c_new_device(adapter,&ov7740_info);
	i2c_put_adapter(adapter);
	
	if(!ov7740_client)
		return -ENODEV;
	
	return 0;
}

static void ov7740_dev_exit(void)
{
	i2c_unregister_device(ov7740_client);
}

module_init(ov7740_dev_init);
module_exit(ov7740_dev_exit);
MODULE_LICENSE("GPL");
