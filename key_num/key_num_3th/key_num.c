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
#include <linux/poll.h>
#include <linux/timer.h>
#include <asm/uaccess.h>
#include <asm/irq.h>

DECLARE_WAIT_QUEUE_HEAD(wait_head);
DEFINE_SPINLOCK(_spin_lock);

static int KeyDevOpen(struct inode *inode, struct file *filp);
static ssize_t KeyDevRead(struct file *filp, char __user *buf, size_t len, loff_t *offset);
static int KeyClose(struct inode *inode, struct file *filp);
static unsigned int KeyDevPoll(struct file *, struct poll_table_struct *);

static struct cdev KeyChrdev;
static struct class *KeyDevClass = NULL;
static struct device  *KeyDevice = NULL; 
static struct semaphore sema_phore;
static struct timer_list my_timer;
static struct KeyDesc *Desc;

static int keypress;

struct KeyDesc {
	int KeyNum;
	int KeyIRQ;
	char *Name;
};

static struct KeyDesc KeyDevDesc[4] = {
	{0x10,IRQ_EINT4,"key1"},
	{0x20,IRQ_EINT5,"key2"},
	{0x30,IRQ_EINT6,"key3"},
	{0x40,IRQ_EINT7,"key4"},
};

static int KeyVal = 0;
static dev_t devt;

const static struct file_operations KeyDevOpr = {
	.owner= THIS_MODULE, 
	.open  = KeyDevOpen,
	.read   = KeyDevRead,
	.poll    =	KeyDevPoll,
	.release= KeyClose,
};

irqreturn_t KeyNumHadler(int irq,void *data)
{	
	Desc =(struct KeyDesc *)data;
	
	mod_timer(&my_timer,msecs_to_jiffies(10));
	
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

static ssize_t KeyDevRead(struct file *filp, char __user *buf, size_t len, loff_t *offset)
{	
	/* 使用poll 机制 */
	spin_lock(&_spin_lock);
	/* 唤醒后把数据拷贝到用户空间 */	
	put_user(KeyVal, buf);
	spin_unlock(&_spin_lock);
	keypress = 0;
	
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

static unsigned int KeyDevPoll(struct file *filp, struct poll_table_struct *pt)
{
	unsigned int mask = 0;
	
	poll_wait(filp,&wait_head,pt); //加入等待队列，但不会立刻休眠

	if(keypress)
		mask |= POLLIN | POLLRDNORM;
	return mask;
}

void timer_function(unsigned long time)
{
	/* 设置标志位，表明事件已经发生 */
	KeyVal = Desc->KeyNum;
	keypress = 1;
	wake_up_interruptible(&wait_head);
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

	init_timer(&my_timer);
	my_timer.function = timer_function ;
	my_timer.data      = 0;
	//my_timer.expires = msecs_to_jiffies(10)+ jiffies;
	return 0;
}

static void __exit KeyDevexit(void)
{
	device_unregister(KeyDevice);
	class_destroy(KeyDevClass);
	unregister_chrdev(devt, "keydev");
	del_timer_sync(&my_timer);
	
	printk("KeyDev Exit.\n");
}

module_init(KeyDevInit);
module_exit(KeyDevexit);

MODULE_LICENSE("GPL");

