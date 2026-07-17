

PC_SRC_DIR := $(realpath $(dir $(lastword $(MAKEFILE_LIST))))
PC_CONFIG_DIR := $(PC_SRC_DIR)/config
PC_HEADER_DIR := $(PC_SRC_DIR)/include
PC_TOOL_DIR := $(PC_SRC_DIR)/tools
PC_MICRORL_SRC_DIR := $(PC_SRC_DIR)/microrl
PC_LIBMICROKITCO_DIR := $(LIBMICROKITCO_PATH)
PC_LIBTRUSTEDLO_DIR := $(CARRELS)/dep/libtrustedlo

pc:
	mkdir -p pc

PC_BUILD_DIR_GEN := $(BUILD_DIR)/pc/generated

PC_MONITOR_VM_LAYOUT := $(PC_CONFIG_DIR)/monitor_vm_layout.py
PC_MONITOR_VM_LAYOUT_GEN := $(PC_TOOL_DIR)/gen_vm_layout.py
PC_MONITOR_VM_LAYOUT_HEADER := $(PC_BUILD_DIR_GEN)/monitor_vm_layout.h

PC_TSLDR_BUILD_DIR := $(BUILD_DIR)/pc/libtrustedlo
PC_TSLDR_BUILD_DIR_GEN := $(PC_TSLDR_BUILD_DIR)/generated
PC_TSLDR_VM_LAYOUT_HEADER := $(PC_TSLDR_BUILD_DIR_GEN)/tsldr_vm_layout.h

PC_LIBTRUSTEDLO_OBJ := libtrustedlo/libtrustedlo.a

# ===================== unikraft variables ==========================

BM_UNIKRAFT_DIR := $(CARRELS)/dep/unikraft
BM_CATALOG_CORE_DIR := $(CARRELS)/dep/catalog-core

BM_UK_APPLICATION ?= c-hello
BM_UK_PAYLOAD_ELF ?= $(BM_UK_APPLICATION)_default-arm64

BM_UK_CONFIG ?= uk-carrels-arm.config
BM_UK_CONFIG_SRC := $(PC_CONFIG_DIR)/uk/$(BM_UK_CONFIG)

BM_UK_APP_DIR := $(BM_CATALOG_CORE_DIR)/$(BM_UK_APPLICATION)
BM_UK_BUILD_DIR := $(BUILD_DIR)/uk
BM_UK_BUILT_ELF := $(BM_UK_BUILD_DIR)/$(BM_UK_PAYLOAD_ELF)
BM_UK_CONFIGURED := $(BM_UK_BUILD_DIR)/.configured

BM_UK_MAKE_ARGS := \
	UK_ROOT=$(BM_UNIKRAFT_DIR) \
	UK_APP=$(BM_UK_APP_DIR) \
	UK_BUILD=$(BM_UK_BUILD_DIR) \
	SDDF=$(SDDF) \
	LIBMICROKITCO_PATH=$(LIBMICROKITCO_PATH) \
	LIBTRUSTEDLO_PATH=$(PC_LIBTRUSTEDLO_DIR) \
	LIBTRUSTEDLO_LIB=$(abspath pc/$(PC_LIBTRUSTEDLO_OBJ)) \
	MICROKIT_SDK=$(MICROKIT_SDK) \
	MICROKIT_BOARD=$(MICROKIT_BOARD) \
	MICROKIT_CONFIG=$(MICROKIT_CONFIG) \
	BOARD_DIR=$(BOARD_DIR) \
	SDDF_UTIL_LIB=$(abspath libsddf_util.a) \
	TSLDR_HEADER=$(PC_TSLDR_BUILD_DIR_GEN)

# ===================== unikraft variables ==========================

PC_CFLAGS := \
	-I$(CONTAINER_LIBC_INCLUDE) \
	-I$(PC_HEADER_DIR) \
	-I$(PC_SRC_DIR) \
	-I$(PC_MICRORL_SRC_DIR)/include \
	-I$(PC_LIBTRUSTEDLO_DIR)/include \
	-I$(PC_LIBMICROKITCO_DIR) \
	-I$(PC_BUILD_DIR_GEN) \
	-I$(PC_TSLDR_BUILD_DIR_GEN)

LIBMICROKITCO_CFLAGS_pc := ${PC_CFLAGS}
PC_LIBMICROKITCO_OBJ := libmicrokitco_pc.a


PC_ECHO_CLIENT_OBJS := \
	pc/client/client_echo.o \
	pc/client/early-init.o

