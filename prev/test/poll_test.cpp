#include <gtest/gtest.h>

#include <fcntl.h>
#include <sys/select.h>
#include <sys/time.h>

#include <thread>

const char* pipe_filename = "../scull_pipe_dev";

void write_pipe_fn(size_t write_count) {
  ASSERT_EQ(0, sleep(1));
  int pipe_fd = open(pipe_filename, O_WRONLY);
  ASSERT_NE(-1, pipe_fd);

  std::vector<int> buf(write_count);
  for (size_t i = 0; i < write_count; ++i) {
    buf[i] = i;
  }
  char* p = reinterpret_cast<char*>(buf.data());
  size_t last_bytes = buf.size() * sizeof(int);
  while (last_bytes > 0) {
    ssize_t write_bytes = TEMP_FAILURE_RETRY(write(pipe_fd, p, last_bytes));
    ASSERT_GT(write_bytes, 0);
    last_bytes -= write_bytes;
    p += write_bytes;
  }

  ASSERT_EQ(0, close(pipe_fd));
}

TEST(scull_pipe_dev, select) {
  const size_t test_count = 4096;
  std::thread thread(write_pipe_fn, test_count);

  int pipe_fd = open(pipe_filename, O_RDONLY);
  ASSERT_NE(-1, pipe_fd);

  std::vector<int> buf(test_count);
  size_t last_bytes = buf.size() * sizeof(int);
  char* p = reinterpret_cast<char*>(buf.data());
  while (last_bytes > 0) {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(pipe_fd, &readfds);
    ASSERT_EQ(1, select(pipe_fd + 1, &readfds, NULL, NULL, NULL));
    ASSERT_TRUE(FD_ISSET(pipe_fd, &readfds));

    ssize_t read_bytes = read(pipe_fd, p, last_bytes);
    ASSERT_GT(read_bytes, 0);
    last_bytes -= read_bytes;
    p += read_bytes;
  }
  for (size_t i = 0; i < sizeof(buf)/sizeof(buf[0]); ++i) {
    ASSERT_EQ(i, buf[i]);
  }

  fd_set readfds, exceptfds;
  FD_ZERO(&readfds);
  FD_ZERO(&exceptfds);
  FD_SET(pipe_fd, &readfds);
  FD_SET(pipe_fd, &exceptfds);
  ASSERT_EQ(1, select(pipe_fd + 1, &readfds, NULL, &exceptfds, NULL));
  ASSERT_TRUE(FD_ISSET(pipe_fd, &readfds));
  ASSERT_FALSE(FD_ISSET(pipe_fd, &exceptfds));
  char c;
  ASSERT_EQ(0, read(pipe_fd, &c, 1));

  ASSERT_EQ(0, close(pipe_fd));

  thread.join();
}
