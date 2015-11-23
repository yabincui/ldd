#include <gtest/gtest.h>

#include <fcntl.h>
#include <linux/ioctl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define SCULL_IOC_MAGIC 'z'
enum {
  SCULL_IOC_NR_FIRST = 0x80,
  SCULL_IOC_NR_RESET_QUANTUM_QSET = SCULL_IOC_NR_FIRST,
  SCULL_IOC_NR_GET_QUANTUM,
  SCULL_IOC_NR_SET_QUANTUM,
  SCULL_IOC_NR_GET_QSET,
  SCULL_IOC_NR_SET_QSET,
  SCULL_IOC_NR_LAST,
};

#define SCULL_IOC_RESET_QUANTUM_QSET  _IO(SCULL_IOC_MAGIC, SCULL_IOC_NR_RESET_QUANTUM_QSET)
#define SCULL_IOC_GET_QUANTUM _IO(SCULL_IOC_MAGIC, SCULL_IOC_NR_GET_QUANTUM)
#define SCULL_IOC_SET_QUANTUM _IO(SCULL_IOC_MAGIC, SCULL_IOC_NR_SET_QUANTUM)
#define SCULL_IOC_GET_QSET    _IO(SCULL_IOC_MAGIC, SCULL_IOC_NR_GET_QSET)
#define SCULL_IOC_SET_QSET    _IO(SCULL_IOC_MAGIC, SCULL_IOC_NR_SET_QSET)

TEST(scull_dev, ioctl) {
  const char* filename = "../scull_dev0";
  int fd = open(filename, O_RDONLY);
  ASSERT_NE(-1, fd);
  ASSERT_EQ(0, ioctl(fd, SCULL_IOC_RESET_QUANTUM_QSET));
  unsigned original_quantum = ioctl(fd, SCULL_IOC_GET_QUANTUM);
  ASSERT_GT(original_quantum, 0);
  ASSERT_EQ(original_quantum, ioctl(fd, SCULL_IOC_SET_QUANTUM, 32));
  ASSERT_EQ(32, ioctl(fd, SCULL_IOC_GET_QUANTUM));

  unsigned original_qset = ioctl(fd, SCULL_IOC_GET_QSET);
  ASSERT_GT(original_qset, 0);
  ASSERT_EQ(original_qset, ioctl(fd, SCULL_IOC_SET_QSET, 64));
  ASSERT_EQ(64, ioctl(fd, SCULL_IOC_GET_QSET));

  ASSERT_EQ(0, ioctl(fd, SCULL_IOC_RESET_QUANTUM_QSET));
  ASSERT_EQ(original_quantum, ioctl(fd, SCULL_IOC_GET_QUANTUM));
  ASSERT_EQ(original_qset, ioctl(fd, SCULL_IOC_GET_QSET));

  ASSERT_EQ(0, close(fd));
}