PC_FAULTING_CLIENT_OBJS := \
	pc/client/client_faulting.o \
	pc/client/early-init.o

PC_LOOPING_CLIENT_OBJS := \
	pc/client/client_looping.o \
	pc/client/early-init.o

PC_TIMEOUT_CLIENT_OBJS := \
	pc/client/client_timeout.o \
	pc/client/early-init.o

PC_BENCH_SIMPLE_OBJS := \
	pc/client/bench_simple.o \
	pc/client/early-init.o

PC_MONITOR_OBJS := \
	pc/monitor/entry.o \
	pc/monitor/mcall.o \
	pc/monitor/fault/fault.o \
	pc/monitor/request/deploy.o \
	pc/monitor/request/query.o \
	pc/monitor/request/resume.o \
	pc/monitor/request/stop.o \
	pc/monitor/request/support.o \
	pc/monitor/request/suspend.o \
	pc/monitor/forwarder.o \
	pc/monitor/service/service_installer.o \
	pc/monitor/service/service_manifest.o \
	pc/monitor/service/service_planner.o \
	pc/monitor/service/service_registry.o \
	pc/util/pico_vfs.o

PC_ORCHESTRATOR_OBJS := \
	pc/orchestrator/orchestrator.o \
	pc/util/pico_vfs.o \
	pc/microrl.o

PC_PROTOCON_OBJS :=
PC_TRAMPOLINE_OBJS :=

PC_OBJS := \
	$(PC_ORCHESTRATOR_OBJS) \
	$(PC_MONITOR_OBJS) \
	$(PC_PROTOCON_OBJS) \
	$(PC_TRAMPOLINE_OBJS) \
	$(PC_ECHO_CLIENT_OBJS) \
	$(PC_FAULTING_CLIENT_OBJS) \
	$(PC_LOOPING_CLIENT_OBJS) \
	$(PC_TIMEOUT_CLIENT_OBJS) \
	$(PC_BENCH_SIMPLE_OBJS)


$(PC_MONITOR_VM_LAYOUT_HEADER): pc \
	$(PC_MONITOR_VM_LAYOUT) $(PC_MONITOR_VM_LAYOUT_GEN)
	@mkdir -p $(dir $@)
	python3 -B $(PC_MONITOR_VM_LAYOUT_GEN) \
		--config $(PC_MONITOR_VM_LAYOUT) \
		--header-output $@

pc/$(PC_LIBTRUSTEDLO_OBJ): pc
	make -f $(PC_LIBTRUSTEDLO_DIR)/Makefile \
			LIBTRUSTEDLO_PATH=$(PC_LIBTRUSTEDLO_DIR) \
			TARGET=$(TARGET) \
			MICROKIT_SDK:=$(MICROKIT_SDK) \
			BUILD_DIR:=pc \
			MICROKIT_BOARD:=$(MICROKIT_BOARD) \
			MICROKIT_CONFIG:=$(MICROKIT_CONFIG) \
			CPU:=$(CPU) \
			LLVM:=1

# ===================== unikraft variables ==========================

.PHONY: uk-build
uk-build: $(BM_UK_CONFIGURED) libsddf_util.a | pc
	$(MAKE) -C $(BM_UK_APP_DIR) \
		$(BM_UK_MAKE_ARGS) \
		-j$$(nproc)
	cp $(BM_UK_BUILT_ELF) pc/$(BM_UK_PAYLOAD_ELF)


$(BM_UK_CONFIGURED): $(BM_UK_CONFIG_SRC)
	$(MAKE) -C $(BM_UK_APP_DIR) \
		$(BM_UK_MAKE_ARGS) \
		distclean
	$(MAKE) -C $(BM_UK_APP_DIR) \
		$(BM_UK_MAKE_ARGS) \
		UK_DEFCONFIG=$(BM_UK_CONFIG_SRC) \
		defconfig
	mkdir -p $(BM_UK_BUILD_DIR)
	touch $@

# ===================== unikraft variables ==========================

vpath client/%.c $(PC_SRC_DIR)/src
vpath util/%.c $(PC_SRC_DIR)/src
vpath monitor/%.c $(PC_SRC_DIR)/src
vpath monitor/service/%.c $(PC_SRC_DIR)/src
vpath monitor/fault/%.c $(PC_SRC_DIR)/src
vpath monitor/lifecycle/%.c $(PC_SRC_DIR)/src
vpath monitor/request/%.c $(PC_SRC_DIR)/src
vpath orchestrator/%.c $(PC_SRC_DIR)/src

