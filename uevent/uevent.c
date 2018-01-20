#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/slab.h>

static struct kobject *parent = NULL;
static struct kobject *child = NULL;
static struct kset  *c_kset = NULL;
static unsigned int flags = 1;

static ssize_t demo_show(struct kobject *kobj, struct attribute *attr, char *buf)
{
	//count += sprintf(&buf[count],"%lu\n",flags);

	printk("flag = %d\n",flags);
	return 0;	
}

static ssize_t demo_store(struct kobject *kobj, struct attribute *attr, const char *buf, size_t size)
{
	char tmp_buf[10];
	long tmp_flag;
	
	strncpy(tmp_buf,buf,size);
	tmp_flag = simple_strtol(tmp_buf, NULL, 10);
	flags = tmp_flag;
	
	switch(tmp_flag)
	{
		case 0:
		printk("add.\n");
		kobject_uevent(child,KOBJ_ADD);
		break;
		
		case 1:
		printk("remove.\n");
		kobject_uevent(child,KOBJ_REMOVE);
		break;
		
		case 2:
		printk("change.\n");
		kobject_uevent(child,KOBJ_CHANGE);
		break;
		
		case 3:
		printk("move.\n");
		kobject_uevent(child,KOBJ_MOVE);
		break;
		
		case 4:
		printk("online.\n");
		kobject_uevent(child,KOBJ_ONLINE);
		break;
		
		case 5:
		printk("offline.\n");
		kobject_uevent(child,KOBJ_OFFLINE);
		break;
	}
	

	return size;
}

static struct sysfs_ops attr_ops = {
	.show = demo_show,
	.store = demo_store,
};

static struct kobj_type demo_ktype = {
	.sysfs_ops = &attr_ops,
};

static struct attribute cld_attr = {
	.name = "child_attr",
	.mode = S_IRUGO | S_IWUSR,
};

static int demo_init(void)
{
#if 0
	parent = kobject_create_and_add("demo_parent",NULL);
	if(IS_ERR(parent))
	{
		return -1;
	}

#endif

	c_kset = kset_create_and_add("c_kset",NULL,NULL);
	if(IS_ERR(c_kset))
	{
		return -1;
	}

	child = kzalloc(sizeof(struct kobject),GFP_KERNEL);
	if(IS_ERR(child))
	{
		return -1;
	}

	child->kset = c_kset;
	kobject_init_and_add(child,&demo_ktype,NULL,"child");

	if(sysfs_create_file(child,&cld_attr)) 
	{
		printk("create file error.\n");
		return -1;
	}
	
	return 0;
}

static void demo_exit(void)
{
	sysfs_remove_file(child,&cld_attr);
	//kobject_del(parent);
	kobject_del(child);
	kset_unregister(c_kset);
}

module_init(demo_init);
module_exit(demo_exit);

MODULE_LICENSE("GPL");
