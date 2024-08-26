#  
# 
#

include .env

target      = main
target_emcc = markov.js 

cc = ${CC}
wc = ${WC} 
bc = ${BC}

nil := 
space := $(nil) $(nil)

source_ext = c
header_directory = include
source_directory = source
object_directory = object
binary_directory = .
bison_source = $(source_directory)/bison.c
bison_header = $(header_directory)/bison.h
bison_object = $(object_directory)/bison.o
bison_object_wasm = $(object_directory)/bison.o
bison_grammar = grammar.y
sources_all = $(wildcard $(source_directory)/*.c)
sources_all += $(wildcard $(source_directory)/algorithm/*.c)
sources_all += $(wildcard $(source_directory)/context/*.c)
sources_all += $(wildcard $(source_directory)/grammar/*.c)

sources      = $(subst emcc.c,,$(sources_all))
sources_emcc = $(subst main.c,,$(sources_all))

headers = $(wildcard $(header_directory)/*.h)

headers_emcc = $(wildcard $(header_directory)/*.h)

objects_all  := $(sources_all:$(source_directory)/%.c=$(object_directory)/%.o)

objects      = $(filter-out $(object_directory)/emcc.o,$(objects_all))
objects_emcc = $(filter-out $(object_directory)/main.o,$(objects_all))


nil := 
space := $(nil) $(nil)
rm = rm -f

gccflags = -Wall -Wpedantic -g -O3 
compiler_flags := $(gccflags) 
includes := -I$(header_directory)
libraries := 
wasm_flags := -s LINKABLE=1 -sEXPORTED_RUNTIME_METHODS=ccall

define speaker
	@echo [make:$$PPID] $(1)
	@$(1)
endef

all: native wasm

native: $(binary_directory)/$(target)

wasm: $(binary_directory)/$(target_emcc)

$(binary_directory)/$(target): $(bison_object) $(objects) 
	$(call speaker,\
	$(cc) $(objects) $(bison_object) -o $@ $(libraries))

$(binary_directory)/$(target_emcc): $(bison_object_wasm) $(objects_emcc) 
	$(call speaker,\
	$(wc) $(objects_emcc) $(bison_object_wasm) -o $@ $(wasm_flags))

$(objects): $(object_directory)/%.o: $(source_directory)/%.$(source_ext) 
	mkdir -p object
	mkdir -p object/algorithm
	mkdir -p object/context
	mkdir -p object/grammar
	$(call speaker,\
	$(cc) $(compiler_flags) -c $< -o $@ $(includes)) 

$(objects_emcc): $(object_directory)/%.o: $(source_directory)/%.$(source_ext) 
	mkdir -p object
	mkdir -p object/algorithm
	mkdir -p object/context
	mkdir -p object/grammar
	$(call speaker,\
	$(wc) $(compiler_flags) -c $< -o $@ $(includes)) 

$(bison_object): $(bison_source)
	mkdir -p object
	$(call speaker,\
	$(cc) $(compiler_flags) -c $< -o $@ $(includes)) 

$(bison_object_wasm): $(bison_source)
	mkdir -p object
	$(call speaker,\
	$(wc) $(compiler_flags) -c $< -o $@ $(includes)) 

$(bison_source): $(bison_grammar)
	$(call speaker,\
	$(bc) -Wconflicts-sr -Wcounterexamples $< --output=$@ --defines=$(bison_header))

.PHONY: clean
clean:
	@$(rm) $(objects_all)
	@$(rm) $(bison_source)
	@$(rm) $(bison_header)

.PHONY: remove
remove: clean
	@$(rm) $(BINDIR)/$(target)
