build        := $(if $(build),$(build),build)

manual_src := $(wildcard *.org)
manual_html:= $(patsubst %.org,$(build)/html/%.html,$(manual_src))
manual_org:= $(patsubst %.org,$(build)/org/%.org,$(manual_src))
$(build)/html/%.html: %.org
	@mkdir -p `dirname $@`
	emacs $< -Q --batch --kill --eval '(org-html-export-to-html)'
	mv $(patsubst %.org,%.html,$<) $@

$(build)/org/%.org: %.org
	@mkdir -p `dirname $@`
	cp $< $@

$(build)/man.tar.xz: $(manual_html) $(manual_org)
	tar -C $(build) -cf $(build)/man.tar html org
	cd $(build) && xz man.tar
