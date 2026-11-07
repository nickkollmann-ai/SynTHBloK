# Shared by every single plugin's Makefile (singles/<slug>/Makefile sets SOURCES and RES, then includes this).
# The module sources live in the repo's src/ and are found through VPATH, so each single compiles its own copy
# into its own build/ folder. Its res/ files and the licence are copied in from the repo root before linking.

RACK_DIR ?= ../../..
SHARED := ../../src
VPATH += $(SHARED)
# (no -I$(SHARED): Rack's rack.hpp includes its own plugin.hpp, which ours would shadow)

# development builds only (build_and_install.bat sets it): the PATCH GEN pictures menu item
ifdef SYNTHBLOK_DEV
FLAGS += -DSYNTHBLOK_DEV
endif

# Windows: put winpthread inside plugin.dll (see the root Makefile)
ifeq ($(OS),Windows_NT)
LDFLAGS += -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic
endif

DISTRIBUTABLES += res LICENSE.txt

include $(RACK_DIR)/plugin.mk

prep:
	@for f in $(RES); do mkdir -p "$$(dirname "$$f")"; cp -p "../../$$f" "$$f"; done
	@cp -p ../../LICENSE.txt LICENSE.txt

$(TARGET): | prep
dist: prep

devinstall: all
	mkdir -p "$(PLUGINS_DIR)/$(SLUG)"
	cp $(TARGET) "$(PLUGINS_DIR)/$(SLUG)/"
	cp plugin.json "$(PLUGINS_DIR)/$(SLUG)/"
	cp -r res "$(PLUGINS_DIR)/$(SLUG)/"

.PHONY: prep devinstall
