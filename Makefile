CC ?= cc
MPICC ?= mpicc
MPIEXEC ?= mpirun
PYTHON ?= python3
BUILD_DIR ?= build
CFLAGS ?= -O2 -g
C_STANDARD ?= -std=c11
WARNINGS ?= -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wstrict-prototypes

# Busca dónde está instalado OpenSSL usando las herramientas de Linux o Homebrew
# en macOS. Si se indica una ruta en OPENSSL_PREFIX, usa esa instalación.
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

CPPFLAGS += -Isrc/common -Isrc/distributed $(OPENSSL_CPPFLAGS)
LDLIBS += $(OPENSSL_LDLIBS)
COMPILE_FLAGS = $(CPPFLAGS) $(C_STANDARD) $(CFLAGS) $(WARNINGS) -MMD -MP
COMMON_SOURCES = src/common/aes_ctr.c src/common/search_options.c \
                 src/common/search_reporting.c src/common/file_search.c \
                 src/common/hybrid_order.c src/common/search_timing.c
COMMON_OBJECTS = $(patsubst src/%.c,$(BUILD_DIR)/obj/%.o,$(COMMON_SOURCES))
SEQUENTIAL_OBJECTS = $(BUILD_DIR)/obj/sequential/aes_search_ecb.o \
                     $(BUILD_DIR)/obj/sequential/aes_search_ctr.o \
                     $(BUILD_DIR)/obj/sequential/aes_search_file.o
MPI_OBJECT = $(BUILD_DIR)/obj/distributed/aes_search_mpi.o
MPI_BLOCKS_OBJECT = $(BUILD_DIR)/obj/distributed/aes_search_mpi_blocks.o
MPI_HYBRID_OBJECT = $(BUILD_DIR)/obj/distributed/aes_search_mpi_hybrid.o
FILE_MPI_OBJECT = $(BUILD_DIR)/obj/distributed/file_search_mpi.o
HYBRID_MPI_OBJECT = $(BUILD_DIR)/obj/distributed/hybrid_search_mpi.o
MPI_OBJECTS = $(MPI_OBJECT) $(MPI_BLOCKS_OBJECT) $(MPI_HYBRID_OBJECT) \
              $(FILE_MPI_OBJECT) $(HYBRID_MPI_OBJECT)
TEST_OBJECT = $(BUILD_DIR)/obj/tests/test_common.o
ORDER_TEST_OBJECT = $(BUILD_DIR)/obj/tests/test_hybrid_order.o
HYBRID_MPI_TEST_OBJECT = $(BUILD_DIR)/obj/tests/test_hybrid_mpi.o

.PHONY: all sequential mpi test test-mpi test-mpi-blocks test-mpi-hybrid check clean print-config
all: sequential mpi
sequential: $(BUILD_DIR)/bin/aes_search_ecb $(BUILD_DIR)/bin/aes_search_ctr \
            $(BUILD_DIR)/bin/aes_search_file
mpi: $(BUILD_DIR)/bin/aes_search_mpi $(BUILD_DIR)/bin/aes_search_mpi_blocks \
     $(BUILD_DIR)/bin/aes_search_mpi_hybrid

$(BUILD_DIR)/bin/aes_search_ecb: $(BUILD_DIR)/obj/sequential/aes_search_ecb.o
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/bin/aes_search_ctr: $(BUILD_DIR)/obj/sequential/aes_search_ctr.o $(COMMON_OBJECTS)
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/bin/aes_search_file: $(BUILD_DIR)/obj/sequential/aes_search_file.o $(COMMON_OBJECTS)
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/bin/aes_search_mpi: $(MPI_OBJECT) $(COMMON_OBJECTS)
	@mkdir -p $(@D)
	$(MPICC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/bin/aes_search_mpi_blocks: $(MPI_BLOCKS_OBJECT) $(FILE_MPI_OBJECT) $(COMMON_OBJECTS)
	@mkdir -p $(@D)
	$(MPICC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/bin/aes_search_mpi_hybrid: $(MPI_HYBRID_OBJECT) $(FILE_MPI_OBJECT) $(HYBRID_MPI_OBJECT) $(COMMON_OBJECTS)
	@mkdir -p $(@D)
	$(MPICC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(MPI_OBJECTS): $(BUILD_DIR)/obj/%.o: src/%.c
	@mkdir -p $(@D)
	$(MPICC) $(COMPILE_FLAGS) -c $< -o $@

$(BUILD_DIR)/obj/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(COMPILE_FLAGS) -c $< -o $@

$(TEST_OBJECT) $(ORDER_TEST_OBJECT): $(BUILD_DIR)/obj/tests/%.o: tests/%.c
	@mkdir -p $(@D)
	$(CC) $(COMPILE_FLAGS) -c $< -o $@

$(BUILD_DIR)/bin/test_common: $(TEST_OBJECT) $(COMMON_OBJECTS)
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/bin/test_hybrid_order: $(ORDER_TEST_OBJECT) $(COMMON_OBJECTS)
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(HYBRID_MPI_TEST_OBJECT): tests/test_hybrid_mpi.c
	@mkdir -p $(@D)
	$(MPICC) $(COMPILE_FLAGS) -c $< -o $@

$(BUILD_DIR)/bin/test_hybrid_mpi: $(HYBRID_MPI_TEST_OBJECT) $(HYBRID_MPI_OBJECT) $(FILE_MPI_OBJECT) $(COMMON_OBJECTS)
	@mkdir -p $(@D)
	$(MPICC) $(LDFLAGS) $^ $(LDLIBS) -o $@

test: sequential $(BUILD_DIR)/bin/test_common $(BUILD_DIR)/bin/test_hybrid_order
	$(BUILD_DIR)/bin/test_common
	$(BUILD_DIR)/bin/test_hybrid_order
	$(PYTHON) tests/check_cli.py --bin-dir $(BUILD_DIR)/bin

test-mpi: mpi
	$(PYTHON) tests/check_cli.py --bin-dir $(BUILD_DIR)/bin --mpi --mpiexec "$(MPIEXEC)"

test-mpi-blocks: mpi
	$(PYTHON) tests/check_cli.py --bin-dir $(BUILD_DIR)/bin --mpi-blocks --mpiexec "$(MPIEXEC)"

test-mpi-hybrid: mpi $(BUILD_DIR)/bin/test_hybrid_mpi
	$(PYTHON) tests/check_cli.py --bin-dir $(BUILD_DIR)/bin --mpi-hybrid --mpiexec "$(MPIEXEC)"

check: test test-mpi test-mpi-blocks test-mpi-hybrid

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

-include $(COMMON_OBJECTS:.o=.d) $(SEQUENTIAL_OBJECTS:.o=.d) $(MPI_OBJECTS:.o=.d) \
         $(TEST_OBJECT:.o=.d) $(ORDER_TEST_OBJECT:.o=.d) $(HYBRID_MPI_TEST_OBJECT:.o=.d)
