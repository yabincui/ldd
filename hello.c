#include <linux/cdev.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/semaphore.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#include "scull.h"

MODULE_LICENSE("Dual BSD/GPL");

#define SCULL_IOC_MAGIC 'z'
enum {
  SCULL_IOC_NR_FIRST = 0x80,
  SCULL_IOC_NR_RESET_QUANTUM_QSET = SCULL_IOC_NR_FIRST,
  SCULL_IOC_NR_GET_QUANTUM,
  SCULL_IOC_NR_SET_QUANTUM,
  SCULL_IOC_NR_GET_QSET,
  SCULL_IOC_NR_SET_QSET,
  SCULL_IOC_NR_LAST,
};

#define SCULL_IOC_RESET_QUANTUM_QSET  _IO(SCULL_IOC_MAGIC, SCULL_IOC_NR_RESET_QUANTUM_QSET)
#define SCULL_IOC_GET_QUANTUM _IO(SCULL_IOC_MAGIC, SCULL_IOC_NR_GET_QUANTUM)
#define SCULL_IOC_SET_QUANTUM _IO(SCULL_IOC_MAGIC, SCULL_IOC_NR_SET_QUANTUM)
#define SCULL_IOC_GET_QSET    _IO(SCULL_IOC_MAGIC, SCULL_IOC_NR_GET_QSET)
#define SCULL_IOC_SET_QSET    _IO(SCULL_IOC_MAGIC, SCULL_IOC_NR_SET_QSET)

#define SCULL_QUANTUM   1024
#define SCULL_QSET      1024

unsigned scull_major = 88;
unsigned scull_qset = SCULL_QSET;
unsigned scull_quantum = SCULL_QUANTUM;

module_param(scull_major, uint, S_IRUGO);
module_param(scull_qset, uint, S_IRUGO);
module_param(scull_quantum, uint, S_IRUGO);

unsigned scull_nr_devs = 1;


struct scull_dev {
  struct semaphore sem;
  unsigned qset;
  unsigned quantum;
  uint64_t size;
  struct scull_qset* data;
  struct cdev cdev;
  struct proc_dir_entry* proc_entry;
};

struct scull_dev scull_dev;

static int scull_open(struct inode* inode, struct file* filp);
static int scull_release(struct inode* inode, struct file* filp);
static ssize_t scull_read(struct file* filp, char __user* buf, size_t count, loff_t* f_pos);
static ssize_t scull_write(struct file* filp, const char __user* buf, size_t count, loff_t* f_pos);
static loff_t scull_llseek(struct file* filp, loff_t offset, int whence);
static long scull_ioctl(struct file* filp, unsigned int cmd, unsigned long arg);

static struct file_operations scull_ops = {
  .owner = THIS_MODULE,
  .open = scull_open,
  .release = scull_release,
  .read = scull_read,
  .write = scull_write,
  .llseek = scull_llseek,
  .unlocked_ioctl = scull_ioctl,
};

static int scull_proc_open(struct inode* inode, struct file* filp);

static struct file_operations scull_proc_ops = {
  .owner = THIS_MODULE,
  .open = scull_proc_open,
  .read = seq_read,
  .llseek = seq_lseek,
  .release = seq_release,
};

static int scull_setup_cdev(struct scull_dev* dev, dev_t devno) {
  int err;
  cdev_init(&dev->cdev, &scull_ops);
  dev->cdev.owner = THIS_MODULE;
  dev->cdev.ops = &scull_ops;
  err = cdev_add(&dev->cdev, devno, 1);
  if (err) {
    pr_notice("Error %d adding scull (%d, %d)\n", err, MAJOR(devno), MINOR(devno));
  } else {
    pr_alert("cdev_add (%d, %d) successfully\n", MAJOR(devno), MINOR(devno));
  }
  return err;
}

static void scull_teardown_cdev(struct scull_dev* dev) {
  cdev_del(&dev->cdev);
}

static int scull_setup_proc_file(struct scull_dev* dev) {
  dev->proc_entry = proc_create("scull_device", S_IRUGO, NULL, &scull_proc_ops);
  return dev->proc_entry != NULL ? 0 : 1;
}

