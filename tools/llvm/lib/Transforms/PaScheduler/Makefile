# Makefile for hello pass

# Path to top level of LLVM hierarchy
LEVEL = ../../..

# Name of the library to build
LIBRARYNAME = PaSchedulerBB

# Make the shared library become a loadable module so the tools can 
# dlopen/dlsym on the resulting library.
LOADABLE_MODULE = 1

# Tell the build system which LLVM libraries your pass needs. You'll probably
# need at least LLVMSystem.a, LLVMSupport.a, LLVMCore.a but possibly several
# others too.
LLVMLIBS = #LLVMCore.a LLVMSupport.a LLVMSystem.a

# Include the makefile implementation stuff
include $(LEVEL)/Makefile.common

CXXFLAGS += -I/local/pouly/include
LDFLAGS += -L/local/pouly/lib -lpasched
