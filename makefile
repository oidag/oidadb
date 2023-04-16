# Library source code
lib_src := $(wildcard find c.src/*.c)

# TESTING
test_src := $(wildcard c.src/tests/*_t.c)
test_exec := $(patsubst c.src/tests/%_t.c,build/tests/%_t,$(test_src))
lib_obj_test := $(patsubst c.src/%.c,build/tests/%.o,$(lib_src))

build/tests/%.o: c.src/%.c
	@mkdir -p `dirname $@`
	gcc $(gcc_compile_args) -c $^ -o $@

build/tests/%: gcc_compile_args = -g
build/tests/%_t: c.src/tests/%_t.c $(lib_obj_test)
	@mkdir -p `dirname $@`
	gcc $(gcc_compile_args) -o $@ $^
	$@

test: $(test_exec)


# MANUAL
manual_src := $(wildcard man/*.org)
manual_html:= $(patsubst man/%.org,build/man/%.html,$(manual_src))
build/man/%.html: man/%.org
	@mkdir -p `dirname $@`
	emacs $< -Q --batch --kill --eval '(org-html-export-to-html)'
	mv $(patsubst %.org,%.html,$<) $@

manual: $(manual_html)


# RELEASE BUILDS
# note: debug/test builds are done with cmake, not this makefile
lib_obj_release := $(patsubst c.src/%.c,build/release/%.o,$(lib_src))

build/release/%.o: gcc_compile_args = -fvisibility=hidden -fPIC -O3 -D_ODB_CD_RELEASE
build/release/%.o: c.src/%.c
	@mkdir -p `dirname $@`
	gcc $(gcc_compile_args) -c $^ -o $@

build/release/liboidadb.so: gcc_link_args=-fPIC -O3 -s -shared
build/release/liboidadb.so: $(lib_obj_release)
	@mkdir -p `dirname $@`
	gcc $(gcc_link_args) -o $@ $^

# PACKAGING
# (builds + manual)

includesrc = $(wildcard c.src/include/*.h)
build/oidadb-package.tar.gz: $(manual_src) $(manual_html) build/release/liboidadb.so $(includesrc)
	@mkdir -p build/packaged/include
	@mkdir -p build/packaged/manual-html
	@mkdir -p build/packaged/manual-org
	cp build/release/liboidadb.so build/packaged
	cp $(includesrc) build/packaged/include
	cp $(manual_html) build/packaged/manual-html
	cp $(manual_src) build/packaged/manual-org
	cd build/ && tar -czf oidadb-package.tar.gz -C packaged .

release: $(test_exec) build/oidadb-package.tar.gz
	@echo "OidaDB successfully tested, built, and packaged into 'build/oidadb-package.tar.gz'"


# PUBLISHING
# TODO: remove publishing scripts from here. I'd like to have all publishing handled by external scripts.

publish: build/manual.html build/publish-index.html
	@mkdir -p build
	cd build && timeout 30 ../scripts/ftp-publish.sh publish-index.html
	cd build && timeout 30 ../scripts/ftp-publish.sh manual.html

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

build/publish-index.html:  spec/publish-index.m4.html build/metrics.m4
	@mkdir -p build
	m4 build/metrics.m4 $< > $@


clean:
	-rm -r build

.PHONY: .force clean manual test release
