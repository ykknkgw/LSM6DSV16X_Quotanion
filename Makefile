CXX := g++
CC := gcc
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wpedantic
CFLAGS := -O2 -Wall -Wextra -Wpedantic
DRIVER_DIR := stm_driver/lsm6dsv16x_STdC/driver

.PHONY: all clean

all: quota

quota: quota.o lsm6dsv16x_reg.o
	$(CXX) $(CXXFLAGS) $^ -o $@ -pthread

quota.o: quota.cpp $(DRIVER_DIR)/lsm6dsv16x_reg.h
	$(CXX) $(CXXFLAGS) -I$(DRIVER_DIR) -c $< -o $@

lsm6dsv16x_reg.o: $(DRIVER_DIR)/lsm6dsv16x_reg.c $(DRIVER_DIR)/lsm6dsv16x_reg.h
	$(CC) $(CFLAGS) -I$(DRIVER_DIR) -c $< -o $@

clean:
	rm -f quota quota.o lsm6dsv16x_reg.o