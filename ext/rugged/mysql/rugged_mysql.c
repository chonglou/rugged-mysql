#include "rugged_mysql.h"

VALUE rb_mRugged;
VALUE rb_mRuggedMysql;
VALUE rb_cRuggedBackend;

void
Init_rugged_mysql (void)
{
  rb_mRugged = rb_const_get (rb_cObject, rb_intern ("Rugged"));
  rb_cRuggedBackend = rb_const_get (rb_mRugged, rb_intern ("Backend"));
  rb_mRuggedMysql = rb_const_get (rb_mRugged, rb_intern ("Mysql"));
  Init_rugged_mysql_backend ();
}
