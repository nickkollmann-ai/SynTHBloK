# ------------------------------------------------------------
# SynTHBloK (NoiZee ToyZ) - VCV Rack 2 plugin Makefile
#
#   make             build the plugin
#   make devinstall  build + copy into your Rack plugins folder (fast testing, no source copied)
#   make dist        build a stripped release package: SynTHBloK-<version>-<os>-<cpu>.vcvplugin
#   make install     dist + copy the .vcvplugin into Rack's plugins folder (Rack's own target)
#   make clean       remove build output
#
# Set RACK_DIR to your Rack SDK, e.g.   export RACK_DIR=~/Rack-SDK
# ------------------------------------------------------------

# If RACK_DIR is not defined when calling the Makefile, default to two directories above
RACK_DIR ?= ../..

# FLAGS will be passed to both the C and C++ compiler
FLAGS +=
CFLAGS +=
CXXFLAGS +=

# Linker flags
LDFLAGS +=

# Windows: put winpthread inside plugin.dll. Newer MSYS2 compilers make the plugin need
# nanosleep64, which the older libwinpthread-1.dll bundled with Rack 2.6.x doesn't have,
# so Rack fails to load the plugin with "code 127".
ifeq ($(OS),Windows_NT)
LDFLAGS += -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic
endif

# Add .cpp files to the build
SOURCES += $(wildcard src/*.cpp)

# Files added to the release package by `make dist`.
# The compiled plugin and plugin.json are added automatically (the binary is stripped for you).
DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)
DISTRIBUTABLES += $(wildcard presets)
DISTRIBUTABLES += $(wildcard doc)

# Include the Rack plugin Makefile framework.
# It provides: all, clean, dist, install, and PLUGINS_DIR for your OS.
include $(RACK_DIR)/plugin.mk

# ------------------------------------------------------------
# DEV INSTALL: copy the freshly built plugin straight into Rack's plugins folder.
# PLUGINS_DIR comes from plugin.mk, so this works on Windows, macOS and Linux
# (on Windows: %LOCALAPPDATA%/Rack2/plugins-win-x64). Only the binary, plugin.json and res/
# are copied. The source code is NOT copied into the installed plugin.
# ------------------------------------------------------------
devinstall: all
	mkdir -p "$(PLUGINS_DIR)/$(SLUG)"
	cp $(TARGET) "$(PLUGINS_DIR)/$(SLUG)/"
	cp plugin.json "$(PLUGINS_DIR)/$(SLUG)/"
	cp -r res "$(PLUGINS_DIR)/$(SLUG)/"

.PHONY: devinstall
