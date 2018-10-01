TARGET = hvt
REQUIRES = nova x86_64

LD_TEXT_ADDR = 0x80000000

LIBS = base-nova

SOLO5_DIR := /home/repo/mirage/solo5
HVT_DIR = $(SOLO5_DIR)/tenders/hvt

INC_DIR += $(PRG_DIR)
INC_DIR += $(HVT_DIR)
INC_DIR += $(SOLO5_DIR)/include/solo5

# Use elf.h from the libc port
LIBC_PORT_DIR := $(call select_from_ports,libc)
INC_DIR += $(LIBC_PORT_DIR)/include/libc

ifeq ($(filter-out $(SPECS),x86),)
  ifeq ($(filter-out $(SPECS),32bit),)
    INC_DIR += $(LIBC_PORT_DIR)/include/libc-i386
  endif # 32bit

  ifeq ($(filter-out $(SPECS),64bit),)
    INC_DIR += $(LIBC_PORT_DIR)/include/libc-amd64
  endif # 64bit
endif # x86

SRC_CC += component.cc
SRC_C  += hvt_cpu_x86_64.c

vpath %.c $(HVT_DIR)

CC_CXX_WARN_STRICT =
