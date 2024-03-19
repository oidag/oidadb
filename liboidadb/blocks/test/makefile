build        := $(if $(build),$(build),build)

test_src     := $(wildcard *_t.c)
test_exec    := $(patsubst %_t.c, $(build)/%_t, $(test_src))
lib_src      := $(wildcard ../*.c)
lib_obj_test := $(patsubst ../%.c, $(build)/%.o, $(lib_src))

$(build)/%_t: %_t.c teststuff.c $(lib_obj_test)
	gcc -I../../../include $^ -o $@
	$@

all: $(test_exec)

$(build)/%.o: ../%.c
	@mkdir -p `dirname $@`	
	gcc -I../../../include -g -o $@ -c $^

.PHONY: all
