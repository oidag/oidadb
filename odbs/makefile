build        := $(if $(build),$(build),build)


src := $(wildcard *.c)
obj := $(patsubst %.c,$(build)/%.o,$(src))


$(build)/%.o: gcc_compile_args = -fvisibility=hidden
$(build)/%.o: %.c
	@mkdir -p `dirname $@`
	gcc $(gcc_compile_args) -c $^ -o $@

$(build)/odbs: 
$(build)/odbs: $(obj)
	gcc $(gcc_link_args) -o $@ $^

