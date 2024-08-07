#  
# 
#

include .env

target = main

cc = ${CC}
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
bison_grammar = grammar.y
sources = $(wildcard $(source_directory)/*.c)
sources += $(wildcard $(source_directory)/algorithm/*.c)
sources += $(wildcard $(source_directory)/context/*.c)
sources += $(wildcard $(source_directory)/grammar/*.c)
headers = $(wildcard $(header_directory)/*.h)
objects := $(sources:$(source_directory)/%.c=$(object_directory)/%.o)


entrypoint      := main
test_entrypoint := test
test_target := lab

nil := 
space := $(nil) $(nil)
rm = rm -f

gccflags = -Wall -Wpedantic -g -O3 
compiler_flags := $(gccflags) 
includes := -I$(header_directory)
libraries := 

define speaker
	@echo [make:$$PPID] $(1)
	@$(1)
endef

$(binary_directory)/$(target): $(bison_object) $(objects) 
	$(call speaker,\
	$(cc) $(objects) $(bison_object) -o $@ $(libraries))

$(objects): $(object_directory)/%.o: $(source_directory)/%.$(source_ext) 
	mkdir -p object
	mkdir -p object/algorithm
	mkdir -p object/context
	mkdir -p object/grammar
	$(call speaker,\
	$(cc) $(compiler_flags) -c $< -o $@ $(includes)) 

$(bison_object): $(bison_source)
	mkdir -p object
	$(call speaker,\
	$(cc) $(compiler_flags) -c $< -o $@ $(includes)) 

$(bison_source): $(bison_grammar)
	$(call speaker,\
	$(bc) -Wconflicts-sr -Wcounterexamples $< --output=$@ --defines=$(bison_header))

.PHONY: clean
clean:
	@$(rm) $(objects)
	@$(rm) $(bison_source)
	@$(rm) $(bison_header)

.PHONY: remove
remove: clean
	@$(rm) $(BINDIR)/$(target)
