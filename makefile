build        := $(if $(build),$(build),build)



# Testing
test:
	@mkdir -p $(build)/test
	build=../$(build)/release $(MAKE) test -C liboidadb.src

# the library
$(build)/release/liboidadb.so:
	@mkdir -p `dirname $@`
	build=../$(build)/release $(MAKE) -C liboidadb.src

# odb (s)hell
$(build)/utils/odbs:
	@mkdir -p `dirname $@`
	build=../$(build)/utils $(MAKE) -C odbs.src

# The manual
$(build)/man:
	@mkdir -p `dirname $@`
	build=../$(build)/man $(MAKE) -C man	


# PACKAGING
# (builds + manual)

includesrc = $(wildcard src/include/*.h)
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

publish: build/publish-index.html
	@mkdir -p build
	cd build && timeout 30 ../scripts/ftp-publish.sh publish-index.html


PUBLISHDATE=$(shell date '+%F')
BUILDVERSION := $(shell git describe --tags --abbrev=0 2>/dev/null || echo "v0.0.0")
COMMITS=$(shell git rev-list --all --count)
LASTCOMMIT=$(shell git log -1 --format=%cI)
REVISION=$(shell git log -1 --format=%H)
TODOCOUNT=$(shell grep -rne 'todo:' | wc -l)
LINECOUNT=$(shell ( find ./spec ./src ./man -type f -print0 | xargs -0 cat ) | wc -l)
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

.PHONY: .force clean manual test release build
