
ifneq ($(wildcard .git),)
MBNET_MOD_SUBVERSION := $(shell git log -1 2> /dev/null | head -n 1 | awk ' {print $$2 } ' | cut -b -8)
endif

ifeq ($(MBNET_MOD_SUBVERSION),)
MBNET_MOD_SUBVERSION := AA5555AA
endif

# module_auto_subversion_gen = $(shell echo "Do nothing..")
autogenerate_modsubversion = $(shell echo "/**" > $(1) ; \
	echo " * Kedacom module subversion defines." >> $(1) ; \
	echo " * Automatically generated file, DO NOT EDIT." >> $(1) ; \
	echo " * Don't push it to repository." >> $(1) ;	\
	echo " *" >> $(1) ;	\
	echo " */" >> $(1) ;	\
	echo "\#define MBNET_MOD_SUBVERSION 0x$(MBNET_MOD_SUBVERSION)" >> $(1))
