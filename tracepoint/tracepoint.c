#include <linux/module.h>
#include <linux/kthread.h>

#define CREATE_TRACE_POINTS
#include "tracepoint_trace.h"

MODULE_LICENSE("Dual BSD/GPL");

static void tracepoint_thread_func(void) {
  static unsigned long count;
  set_current_state(TASK_UNINTERRUPTIBLE);
  schedule_timeout(HZ);
  pr_info("hello! %lu\n", count);
  trace_my_tracepoint(jiffies, count);
  count++;
}

static int tracepoint_thread(void *arg) {
  while (!kthread_should_stop())
    tracepoint_thread_func();
  return 0;
}

static struct task_struct *task;

static int tracepoint_init(void) {
	pr_alert("Hello, tracepoint module!\n");
  task = kthread_run(tracepoint_thread, NULL, "tracepoint-thread");
  if (IS_ERR(task))
    return -1;
	return 0;
}

static void tracepoint_exit(void) {
  kthread_stop(task);
	pr_alert("Goodbye, tracepoint module!\n");
}

module_init(tracepoint_init);
module_exit(tracepoint_exit);
