#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/semaphore.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

MODULE_LICENSE("Dual BSD/GPL");

#define SCULL_IOC_MAGIC 'z'
enum {
  SCULL_IOC_NR_FIRST = 0x90,
  SCULL_IOC_NR_LAST,
};

unsigned scull_major = 88;
unsigned long pipe_buffer_size = 4096;

module_param(scull_major, uint, S_IRUGO);
module_param(pipe_buffer_size, ulong, S_IRUGO);

const unsigned scull_minor_start = 16;

unsigned scull_nr_devs = 1;

struct scull_pipe_dev {
  struct semaphore sem;
  char* buffer;
  uint64_t bufsize;
  char* read_head;
  char* write_head;
  wait_queue_head_t reader_wq, writer_wq;
  unsigned nr_reader, nr_writer;
  struct cdev cdev;
};

struct scull_pipe_dev scull_pipe_dev;
unsigned scull_minor = 16;

static int scull_pipe_setup_dev(struct scull_pipe_dev* dev, dev_t devno);
static void scull_pipe_teardown_dev(struct scull_pipe_dev* dev);
static int scull_pipe_setup_cdev(struct scull_pipe_dev* dev, dev_t devno);
static void scull_pipe_teardown_cdev(struct scull_pipe_dev* dev);

static int scull_pipe_open(struct inode* inode, struct file* filp);
static int scull_pipe_release(struct inode* inode, struct file* filp);
static ssize_t scull_pipe_read(struct file* filp, char __user* buf, size_t count, loff_t* f_pos);
static ssize_t scull_pipe_write(struct file* filp, const char __user* buf, size_t count, loff_t* f_pos);
static unsigned int scull_pipe_poll(struct file* filp, struct poll_table_struct* poll_table);

static int is_buffer_empty_and_has_no_writer(struct scull_pipe_dev* dev);

static struct file_operations scull_pipe_ops = {
  .owner = THIS_MODULE,
  .open = scull_pipe_open,
  .release = scull_pipe_release,
  .read = scull_pipe_read,
  .write = scull_pipe_write,
  .poll = scull_pipe_poll,
};

static int scull_pipe_init(void) {
  pr_alert("scull_pipe_init\n");
  if (scull_major != 0) {
    if (register_chrdev_region(MKDEV(scull_major, scull_minor_start), scull_nr_devs, "scull_pipe_device") != 0) {
      goto error_register_dev_t;
    }
  } else {
    dev_t dev;
    if (alloc_chrdev_region(&dev, scull_minor_start, scull_nr_devs, "scull_pipe_device") != 0) {
      goto error_register_dev_t;
    }
    scull_major = MAJOR(dev);
  }
  pr_alert("register/alloc chrdev_region major %d, minor %d-%d\n", scull_major, scull_minor_start, scull_minor_start + scull_nr_devs - 1);

  if (scull_pipe_setup_dev(&scull_pipe_dev, MKDEV(scull_major, scull_minor_start))) {
    goto error_scull_pipe_setup_dev;
  }

  return 0;

error_scull_pipe_setup_dev:
  unregister_chrdev_region(MKDEV(scull_major, scull_minor_start), scull_nr_devs);
error_register_dev_t:
  return 1;
}

static void scull_pipe_exit(void) {
  pr_alert("scull_pipe_exit\n");
  scull_pipe_teardown_dev(&scull_pipe_dev);
  unregister_chrdev_region(MKDEV(scull_major, scull_minor_start), scull_nr_devs);
}

static int scull_pipe_setup_dev(struct scull_pipe_dev* dev, dev_t devno) {
  int retval = 0;

  sema_init(&dev->sem, 1);
  dev->buffer = kmalloc(pipe_buffer_size, GFP_KERNEL);
  if (dev->buffer == NULL) {
    retval = -ENOMEM;
    dev->bufsize = 0;
  } else {
    dev->bufsize = pipe_buffer_size;
  }
  dev->read_head = dev->buffer;
  dev->write_head = dev->buffer;
  dev->nr_reader = 0;
  dev->nr_writer = 0;
  init_waitqueue_head(&dev->reader_wq);
  init_waitqueue_head(&dev->writer_wq);

  if (retval == 0) {
    retval = scull_pipe_setup_cdev(dev, devno);
  }
  return retval;
}

static void scull_pipe_teardown_dev(struct scull_pipe_dev* dev) {
  scull_pipe_teardown_cdev(dev);
  kfree(dev->buffer);
  dev->buffer = NULL;
  dev->read_head = dev->write_head = NULL;
  dev->bufsize = 0;
}

