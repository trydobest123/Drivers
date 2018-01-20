#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <asm/uaccess.h>

#define USB_BULK_BUF_SIZE 512
static int secbulk_probe(struct usb_interface *intf,const struct usb_device_id *id);
static void secbulk_disconnect (struct usb_interface *intf);

struct secbulk{
	unsigned int usb_out_endpoint;
	unsigned char *bulk_buf;
	struct usb_device *udev;
	struct mutex lock;
};

const struct usb_device_id secbulk_id[] = {
	{USB_DEVICE(0x5345,0x1234)},
	{},
};

static struct usb_driver secbulk_driver = {
	.name = "secbulk",
	.id_table = secbulk_id,
	.probe = secbulk_probe,
	.disconnect = secbulk_disconnect,
};

static int secbulk_open(struct inode *node, struct file *filp)
{
	struct usb_interface *intf;
	struct secbulk *dev;
	
	intf = usb_find_interface(&secbulk_driver,(iminor(node)));
	dev = usb_get_intfdata(intf);
	if(!dev)
	{
		printk("not find secbulk device\n");
		return -ENODEV;
	}	
	
	/* ·ÖÅä»º´æ */
	dev->bulk_buf = kzalloc(USB_BULK_BUF_SIZE,GFP_KERNEL);
	if(!dev->bulk_buf)
		return -ENOMEM;
	if(!mutex_trylock(&dev->lock))
		return -EBUSY;
	filp->private_data = dev;
	mutex_unlock(&dev->lock);
	return 0;	
}

static ssize_t secbulk_read(struct file *filp, char __user *buf, size_t cnt, loff_t *off)
{
	return -EPERM;
}

static ssize_t secbulk_write(struct file *filp, const char __user *buf, size_t cnt, loff_t *off)
{
	struct secbulk *dev;
	struct usb_device *udev;
	unsigned int todo_trans = cnt;
	unsigned int cur_trans_len = 0;
	unsigned int actual_lenth;
	unsigned int total_write = 0;
	const char *usr_buf = buf;
	int ret;
	dev = filp->private_data;
	udev = dev->udev;
	mutex_lock(&dev->lock);
	do{
		cur_trans_len = (todo_trans > USB_BULK_BUF_SIZE) ? USB_BULK_BUF_SIZE : todo_trans;
		if(copy_from_user(dev->bulk_buf,usr_buf + total_write,cur_trans_len))
		{
			printk(KERN_ERR "secbulk:copy_from_user failed!\n");
				return -EFAULT; 
		}
			
		ret = usb_bulk_msg(udev, usb_sndbulkpipe(udev, dev->usb_out_endpoint), dev->bulk_buf, cur_trans_len, &actual_lenth, 3*HZ);
		if(ret ||(cur_trans_len != actual_lenth))
		{
			printk(KERN_ERR"usb transmit error.\n");	
			return -EFAULT;
		}
		todo_trans -= cur_trans_len;
		total_write += cur_trans_len; 
	}while(todo_trans > 0);
	
	mutex_unlock(&dev->lock);

	return total_write;
	
}

static int secbulk_release(struct inode *node, struct file *filp)
{
	struct secbulk *dev  = (struct secbulk *)filp->private_data;
	
	kfree(dev->bulk_buf);
	mutex_unlock(&dev->lock);
	return 0;
}

static const struct file_operations secbulk_fops = {
	.owner = THIS_MODULE,
	.open = secbulk_open,
	.write = secbulk_write,
	.read = secbulk_read,
	.release = secbulk_release,
};

static struct usb_class_driver secbulk_class_driver = {
	.name = "secbulk%d",
	.fops   = &secbulk_fops,
	.minor_base=	100,
};

static int secbulk_probe(struct usb_interface *intf,const struct usb_device_id *id)
{
	int i;
	int ret;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint; 
	struct secbulk *secbulk_dev;
	iface_desc = intf->altsetting;

	secbulk_dev = kzalloc(sizeof(struct secbulk),GFP_KERNEL);
	if(!secbulk_dev)
		return -ENOMEM;
	
	for(i = 0; i < iface_desc->desc.bNumEndpoints;i++)
	{
		endpoint = &iface_desc->endpoint[i].desc;
		if(!secbulk_dev->usb_out_endpoint&& usb_endpoint_is_bulk_out(endpoint))
		{
			secbulk_dev->usb_out_endpoint = endpoint->bEndpointAddress;
			printk(KERN_INFO"usb find out endpoint.\n");
			break;
		}
	}
	
	if(!secbulk_dev->usb_out_endpoint)
	{
		printk(KERN_ERR"usb endpoint not found.\n");
		return -EFAULT;
	}

	ret = usb_register_dev(intf,&secbulk_class_driver);
	if(ret)
	{
		printk(KERN_ERR"usb register device error.\n");
		return -EFAULT;
	}

	secbulk_dev->udev = interface_to_usbdev(intf);
	usb_set_intfdata(intf,secbulk_dev);
	mutex_init(&secbulk_dev->lock);
	
	return 0;
}

static void secbulk_disconnect (struct usb_interface *intf)
{	
	struct secbulk *secbulk_dev;
	
	secbulk_dev = usb_get_intfdata(intf);
	if(secbulk_dev != NULL)
		kfree(secbulk_dev);
	usb_deregister_dev(intf, &secbulk_class_driver);
}

static int secbulk_init(void)
{
	int result;
	
	printk(KERN_INFO "secbulk:secbulk loaded\n");
	result = usb_register(&secbulk_driver);
	if(result)
	{
		printk(KERN_ERR"secbulk:usb_register failed: %d\n", result);
		return result; 
	}
	
	return 0;
}

static void secbulk_exit(void)
{
	usb_deregister(&secbulk_driver);
	printk(KERN_INFO"secbulk:secbulk unloaded\n");
}

module_init(secbulk_init);
module_exit(secbulk_exit);
MODULE_LICENSE("GPL");

