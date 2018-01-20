#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/i2c.h>

#include <asm/uaccess.h>

static int at24c08_probe(struct i2c_client *, const struct i2c_device_id *);
static int at24c08_remove(struct i2c_client *) ;
static int at24c02_open (struct inode *, struct file *);
static ssize_t at24c02_read (struct file *, char __user *, size_t, loff_t *);
static ssize_t at24c02_write (struct file *, const char __user *, size_t, loff_t *);
static int at24c02_close(struct inode *, struct file *);
static unsigned int at24c02_poll (struct file *, struct poll_table_struct *);

static struct cdev *at24c02_cdev = NULL;
static struct class *at24c02_class = NULL;
static struct i2c_client *at24c02_client = NULL;
static struct device *at24c02_class_dev = NULL;
static unsigned  cdev_id = 0;

const struct i2c_device_id at24c08_id_table = {
	.name = "at24c08",
};

static struct i2c_driver at24c08_drv = {
	.driver	= {
		.name = "at24c08", 
	},
	
	.probe 	= at24c08_probe,
	.remove	= at24c08_remove,
	.id_table  = &at24c08_id_table,
};

static struct file_operations at24c02_fops = {
	.open   = at24c02_open,
	.write   = at24c02_write,
	.read    = at24c02_read,
	.poll     = at24c02_poll,
	.release =at24c02_close,  
};

static int at24c02_open (struct inode *inode, struct file *filp)
{
	return 0;
}

static ssize_t at24c02_read (struct file *filp, char __user *buf, size_t size, loff_t *lenth)
{
	u8 addr,dat;

	copy_from_user(&addr,buf,1);
	/*Ëæ»ú¶Á*/
	dat = i2c_smbus_read_byte_data(at24c02_client,addr);
	
	copy_to_user(buf,&dat,1);
	
	return 1;	
}

static ssize_t at24c02_write (struct file *filp, const char __user *buf, size_t size, loff_t *lenth)
{
	unsigned char tmpbuf[2];
	unsigned char addr, data;

	copy_from_user(tmpbuf, buf, 2);
	
	printk("addr = %d data = %d\n",tmpbuf[0],tmpbuf[1]);
	
	/* databuf[0]:address,databuf[1]:val */
	if(!i2c_smbus_write_byte_data(at24c02_client,tmpbuf[0],tmpbuf[1]))	
		return 2;
	else
		return -EIO;
}

static int at24c02_close(struct inode *inode, struct file *filp)
{
	return 0;
}

static unsigned int at24c02_poll (struct file *filp, struct poll_table_struct *poll_table)
{
	
	return 0;
}


static int at24c08_probe(struct i2c_client *client, const struct i2c_device_id *dev_id)
{	
	at24c02_client = client;
	
	if(alloc_chrdev_region(&cdev_id,0,1,"at24c02"))
	{
		printk(KERN_WARNING"alloc dev id failed.\n");
		return -EINVAL;
	}
	
	at24c02_cdev = cdev_alloc();
	if (!at24c02_cdev)
	{
		return -ENODEV;
	}

	at24c02_cdev->dev 	 = cdev_id;
	at24c02_cdev->owner = THIS_MODULE;
	at24c02_cdev->ops     = &at24c02_fops;

	cdev_add(at24c02_cdev,cdev_id,1);
	at24c02_class = class_create(THIS_MODULE,"at24xx_dev_class");
	at24c02_class_dev = device_create(at24c02_class,&client->dev,cdev_id,NULL,"at24c02");

	return 0;
}

static int at24c08_remove(struct i2c_client *client) 
{
	printk("remove i2c device!\n");
	device_del(at24c02_class_dev);
	class_destroy(at24c02_class);
	cdev_del(at24c02_cdev);
	unregister_chrdev_region(cdev_id, 1);
	
	return 0;
}

static int __init at24cxx_drv_init(void)
{
	i2c_add_driver(&at24c08_drv);

	return 0;
}

static void __exit at24xx_drv_exit(void)
{
	i2c_del_driver(&at24c08_drv);
}

module_init(at24cxx_drv_init);
module_exit(at24xx_drv_exit);
MODULE_LICENSE("GPL");

