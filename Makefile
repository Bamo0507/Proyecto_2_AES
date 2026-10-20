CC ?= cc
MPICC ?= mpicc
MPIEXEC ?= mpirun
PYTHON ?= python3
BUILD_DIR ?= build
CFLAGS ?= -O2 -g
C_STANDARD ?= -std=c11
WARNINGS ?= -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wstrict-prototypes

# Un prefijo explicito tiene prioridad. En Linux se prefiere pkg-config;
# Homebrew es el fallback para macOS. Tambien se pueden pasar flags a make.
ifndef OPENSSL_PREFIX
OPENSSL_PREFIX := $(shell if ! pkg-config --exists libcrypto >/dev/null 2>&1 && command -v brew >/dev/null 2>&1; then brew --prefix openssl@3 2>/dev/null; fi)
endif
ifneq ($(strip $(OPENSSL_PREFIX)),)
OPENSSL_CPPFLAGS ?= -I$(OPENSSL_PREFIX)/include
OPENSSL_LDLIBS ?= -L$(OPENSSL_PREFIX)/lib -lcrypto
else
OPENSSL_CPPFLAGS ?= $(shell pkg-config --cflags libcrypto 2>/dev/null)
OPENSSL_LDLIBS ?= $(shell pkg-config --libs libcrypto 2>/dev/null || echo -lcrypto)
endif

CPPFLAGS += -Isrc/common $(OPENSSL_CPPFLAGS)
LDLIBS += $(OPENSSL_LDLIBS)
COMPILE_FLAGS = $(CPPFLAGS) $(C_STANDARD) $(CFLAGS) $(WARNINGS) -MMD -MP
COMMON_SOURCES = src/common/aes_ctr.c src/common/search_options.c src/common/search_reporting.c
COMMON_OBJECTS = $(patsubst src/%.c,$(BUILD_DIR)/obj/%.o,$(COMMON_SOURCES))
SEQUENTIAL_OBJECTS = $(BUILD_DIR)/obj/sequential/aes_search_ecb.o $(BUILD_DIR)/obj/sequential/aes_search_ctr.o
MPI_OBJECT = $(BUILD_DIR)/obj/distributed/aes_search_mpi.o
MPI_BLOCKS_OBJECT = $(BUILD_DIR)/obj/distributed/aes_search_mpi_blocks.o
TEST_OBJECT = $(BUILD_DIR)/obj/tests/test_common.o

.PHONY: all sequential mpi test test-mpi test-mpi-blocks check clean print-config
all: sequential mpi
sequential: $(BUILD_DIR)/bin/aes_search_ecb $(BUILD_DIR)/bin/aes_search_ctr
mpi: $(BUILD_DIR)/bin/aes_search_mpi $(BUILD_DIR)/bin/aes_search_mpi_blocks

$(BUILD_DIR)/bin/aes_search_ecb: $(BUILD_DIR)/obj/sequential/aes_search_ecb.o
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/bin/aes_search_ctr: $(BUILD_DIR)/obj/sequential/aes_search_ctr.o $(COMMON_OBJECTS)
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/bin/aes_search_mpi: $(MPI_OBJECT) $(COMMON_OBJECTS)
	@mkdir -p $(@D)
	$(MPICC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/bin/aes_search_mpi_blocks: $(MPI_BLOCKS_OBJECT)
	@mkdir -p $(@D)
	$(MPICC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(MPI_BLOCKS_OBJECT): src/distributed/aes_search_mpi_blocks.c
	@mkdir -p $(@D)
	$(MPICC) $(COMPILE_FLAGS) -c $< -o $@

$(MPI_OBJECT): src/distributed/aes_search_mpi.c
	@mkdir -p $(@D)
	$(MPICC) $(COMPILE_FLAGS) -c $< -o $@

$(BUILD_DIR)/obj/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(COMPILE_FLAGS) -c $< -o $@

$(TEST_OBJECT): tests/test_common.c
	@mkdir -p $(@D)
	$(CC) $(COMPILE_FLAGS) -c $< -o $@

$(BUILD_DIR)/bin/test_common: $(TEST_OBJECT) $(COMMON_OBJECTS)
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

test: sequential $(BUILD_DIR)/bin/test_common
	$(BUILD_DIR)/bin/test_common
	$(PYTHON) tests/check_cli.py --bin-dir $(BUILD_DIR)/bin

test-mpi: mpi
	$(PYTHON) tests/check_cli.py --bin-dir $(BUILD_DIR)/bin --mpi --mpiexec "$(MPIEXEC)"

test-mpi-blocks: mpi
	$(PYTHON) tests/check_cli.py --bin-dir $(BUILD_DIR)/bin --mpi-blocks --mpiexec "$(MPIEXEC)"

check: test test-mpi test-mpi-blocks

print-config:
	@echo 'CC=$(CC)'
	@echo 'MPICC=$(MPICC)'
	@echo 'C_STANDARD=$(C_STANDARD)'
	@echo 'CPPFLAGS=$(CPPFLAGS)'
	@echo 'CFLAGS=$(CFLAGS)'
	@echo 'WARNINGS=$(WARNINGS)'
	@echo 'LDFLAGS=$(LDFLAGS)'
	@echo 'LDLIBS=$(LDLIBS)'

clean:
	rm -rf $(BUILD_DIR)/bin $(BUILD_DIR)/obj

-include $(COMMON_OBJECTS:.o=.d) $(SEQUENTIAL_OBJECTS:.o=.d) $(MPI_OBJECT:.o=.d) $(MPI_BLOCKS_OBJECT:.o=.d) $(TEST_OBJECT:.o=.d)
