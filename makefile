# Text File
# Copyright (C) 2018 Alibaba-inc All rights reserved.
# AUTHOR:   qingfu.cqf
# FILE:     makefile
# COMMENT:  a generate makefile template
# CREATED:  2018-04-26 15:01:10
# MODIFIED: 2018-11-01 20:03:04

GTEST_DIRS=../3rd/googletest
GFLAG_DIRS=../3rd/gflags

CXXFLAGS := -Wall -fPIC -std=c++11 -MMD -MP -march=native
#-Werror -pedantic 
LDFLAGS  :=  \
			-fPIC  \
			-Wl,-rpath='$$ORIGIN/../../release/lib:$$ORIGIN/../../3rd/gflags/lib/:$$ORIGIN/../../3rd/googletest/lib' \
			-Wl,-rpath-link=../../release/lib/:../../release/lib/driver/:../../3rd/gflags/lib

#-Wl,-Bstatic  -Bdynamic \

# for libxxx.a only
DEP_LIBS := 

# -rpath=./:../
TARGETS  :=  framework_test
LIB_DIRS := ../../3rd/googletest/lib/ \
			../../3rd/gflags/lib \
				../../release/lib/
			
#/usr/lib64  \


LIBRARYS := stdc++ pthread m ccdk-pmd numa dl gflags gtest
INCDIRS  := ./include   \
		  ../../release/include/ \
			../../3rd/gflags/include/ \
			../../3rd/googletest/include/ \
		  ../../src/ \

SRC_DIR  :=  \
			./src/ \


SRC_FILE :=  \

MACROS := \



SRCEXTS := .c .cpp .cc

# don't modify follow lines , if you don't known
CC       := gcc
CXX      := g++

OBJECTS := $(addprefix ./build/, $(foreach sfx,$(SRCEXTS),$(patsubst %$(sfx),%.o,$(foreach d,$(SRC_DIR),$(wildcard $(addprefix $(d)/*,$(sfx)))))) $(foreach f,$(SRC_FILE),$(patsubst %$(suffix $(f)),%.o,$(f))))

INCLUDE := $(addprefix -I,$(INCDIRS))
LIB_ARGS := $(addprefix -L,$(LIB_DIRS)) $(addprefix -l,$(LIBRARYS))
MACRO_ARGS := $(addprefix -D,$(MACROS))



release: CXXFLAGS += -O2
release: ccdk-pmd all

debug: CXXFLAGS += -DDEBUG -g
debug: ccdk-pmd all 

all: build $(TARGETS) 

./build/%.o: %.c
	@mkdir -p $(@D)
	-@rm -rvf $(TARGETS)
	$(CC) $(CXXFLAGS) $(MACRO_ARGS) $(INCLUDE) -o $@ -c $<

./build/%.o: %.cpp
	@mkdir -p $(@D)
	-@rm -rvf $(TARGETS)
	$(CXX) $(CXXFLAGS) $(MACRO_ARGS) $(INCLUDE) -o $@ -c $<

./build/%.o: %.cc
	@mkdir -p $(@D)
	-@rm -rvf $(TARGETS)
	$(CXX) $(CXXFLAGS) $(MACRO_ARGS) $(INCLUDE) -o $@ -c $<

$(TARGETS): $(OBJECTS)
	-@mkdir -p $(@D)
	rm -rvf $@
	@echo "Build $@" 
	-@mkdir -p  ./release/include/
	@if [ "$(suffix $@)" == ".so" ]; then \
	echo "$(CXX) $(OBJECTS) $(CXXFLAGS) $(LIB_ARGS) $(INCLUDE) $(LDFLAGS)  -shared -o $@ "; \
	$(CXX) $(OBJECTS) $(CXXFLAGS) $(LIB_ARGS) $(INCLUDE) $(LDFLAGS)  -shared -o $@ ; \
elif [ "$(suffix $@)" == ".a" ]; then \
	mkdir  -p .tmp_ar; \
	for aa in $(DEP_LIBS); do \
		libname=$${aa%\.a} ; \
		libname=$${libname##*/} ; \
	  mkdir  -p .tmp_ar/$$libname; \
		cd ./.tmp_ar/$$libname; \
		ar -x ./../../$$aa ; \
		cd - ; \
	done ; \
	o_files="$$(find ./.tmp_ar/ -name "*.o"|xargs)"; \
	echo "ar -rcs -o $@ $(OBJECTS) $$o_files"; \
	ar -r -o $@ $(OBJECTS) $$o_files; \
	/bin/rm -fr ./.tmp_ar ; \
else \
	echo "$(CXX) $(CXXFLAGS) $(OBJECTS) $(LIB_ARGS) $(INCLUDE) $(LDFLAGS) -o $@ "; \
	$(CXX) $(CXXFLAGS) $(OBJECTS) $(LIB_ARGS) $(INCLUDE) $(LDFLAGS) -o $@ ; \
fi

.PHONY: all build clean debug release  ccdk-pmd 
	
build:
	@mkdir -p build

ccdk-pmd: 
	make -C ../../

driver_setup:
	-@rmmod nsa_dma
	-@modprobe uio
	-@insmod ../../3rd/dpdk/lib/modules/3.10.0-693.2.2.el7.x86_64/extra/dpdk/igb_uio.ko
	-@../../3rd/dpdk/dpdk/usertools/dpdk-devbind.py -b igb_uio 81:00.0

driver_remove:
	-@../../3rd/dpdk/dpdk/usertools/dpdk-devbind.py -u 81:00.0
	-@rmmod igb_uio
	-@rmmod uio
	-@insmod ./nsa_dma.ko


	

clean:
	@rm -rvf ./build/*
	@rm -rvf $(TARGETS)