pc/%.o: CFLAGS := $(PC_CFLAGS) $(CFLAGS)

pc/%.o: %.c | pc $(PC_MONITOR_VM_LAYOUT_HEADER) pc/$(PC_LIBTRUSTEDLO_OBJ)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) $< -o $@

pc/microrl.o: CFLAGS := $(PC_CFLAGS) $(CFLAGS) \
	-I$(PC_MICRORL_SRC_DIR)/include

pc/microrl.o: $(PC_MICRORL_SRC_DIR)/microrl.c | pc
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) $< -o $@

orchestrator.elf: LDFLAGS += -L$(BOARD_DIR)/lib
orchestrator.elf: $(PC_ORCHESTRATOR_OBJS) \
			  	  $(PC_LIBMICROKITCO_OBJ) pc/$(PC_LIBTRUSTEDLO_OBJ) libsddf_util.a \
              	  $(CONTAINER_LIBC_LIB)
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@


protocon.elf:
	cp $(BUILD_DIR)/pc/libtrustedlo/loader.elf $@

trampoline.elf:
	cp $(BUILD_DIR)/pc/libtrustedlo/trampoline.elf $@

payloads.o: \
		protocon.elf \
		trampoline.elf
	cp $(PC_SRC_DIR)/src/monitor/package_payloads.S .
	$(CC) -c $(CFLAGS) \
		-DCARRELS_PROTOCON_PATH=\"$(BUILD_DIR)/protocon.elf\" \
		-DCARRELS_TRAMPOLINE_PATH=\"$(BUILD_DIR)/trampoline.elf\" \
		package_payloads.S -o $@

monitor.elf: LDFLAGS += -L$(BOARD_DIR)/lib
monitor.elf: \
		$(PC_MONITOR_OBJS) \
		pc/$(PC_LIBTRUSTEDLO_OBJ) \
		$(PC_LIBMICROKITCO_OBJ) \
		$(CONTAINER_LIBC_LIB) \
		libsddf_util.a payloads.o
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@


PC_CLIENT_NAMES := \
	bench_simple \
	client_echo \
	client_looping \
	client_faulting \
	client_timeout

PC_CLIENT_ELFS := $(addsuffix .elf,$(PC_CLIENT_NAMES))
PC_CLIENT_IMGS := $(addsuffix .img,$(PC_CLIENT_NAMES))

PC_SERVICE_MANIFEST := $(PC_SRC_DIR)/src/client/service.mf
PC_UNIKRAFT_MANIFEST := $(PC_SRC_DIR)/src/client/uk.mf

$(PC_CLIENT_ELFS): LDFLAGS += -L$(BOARD_DIR)/lib

bench_simple.elf:    $(PC_BENCH_SIMPLE_OBJS)
client_echo.elf:     $(PC_ECHO_CLIENT_OBJS)
client_looping.elf:  $(PC_LOOPING_CLIENT_OBJS)
client_faulting.elf: $(PC_FAULTING_CLIENT_OBJS)
client_timeout.elf:  $(PC_TIMEOUT_CLIENT_OBJS)

$(PC_CLIENT_ELFS): libsddf_util.a pc/$(PC_LIBTRUSTEDLO_OBJ)
	$(LD) $(LDFLAGS) -Ttext=0x2800000 $^ $(LIBS) -o $@

PC_SERVICE_IMGS := $(PC_CLIENT_IMGS) unikraft.img

.PHONY: pc-images
pc-images: $(PC_SERVICE_IMGS)

$(PC_SERVICE_IMGS): %.img: %.elf $(PC_SERVICE_MANIFEST) \
		$(PC_TOOL_DIR)/service-helper.py
	PYTHONPATH=$(SDDF)/tools/meta:$$PYTHONPATH $(PYTHON) \
		$(PC_TOOL_DIR)/service-helper.py \
		--mf $(PC_SERVICE_MANIFEST) \
		--elf $< \
		-o $@

unikraft.elf: uk-build
	cp pc/$(BM_UK_PAYLOAD_ELF) unikraft.elf


unikraft.img: unikraft.elf $(PC_UNIKRAFT_MANIFEST) \
		$(PC_TOOL_DIR)/service-helper.py
	PYTHONPATH=$(SDDF)/tools/meta:$$PYTHONPATH $(PYTHON) \
		$(PC_TOOL_DIR)/service-helper.py \
		--mf $(PC_UNIKRAFT_MANIFEST) \
		--elf $< \
		-o $@

-include $(PC_OBJS:.o=.d)
