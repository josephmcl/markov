#  
# 
#

include .env

target = main

cc = gcc-13

source_ext = c
header_directory = include
source_directory = source
object_directory = object
binary_directory = .
sources := $(wildcard $(source_directory)/*.c)
headers := $(wildcard $(header_directory)/*.h)
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

$(binary_directory)/$(target): $(objects)
	$(call speaker,\
	$(cc) $(objects) -o $@ $(libraries))

$(objects): $(object_directory)/%.o: $(source_directory)/%.$(source_ext)
	$(call speaker,\
	$(cc) $(compiler_flags) -c $< -o $@ $(includes)) 

.PHONY: clean
clean:
	@$(rm) $(objects)

.PHONY: remove
remove: clean
	@$(rm) $(BINDIR)/$(target)