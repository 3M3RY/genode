MIRROR_FROM_REP_DIR = \
	lib/import/import-gmp.mk \
	lib/mk/gmp.inc \
	lib/mk/gmp.mk \
	lib/mk/gmp-mpf.mk \
	lib/mk/gmp-mpq.mk \
	lib/mk/gmp-mpz.mk \
	lib/mk/spec/arm/gmp-mpn.mk \
	lib/mk/spec/x86_32/gmp-mpn.mk \
	lib/mk/spec/x86_64/gmp-mpn.mk \
	
content: $(MIRROR_FROM_REP_DIR) LICENSE include src/lib/gmp

$(MIRROR_FROM_REP_DIR):
	$(mirror_from_rep_dir)

PORT_DIR := $(call port_dir,$(REP_DIR)/ports/gmp)
PORT_SRC_DIR := $(PORT_DIR)/src/lib/gmp

include:
	mkdir -p $@
	cp -r $(PORT_DIR)/include/* $@/
	cp -r $(REP_DIR)/include/gmp/* $@/
	cp -r $(REP_DIR)/include/32bit/gmp/* $@/
	cp -r $(REP_DIR)/include/64bit/gmp/* $@/

src/lib/gmp:
	mkdir -p $@
	cp -r $(PORT_SRC_DIR)/* $@
	cp -r $(REP_DIR)/src/lib/gmp/* $@
	echo "LIBS = gmp" > $@/target.mk

LICENSE:
	cp $(PORT_SRC_DIR)/COPYING $@