static void scull_teardown_proc_file(struct scull_dev* dev) {
  proc_remove(dev->proc_entry);
}

static int hello_init(void) {
  pr_alert("Hello, World!\n");
  pr_alert("In process \"%s\" (pid %d, tgid %d)\n", current->comm, current->pid, current->tgid);

  if (scull_major != 0) {
    if (register_chrdev_region(MKDEV(scull_major, 0), scull_nr_devs, "scull_device") != 0){
      goto error_register_dev_t;
    }
  } else {
    dev_t dev;
    if (alloc_chrdev_region(&dev, 0, scull_nr_devs, "scull_device") != 0) {
      goto error_register_dev_t;
    }
    scull_major = MAJOR(dev);
  }
  pr_alert("register/alloc chrdev_region major %d, minor 0-%d\n", scull_major, scull_nr_devs - 1);

  sema_init(&scull_dev.sem, 1);
  scull_dev.quantum = scull_quantum;
  scull_dev.qset = scull_qset;
  scull_dev.size = 0;
  scull_dev.data = NULL;
  if (scull_setup_cdev(&scull_dev, MKDEV(scull_major, 0)) != 0) {
    goto error_scull_setup_cdev;
  }

  if (scull_setup_proc_file(&scull_dev) != 0) {
    goto error_scull_setup_proc_file;
  }

  return 0;
error_scull_setup_proc_file:
  scull_teardown_cdev(&scull_dev);
error_scull_setup_cdev:
  unregister_chrdev_region(MKDEV(scull_major, 0), scull_nr_devs);
error_register_dev_t:
  return 1;
}

static int scull_trim(struct scull_dev* dev) {
  unsigned qset;
  struct scull_qset* dptr, *next;
  if (down_interruptible(&dev->sem)) {
    return -ERESTARTSYS;
  }
  qset = dev->qset;
  for (dptr = dev->data; dptr != NULL; dptr = next) {
    unsigned i;
    if (dptr->data) {
      for (i = 0; i < qset; ++i) {
        kfree(dptr->data[i]);
      }
      kfree(dptr->data);
    }
    next = dptr->next;
    kfree(dptr);
  }
  dev->size = 0;
  dev->quantum = scull_quantum;
  dev->qset = scull_qset;
  dev->data = NULL;

  up(&dev->sem);
  return 0;
}

static void hello_exit(void) {
  pr_alert("Goodbye, cruel world\n");
  pr_alert("In process \"%s\" (pid %d, tgid %d)\n", current->comm, current->pid, current->tgid);
  scull_trim(&scull_dev);
  scull_teardown_proc_file(&scull_dev);
  scull_teardown_cdev(&scull_dev);
  unregister_chrdev_region(MKDEV(scull_major, 0), scull_nr_devs);
}

static int scull_open(struct inode* inode, struct file* filp) {
  struct scull_dev* dev;
  pr_alert("scull_open\n");
  dev = container_of(inode->i_cdev, struct scull_dev, cdev);
  filp->private_data = dev;
  if ((filp->f_flags & O_ACCMODE) == O_WRONLY) {
    return scull_trim(dev);
  }
  return 0;
}

static int scull_release(struct inode* inode, struct file* filp) {
  pr_alert("scull_release\n");
  return 0;
}

