#include "rubbed_mysql.h"

VALUE rb_mRugged;
VALUE rb_mRuggedMysql;
VALUE rb_cRuggedBackend;
VALUE rb_cRuggedMysqlBackend;

void Init_rugged_mysql(void) {
    rb_mRugged = rb_const_get(rb_cObject, rb_intern("Rugged"));
    rb_cRuggedBackend = rb_const_get(rb_mRugged, rb_intern("Backend"));
    rb_mRuggedMysql = rb_const_get(rb_mRugged, rb_intern("Mysql"));
    Init_rugged_mysql_backend();
}

void Init_rugged_mysql_backend(void) {
    rb_cRuggedMysqlBackend = rb_define_class_under(rb_mRuggedMysql, "Backend", rb_cRuggedBackend);
    rb_define_singleton_method(rb_cRuggedMysqlBackend, "new", rb_rugged_mysql_backend_new, 1);
}

/*
Public: Initialize a mysql backend.
opts - hash containing the connection options.
:host - (optional)string, default localhost
:port - (optional)integer, default 3306
:username - string
:password - (optional) string, default null
:database - string
*/
static VALUE rb_rugged_mysql_backend_new(VALUE klass, VALUE rb_opts) {
    VALUE val;

    char *host = "localhost";
    char *password = NULL;
    char *database;
    char *username;
    int port = 3306;

    Check_Type(rb_opts, T_HASH);

    if ((val = rb_hash_aref(rb_opts, ID2SYM(rb_intern("host")))) != Qnil) {
        Check_Type(val, T_STRING);
        host = StringValueCStr(val);
    }

    if ((val = rb_hash_aref(rb_opts, ID2SYM(rb_intern("port")))) != Qnil) {
        Check_Type(val, T_FIXNUM);
        port = NUM2INT(val);
    }

    val = rb_hash_aref(rb_opts, ID2SYM(rb_intern("username")));
    Check_Type(val, T_STRING);
    username = StringValueCStr(val);

    if ((val = rb_hash_aref(rb_opts, ID2SYM(rb_intern("password")))) != Qnil) {
        Check_Type(val, T_STRING);
        password = StringValueCStr(val);
    }

    val = rb_hash_aref(rb_opts, ID2SYM(rb_intern("database")));
    Check_Type(val, T_STRING);
    database = StringValueCStr(val);

    git_odb_backend *backend;
    git_odb_backend_mysql(&backend, host,
            username, password, database,
            port, NULL, NULL)
    return Data_Wrap_Struct(klass, NULL, mysql_backend__free, rugged_redis_backend_new(host, port, password));

}