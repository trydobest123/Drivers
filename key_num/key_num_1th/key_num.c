#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/wait.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/kdev_t.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <asm/uaccess.h>
#include <asm/irq.h>

DECLARE_WAIT_QUEUE_HEAD(wait_head);
DEFINE_SPINLOCK(_spin_lock);

static int KeyDevOpen(struct inode *inode, struct file *filp);
static ssize_t KeyDevRead(struct file *filp, char __user *buf, size_t len, loff_t *offset);
static int KeyClose(struct inode *inode, struct file *filp);

static struct cdev KeyChrdev;
static struct class *KeyDevClass = NULL;
static struct device  *KeyDevice = NULL; 
static struct semaphore sema_phore;

struct KeyDesc {
	int KeyNum;
	int KeyIRQ;
	char *Name;
};

static struct KeyDesc KeyDevDesc[4] = {
	{0x10,IRQ_EINT0,"key1"},
	{0x20,IRQ_EINT2,"key2"},
	{0x30,IRQ_EINT11,"key3"},
	{0x40,IRQ_EINT19,"key4"},
};

static int KeyVal = 0;
static dev_t devt;

const static struct file_operations KeyDevOpr = {
	.owner = THIS_MODULE, 
	.open   = KeyDevOpen,
	.read    = KeyDevRead,
	.release= KeyClose,
};

irqreturn_t KeyNumHadler(int irq,void *data)
{	
	struct KeyDesc *Desc =(struct KeyDesc *)data;
	
	KeyVal = Desc->KeyNum;
	
	wake_up_interruptible(&wait_head);
	
	return IRQ_RETVAL(IRQ_HANDLED);
}

static int KeyDevOpen(struct inode *inode, struct file *filp)
{
	int i = 0;
	static int cnt = 0;

	down(&sema_phore);
	printk("cnt = %d\n",++cnt);
	
	/* 申请中断 */
	for(i = 0; i < 4; i++)	
	{
		request_irq(KeyDevDesc[i].KeyIRQ ,KeyNumHadler,IRQF_TRIGGER_RISING,KeyDevDesc[i].Name,&KeyDevDesc[i]);
	}
	
	return 0;
}

static void my_wake(wait_queue_head_t *wait_head_t)
{
	DECLARE_WAITQUEUE(wait,current);

	set_current_state(TASK_INTERRUPTIBLE);
	add_wait_queue(wait_head_t, &wait);
	schedule();
	remove_wait_queue(wait_head_t, &wait);
}
	
static ssize_t KeyDevRead(struct file *filp, char __user *buf, size_t len, loff_t *offset)
{	
	/*当没有数据的时候就进行休眠*/
	my_wake(&wait_head);

	spin_lock(&_spin_lock);
	/* 唤醒后把数据拷贝到用户空间 */	
	put_user(KeyVal, buf);
	spin_unlock(&_spin_lock);
		
	return 0;
}

static int KeyClose(struct inode *inode, struct file *filp)
{
	int i;

	for(i = 0; i < 4; i++)
	{
		free_irq(KeyDevDesc[i].KeyIRQ,&KeyDevDesc[i]);
	}

	up(&sema_phore);
	return 0;
}

static int __init KeyDevInit(void)
{	
	int ret;
	
	ret = alloc_chrdev_region(&devt,0, 1,"keydev");
	if(ret < 0)
	{
		return -1;
	}
	
	cdev_init(&KeyChrdev,&KeyDevOpr);
	
	cdev_add(&KeyChrdev,devt,1);
	
	/* 分配类，让设备自动创建节点 	*/
	KeyDevClass = class_create(THIS_MODULE,"keydev");
	if(!KeyDevClass)
	{
		return -ENOMEM;
	}
	
	KeyDevice = device_create(KeyDevClass,NULL,devt,NULL,"keydev");
	if(!KeyDevice)
	{
		return -ENOMEM;
	}

	sema_init(&sema_phore, 1);
	return 0;
}

static void __exit KeyDevexit(void)
{
	device_unregister(KeyDevice);
	class_destroy(KeyDevClass);
	unregister_chrdev(devt, "keydev");
	
	printk("KeyDev Exit.\n");
}

module_init(KeyDevInit);
module_exit(KeyDevexit);

MODULE_LICENSE("GPL");

