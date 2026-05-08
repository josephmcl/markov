#  
# 
#

include .env

target      = main
target_emcc = markov.js 

cc = ${CC}
wc = ${WC} -s LINKABLE=1 -s EXPORTED_RUNTIME_METHODS=["ccall","print","printErr"] -s ALLOW_MEMORY_GROWTH=1
bc = ${BC}

nil := 
space := $(nil) $(nil)

source_ext = c
header_directory = include
source_directory = source
build_directory  = build
native_directory = $(build_directory)/native
wasm_directory   = $(build_directory)/wasm
native_object_directory = $(native_directory)/object
wasm_object_directory = $(wasm_directory)/object
binary_directory = .
bison_source = $(source_directory)/bison.c
bison_header = $(header_directory)/bison.h
bison_object_native = $(native_object_directory)/bison.o
bison_object_wasm = $(wasm_object_directory)/bison.o
bison_grammar = grammar.y
sources_all = $(wildcard $(source_directory)/*.c)
sources_all += $(wildcard $(source_directory)/algorithm/*.c)
sources_all += $(wildcard $(source_directory)/context/*.c)
sources_all += $(wildcard $(source_directory)/grammar/*.c)

sources      = $(subst emcc.c,,$(sources_all))
sources_emcc = $(subst main.c,,$(sources_all))

headers = $(wildcard $(header_directory)/*.h)

headers_emcc = $(wildcard $(header_directory)/*.h)

objects_native_ = $(sources_all:$(source_directory)/%.c=$(native_object_directory)/%.o)
objects_native = $(filter-out $(native_object_directory)/emcc.o $(native_object_directory)/enumerate_main.o $(native_object_directory)/verify_records_main.o,$(objects_native_))

objects_wasm_ = $(sources_all:$(source_directory)/%.c=$(wasm_object_directory)/%.o)
objects_wasm = $(filter-out $(wasm_object_directory)/main.o $(wasm_object_directory)/enumerate_main.o $(wasm_object_directory)/verify_records_main.o,$(objects_wasm_))


nil := 
space := $(nil) $(nil)
rm = rm -f

gccflags = -Wall -Wpedantic -g -O3 
compiler_flags := $(gccflags) 
includes := -I$(header_directory)
libraries := 
wasm_flags := 

define speaker
	@echo [make:$$PPID] $(1)
	@$(1)
endef

all: native wasm enumerate verify_records

native: $(binary_directory)/$(target)

# OpenMP support: set OPENMP=1 to enable parallel enumeration.
# Apple clang doesn't ship libomp, so we use Homebrew's libomp explicitly.
# On Linux/GCC the simpler -fopenmp flag suffices.
ifeq ($(OPENMP),1)
  OPENMP_DEFINES := -DENUM_USE_OPENMP
  OPENMP_CFLAGS  := -fopenmp
  OPENMP_LDFLAGS := -fopenmp
else
  OPENMP_DEFINES :=
  OPENMP_CFLAGS  :=
  OPENMP_LDFLAGS :=
endif

enumerate: $(binary_directory)/enumerate
verify_records: $(binary_directory)/verify_records

enumerate_objs = $(native_object_directory)/enumerate_main.o \
                 $(native_object_directory)/markov_record.o \
                 $(native_object_directory)/markov_enumerate.o \
                 $(native_object_directory)/markov_observer.o \
                 $(native_object_directory)/markov_linearity.o \
                 $(native_object_directory)/markov_records.o

verify_objs = $(native_object_directory)/verify_records_main.o \
              $(native_object_directory)/markov_record.o \
              $(native_object_directory)/markov_records.o

$(native_object_directory)/enumerate_main.o: $(source_directory)/enumerate_main.c
	mkdir -p $(native_object_directory)
	$(call speaker,\
	$(cc) $(compiler_flags) $(OPENMP_DEFINES) $(OPENMP_CFLAGS) -c $< -o $@ $(includes))

$(native_object_directory)/verify_records_main.o: $(source_directory)/verify_records_main.c
	mkdir -p $(native_object_directory)
	$(call speaker,\
	$(cc) $(compiler_flags) -c $< -o $@ $(includes))

$(binary_directory)/enumerate: $(enumerate_objs)
	$(call speaker,\
	$(cc) $(enumerate_objs) -o $@ $(libraries) $(OPENMP_LDFLAGS))

$(binary_directory)/verify_records: $(verify_objs)
	$(call speaker,\
	$(cc) $(verify_objs) -o $@ $(libraries))

$(binary_directory)/$(target): $(bison_object_native) $(objects_native) 
	$(call speaker,\
	$(cc) $(objects_native) $(bison_object_native) -o $@ $(libraries))

$(objects_native): $(native_object_directory)/%.o: $(source_directory)/%.$(source_ext) 
	mkdir -p $(native_object_directory)
	mkdir -p $(native_object_directory)/algorithm
	mkdir -p $(native_object_directory)/context
	mkdir -p $(native_object_directory)/grammar
	$(call speaker,\
	$(cc) $(compiler_flags) -c $< -o $@ $(includes)) 

$(bison_object_native): $(bison_source)
	mkdir -p $(native_object_directory)
	$(call speaker,\
	$(cc) $(compiler_flags) -c $< -o $@ $(includes)) 

$(bison_source): $(bison_grammar)
	$(call speaker,\
	$(bc) -Wconflicts-sr -Wcounterexamples $< --output=$@ --defines=$(bison_header))

wasm: $(binary_directory)/$(target_emcc)
$(binary_directory)/$(target_emcc): $(bison_object_wasm) $(objects_wasm) 
	$(call speaker,\
	$(wc) $(objects_wasm) $(bison_object_wasm) -o $@ $(wasm_flags))
	
$(objects_wasm): $(wasm_object_directory)/%.o: $(source_directory)/%.$(source_ext) 
	mkdir -p $(wasm_object_directory)
	mkdir -p $(wasm_object_directory)/algorithm
	mkdir -p $(wasm_object_directory)/context
	mkdir -p $(wasm_object_directory)/grammar
	$(call speaker,\
	$(wc) $(compiler_flags) -c $< -o $@ $(includes)) 

$(bison_object_wasm): $(bison_source)
	mkdir -p $(wasm_object_directory)
	$(call speaker,\
	$(wc) $(compiler_flags) -c $< -o $@ $(includes)) 

.PHONY: clean
clean:
	@$(rm) $(objects_native)
	@$(rm) $(objects_wasm)
	@$(rm) $(bison_object_native)
	@$(rm) $(bison_object_wasm)
	@$(rm) $(bison_source)
	@$(rm) $(bison_header)

.PHONY: remove
remove: clean
	@$(rm) $(BINDIR)/$(target)
