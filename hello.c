#include <linux/init.h>
#include <linux/module.h>
#include <linux/sched.h>

MODULE_LICENSE("Dual BSD/GPL");

static int hello_init(void) {
  pr_alert("Hello, World!\n");
  pr_alert("In process \"%s\" (pid %d, tgid %d)\n", current->comm, current->pid, current->tgid);
  return 0;
}

static void hello_exit(void) {
  pr_alert("Goodbye, cruel world\n");
  pr_alert("In process \"%s\" (pid %d, tgid %d)\n", current->comm, current->pid, current->tgid);
}

module_init(hello_init);
module_exit(hello_exit);
