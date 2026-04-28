all:
	@echo "Val in submake: " $(shell ./bmake -r -f ../../test_v10.mk)
