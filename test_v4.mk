VAR=value
all:
	@echo "MAKEFLAGS=$${MAKEFLAGS}"
	@$(MAKE) -r -f ../../test_v5.mk
