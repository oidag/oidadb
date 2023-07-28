build        := $(if $(build),$(build),build)

# Library source code
lib_src := $(wildcard *.c)

# RELEASE BUILDS
# note: debug/test builds are done with cmake, not this makefile
lib_obj_release := $(patsubst %.c,$(build)/%.o,$(lib_src))

default: $(build)/liboidadb.so

$(build)/%.o: gcc_compile_args = -fvisibility=hidden -fPIC -O3 -D_ODB_CD_RELEASE
$(build)/%.o: %.c
	@mkdir -p `dirname $@`
	gcc $(gcc_compile_args) -c $^ -o $@

$(build)/liboidadb.so: gcc_link_args=-fPIC -O3 -s -shared
$(build)/liboidadb.so: $(lib_obj_release)
	gcc $(gcc_link_args) -o $@ $^

# Testing
test:
	@mkdir -p $(build)/test
	build=../$(build)/test $(MAKE) -C test

.PHONEY: test
