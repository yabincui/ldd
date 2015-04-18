
CC = g++
CFLAGS = -std=c++11 -Wall -I$(GTEST_DIR)/include
LDFLAGS = -L$(GTEST_DIR)/lib -lgtest_main -lgtest -lpthread

all: scull_unit_test

scull_unit_test: ioctl_test.o poll_test.o
	$(CC) -o $@ $^ $(LDFLAGS)

%.o : %.cpp
	$(CC) $(CFLAGS) -c -o $@ $<