static int scull_pipe_setup_cdev(struct scull_pipe_dev* dev, dev_t devno) {
  int err;
  cdev_init(&dev->cdev, &scull_pipe_ops);
  dev->cdev.owner = THIS_MODULE;
  dev->cdev.ops = &scull_pipe_ops;
  err = cdev_add(&dev->cdev, devno, 1);
  if (err) {
    pr_alert("Error %d adding scull_pipe (%d, %d)\n", err, MAJOR(devno), MINOR(devno));
  } else {
    pr_alert("cdev_add (%d, %d) successfully\n", MAJOR(devno), MINOR(devno));
  }
  return err;
}

static void scull_pipe_teardown_cdev(struct scull_pipe_dev* dev) {
  cdev_del(&dev->cdev);
}

static int scull_pipe_open(struct inode* inode, struct file* filp) {
  struct scull_pipe_dev* dev;
  int retval = 0;
  pr_alert("scull_pipe_open\n");
  dev = container_of(inode->i_cdev, struct scull_pipe_dev, cdev);
  filp->private_data = dev;
  if (down_interruptible(&dev->sem)) {
    retval = -ERESTARTSYS;
    goto out;
  }

  if ((filp->f_flags & O_ACCMODE) == O_RDONLY) {
    dev->nr_reader++;
    // Wait for writer if there is no data and no writer.
    if (is_buffer_empty_and_has_no_writer(dev) && (filp->f_flags & O_NONBLOCK) == 0) {
      up(&dev->sem);
      if (wait_event_interruptible(dev->reader_wq, !is_buffer_empty_and_has_no_writer(dev))) {
        retval = -ERESTARTSYS;
        goto out;
      }
      if (down_interruptible(&dev->sem)) {
        retval = -ERESTARTSYS;
        goto out;
      }
    }
  } else if ((filp->f_flags & O_ACCMODE) == O_WRONLY) {
    dev->nr_writer++;
  } else {
    retval = -EINVAL;
    goto out_with_lock;
  }

out_with_lock:
  up(&dev->sem);
out:
  return retval;
}

static int scull_pipe_release(struct inode* inode, struct file* filp) {
  struct scull_pipe_dev* dev = filp->private_data;
  int retval = 0;
  pr_alert("scull_pipe_release\n");

  if (down_interruptible(&dev->sem)) {
    retval = -ERESTARTSYS;
    goto out;
  }

  if ((filp->f_flags & O_ACCMODE) == O_RDONLY) {
    dev->nr_reader--;
  } else if ((filp->f_flags & O_ACCMODE) == O_WRONLY) {
    dev->nr_writer--;
  }

  up(&dev->sem);
out:
  return retval;
}

static int is_buffer_empty(struct scull_pipe_dev* dev) {
  return dev->read_head == dev->write_head;
}

static int is_buffer_empty_and_has_writer(struct scull_pipe_dev* dev) {
  return dev->nr_writer != 0 && is_buffer_empty(dev);
}

static int is_buffer_empty_and_has_no_writer(struct scull_pipe_dev* dev) {
  return dev->nr_writer == 0 && is_buffer_empty(dev);
}

static ssize_t scull_pipe_read(struct file* filp, char __user* buf, size_t count, loff_t* f_pos) {
  struct scull_pipe_dev* dev = filp->private_data;
  ssize_t retval = 0;
  uint64_t available_count;
  size_t last_count, read_count;

  pr_alert("scull_pipe_read\n");

  if (down_interruptible(&dev->sem)) {
    retval = -ERESTARTSYS;
    goto out;
  }

  while (is_buffer_empty_and_has_writer(dev)) {
    up(&dev->sem);
    if (filp->f_flags & O_NONBLOCK) {
      retval =  -EAGAIN;
      goto out;
    }
    retval = wait_event_interruptible(dev->reader_wq, !is_buffer_empty_and_has_writer(dev));
    if (retval != 0) {
      retval = -ERESTARTSYS;
      goto out;
    }
    if (down_interruptible(&dev->sem)) {
      retval = -ERESTARTSYS;
      goto out;
    }
  }
  if (dev->read_head <= dev->write_head) {
    available_count = dev->write_head - dev->read_head;
  } else {
    available_count = dev->write_head + dev->bufsize - dev->read_head;
  }
  if (count > available_count) {
    count = available_count;
  }
  last_count = count;

  if (dev->read_head + last_count >= dev->buffer + dev->bufsize) {
    read_count = dev->buffer + dev->bufsize - dev->read_head;
    if (copy_to_user(buf, dev->read_head, read_count)) {
      retval = -EFAULT;
      goto out_with_lock;
    }
    last_count -= read_count;
    buf += read_count;
    dev->read_head = dev->buffer;
  }
  if (last_count > 0) {
    if (copy_to_user(buf, dev->read_head, last_count)) {
      retval = -EFAULT;
      goto out_with_lock;
    }
    dev->read_head += last_count;
  }

  wake_up_interruptible(&dev->writer_wq);
  retval = count;

out_with_lock:
  up(&dev->sem);
out:
  pr_alert("scull_pipe_read retval = %zd\n", retval);
  return retval;
}

