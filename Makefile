# Project Name
TARGET = altair

# Uncomment to use LGPL (like ReverbSc, etc.)
USE_DAISYSP_LGPL=1

# Compiler options
#OPT = -O1
OPT=-O3

#APP_TYPE = BOOT_SRAM

# Sources and Hothouse header files
CPP_SOURCES = altair.cpp ../hothouse.cpp ImpulseResponse/ImpulseResponse.cpp ImpulseResponse/dsp.cpp
C_INCLUDES = -I.. -I../../RTNeural -I../../RTNeural/modules/Eigen

# Library Locations
LIBDAISY_DIR = ../../libDaisy
DAISYSP_DIR = ../../DaisySP

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

CPPFLAGS += -DRTNEURAL_DEFAULT_ALIGNMENT=8 -DRTNEURAL_NO_DEBUG=1
#-ffast-math -flto -mfloat-abi=hard -mfpu=fpv5-sp-d16

# Global helpers
include ../Makefile
