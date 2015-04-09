#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/sched.h>
#include <linux/types.h>

MODULE_LICENSE("Dual BSD/GPL");

unsigned scull_major = 0;

module_param(scull_major, uint, S_IRUGO);

unsigned scull_nr_devs = 1;

static int hello_init(void) {
  dev_t dev;

  pr_alert("Hello, World!\n");
  pr_alert("In process \"%s\" (pid %d, tgid %d)\n", current->comm, current->pid, current->tgid);

  if (scull_major != 0) {
    if (register_chrdev_region(MKDEV(scull_major, 0), scull_nr_devs, "scull_device") != 0){
      goto error_register_dev_t;
    }
  } else {
    if (alloc_chrdev_region(&dev, 0, scull_nr_devs, "scull_device") != 0) {
      goto error_register_dev_t;
    }
    scull_major = MAJOR(dev);
  }

  return 0;
error_register_dev_t:
  return 1;
}

static void hello_exit(void) {
  pr_alert("Goodbye, cruel world\n");
  pr_alert("In process \"%s\" (pid %d, tgid %d)\n", current->comm, current->pid, current->tgid);

  unregister_chrdev_region(MKDEV(scull_major, 0), scull_nr_devs);
}

module_init(hello_init);
module_exit(hello_exit);
