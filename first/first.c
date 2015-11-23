#include <linux/module.h>

MODULE_LICENSE("Dual BSD/GPL");

static int first_init(void) {
	pr_alert("Hello, first module!\n");
	return 0;
}

static void first_exit(void) {
	pr_alert("Goodbye, first module!\n");
}

module_init(first_init);
module_exit(first_exit);
