#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/time.h>

MODULE_LICENSE("Dual BSD/GPL");

const char* delay_by_sched_filename = "jitschedule";
const char* delay_by_waitqueue_filename = "jitwaitqueue";

enum delay_method {
  DELAY_BY_SCHED,
  DELAY_BY_WAITQUEUE,
  DELAY_METHOD_COUNT,
};

#define DEV_COUNT DELAY_METHOD_COUNT

struct scull_delay_dev {
  enum delay_method delay_method;
  struct proc_dir_entry* proc_entry;
};


struct scull_delay_dev scull_delay_devs[DEV_COUNT];

static int scull_delay_proc_open(struct inode* inode, struct file* filp);

static struct file_operations scull_delay_proc_ops = {
  .owner = THIS_MODULE,
  .open = scull_delay_proc_open,
  .read = seq_read,
  .llseek = seq_lseek,
  .release = seq_release,
};

static int scull_delay_setup_devs(struct scull_delay_dev devs[DEV_COUNT]) {
  int retval = 0;
  memset(devs, 0, sizeof(struct scull_delay_dev) * DEV_COUNT);
  devs[0].proc_entry = proc_create(delay_by_sched_filename, S_IRUGO, NULL, &scull_delay_proc_ops);
  if (devs[0].proc_entry == NULL) {
    retval = 1;
    goto out;
  }
  devs[0].delay_method = DELAY_BY_SCHED;

  devs[1].proc_entry = proc_create(delay_by_waitqueue_filename, S_IRUGO, NULL, &scull_delay_proc_ops);
  if (devs[1].proc_entry == NULL) {
    retval = 1;
    goto out_create_dev1_error;
  }
  devs[1].delay_method = DELAY_BY_WAITQUEUE;

  return 0;

out_create_dev1_error:
  proc_remove(devs[0].proc_entry);
  devs[0].proc_entry = NULL;
out:
  return retval;
}

static void scull_delay_teardown_devs(struct scull_delay_dev devs[DEV_COUNT]) {
  int i;
  for (i = 0; i < DEV_COUNT; ++i) {
    if (devs[i].proc_entry != NULL) {
      proc_remove(devs[i].proc_entry);
    }
  }
}

static int scull_delay_init(void) {
  int retval = 0;
  struct timeval tv;
  pr_alert("scull_delay_init\n");
  pr_alert("module install time %llu\n", get_jiffies_64());
  do_gettimeofday(&tv);
  pr_alert("gettimeofday (%lds %ldus)\n", tv.tv_sec, tv.tv_usec);
  pr_alert("HZ = %d\n", HZ);

  if (scull_delay_setup_devs(scull_delay_devs)) {
    retval = 1;
    goto out;
  }

out:
  return retval;
}

static void scull_delay_exit(void) {
  struct timespec ts;

  scull_delay_teardown_devs(scull_delay_devs);

  pr_alert("scull_delay_exit\n");
  pr_alert("module uninstall time %llu\n", get_jiffies_64());
  ts = current_kernel_time();
  pr_alert("current_kernel_time (%lds %ldns)\n", ts.tv_sec, ts.tv_nsec);
}

static void* scull_delay_seq_start(struct seq_file* m, loff_t* pos);
static void scull_delay_seq_stop(struct seq_file* m, void* v);
static void* scull_delay_seq_next(struct seq_file* m, void* v, loff_t* pos);
static int scull_delay_seq_show(struct seq_file* m, void* v);

static struct seq_operations seq_ops = {
  .start = scull_delay_seq_start,
  .stop = scull_delay_seq_stop,
  .next = scull_delay_seq_next,
  .show = scull_delay_seq_show,
};

static int scull_delay_proc_open(struct inode* inode, struct file* filp) {
  int retval = 0;
  struct scull_delay_dev** p;
  pr_alert("scull_delay_proc_open for %s\n", filp->f_dentry->d_name.name);

  retval = seq_open(filp, &seq_ops);
  if (retval != 0) {
    goto out;
  }
  p = (struct scull_delay_dev**)&((struct seq_file*)(filp->private_data))->private;

  if (strcmp(filp->f_dentry->d_name.name, delay_by_sched_filename) == 0) {
    *p = &scull_delay_devs[0];
  } else if (strcmp(filp->f_dentry->d_name.name, delay_by_waitqueue_filename) == 0) {
    *p = &scull_delay_devs[1];
  } else {
    *p = NULL;
  }
out:
  return retval;
}

static void* scull_delay_seq_start(struct seq_file* m, loff_t* pos) {
  pr_alert("scull_delay_seq_start, *pos = %lld\n", *pos);
  if (*pos >= 10) {
    return NULL;
  }
  return m->private;
}

static void scull_delay_seq_stop(struct seq_file* m, void* v) {
  struct scull_delay_dev** p = (struct scull_delay_dev**)&(m->private);
  pr_alert("scull_delay_seq_stop\n");
  *p = NULL;
}

static void* scull_delay_seq_next(struct seq_file* m, void* v, loff_t* pos) {
  pr_alert("scull_delay_seq_next, *pos = %lld\n", *pos);
  if (*pos == 10) {
    return NULL;
  }
  ++*pos;
  return v;
}

static int delay_by_schedule(long delay) {
  unsigned long j1 = jiffies + delay;
  while (time_before(jiffies, j1)) {
    schedule();
  }
  return 0;
}

static int delay_by_waitqueue(long delay) {
  wait_queue_head_t wait;
  init_waitqueue_head(&wait);
  return wait_event_interruptible_timeout(wait, 0, delay);
}

static int scull_delay_seq_show(struct seq_file* m, void* v) {
  struct scull_delay_dev* dev = v;
  int retval = 0;

  pr_alert("scull_delay_seq_show, v = %p, &scull_delay_devs[0] = %p, jiffies = %lu\n",
           v, &scull_delay_devs[0], jiffies);
  seq_printf(m, "%lu ", jiffies);

  if (dev->delay_method == DELAY_BY_SCHED) {
    retval = delay_by_schedule(HZ);
  } else if (dev->delay_method == DELAY_BY_WAITQUEUE) {
    retval = delay_by_waitqueue(HZ);
  }

  seq_printf(m, "%lu\n", jiffies);
  return retval;
}

module_init(scull_delay_init);
module_exit(scull_delay_exit);
