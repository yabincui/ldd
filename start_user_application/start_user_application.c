#include <linux/kmod.h>
#include <linux/module.h>

MODULE_LICENSE("Dual BSD/GPL");

static void call_logger(const char* message) {
	static char *argv[] = {"/usr/bin/logger", "msg", NULL};
	static char *envp[] = {
		"HOME=/",
		"TERM=linux",
		"PATH=/sbin:/bin:/usr/sbin:/usr/bin", NULL,
	};
	int result;
	argv[1] = (char*)message;
	result = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
	if (result != 0) {
		pr_alert("call_logger, result = %d\n", result);
	}
}

static int start_user_application_init(void) {
	pr_alert("Hello, start user application module!\n");
	call_logger("start_user_application_init");
	return 0;
}

static void start_user_application_exit(void) {
	pr_alert("Goodbye, start user application module!\n");
	call_logger("start_user_application_exit");
}

module_init(start_user_application_init);
module_exit(start_user_application_exit);
