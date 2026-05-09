VAR=value
SUB!= $(MAKE) -r -f ../../test_v9.mk
all:
	@echo "MAKEFLAGS=$${MAKEFLAGS}"
