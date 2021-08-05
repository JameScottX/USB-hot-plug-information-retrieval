/*
 * @Author: Junwen Cui
 * @Date: 2021-07-26 16:50:53 
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/errno.h>
#include <linux/uaccess.h>

#include <linux/fb.h>
#include <linux/notifier.h>
#include <linux/proc_fs.h>
#include <linux/syscore_ops.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/ktime.h>
#include <linux/time.h>
#include <linux/mutex.h>

#define LOGI(...)	(pr_info(__VA_ARGS__))
#define LOGE(...)	(pr_err(__VA_ARGS__))

#define MESSAGE_BUFFER_SIZE 512

struct usb_message_t {

	signed long long kernel_time;			/* 8 bytes */
	// struct	timespec64 timeval_utc;			/* 16 bytes	*/
	char _flag; 							/* 0表示拔出 1表示插入 */
	// 这里为了结构体数据空间对齐
	char name[31];							/* 定义一定长度的名字 */
};

struct usb_monitor_t {
	// 初始化信息缓存
    struct	usb_message_t message[MESSAGE_BUFFER_SIZE];
    int		usb_message_count;
	int		usb_message_index_read;
    int		usb_message_index_write;
	// 定义等待队列头
    wait_queue_head_t	usb_monitor_queue;
	// 启用互斥锁
    struct	mutex usb_monitor_mutex;
	// 初始的标志
	char * flag;
};

static struct usb_monitor_t *monitor;
static char *TAG = "MONITOR";

static ssize_t usb_monitor_read(struct file *filp, char __user *buf,
				size_t size, loff_t *ppos)
{
	int index;
	size_t message_size = sizeof(struct usb_message_t);

	if (size < message_size) {
		LOGE("%s:read size is smaller than message size!\n", TAG);
		return -EINVAL;
	}
	
	// 在循环队列没有数据时，将此进程挂起 
    wait_event_interruptible(monitor->usb_monitor_queue,
		monitor->usb_message_count > 0);

	// 互斥锁，保护数据 
    mutex_lock(&monitor->usb_monitor_mutex);

	// 循环队列有数据时，进行数据拷贝，拷贝到buf地址下
    if (monitor->usb_message_count > 0) {
		index = monitor->usb_message_index_read;
		// 内核空间与用户空间内存不能直接互访，使用copy_to_user，将数据拷贝到用户空间
		// 若从用户空间拷贝到内核空间 copy_from_user, 非常的形象 
		if (copy_to_user(buf, &monitor->message[index], message_size)) {
			// 出错后不能忘记 解锁 
			mutex_unlock(&monitor->usb_monitor_mutex);
			return -EFAULT;
		}	

		printk(KERN_INFO "### Read out %s, left num is  %d\n",monitor->message[index].name,
		monitor->usb_message_count);

		// 循环队列 读取地址增加 
		monitor->usb_message_index_read++;
		if (monitor->usb_message_index_read >= MESSAGE_BUFFER_SIZE)
			monitor->usb_message_index_read = 0;
		// 队列数据减少 
		monitor->usb_message_count--;
	}
	// 对应的解锁 
	mutex_unlock(&monitor->usb_monitor_mutex);

	return message_size;
}

// 定义字符设备驱动结构 
static const struct file_operations usb_monitor_fops = {
	.owner = THIS_MODULE,
	.read = usb_monitor_read  // 指定读函数
	// 这里不需要写函数，所以也就取消了
};

