#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/usb.h>

MODULE_LICENSE("Dual BSD/GPL");

static int usb_notify_subscriber(struct notifier_block *self,
    unsigned long action, void *dev) {
  pr_info("usb_notify_subscriber\n");
  switch (action) {
    case USB_DEVICE_ADD:
      pr_info("usb_notify_subscriber:USB device added\n");
      break;
    case USB_DEVICE_REMOVE:
      pr_info("usb_notify_subscriber:USB device removed\n");
      break;
    case USB_BUS_ADD:
      pr_info("usb_notify_subscriber:USB Bus added\n");
      break;
    case USB_BUS_REMOVE:
      pr_info("usb_notify_subscriber:USB Bus removed\n");
      break;
  }
  return NOTIFY_OK;
}

static struct notifier_block usb_simple_nb = {
  .notifier_call = usb_notify_subscriber,
};

static int first_init(void) {
  pr_info("Init USB simple subscriber.\n");
  usb_register_notify(&usb_simple_nb);
	return 0;
}

static void first_exit(void) {
  usb_unregister_notify(&usb_simple_nb);
  pr_info("Remove USB simple subscriber.\n");
}

module_init(first_init);
module_exit(first_exit);
