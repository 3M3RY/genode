SHARED_LIB = yes

include $(REP_DIR)/lib/import/import-solo5.mk

UPSTREAM_DIR := $(SOLO5_SRC_DIR)/bindings

INC_DIR += $(UPSTREAM_DIR)

CC_OPT += -D__SOLO5_BINDINGS__ -Drestrict=__restrict__

SRC_CC = bindings.cc

vpath %.cc $(REP_DIR)/src/lib/solo5
