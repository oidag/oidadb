build        := $(if $(build),$(build),build)

manual_src := $(wildcard *.org)
manual_html:= $(patsubst %.org,$(build)/%.html,$(manual_src))
$(build)/%.html: %.org
	@mkdir -p `dirname $@`
	emacs $< -Q --batch --kill --eval '(org-html-export-to-html)'
	mv $(patsubst %.org,%.html,$<) $@

manual: $(manual_html)
