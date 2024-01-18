build        := $(if $(build),$(build),build)


src := $(wildcard *.c)
obj := $(patsubst %.c,$(build)/%.o,$(src))


$(build)/%.o: CFLAGS += -fvisibility=hidden
$(build)/%.o: %.c
	@mkdir -p `dirname $@`
	gcc -I../include $(CFLAGS) -c $^ -o $@

$(build)/odbs: 
$(build)/odbs: $(obj)
	gcc -L../liboidadb/build $(LDFLAGS) -loidadb -o $@ $^