static ssize_t scull_read(struct file* filp, char __user* buf, size_t count, loff_t* f_pos) {
  struct scull_dev* dev = filp->private_data;
  struct scull_qset* dptr;
  unsigned quantum;
  unsigned qset;
  uint64_t itemsize;
  uint64_t last_pos = *f_pos;
  size_t last_count;
  int retval = 0;
  pr_alert("scull_read, size = %llu\n", dev->size);

  if (down_interruptible(&dev->sem)) {
    return -ERESTARTSYS;
  }
  quantum = dev->quantum;
  qset = dev->qset;
  itemsize = (uint64_t)quantum * qset;

  for (dptr = dev->data; dptr != NULL; dptr = dptr->next) {
    if (last_pos < itemsize) {
      break;
    }
    last_pos -= itemsize;
  }

  if (count > dev->size - *f_pos) {
    count = dev->size - *f_pos;
  }
  last_count = count;
  pr_alert("last_count = %zu\n", last_count);
  while (last_count != 0) {
    void** data;
    unsigned quantum_id;
    char* p;
    unsigned copy_count;

    if (dptr == NULL) {
      break;
    }
    data = dptr->data;
    quantum_id = last_pos / quantum;
    if (data == NULL || data[quantum_id] == NULL) {
      break;
    }
    p = (char*)data[quantum_id] + last_pos % quantum;
    copy_count = quantum - last_pos % quantum;
    if (copy_count > last_count) {
      copy_count = last_count;
    }
    if (copy_to_user(buf, p, copy_count) != 0) {
      retval = -EFAULT;
      goto out;
    }
    last_count -= copy_count;
    last_pos += copy_count;
    buf += copy_count;
    if (last_count != 0 && last_pos >= itemsize) {
      dptr = dptr->next;
      last_pos -= itemsize;
    }
  }

  retval = count - last_count;
  *f_pos += retval;

  pr_alert("scull_read, count = %zu, ret_val = %d, *f_pos = %lld\n",
           count, retval, *f_pos);

out:
  up(&dev->sem);
  return retval;
}

static loff_t scull_llseek(struct file* filp, loff_t offset, int whence) {
  struct scull_dev* dev = filp->private_data;
  uint64_t size;
  loff_t new_pos;
  loff_t retval = 0;

  if (down_interruptible(&dev->sem)) {
    return -ERESTARTSYS;
  }
  size = dev->size;

  if (whence == 0) {
    new_pos = offset;
  } else if (whence == 1) {
    new_pos = size + offset;
  } else if (whence == 2) {
    new_pos = filp->f_pos + offset;
  } else {
    retval = -EINVAL;
    goto out;
  }

  if (new_pos < 0 || new_pos > size) {
    retval = -EINVAL;
    goto out;
  }
  filp->f_pos = new_pos;
  retval = new_pos;
out:
  up(&dev->sem);
  return retval;
}
static ssize_t scull_write(struct file* filp, const char __user* buf, size_t count, loff_t* f_pos) {
  struct scull_dev* dev = filp->private_data;
  struct scull_qset* dptr, *prev_qset;
  unsigned quantum;
  unsigned qset;
  uint64_t itemsize;
  uint64_t last_pos = *f_pos;
  size_t last_count;
  int retval = 0;

  if (down_interruptible(&dev->sem)) {
    return -ERESTARTSYS;
  }
  quantum = dev->quantum;
  qset = dev->qset;
  itemsize = (uint64_t)quantum * qset;

  pr_alert("scull_write\n");

  prev_qset = NULL;
  for (dptr = dev->data; dptr != NULL; dptr = dptr->next) {
    if (last_pos < itemsize) {
      break;
    }
    last_pos -= itemsize;
    prev_qset = dptr;
  }

  last_count = count;
  while (last_count != 0) {
    void** data;
    unsigned quantum_id;
    char* p;
    unsigned copy_count;
    if (dptr == NULL) {
      dptr = kmalloc(sizeof(struct scull_qset), GFP_KERNEL);
      if (dptr == NULL) {
        retval = -ENOMEM;
        goto out;
      }
      dptr->next = NULL;
      dptr->data = NULL;
      if (prev_qset != NULL) {
        prev_qset->next = dptr;
      } else {
        dev->data = dptr;
      }
    }
    data = dptr->data;
    if (data == NULL) {
      data = kmalloc(qset * sizeof(void*), GFP_KERNEL);
      if (data == NULL) {
        retval = -ENOMEM;
        goto out;
      }
      memset(data, 0, qset * sizeof(void*));
      dptr->data = data;
    }
    quantum_id = last_pos / quantum;
    if (data[quantum_id] == NULL) {
      data[quantum_id] = kmalloc(quantum, GFP_KERNEL);
      if (data[quantum_id] == NULL) {
        retval = -ENOMEM;
        goto out;
      }
      memset(data[quantum_id], 0, sizeof(quantum));
    }
    p = (char*)data[quantum_id] + last_pos % quantum;
    copy_count = quantum - last_pos % quantum;
    if (copy_count > last_count) {
      copy_count = last_count;
    }
    if (copy_from_user(p, buf, copy_count) != 0) {
      retval = -EFAULT;
      goto out;
    }
    last_count -= copy_count;
    last_pos += copy_count;
    buf += copy_count;
    if (last_count != 0 && last_pos >= itemsize) {
      prev_qset = dptr;
      dptr = dptr->next;
      last_pos -= itemsize;
    }
  }

  retval = count - last_count;
  *f_pos += retval;
  if (*f_pos > dev->size) {
    dev->size = *f_pos;
  }
  pr_alert("scull_write, count = %zu, retval = %d, *f_pos = %lld, size = %llu\n",
           count, retval, *f_pos, dev->size);

out:
  up(&dev->sem);
  return retval;
}

