#ifndef odbs_h_
#define odbs_h_

#include <oidadb/oidadb.h>
#include <oidadb-internal/odbfile.h>

#include "cli.h"

// helpers?
const odb_spec_struct_struct *odbfile_stk(odb_sid sid); // returns 0 on error/eof
const odb_spec_index_entry   *odbfile_ent(odb_eid eid); // returns 0 on error/eof
const odb_spec_head          *odbfile_head();
const void *odbfile_page(odb_pid);
unsigned int odbfile_pagesize();

int index_print();
int page_print();
int print_btree();
int print_obj();


#endif