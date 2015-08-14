##===- lib/Transforms/Contech/Makefile -----------------------*- Makefile -*-===##
#
#                     The LLVM Compiler Infrastructure
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
##===----------------------------------------------------------------------===##

LEVEL = ../../..
LIBRARYNAME = LLVMDynamicAnalysis
LOADABLE_MODULE = 1
USEDLIBS = Task.a 

ifneq ($(REQUIRES_RTTI), 1)
ifneq ($(REQUIRES_EH), 1)
EXPORTED_SYMBOL_FILE = $(PROJ_SRC_DIR)/DynamicAnalysis.exports
endif
endif

include $(LEVEL)/Makefile.common

LIB = $(LIB)
