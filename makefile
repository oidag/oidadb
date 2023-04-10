
publish: build/manual.html build/publish-index.html
	@mkdir -p build
	cd build && timeout 30 ../scripts/ftp-publish.sh publish-index.html
	cd build && timeout 30 ../scripts/ftp-publish.sh manual.html

build/publish-index.html:  spec/publish-index.m4.html build/metrics.m4
	@mkdir -p build
	m4 build/metrics.m4 $< > $@


manual_src := $(wildcard man/*.org)
manual_html:= $(patsubst man/%.org,build/man/%.html,$(manual_src))

build/man/%.html: man/%.org
	@mkdir -p `dirname $@`
	emacs $< -Q --batch --kill --eval '(org-html-export-to-html)'
	mv $(patsubst %.org,%.html,$<) $@

doc: $(manual_html)

build/release/liboidadb.so: .force
	cmake --build build/cmakerel --target oidadb -v

includesrc = $(wildcard c.src/include/*.h)
oidadb-release.tar.gz: $(manual_src) $(manual_html) build/release/liboidadb.so $(includesrc)
	@mkdir -p build/packaged/include
	@mkdir -p build/packaged/manual-html
	@mkdir -p build/packaged/manual-org
	cp build/release/liboidadb.so build/packaged
	cp $(includesrc) build/packaged/include
	cp $(manual_html) build/packaged/manual-html
	cp $(manual_src) build/packaged/manual-org
	cd build/ && tar -czf oidadb-release.tar.gz -C packaged .

release: oidadb-release.tar.gz

PUBLISHDATE=$(shell date '+%F')
BUILDVERSION := $(shell git describe --tags --abbrev=0 2>/dev/null || echo "v0.0.0")
COMMITS=$(shell git rev-list --all --count)
LASTCOMMIT=$(shell git log -1 --format=%cI)
REVISION=$(shell git log -1 --format=%H)
TODOCOUNT=$(shell grep -rne 'todo:' | wc -l)
LINECOUNT=$(shell ( find ./spec ./c.src ./man -type f -print0 | xargs -0 cat ) | wc -l)
build/metrics.m4: .force
	@mkdir -p build
	echo 'dnl' > $@
	echo 'define(PUBLISHDATE, $(PUBLISHDATE))dnl' >> $@
	echo 'define(COMMITS, $(COMMITS))dnl' >> $@
	echo 'define(LASTCOMMIT, $(LASTCOMMIT))dnl' >> $@
	echo 'define(BUILDVERSION, $(BUILDVERSION))dnl' >> $@
	echo 'define(REVISION, $(REVISION))dnl' >> $@
	echo 'define(LINECOUNT, $(LINECOUNT))dnl' >> $@
	echo 'define(TODOCOUNT, $(TODOCOUNT))dnl' >> $@


clean:
	-rm -r build

.PHONY: publish .force clean doc release