static int is_buffer_full(struct scull_pipe_dev* dev) {
  int retval = 0;
  if (dev->read_head < dev->write_head) {
    if (dev->read_head == dev->buffer && dev->write_head == dev->buffer + dev->bufsize - 1) {
      retval = 1;
    }
  } else if (dev->write_head + 1 == dev->read_head) {
    retval = 1;
  }
  return retval;
}

static ssize_t scull_pipe_write(struct file* filp, const char __user* buf, size_t count, loff_t* f_pos) {
  struct scull_pipe_dev* dev = filp->private_data;
  ssize_t retval = 0;
  uint64_t available_space;
  size_t last_count, write_count;

  pr_alert("scull_pipe_write\n");

  if (down_interruptible(&dev->sem)) {
    retval = -ERESTARTSYS;
    goto out;
  }

  while (is_buffer_full(dev)) {
    up(&dev->sem);
    if (filp->f_flags & O_NONBLOCK) {
      retval = -EAGAIN;
      goto out;
    }
    if (wait_event_interruptible(dev->writer_wq, !is_buffer_full(dev))) {
      retval = -ERESTARTSYS;
      goto out;
    }
    if (down_interruptible(&dev->sem)) {
      retval = -ERESTARTSYS;
      goto out;
    }
  }

  pr_alert("scull_pipe_write (buffer = %p, read_head = %p, write_head = %p, bufsize = %llu\n",
           dev->buffer, dev->read_head, dev->write_head, dev->bufsize);

  if (dev->read_head <= dev->write_head) {
    available_space = dev->read_head + dev->bufsize - 1 - dev->write_head;
  } else {
    available_space = dev->read_head - 1 - dev->write_head;
  }
  if (count > available_space) {
    count = available_space;
  }
  last_count = count;
  pr_alert("scull_pipe_write last_count = %zu\n", last_count);

  if (dev->write_head + last_count >= dev->buffer + dev->bufsize) {
    write_count = dev->buffer + dev->bufsize - dev->write_head;
    if (copy_from_user(dev->write_head, buf, write_count)) {
      retval = -EFAULT;
      goto out_with_lock;
    }
    last_count -= write_count;
    buf += write_count;
    dev->write_head = dev->buffer;
  }
  if (last_count > 0) {
    if (copy_from_user(dev->write_head, buf, last_count)) {
      retval = -EFAULT;
      goto out_with_lock;
    }
    dev->write_head += last_count;
  }

  wake_up_interruptible(&dev->reader_wq);
  retval = count;

out_with_lock:
  up(&dev->sem);
out:
  pr_alert("scull_pipe_write retval = %zd\n", retval);
  return retval;
}

static unsigned int scull_pipe_poll(struct file* filp, struct poll_table_struct* poll_table) {
  struct scull_pipe_dev* dev = filp->private_data;
  unsigned int mask = 0;

  pr_alert("scull_pipe_poll\n");

  down(&dev->sem);
  poll_wait(filp, &dev->reader_wq, poll_table);
  poll_wait(filp, &dev->writer_wq, poll_table);
  if (!is_buffer_empty(dev)) {
    mask |= POLLIN | POLLRDNORM;
  }
  if (!is_buffer_full(dev)) {
    mask |= POLLOUT | POLLRDNORM;
  }
  if ((filp->f_flags & O_ACCMODE) == O_RDONLY && is_buffer_empty_and_has_no_writer(dev)) {
    mask |= POLLHUP;
  }

  up(&dev->sem);
  return mask;
}

module_init(scull_pipe_init);
module_exit(scull_pipe_exit);