int write_in_message(char status,struct usb_device *usb_dev){

	int index;
	index = monitor->usb_message_index_write;
	monitor->message[index].kernel_time = ktime_to_ns(ktime_get());

	// 这里需要非常的注意
	// 有些设备是没有设备名称的（比如 Arduino UNO），usb_dev->product 就是空指针，直接操作拷贝会导致系统死机
	if(usb_dev->product ==NULL){
		memcpy(monitor->message[index].name,"NULL",4 );
		printk("### write_in_message GET NULL \n");
	}else{
	// 这里很简单的拷贝一段字符串 
		printk("### write_in_message %ld\n", strlen(usb_dev->product));
		memcpy(monitor->message[index].name,usb_dev->product,strlen(usb_dev->product) );
	}
	// USB状态记录 
	monitor->message[index]._flag = status;
	// ktime_get_ts64(&monitor->message[index].timeval_utc);
	if (monitor->usb_message_count < MESSAGE_BUFFER_SIZE)
		monitor->usb_message_count++;
	// 环形队列中写地址增加，超出回0
	monitor->usb_message_index_write++;
	if (monitor->usb_message_index_write >= MESSAGE_BUFFER_SIZE)
		monitor->usb_message_index_write = 0;

	return index;
}

// USB设备的通知回调函数
static int usb_notify(struct notifier_block *self, unsigned long action, void *dev) { 

    struct usb_device *usb_dev = (struct usb_device*)dev;
    int index;

	// 互斥锁，保护monitor中的数据
	mutex_lock(&monitor->usb_monitor_mutex);

    switch (action) {
	// 状态判断 
    case USB_DEVICE_ADD: 
		// USB设备插入时进行添加
		index = write_in_message(1,usb_dev);
        printk(KERN_INFO "### The add device name is %s %d\n", monitor->message[index].name,
		monitor->usb_message_count);
		// 唤醒中断，注意这里已经进行加锁，在此线程结束后，usb_monitor_read数据才能真正被拷贝
		wake_up_interruptible(&monitor->usb_monitor_queue);

        break;

    case USB_DEVICE_REMOVE: 

		index = write_in_message(0,usb_dev);
		printk(KERN_INFO "### The remove device name is %s %d\n", monitor->message[index].name,
		monitor->usb_message_count);
		// 唤醒中断，注意这里已经进行加锁，在此线程结束后，usb_monitor_read数据才能真正被拷贝
		wake_up_interruptible(&monitor->usb_monitor_queue);

        // printk(KERN_INFO "USB device removed %s \n",usb_dev->product); 
        break; 
    case USB_BUS_ADD: 
        // printk(KERN_INFO "USB Bus added \n"); 
        break; 
    case USB_BUS_REMOVE: 
        // printk(KERN_INFO "USB Bus removed \n"); 
        break;
	} 

	mutex_unlock(&monitor->usb_monitor_mutex);

    return NOTIFY_OK; 
} 

static struct notifier_block usb_nb = { 
    .notifier_call = usb_notify, // 指定USB通知回调函数 
}; 

static int init_module_(void) { 

	// 向内核申请空间
	monitor = kzalloc(sizeof(struct usb_monitor_t), GFP_KERNEL);

	if (!monitor) {
		LOGE("%s:failed to kzalloc\n", TAG);
		return -ENOMEM;
	}
	// 初始化环形队列读写地址和大小 
	monitor->usb_message_count = 0;
	monitor->usb_message_index_read = 0;
	monitor->usb_message_index_write = 0;
	monitor->flag = "init_module_ is inited !\n";
	// 在/proc 下创建虚拟文件，用于 用户和内核交互，这里只有读的要求 
	proc_create("usb_monitor", 0644, NULL, &usb_monitor_fops);
	
	// 初始化等待队列，这里的作用为了让usb_monitor_read 没有数据的时候挂起，不让用户频繁调用
	init_waitqueue_head(&monitor->usb_monitor_queue);
    printk(KERN_INFO "Init USB hook.\n"); 
	// 注册USB状态改变通知
    usb_register_notify(&usb_nb); 
    return 0; 
}

static void cleanup_module_(void) { 

	// 这里需要将之前创建的虚拟文件删除 
	remove_proc_entry("usb_monitor", NULL); 
	// 删除USB状态改变通知
    usb_unregister_notify(&usb_nb); 
    printk(KERN_INFO "Remove USB hook\n");
	// 释放空间 
	kfree(monitor);
} 


module_init(init_module_);
module_exit(cleanup_module_);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("jun_wencui@126.com");

