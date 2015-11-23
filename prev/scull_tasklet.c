#include <asm/current.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/stat.h>
#include <linux/wait.h>

MODULE_LICENSE("Dual BSD/GPL");

static unsigned tdelay = 10;

module_param(tdelay, uint, S_IRUGO);

const char* scull_tasklet_filename = "jittasklet3";
const char* scull_tasklethi_filename = "jittasklethi3";

#define JIT_ASYNC_LOOPS 10

struct scull_tasklet_dev {
  bool use_hi_tasklet;
  struct tasklet_struct tasklet;
  char buf[128 * JIT_ASYNC_LOOPS];
  char* buf_head;
  size_t loop_count;
  unsigned long prev_jiffies;
  wait_queue_head_t wq;
  struct proc_dir_entry* proc_entry;
};

#define DEV_COUNT 2

struct scull_tasklet_dev scull_tasklet_devs[DEV_COUNT];

static int scull_tasklet_proc_open(struct inode* inode, struct file* filp);

static struct file_operations scull_tasklet_proc_ops = {
  .owner = THIS_MODULE,
  .open = scull_tasklet_proc_open,
  .read = seq_read,
  .llseek = seq_lseek,
  .release = seq_release,
};

static int scull_tasklet_setup_dev(struct scull_tasklet_dev devs[DEV_COUNT]) {
  int retval = 0;
  memset(devs, 0, sizeof(struct scull_tasklet_dev) * DEV_COUNT);

  devs[0].proc_entry = proc_create(scull_tasklet_filename, S_IRUGO, NULL, &scull_tasklet_proc_ops);
  if (devs[0].proc_entry == NULL) {
    retval = 1;
    goto out;
  }
  devs[0].use_hi_tasklet = false;

  devs[1].proc_entry = proc_create(scull_tasklethi_filename, S_IRUGO, NULL, &scull_tasklet_proc_ops);
  if (devs[1].proc_entry == NULL) {
    retval = 1;
    goto out_error_proc_create_dev1;
  }
  devs[1].use_hi_tasklet = true;
  return 0;

out_error_proc_create_dev1:
  proc_remove(devs[0].proc_entry);
  devs[0].proc_entry = NULL;
out:
  return retval;
}

static void scull_tasklet_teardown_dev(struct scull_tasklet_dev devs[DEV_COUNT]) {
  int i;
  for (i = 0; i < DEV_COUNT; ++i) {
    if (devs[i].proc_entry != NULL) {
      proc_remove(devs[i].proc_entry);
    }
  }
}

static int scull_tasklet_init(void) {
  int retval = 0;
  pr_alert("scull_tasklet_init\n");
  if (scull_tasklet_setup_dev(scull_tasklet_devs)) {
    retval = 1;
    goto out;
  }
out:
  return retval;
}

static void scull_tasklet_exit(void) {
  pr_alert("scull_tasklet_exit\n");
  scull_tasklet_teardown_dev(scull_tasklet_devs);
}

module_init(scull_tasklet_init);
module_exit(scull_tasklet_exit);

static void* scull_tasklet_seq_start(struct seq_file* m, loff_t* pos);
static void scull_tasklet_seq_stop(struct seq_file* m, void* v);
static void* scull_tasklet_seq_next(struct seq_file* m, void* v, loff_t* pos);
static int scull_tasklet_seq_show(struct seq_file* m, void* v);

static struct seq_operations seq_ops = {
  .start = scull_tasklet_seq_start,
  .stop = scull_tasklet_seq_stop,
  .next = scull_tasklet_seq_next,
  .show = scull_tasklet_seq_show,
};

static int scull_tasklet_proc_open(struct inode* inode, struct file* filp) {
  int retval = 0;
  void** p;

  pr_alert("scull_tasklet_proc_open\n");
  retval = seq_open(filp, &seq_ops);
  if (retval != 0) {
    goto out;
  }
  p = &((struct seq_file*)filp->private_data)->private;

  if (strcmp(filp->f_dentry->d_name.name, scull_tasklet_filename) == 0) {
    *p = &scull_tasklet_devs[0];
  } else {
    *p = &scull_tasklet_devs[1];
  }
out:
  return retval;
}

static void* scull_tasklet_seq_start(struct seq_file* m, loff_t* pos) {
  pr_alert("scull_tasklet_seq_start\n");
  if (*pos != 0) {
    return NULL;
  }
  return m->private;
}

static void scull_tasklet_seq_stop(struct seq_file* m, void* v) {
  pr_alert("scull_tasklet_seq_stop\n");
  m->private = NULL;
}

static void* scull_tasklet_seq_next(struct seq_file* m, void* v, loff_t* pos) {
  pr_alert("scull_tasklet_seq_next\n");
  ++*pos;
  return NULL;
}

static void jit_tasklet_fn(unsigned long data) {
  struct scull_tasklet_dev* dev = (struct scull_tasklet_dev*)data;

  dev->buf_head += snprintf(dev->buf_head, dev->buf + sizeof(dev->buf) - dev->buf_head,
                            "%12ld   %5ld   %5d   %5d   %5d   %s\n",
                            jiffies, jiffies - dev->prev_jiffies, in_interrupt() ? 1 : 0,
                            current->pid, smp_processor_id(), current->comm);

  if (--dev->loop_count) {
    dev->prev_jiffies = jiffies;
    if (dev->use_hi_tasklet) {
      tasklet_hi_schedule(&dev->tasklet);
    } else {
      tasklet_schedule(&dev->tasklet);
    }
  } else {
    wake_up_interruptible(&dev->wq);
  }
}

static int scull_tasklet_seq_show(struct seq_file* m, void* v) {
  int retval = 0;
  struct scull_tasklet_dev* dev = v;
  pr_alert("scull_tasklet_seq_show\n");
  seq_printf(m, "scull_tasklet_seq_show, v = %p\n", dev);

  init_waitqueue_head(&dev->wq);
  dev->buf_head = dev->buf;
  dev->loop_count = JIT_ASYNC_LOOPS;
  dev->prev_jiffies = jiffies;
  tasklet_init(&dev->tasklet, jit_tasklet_fn, (unsigned long)dev);
  if (dev->use_hi_tasklet) {
    tasklet_hi_schedule(&dev->tasklet);
  } else {
    tasklet_schedule(&dev->tasklet);
  }

  retval = wait_event_interruptible(dev->wq, dev->loop_count == 0);
  if (retval != 0) {
    goto out;
  }
  seq_printf(m, "%12s   %5s   %5s   %5s   %5s  %s\n", "time", "delta", "inirq", "pid", "cpu", "command");
  seq_write(m, dev->buf, strlen(dev->buf));

out:
  return retval;
}
