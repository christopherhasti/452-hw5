#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("BSU CS 452 HW5 Scanner (Undergraduate)");

#ifndef DEVNAME
#define DEVNAME "Scanner"
#endif

/* Per-init() data */
typedef struct {
    dev_t devno;
    struct cdev cdev;
    char *default_seps;
    int num_default_seps;
} Device;

/* Per-open() data (Scanner instance) */
typedef struct {
    char *seps;
    int num_seps;
    
    char *data;
    int data_len;
    
    int data_pos;       /* Current scan position in data */
    int token_len;      /* Length of current token (-1 if seeking next) */
    int token_consumed; /* Bytes of current token sent to user */
    
    int expecting_seps; /* Flag set by ioctl */
} File;

static Device device;

/* Helper to check if a character is a separator. Works perfectly with NUL bytes. */
static int is_sep(char c, char *seps, int num_seps) {
    int i;
    for (i = 0; i < num_seps; i++) {
        if (c == seps[i]) return 1;
    }
    return 0;
}

static int scanner_open(struct inode *inode, struct file *filp) {
    File *file = kmalloc(sizeof(*file), GFP_KERNEL);
    if (!file) {
        printk(KERN_ERR "%s: kmalloc() failed in open\n", DEVNAME);
        return -ENOMEM;
    }
    
    file->num_seps = device.num_default_seps;
    file->seps = kmalloc(file->num_seps, GFP_KERNEL);
    if (!file->seps) {
        kfree(file);
        printk(KERN_ERR "%s: kmalloc() failed for seps in open\n", DEVNAME);
        return -ENOMEM;
    }
    memcpy(file->seps, device.default_seps, file->num_seps);
    
    file->data = NULL;
    file->data_len = 0;
    file->data_pos = 0;
    file->token_len = -1;
    file->token_consumed = 0;
    file->expecting_seps = 0;
    
    filp->private_data = file;
    return 0;
}

static int scanner_release(struct inode *inode, struct file *filp) {
    File *file = filp->private_data;
    if (file->seps) kfree(file->seps);
    if (file->data) kfree(file->data);
    kfree(file);
    return 0;
}

static ssize_t scanner_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos) {
    File *file = filp->private_data;
    int remaining;
    int to_copy;
    
    if (file->token_len == -1) {
        int end;
        
        /* Skip any leading or consecutive separators */
        while (file->data_pos < file->data_len && is_sep(file->data[file->data_pos], file->seps, file->num_seps)) {
            file->data_pos++;
        }
        
        if (file->data_pos >= file->data_len) {
            return -1; /* End of data */
        }
        
        /* Scan forward to find the end of the current token */
        end = file->data_pos;
        while (end < file->data_len && !is_sep(file->data[end], file->seps, file->num_seps)) {
            end++;
        }
        
        file->token_len = end - file->data_pos;
        file->token_consumed = 0;
    }
    
    remaining = file->token_len - file->token_consumed;
    
    if (remaining == 0) {
        file->data_pos += file->token_len; /* Advance data_pos past the completed token */
        file->token_len = -1;              /* Reset state for the next call */
        return 0;                          /* Return 0 to indicate End of Token */
    }
    
    to_copy = remaining < count ? remaining : count;
    
    if (copy_to_user(buf, file->data + file->data_pos + file->token_consumed, to_copy)) {
        printk(KERN_ERR "%s: copy_to_user() failed\n", DEVNAME);
        return -EFAULT;
    }
    
    file->token_consumed += to_copy;
    return to_copy;
}

static ssize_t scanner_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos) {
    File *file = filp->private_data;
    char *kbuf;
    
    if (count == 0) {
        if (file->expecting_seps) {
            if (file->seps) kfree(file->seps);
            file->seps = NULL;
            file->num_seps = 0;
            file->expecting_seps = 0;
        } else {
            if (file->data) kfree(file->data);
            file->data = NULL;
            file->data_len = 0;
            file->data_pos = 0;
            file->token_len = -1;
            file->token_consumed = 0;
        }
        return 0;
    }
    
    kbuf = kmalloc(count, GFP_KERNEL);
    if (!kbuf) {
        printk(KERN_ERR "%s: kmalloc() failed in write\n", DEVNAME);
        return -ENOMEM;
    }
    
    if (copy_from_user(kbuf, buf, count)) {
        kfree(kbuf);
        printk(KERN_ERR "%s: copy_from_user() failed in write\n", DEVNAME);
        return -EFAULT;
    }
    
    if (file->expecting_seps) {
        if (file->seps) kfree(file->seps);
        file->seps = kbuf;
        file->num_seps = count;
        file->expecting_seps = 0;
    } else {
        if (file->data) kfree(file->data);
        file->data = kbuf;
        file->data_len = count;
        
        /* Reset scanning state for the new sequence */
        file->data_pos = 0;
        file->token_len = -1;
        file->token_consumed = 0;
    }
    
    return count;
}

static long scanner_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
    File *file = filp->private_data;
    if (cmd == 0) {
        file->expecting_seps = 1;
        return 0;
    }
    return -EINVAL;
}

static struct file_operations ops = {
    .open = scanner_open,
    .release = scanner_release,
    .read = scanner_read,
    .write = scanner_write,
    .unlocked_ioctl = scanner_ioctl,
    .owner = THIS_MODULE
};

static int __init my_init(void) {
    int err;
    
    device.num_default_seps = 4;
    device.default_seps = kmalloc(device.num_default_seps, GFP_KERNEL);
    if (!device.default_seps) {
        printk(KERN_ERR "%s: kmalloc() failed in init\n", DEVNAME);
        return -ENOMEM;
    }
    
    /* Default separators as defined by assignment */
    device.default_seps[0] = ' ';
    device.default_seps[1] = '\t';
    device.default_seps[2] = '\n';
    device.default_seps[3] = ':';
    
    err = alloc_chrdev_region(&device.devno, 0, 1, DEVNAME);
    if (err < 0) {
        printk(KERN_ERR "%s: alloc_chrdev_region() failed\n", DEVNAME);
        kfree(device.default_seps);
        return err;
    }
    
    cdev_init(&device.cdev, &ops);
    device.cdev.owner = THIS_MODULE;
    
    err = cdev_add(&device.cdev, device.devno, 1);
    if (err) {
        printk(KERN_ERR "%s: cdev_add() failed\n", DEVNAME);
        unregister_chrdev_region(device.devno, 1);
        kfree(device.default_seps);
        return err;
    }
    
    printk(KERN_INFO "%s: init\n", DEVNAME);
    return 0;
}

static void __exit my_exit(void) {
    cdev_del(&device.cdev);
    unregister_chrdev_region(device.devno, 1);
    kfree(device.default_seps);
    printk(KERN_INFO "%s: exit\n", DEVNAME);
}

module_init(my_init);
module_exit(my_exit);