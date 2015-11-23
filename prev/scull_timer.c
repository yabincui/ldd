#include <asm/current.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/stat.h>
#include <linux/timer.h>
#include <linux/wait.h>

MODULE_LICENSE("Dual BSD/GPL");

static unsigned tdelay = 10;

module_param(tdelay, uint, S_IRUGO);

const char* scull_timer_filename = "jittimer";

#define JIT_ASYNC_LOOPS 10

struct scull_timer_dev {
  struct timer_list timer;
  char buf[128 * JIT_ASYNC_LOOPS];
  char* buf_head;
  size_t loop_count;
  unsigned long prev_jiffies;
  wait_queue_head_t wq;
  struct proc_dir_entry* proc_entry;
};

struct scull_timer_dev scull_timer_dev;

static int scull_timer_proc_open(struct inode* inode, struct file* filp);

static struct file_operations scull_timer_proc_ops = {
  .owner = THIS_MODULE,
  .open = scull_timer_proc_open,
  .read = seq_read,
  .llseek = seq_lseek,
  .release = seq_release,
};

static int scull_timer_setup_dev(struct scull_timer_dev* dev) {
  int retval = 0;
  memset(dev, 0, sizeof(struct scull_timer_dev));
  dev->proc_entry = proc_create(scull_timer_filename, S_IRUGO, NULL, &scull_timer_proc_ops);
  if (dev->proc_entry == NULL) {
    retval = 1;
    goto out;
  }
out:
  return retval;
}

static void scull_timer_teardown_dev(struct scull_timer_dev* dev) {
  if (dev->proc_entry != NULL) {
    proc_remove(dev->proc_entry);
  }
}

static int scull_timer_init(void) {
  int retval = 0;
  pr_alert("scull_timer_init\n");
  if (scull_timer_setup_dev(&scull_timer_dev)) {
    retval = 1;
    goto out;
  }
out:
  return retval;
}

static void scull_timer_exit(void) {
  pr_alert("scull_timer_exit\n");
  scull_timer_teardown_dev(&scull_timer_dev);
}

module_init(scull_timer_init);
module_exit(scull_timer_exit);

static void* scull_timer_seq_start(struct seq_file* m, loff_t* pos);
static void scull_timer_seq_stop(struct seq_file* m, void* v);
static void* scull_timer_seq_next(struct seq_file* m, void* v, loff_t* pos);
static int scull_timer_seq_show(struct seq_file* m, void* v);

static struct seq_operations seq_ops = {
  .start = scull_timer_seq_start,
  .stop = scull_timer_seq_stop,
  .next = scull_timer_seq_next,
  .show = scull_timer_seq_show,
};

static int scull_timer_proc_open(struct inode* inode, struct file* filp) {
  pr_alert("scull_timer_proc_open\n");
  return seq_open(filp, &seq_ops);
}

static void* scull_timer_seq_start(struct seq_file* m, loff_t* pos) {
  pr_alert("scull_timer_seq_start\n");
  if (*pos != 0) {
    return NULL;
  }
  return &scull_timer_dev;
}

static void scull_timer_seq_stop(struct seq_file* m, void* v) {
  pr_alert("scull_timer_seq_stop\n");
}

static void* scull_timer_seq_next(struct seq_file* m, void* v, loff_t* pos) {
  pr_alert("scull_timer_seq_next\n");
  ++*pos;
  return NULL;
}

static void jit_timer_fn(unsigned long data) {
  struct scull_timer_dev* dev = (struct scull_timer_dev*)data;

  dev->buf_head += snprintf(dev->buf_head, dev->buf + sizeof(dev->buf) - dev->buf_head,
                            "%12ld   %5ld   %5d   %5d   %5d   %s\n",
                            jiffies, jiffies - dev->prev_jiffies, in_interrupt() ? 1 : 0,
                            current->pid, smp_processor_id(), current->comm);

  if (--dev->loop_count) {
    dev->prev_jiffies = jiffies;
    dev->timer.expires = jiffies + tdelay;
    add_timer(&dev->timer);
  } else {
    wake_up_interruptible(&dev->wq);
  }
}

static int scull_timer_seq_show(struct seq_file* m, void* v) {
  int retval = 0;
  struct scull_timer_dev* dev = v;
  pr_alert("scull_timer_seq_show\n");
  seq_printf(m, "scull_timer_seq_show, v = %p\n", dev);

  init_waitqueue_head(&dev->wq);
  dev->buf_head = dev->buf;
  dev->loop_count = JIT_ASYNC_LOOPS;
  dev->prev_jiffies = jiffies;
  init_timer(&dev->timer);
  dev->timer.data = (unsigned long)dev;
  dev->timer.function = jit_timer_fn;
  dev->timer.expires = jiffies + tdelay;
  add_timer(&dev->timer);

  retval = wait_event_interruptible(dev->wq, dev->loop_count == 0);
  if (retval != 0) {
    goto out;
  }
  seq_printf(m, "%12s   %5s   %5s   %5s   %5s  %s\n", "time", "delta", "inirq", "pid", "cpu", "command");
  seq_write(m, dev->buf, strlen(dev->buf));

out:
  return retval;
}
