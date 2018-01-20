#include <linux/init.h>   
#include <linux/module.h>   
#include <linux/kthread.h>   
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/completion.h>

static DECLARE_COMPLETION(comp);
static struct task_struct * _tsk;  
static struct task_struct * _tsk1;
static struct task_struct * _tsk2;

static int tc = 0;
 

static int thread_function(void *data)  
{  

    do {  
		printk(KERN_INFO "IN thread_function thread_function: %d times \n", tc);    
         	wait_for_completion(&comp);
		 
		printk(KERN_INFO "has been woke up !\n");
    }while(!kthread_should_stop());  
	
    		return tc;  
}   

static int thread_function_1(void *data)  
{  
    do {  
		printk(KERN_INFO "IN thread_function_1 thread_function: %d times\n", ++tc);
		if(tc == 10)
		{
			complete_all(&comp);
			tc = 0;
		}
		
		msleep_interruptible(1000);              

    }while(!kthread_should_stop());  
	
    return tc;  
}  

static int thread_function_2(void *data)  
{  
    do {  

		wait_for_completion(&comp);
		
		printk(KERN_INFO "third thread wakeup.\n");

    }while(!kthread_should_stop());  
	
    return tc;  
}  

static int hello_init(void)  
{  
    printk(KERN_INFO "Hello, world!\n");  
    _tsk = kthread_create(thread_function, NULL, "mythread"); 
	
    if (IS_ERR(_tsk)) {  
        printk(KERN_INFO "first create kthread failed!\n");  
    }  
    else {  
        printk(KERN_INFO "first create ktrhead ok!\n");  
	 wake_up_process(_tsk); //唤醒刚创建的线程
    }   
          _tsk1 = kthread_create(thread_function_1,NULL, "mythread2");
    if (IS_ERR(_tsk1)) {  
        printk(KERN_INFO "second create kthread failed!\n");  
    }  
    else {  
        printk(KERN_INFO "second create ktrhead ok!\n");  
	  wake_up_process(_tsk1);
    }  

	_tsk2 = kthread_create(thread_function_2,NULL, "mythread2");
    if (IS_ERR(_tsk2)) {  
        printk(KERN_INFO "third create kthread failed!\n");  
    }  
    else {  
        printk(KERN_INFO "third create ktrhead ok!\n");  
	  wake_up_process(_tsk2);
    }  
	
    return 0;  
}  
 
static void hello_exit(void)  
{  
	printk(KERN_INFO "Hello, exit!\n");  
	if (!IS_ERR(_tsk)){  
		int ret = kthread_stop(_tsk);  
		printk(KERN_INFO "First thread function has stopped ,return %d\n", ret);  
	}
	
	if(!IS_ERR(_tsk1))
	{
		   int ret = kthread_stop(_tsk1);
		   printk(KERN_INFO "Second thread function_1 has stopped ,return %d\n",ret);
	}
	
	if(!IS_ERR(_tsk2))
	{
		   int ret = kthread_stop(_tsk2);
		   printk(KERN_INFO "Second thread function_2 has stopped ,return %d\n",ret);
	}
}   

module_init(hello_init);  
module_exit(hello_exit);  
MODULE_LICENSE("GPL"); 

