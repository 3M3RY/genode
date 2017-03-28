content: include mk/spec lib LICENSE

include:
	cp -r $(REP_DIR)/include $@

LIB_MK_FILES := base.mk ld.mk ldso-startup.mk

lib:
	mkdir -p lib/mk lib/symbols
	cp $(addprefix $(REP_DIR)/lib/mk/,$(LIB_MK_FILES)) lib/mk/
	cp $(REP_DIR)/lib/symbols/ld lib/symbols/
	touch lib/mk/config.mk

SPECS := x86_32 x86_64 32bit 64bit

mk/spec:
	mkdir -p $@
	cp $(foreach spec,$(SPECS),$(REP_DIR)/mk/spec/$(spec).mk) $@

LICENSE:
	cp $(GENODE_DIR)/LICENSE $@

