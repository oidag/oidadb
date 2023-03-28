#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include "dialog_u.h"
#include "../../odb-structures.h"

typedef struct dialog_index_t {
	odb_spec_head head;
	odb_spec_index index;
	odb_spec_index_entry index_entry;
	odb_spec_index_entry deleted_entry;
	odb_spec_index_entry structure_entry;

} dialog_index_t;
dialog_index_t settings = (dialog_index_t){

};

static const char *type2str(odb_type t) {
	switch (t) {
		case(ODB_ELMINIT): return "ODB_ELMINIT";
		case(ODB_ELMDEL):  return "ODB_ELMDEL";
		case(ODB_ELMSTRCT):return "ODB_ELMSTRCT";
		case(ODB_ELMOBJ):  return "ODB_ELMOBJ";
		case(ODB_ELMENTS): return "ODB_ELMENTS";
		case(ODB_ELMPEND): return "ODB_ELMPEND";
		case(ODB_ELMLOOKUP):return "ODB_ELMLOOKUP";
		default: return "UNKNOWN";
	}
}

static void draw(int width) {
	glLoadIdentity();
	glBegin(GL_QUADS);
	color_glset(color_cyan700);
	glVertex2f(-1,-1);
	glVertex2f(-1,1);
	glVertex2f(1,1);
	glVertex2f(1,-1);
	glEnd();

	// dump the entries
	const int entriesperpage = settings.head.intro.entrysize;

}

void dialog_index_start() {
	dialog_start();
	dialog_drawbody(draw);
	dialog_settitle("index");
	//settings = settings;
	dialog_invalidatef();

	return;

	// todo: testing... open the file and all that shit.
	const char *file = "t0001_io.oidadb";
	int fd = open(file, O_RDONLY);
	if(fd == -1)
	{
		perror("open");
	}
	term_log("file opened : %s", file);
	read(fd, &settings.head, sizeof(odb_spec_head));
	term_log("file id[0:8]: %0.8x",  *(int *)settings.head.intro.id);
	unsigned int indexstart = settings.head.intro.pagemul * settings.head.intro
			.pagesize;
	term_log("index start : 0x%x", indexstart);
	lseek(fd, indexstart, SEEK_SET);
	read(fd, &settings.index, sizeof(odb_spec_index));
	term_log("index found : %s", type2str(settings.index.head.ptype));
	read(fd, &settings.index_entry, sizeof(odb_spec_index_entry));
	term_log("index pages : %ld", settings.index_entry.ref0c);
	term_log("ents/indx.  : %d", settings.index_entry.objectsperpage);
	read(fd, &settings.deleted_entry, sizeof(odb_spec_index_entry));
	term_log("delete pages: %ld", settings.deleted_entry.ref0c);
	read(fd, &settings.structure_entry, sizeof(odb_spec_index_entry));
	term_log("struct pages: %ld", settings.structure_entry.ref0c);
	//term_log("Must work\n");
	//term_log("Must work\nBery guud.\n");

}