static long scull_ioctl(struct file* filp, unsigned int cmd, unsigned long arg) {
  long retval = 0;

  if (_IOC_TYPE(cmd) != SCULL_IOC_MAGIC ||
      !(_IOC_NR(cmd) >= SCULL_IOC_NR_FIRST && _IOC_NR(cmd) < SCULL_IOC_NR_LAST)) {
    return -ENOTTY;
  }
  switch (cmd) {
    case SCULL_IOC_RESET_QUANTUM_QSET:
      scull_quantum = SCULL_QUANTUM;
      scull_qset = SCULL_QSET;
      break;
    case SCULL_IOC_GET_QUANTUM:
      retval = scull_quantum;
      break;
    case SCULL_IOC_SET_QUANTUM:
      retval = scull_quantum;
      scull_quantum = arg;
      break;
    case SCULL_IOC_GET_QSET:
      retval = scull_qset;
      break;
    case SCULL_IOC_SET_QSET:
      retval = scull_qset;
      scull_qset = arg;
      break;
    default:
      retval = -ENOTTY;
  }
  return retval;
}

static int scull_proc_open(struct inode* inode, struct file* filp);
static void* scull_seq_start(struct seq_file* m, loff_t* pos);
static void scull_seq_stop(struct seq_file* m, void* v);
static void* scull_seq_next(struct seq_file* m, void* v, loff_t* pos);
static int scull_seq_show(struct seq_file* m, void* v);

static struct seq_operations seq_ops = {
  .start = scull_seq_start,
  .stop = scull_seq_stop,
  .next = scull_seq_next,
  .show = scull_seq_show,
};

static int scull_proc_open(struct inode* inode, struct file* filp) {
  return seq_open(filp, &seq_ops);
}

static void* scull_seq_start(struct seq_file* m, loff_t* pos) {
  if (*pos > 0) {
    return NULL;
  }
  return &scull_dev;
}

static void scull_seq_stop(struct seq_file* m, void* v) {

}

static void* scull_seq_next(struct seq_file* m, void* v, loff_t* pos) {
  ++*pos;
  return NULL;
}

static int scull_seq_show(struct seq_file* m, void* v) {
  struct scull_dev* dev = v;
  struct scull_qset* dptr;
  unsigned i;

  if (down_interruptible(&dev->sem)) {
    return -ERESTARTSYS;
  }

  seq_printf(m, "Device (%d,%d): qset %u, quantum %u, size %llu\n",
             MAJOR(dev->cdev.dev), MINOR(dev->cdev.dev), dev->qset,
             dev->quantum, dev->size);
  for (dptr = dev->data; dptr != NULL; dptr = dptr->next) {
    seq_printf(m, "  item at %p, qset at %p\n", dptr, dptr->data);
    // Dump only the least item.
    if (dptr->data && dptr->next == NULL) {
      for (i = 0; i < dev->qset; ++i) {
        seq_printf(m, "    %4u: %8p\n", i, dptr->data[i]);
      }
    }
  }
  up(&dev->sem);
  return 0;
}

module_init(hello_init);
module_exit(hello_exit);
