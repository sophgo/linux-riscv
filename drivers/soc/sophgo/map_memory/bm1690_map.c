#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/device/class.h>
#include <linux/device.h>

#define DEVICE_NAME "map_mem"
#define IOCTL_SET_PHYS_ADDR _IOW('p', 1, unsigned long)

static int major;
static struct class *cls;
static void __iomem *mapped_memory;

static struct map_user_memory {
	uint64_t pa;
	uint64_t size;
	uint64_t type;
} user_memory;

static int device_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int device_release(struct inode *inode, struct file *file)
{
	return 0;
}

static long device_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	if (copy_from_user(&user_memory, (void __user *)arg, sizeof(user_memory))) {
		return -EFAULT;
	}
	return 0;
}

static int device_mmap(struct file *file, struct vm_area_struct *vma)
{
	unsigned long size = vma->vm_end - vma->vm_start;

	if (user_memory.type == 1) {
		pr_err("map device memory\n");
		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	} else if (user_memory.type == 2) {
		pr_err("map writecombine memory\n");
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	}

	if (remap_pfn_range(vma,
			    vma->vm_start,
			    vma->vm_pgoff,
			    size,
			    vma->vm_page_prot)) {
		return -EAGAIN;
	}

	pr_err("map 0x%lx, size:0x%lx\n", vma->vm_pgoff, size);

	return 0;
}

static struct file_operations fops = {
	.owner = THIS_MODULE,
	.open = device_open,
	.release = device_release,
	.unlocked_ioctl = device_ioctl,
	.mmap = device_mmap,
};

static int __init phys_mem_init(void)
{
	major = register_chrdev(0, DEVICE_NAME, &fops);
	if (major < 0) {
		printk(KERN_ALERT "Failed to register character device\n");
		return major;
	}

	cls = class_create(DEVICE_NAME);
	device_create(cls, NULL, MKDEV(major, 0), NULL, DEVICE_NAME);
	pr_err("phys_mem device registered with major number %d\n", major);

	return 0;
}

static void __exit phys_mem_exit(void)
{
	device_destroy(cls, MKDEV(major, 0));
	class_destroy(cls);
	unregister_chrdev(major, DEVICE_NAME);
	if (mapped_memory) {
		iounmap(mapped_memory);
	}
	printk(KERN_INFO "phys_mem device unregistered\n");
}

module_init(phys_mem_init);
module_exit(phys_mem_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("tingzhu.wang@sophgo.com");
MODULE_DESCRIPTION("a simple device driver for mapping physical memory to user space");
