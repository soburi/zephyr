# Copyright 2024 The Zephyr Authors
# SPDX-License-Identifier: Apache-2.0

.PHONY: all bindings embed clean

WASI_SDK ?= /opt/wasi-sdk
WIT_BINDGEN ?= wit-bindgen
OD ?= od
TR ?= tr
SED ?= sed
AWK ?= awk

CLANG := $(WASI_SDK)/bin/clang
WIT_WORLD := blink
WIT_FILE := zephyr-led.wit
BINDINGS_DIR := wit-bindgen
WASM_BINARY := test.wasm
HEADER := ../src/test_wasm.h

WIT_BINDING_SRCS := $(BINDINGS_DIR)/blink.c $(BINDINGS_DIR)/blink_component_type.o
WIT_BINDING_HDRS := $(BINDINGS_DIR)/blink.h
WIT_BINDINGS := $(WIT_BINDING_SRCS) $(WIT_BINDING_HDRS)

CLANG_FLAGS := -O3 -I$(BINDINGS_DIR)
CLANG_LDFLAGS := \
	-z stack-size=4096 -Wl,--initial-memory=65536 \
	-Wl,--export=main -Wl,--export=__main_argc_argv \
	-Wl,--export=__data_end -Wl,--export=__heap_base \
	-Wl,--strip-all,--no-entry \
	-Wl,--allow-undefined \
	-nostdlib

all: $(WASM_BINARY)

bindings: $(WIT_BINDINGS)

$(BINDINGS_DIR):
	mkdir -p $@

$(WIT_BINDINGS): $(WIT_FILE) | $(BINDINGS_DIR)
	$(WIT_BINDGEN) c --out-dir $(BINDINGS_DIR) --world $(WIT_WORLD) $(WIT_FILE)

$(WASM_BINARY): main.c $(WIT_BINDINGS)
	$(CLANG) $(CLANG_FLAGS) -o $@ main.c $(WIT_BINDING_SRCS) $(CLANG_LDFLAGS)

embed: $(HEADER)

$(HEADER): $(WASM_BINARY)
	mkdir -p $(dir $(HEADER))
	$(OD) -An -v -tx1 $< | $(TR) -s ' ' '\n' | $(SED) '/^$$/d' | \
	$(AWK) 'BEGIN {print "unsigned char __aligned(4) wasm_test_file[] = {"} \
	{bytes[NR]=$$1} \
	END {for (i=1; i<=NR; ++i) { \
	        if ((i-1)%12==0) printf("    "); \
	        printf("0x%s", bytes[i]); \
	        if (i!=NR) { \
	                if (i%12==0) printf(",\\n"); \
	                else printf(", "); \
	        } else { \
	                printf("\\n"); \
	        } \
	} print("};");}' > $@

clean:
	rm -f $(WASM_BINARY)
	rm -rf $(BINDINGS_DIR)
