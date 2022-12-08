
publish: build/manual.html build/publish-index.html
	@mkdir -p build
	cd build && ../scripts/ftp-publish.sh publish-index.html
	cd build && ../scripts/ftp-publish.sh manual.html

build/publish-index.html:  spec/publish-index.m4.html build/metrics.m4
	@mkdir -p build
	m4 build/metrics.m4 $< > $@

build/manual.html: spec/manual.org
	@mkdir -p build
	emacs $< $@ --batch --kill -f org-html-export-to-html


PUBLISHDATE=$(shell date '+%F')
COMMITS=$(shell git rev-list --all --count)
LASTCOMMIT=$(shell git log -1 --format=%cI)
REVISION=$(shell git log -1 --format=%H)
LINECOUNT=$(shell ( find ./c.src -type f -print0 | xargs -0 cat ) | wc -l)
SPECLINECOUNT=$(shell ( find ./spec -type f -print0 | xargs -0 cat ) | wc -l)
build/metrics.m4: .force
	@mkdir -p build
	echo 'dnl' > $@
	echo 'define(PUBLISHDATE, $(PUBLISHDATE))dnl' >> $@
	echo 'define(COMMITS, $(COMMITS))dnl' >> $@
	echo 'define(LASTCOMMIT, $(LASTCOMMIT))dnl' >> $@
	echo 'define(REVISION, $(REVISION))dnl' >> $@
	echo 'define(LINECOUNT, $(LINECOUNT))dnl' >> $@
	echo 'define(SPECLINECOUNT, $(SPECLINECOUNT))dnl' >> $@


clean:
	-rm -r build

.PHONY: publish .force clean
