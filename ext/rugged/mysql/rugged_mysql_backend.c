#include <git2.h>
#include <git2/sys/odb_backend.h>
#include <git2/sys/refdb_backend.h>
#include <rugged.h>

#include "rugged_mysql.h"

extern VALUE rb_mRuggedMysql;
extern VALUE rb_cRuggedBackend;

VALUE rb_cRuggedMysqlBackend;

int git_odb__error_ambiguous(char *);
int git_odb__error_notfound(char *);
int git_odb__error_ambiguous(char *);

typedef struct _rugged_backend {
	int (*odb_backend) (git_odb_backend ** backend_out,
			    struct _rugged_backend * backend);
	int (*refdb_backend) (git_refdb_backend ** backend_out,
			      struct _rugged_backend * backend);
} rugged_backend;

typedef struct {
	rugged_backend backend;
	char *host;
	int port;
	char *socket;
	char *username;
	char *password;
	char *database;
} rugged_mysql_backend;

static void rb_rugged_mysql_backend__free(rugged_mysql_backend * backend)
{
	free(backend->host);
	free(backend->socket);
	free(backend->username);
	if (backend->password != NULL) {
		free(backend->password);
	}
	free(backend->database);
	free(backend);
}

static int
rugged_mysql__odb_backend(git_odb_backend ** backend_out,
			  rugged_backend * backend)
{
	rugged_mysql_backend *rugged_backend = (rugged_mysql_backend *) backend;

	return git_odb_backend_mysql(backend_out, rugged_backend->host,
				     rugged_backend->port,
				     rugged_backend->socket,
				     rugged_backend->username,
				     rugged_backend->password,
				     rugged_backend->database, NULL);
}

static int
rugged_mysql__refdb_backend(git_refdb_backend ** backend_out,
			    rugged_backend * backend)
{
	rugged_mysql_backend *rugged_backend = (rugged_mysql_backend *) backend;

	return git_refdb_backend_mysql(backend_out, rugged_backend->host,
				       rugged_backend->port,
				       rugged_backend->socket,
				       rugged_backend->username,
				       rugged_backend->password,
				       rugged_backend->database, NULL);
}

static rugged_mysql_backend *rugged_mysql_backend_new(char *host, int port,
						      char *socket,
						      char *username,
						      char *password,
						      char *database)
{
	rugged_mysql_backend *mysql_backend =
	    malloc(sizeof(rugged_mysql_backend));

	mysql_backend->backend.odb_backend = rugged_mysql__odb_backend;
	mysql_backend->backend.refdb_backend = rugged_mysql__refdb_backend;

	mysql_backend->host = strdup(host);
	mysql_backend->port = port;
	mysql_backend->socket = strdup(socket);
	mysql_backend->username = strdup(username);
	mysql_backend->password = password == NULL ? NULL : strdup(password);

	return mysql_backend;
}

/*
Public: Initialize a mysql backend.
opts - hash containing the connection options.
:host - (optional) string, default localhost
:port - (optional) integer, default 3306
:password - (optional) string, default NULL
:socket - (optional) string, default /var/run/mysqld/mysqld.sock
:username - (optional) string, default root
:database - string
*/
static VALUE rb_rugged_mysql_backend_new(VALUE klass, VALUE rb_opts)
{
	VALUE val;
	char *host;
	char *socket = "/var/run/mysqld/mysqld.sock";
	char *database;
	char *username = "root";
	char *password = NULL;
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

	if ((val = rb_hash_aref(rb_opts, ID2SYM(rb_intern("socket")))) != Qnil) {
		Check_Type(val, T_STRING);
		socket = StringValueCStr(val);
	}

	if ((val =
	     rb_hash_aref(rb_opts, ID2SYM(rb_intern("username")))) != Qnil) {
		Check_Type(val, T_STRING);
		username = StringValueCStr(val);
	}

	if ((val =
	     rb_hash_aref(rb_opts, ID2SYM(rb_intern("password")))) != Qnil) {
		Check_Type(val, T_STRING);
		password = StringValueCStr(val);
	}

	val = rb_hash_aref(rb_opts, ID2SYM(rb_intern("database")));
	Check_Type(val, T_STRING);
	database = StringValueCStr(val);

	return Data_Wrap_Struct(klass, NULL, rb_rugged_mysql_backend__free,
				rugged_mysql_backend_new(host, port, socket,
							 username, password,
							 database));
}

void Init_rugged_mysql_backend(void)
{
	rb_cRuggedMysqlBackend =
	    rb_define_class_under(rb_mRuggedMysql, "Backend",
				  rb_cRuggedBackend);
	rb_define_singleton_method(rb_cRuggedMysqlBackend, "new",
				   rb_rugged_mysql_backend_new, 1);
}
