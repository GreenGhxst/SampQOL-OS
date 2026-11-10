# fm1mix: LD_PRELOAD shim that mixes the open FM1 DX7 engine into the
# MPC Sample app. `make` builds the aarch64 library for the MPC Sample,
# `make test` builds host and aarch64 test binaries and runs them.
CROSS   ?= aarch64-linux-gnu-
ARM_CC  ?= $(CROSS)gcc
HOST_CC ?= cc
QEMU    ?= qemu-aarch64
SYSROOT ?= /usr/aarch64-linux-gnu

ENG        = engine/fm1_open
ENGINE_SRC = $(addprefix $(ENG)/web/synth/, dx7core.c dx7env.c dx7lfo.c dx7math.c dx7note.c dx7pitchenv.c dx7synth.c adsr.c fixed.c)
SHIM_SRC   = shim/fm1mix.c shim/fm1_bank.c
INC        = -I$(ENG)/web/synth -I$(ENG)/third_party/synth -I$(ENG)/sdk/include -I$(ENG)/app -Ishim
CFLAGS     = -std=gnu11 -O2 -fPIC -fvisibility=hidden -fno-stack-protector \
             -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -DFM1_HOST_TEST -DFM1_OPEN_WEB \
             -Wall -Wextra -Wno-unused-parameter $(INC)
BUILD      = build

.PHONY: all host test test-host test-arm clean

all: $(BUILD)/fm1mix.so

# Device build: no libc is linked, so the library carries no glibc symbol
# versions and loads on any glibc the MPC Sample ships (mpcqol.so needs 2.30).
$(BUILD)/fm1mix.so: $(SHIM_SRC) $(ENGINE_SRC)
	@mkdir -p $(BUILD)
	$(ARM_CC) $(CFLAGS) -shared -nostdlib -Wl,-soname,fm1mix.so -Wl,--strip-all -o $@ $^ -lgcc
	@echo "--- $@"; file $@ | sed 's/.*: //'
	@if readelf -V $@ | grep -q 'Version needs'; then echo "ERROR: versioned glibc symbols in $@"; readelf -V $@; exit 1; fi
	@echo "no glibc symbol versions required: OK"

host: $(BUILD)/host/fm1mix.so

$(BUILD)/host/fm1mix.so: $(SHIM_SRC) $(ENGINE_SRC)
	@mkdir -p $(BUILD)/host
	$(HOST_CC) $(CFLAGS) -shared -o $@ $^ -ldl

# ---- tests ----
TEST_CFLAGS = -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter

$(BUILD)/host/libasound.so.2: test/fake_alsa.c
	@mkdir -p $(BUILD)/host
	$(HOST_CC) $(TEST_CFLAGS) -shared -fPIC -Wl,-soname,libasound.so.2 -o $@ $<

$(BUILD)/host/test_app: test/test_app.c $(BUILD)/host/libasound.so.2
	$(HOST_CC) $(TEST_CFLAGS) -o $@ $< -L$(BUILD)/host -l:libasound.so.2

$(BUILD)/host/chain_shim.so: test/chain_shim.c
	@mkdir -p $(BUILD)/host
	$(HOST_CC) $(TEST_CFLAGS) -shared -fPIC -o $@ $< -ldl

$(BUILD)/arm/chain_shim.so: test/chain_shim.c
	@mkdir -p $(BUILD)/arm
	$(ARM_CC) $(TEST_CFLAGS) -shared -fPIC -o $@ $< -ldl

$(BUILD)/host/check_abi: test/check_abi.c
	@mkdir -p $(BUILD)/host
	$(HOST_CC) $(TEST_CFLAGS) -o $@ $<

$(BUILD)/arm/libasound.so.2: test/fake_alsa.c
	@mkdir -p $(BUILD)/arm
	$(ARM_CC) $(TEST_CFLAGS) -shared -fPIC -Wl,-soname,libasound.so.2 -o $@ $<

$(BUILD)/arm/test_app: test/test_app.c $(BUILD)/arm/libasound.so.2
	$(ARM_CC) $(TEST_CFLAGS) -o $@ $< -L$(BUILD)/arm -l:libasound.so.2

test-host: $(BUILD)/host/fm1mix.so $(BUILD)/host/test_app $(BUILD)/host/check_abi $(BUILD)/host/chain_shim.so
	./$(BUILD)/host/check_abi
	rm -rf $(BUILD)/host/out && mkdir -p $(BUILD)/host/out
	cd $(BUILD)/host/out && FM1MIX_DIR=$(CURDIR)/test/conf LD_LIBRARY_PATH=.. LD_PRELOAD=$(CURDIR)/$(BUILD)/host/fm1mix.so ../test_app
	python3 -I test/check.py $(BUILD)/host/out
	@echo "--- host, chained behind another interposer (like mpcqol.so)"
	rm -rf $(BUILD)/host/out && mkdir -p $(BUILD)/host/out
	cd $(BUILD)/host/out && FM1MIX_DIR=$(CURDIR)/test/conf LD_LIBRARY_PATH=.. LD_PRELOAD=$(CURDIR)/$(BUILD)/host/chain_shim.so:$(CURDIR)/$(BUILD)/host/fm1mix.so ../test_app
	python3 -I test/check.py $(BUILD)/host/out

test-arm: $(BUILD)/fm1mix.so $(BUILD)/arm/test_app $(BUILD)/arm/chain_shim.so
	rm -rf $(BUILD)/arm/out && mkdir -p $(BUILD)/arm/out
	cd $(BUILD)/arm/out && $(QEMU) -L $(SYSROOT) -E FM1MIX_DIR=$(CURDIR)/test/conf -E LD_LIBRARY_PATH=$(CURDIR)/$(BUILD)/arm -E LD_PRELOAD=$(CURDIR)/$(BUILD)/fm1mix.so $(CURDIR)/$(BUILD)/arm/test_app
	python3 -I test/check.py $(BUILD)/arm/out
	@echo "--- aarch64, chained behind another interposer (like mpcqol.so)"
	rm -rf $(BUILD)/arm/out && mkdir -p $(BUILD)/arm/out
	cd $(BUILD)/arm/out && $(QEMU) -L $(SYSROOT) -E FM1MIX_DIR=$(CURDIR)/test/conf -E LD_LIBRARY_PATH=$(CURDIR)/$(BUILD)/arm -E LD_PRELOAD=$(CURDIR)/$(BUILD)/arm/chain_shim.so:$(CURDIR)/$(BUILD)/fm1mix.so $(CURDIR)/$(BUILD)/arm/test_app
	python3 -I test/check.py $(BUILD)/arm/out

test: test-host test-arm
	@echo "ALL TESTS PASSED"

clean:
	rm -rf $(BUILD)
