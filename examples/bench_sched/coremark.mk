# SPDX-License-Identifier: BSD-2-Clause

# CoreMark is provided by uk-on-mk. Keep its sources outside this example and
# compile them once into a relocatable object shared by the victim wrapper.
COREMARK_DIR ?= $(UK_ON_MK_DIR)/dep/coremark
COREMARK_OBJ_DIR := coremark-obj
COREMARK_CFLAGS := \
	-DCOMPUTE_COROS=1 \
	-DCOREMARK_ITERS_PER_YIELD=1 \
	-DBENCH_SECONDS=1 \
	-I$(COREMARK_DIR)

COREMARK_HEADERS := \
	$(COREMARK_DIR)/coremark.h \
	$(COREMARK_DIR)/core_portme.h \
	$(COREMARK_DIR)/trace_platform.h

COREMARK_OBJECTS := \
	$(COREMARK_OBJ_DIR)/core_list_join.o \
	$(COREMARK_OBJ_DIR)/core_main.o \
	$(COREMARK_OBJ_DIR)/core_matrix.o \
	$(COREMARK_OBJ_DIR)/core_portme.o \
	$(COREMARK_OBJ_DIR)/core_state.o \
	$(COREMARK_OBJ_DIR)/core_util.o \
	$(COREMARK_OBJ_DIR)/ee_printf.o \
	$(COREMARK_OBJ_DIR)/cvt.o

$(COREMARK_OBJ_DIR)/core_main.o: CFLAGS += -Dmain=coremark_main

$(COREMARK_OBJ_DIR)/%.o: $(COREMARK_DIR)/%.c $(COREMARK_HEADERS)
	mkdir -p $(COREMARK_OBJ_DIR)
	$(CC) $(CFLAGS) $(COREMARK_CFLAGS) -c $< -o $@

$(COREMARK_OBJ_DIR)/ee_printf.o $(COREMARK_OBJ_DIR)/cvt.o: \
		$(COREMARK_OBJ_DIR)/%.o: $(COREMARK_DIR)/barebones/%.c $(COREMARK_HEADERS)
	mkdir -p $(COREMARK_OBJ_DIR)
	$(CC) $(CFLAGS) $(COREMARK_CFLAGS) -c $< -o $@

coremark.o: $(COREMARK_OBJECTS)
	$(LD) -r $^ -o $@

