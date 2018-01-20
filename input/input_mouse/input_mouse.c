#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb/input.h>
#include <linux/hid.h>

static struct input_dev	 *usbkey_dev;
static struct urb *umk_urb;	
static char *vir_buf = NULL;
static dma_addr_t phys_buf;

static struct usb_device_id usb_mouse_key_id_table [] = {
	{ USB_INTERFACE_INFO(USB_INTERFACE_CLASS_HID, USB_INTERFACE_SUBCLASS_BOOT,
		USB_INTERFACE_PROTOCOL_MOUSE) },
	{ }	/* Terminating entry */
};

int usbmosue_key_open(struct input_dev *dev)
{
	/* 首先提交一次URB */
	usb_submit_urb(umk_urb, GFP_KERNEL);
	
	return 0;
}

void usbmouse_key_irq(struct urb *urb)
{
	unsigned char *dat = vir_buf;

	/*上报事件*/
	input_event(usbkey_dev,EV_KEY,KEY_L,dat[0] & 0x01);
	input_event(usbkey_dev,EV_KEY,KEY_S,dat[0]&0x02);
	input_event(usbkey_dev,EV_KEY,KEY_ENTER,dat[0]&0x04);
	
	input_event(usbkey_dev,EV_REL,REL_X, dat[1]);
	input_event(usbkey_dev,EV_REL, REL_Y, dat[2]);
	input_event(usbkey_dev,EV_REL, REL_WHEEL, dat[3]);

	input_sync(usbkey_dev);

	usb_submit_urb(umk_urb, GFP_KERNEL);
}

static int usb_mouse_key_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *usbdev;
	struct usb_endpoint_descriptor *endpoint;
	struct usb_host_interface	*interface;
	
	unsigned int pipe  = 0;
	unsigned int len = 0;
	
	printk("USB Device Found.\n");

	usbdev   = interface_to_usbdev(intf);
	interface = intf->cur_altsetting;
	endpoint = &interface->endpoint[0].desc;
		
	/* 分配一个input_device结构体 */
	usbkey_dev = input_allocate_device();
	
	if(!usbkey_dev)
	{
		printk("malloc input device fail.\n");
		return -1;
	}

	usbkey_dev->name = "usbmouse_key";
	
	/* 设置input_dev */
	/* 设置能产生什么类型的事件 */
	set_bit(EV_KEY,usbkey_dev->evbit);
	set_bit(EV_REL,usbkey_dev->evbit);
	
	/* 设置产生事件中的哪一种 */
	set_bit(REL_X,usbkey_dev->relbit);
	set_bit(REL_Y,usbkey_dev->relbit);
	set_bit(REL_WHEEL,usbkey_dev->relbit);
	set_bit(KEY_L,usbkey_dev->keybit);
	set_bit(KEY_S,usbkey_dev->keybit);
	set_bit(KEY_ENTER,usbkey_dev->keybit);
	
	usbkey_dev->open = usbmosue_key_open;
	
	input_register_device(usbkey_dev);
	
	/*硬件相关的操作*/
	/* 分配一个usb_urb 请求块 */
	umk_urb = usb_alloc_urb(0, GFP_KERNEL);
	if(!umk_urb)
	{
		printk("malloc usb urb fail.\n");
		return -1;
	}
	
	/* 数据源 */
	pipe = usb_rcvintpipe(usbdev, endpoint->bEndpointAddress); //获取主机读取数据的端点
	/*数据长度*/
	len = endpoint->wMaxPacketSize;
	/* 数据目的*/
	/* 返回分配内存的虚拟地址 */
	 vir_buf = usb_alloc_coherent(usbdev, len, GFP_ATOMIC, &phys_buf);

	usb_fill_int_urb(umk_urb, usbdev, pipe, vir_buf,len,usbmouse_key_irq, NULL, endpoint->bInterval);
	umk_urb->transfer_dma = phys_buf;
	umk_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	/* 提交URB */
	usb_submit_urb(umk_urb, GFP_KERNEL);
	
	return 0;
}

static void usb_mouse_key_disconnect(struct usb_interface *intf)
{
	printk("USB Device Disconnect.\n");
	usb_kill_urb(umk_urb);
	usb_free_urb(umk_urb);
}


static struct usb_driver usbmouse_key= {
	.name		= "usbmousekey",
	.probe		= usb_mouse_key_probe,
	.disconnect	= usb_mouse_key_disconnect,
	.id_table	= usb_mouse_key_id_table,
};

static int mousekey_init(void)
{
	usb_register(&usbmouse_key);
	
	return 0;
}

static void mousekey_exit(void)
{
	usb_deregister(&usbmouse_key);
	input_unregister_device(usbkey_dev);
	kfree(usbkey_dev);
}

module_init(mousekey_init);
module_exit(mousekey_exit);

MODULE_LICENSE("GPL");

