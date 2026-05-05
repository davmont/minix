VAR = outer
all:
	@echo "MAKEFLAGS in test: $${MAKEFLAGS}"
	@$(MAKE) -r -f ../../test_v_sub.mk
