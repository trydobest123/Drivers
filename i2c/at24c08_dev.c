#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>

static struct i2c_client *s3c_i2c_client = NULL;


static const unsigned short at24cxx_addr_list[] = {0x60,0x50,0x51,I2C_CLIENT_END};
static const struct i2c_board_info at24xx_board_info = {
	.type = "at24c08",
	.addr = 0x20,
};

static int __init at24cxx_dev_init(void)
{
	struct i2c_board_info at24c08_info;
	struct i2c_adapter *s3c_i2c_adapter;
	
	s3c_i2c_adapter = i2c_get_adapter(0);
	if(NULL == s3c_i2c_adapter)
	{
		printk("no adapter find!\n");
		return -ENXIO;
	}

	printk("adapter name: %s\n",s3c_i2c_adapter->name);
	
	s3c_i2c_client = i2c_new_probed_device(s3c_i2c_adapter,&at24xx_board_info,at24cxx_addr_list,NULL);
	if(!s3c_i2c_client )
	{
		printk("client is invalid.\n");

		return -ENXIO;
	}
	
	i2c_put_adapter(s3c_i2c_adapter);
	
	printk("client device address is 0x%x.\n",s3c_i2c_client->addr);
	
	return 0;
}

static void __exit at24cxx_dev_exit(void)
{
	i2c_unregister_device(s3c_i2c_client);
}

module_init(at24cxx_dev_init);
module_exit(at24cxx_dev_exit);
MODULE_LICENSE("GPL");

