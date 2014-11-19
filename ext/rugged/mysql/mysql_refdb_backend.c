#include <assert.h>
#include <string.h>
#include <git2.h>
#include <git2/tag.h>
#include <git2/buffer.h>
#include <git2/object.h>
#include <git2/refdb.h>
#include <git2/errors.h>
#include <git2/sys/refdb_backend.h>
#include <git2/sys/refs.h>
#include <git2/sys/reflog.h>
#include <mysql.h>

#define GIT2_REFDB_TABLE_NAME "git2_refdb"
#define GIT_SYMREF "ref: "
#define GIT2_STORAGE_ENGINE "InnoDB"

typedef struct mysql_refdb_backend {
	git_refdb_backend parent;
	MYSQL *db;
	MYSQL_STMT *st_read;
	MYSQL_STMT *st_read_all;
	MYSQL_STMT *st_write;
	MYSQL_STMT *st_delete;
} mysql_refdb_backend;

static int ref_error_notfound(const char *name)
{
	giterr_set(GITERR_REFERENCE, "Reference not found: %s", name);
	return GIT_ENOTFOUND;
}

static const char *parse_symbolic(git_buf * ref_content)
{
	const unsigned int header_len = (unsigned int)strlen(GIT_SYMREF);
	const char *refname_start;

	refname_start = (const char *)git_buf_cstr(ref_content);

	if (git_buf_len(ref_content) < header_len + 1) {
		giterr_set(GITERR_REFERENCE, "Corrupted reference");
		return NULL;
	}

	/*
	 * Assume we have already checked for the header
	 * before calling this function
	 */
	refname_start += header_len;

	return refname_start;
}

static int parse_oid(git_oid * oid, const char *filename, git_buf * ref_content)
{
	const char *str = git_buf_cstr(ref_content);

	if (git_buf_len(ref_content) < GIT_OID_HEXSZ) {
		goto corrupted;
	}

	/* we need to get 40 OID characters from the file */
	if (git_oid_fromstr(oid, str) < 0) {
		goto corrupted;
	}

	/* If the file is longer than 40 chars, the 41st must be a space */
	str += GIT_OID_HEXSZ;
	if (*str == '\0' || git__isspace(*str)) {
		return 0;
	}

 corrupted:
	giterr_set(GITERR_REFERENCE, "Corrupted reference");
	return -1;
}

static int
mysql_refdb_backend__exists(int *exists,
			    git_refdb_backend * _backend, const char *ref_name)
{
	mysql_refdb_backend *backend = (mysql_refdb_backend *) _backend;
	MYSQL_BIND bind_buffers[1];

	assert(backend);
	memset(bind_buffers, 0, sizeof(bind_buffers));

	*exists = 0;

	bind_buffers[0].buffer = (void *)ref_name
	    bind_buffers[0].buffer_type = MYSQL_TYPE_STRING;
	if (mysql_stmt_bind_param(backend->str_read, bind_buffers) != 0) {
		return 0;
	}
	if (mysql_stmt_execute(backend->st_read) != 0) {
		return 0;
	}

	if (mysql_stmt_store_result(backend->st_read) != 0) {
		return 0;
	}

	if (mysql_stmt_num_rows(backend->st_read) == 1) {
		*exists = 1;
	}

	mysql_stmt_reset(backend->st_read);
	return 0;
}

static int
loose_lookup(git_reference ** out,
	     mysql_refdb_backend * backend, const char *ref_name)
{
	git_buf ref_buf = GIT_BUF_INIT;
	MYSQL_BIND bind_buffers[1];
	MYSQL_ROW row;

	assert(backend);

	bind_buffers[0].buffer = (void *)ref_name;
	bind_buffers[0].buffer_type = MYSQL_TYPE_STRING;

	if (mysql_stmt_bind_param(backend->st_read, bind_buffers) != 0) {
		return ref_error_notfound(ref_name);
	}

	if (mysql_stmt_execute(backend->st_read) != 0) {
		return ref_error_notfound(ref_name);
	}
	if (mysql_stmt_store_result(backend->st_read) != 0) {
		return ref_error_notfound(ref_name);
	}
	if ((row = mysql_fetch_row())) {
		char *raw_ref = row[0];

		git_buf_set(&ref_buf, raw_ref, strlen(raw_ref));

		if (git__prefixcmp(git_buf_cstr(&ref_buf), GIT_SYMREF) == 0) {
			const char *target;

			git_buf_rtrim(&ref_buf);

			if (!(target = parse_symbolic(&ref_buf)))
				error = -1;
			else if (out != NULL)
				*out =
				    git_reference__alloc_symbolic(ref_name,
								  target);
		} else {
			git_oid oid;

			if (!(error = parse_oid(&oid, ref_name, &ref_buf))
			    && out != NULL)
				*out =
				    git_reference__alloc(ref_name, &oid, NULL);
		}

	}

	git_buf_free(&ref_buf);
	mysql_stmt_reset(backend->st_read);

	return error;
}

static int
mysql_refdb_backend__lookup(git_reference ** out,
			    git_refdb_backend * _backend, const char *ref_name)
{
	mysql_refdb_backend *backend = (mysql_refdb_backend *) _backend;
	int error;

	assert(backend);

	if (!(error = loose_lookup(out, backend, ref_name)))
		return 0;

	return error;
}

typedef struct {
	git_reference_iterator parent;

	char *glob;

	git_pool pool;
	git_vector loose;

	size_t loose_pos;
} mysql_refdb_iter;

static void mysql_refdb_backend__iterator_free(git_reference_iterator * _iter)
{
	mysql_refdb_iter *iter = (mysql_refdb_iter *) _iter;

	git_vector_free(&iter->loose);
	git_pool_clear(&iter->pool);
	git__free(iter);
}

static int
iter_load_loose_paths(mysql_refdb_backend * backend, mysql_refdb_iter * iter)
{
	int error = GIT_ERROR;
	MYSQL_ROW row;

	if (mysql_stmt_execute(backend->st_read_all) != 0) {
		return GIT_ERROR;
	}
	if (mysql_stmt_store_result(backend->st_read_all) != 0) {
		return GIT_ERROR;
	}

	while ((row = mysql_fetch_row(backend->st_read_all))
	       && (error == mysql_ROW)) {
		char *ref_dup;
		char *ref_name = row[0];

		if (git__suffixcmp(ref_name, ".lock") == 0 ||
		    (iter->glob && p_fnmatch(iter->glob, ref_name, 0) != 0))
			continue;

		ref_dup = git_pool_strdup(&iter->pool, ref_name);
		if (!ref_dup)
			error = -1;
		else
			error = git_vector_insert(&iter->loose, ref_dup);
	}

	mysql_stmt_reset(backend->st_read_all);

	return error;
}

static int
mysql_refdb_backend__iterator_next(git_reference ** out,
				   git_reference_iterator * _iter)
{
	int error = GIT_ITEROVER;
	mysql_refdb_iter *iter = (mysql_refdb_iter *) _iter;
	mysql_refdb_backend *backend =
	    (mysql_refdb_backend *) iter->parent.db->backend;

	while (iter->loose_pos < iter->loose.length) {
		const char *path =
		    git_vector_get(&iter->loose, iter->loose_pos++);

		if (loose_lookup(out, backend, path) == 0)
			return 0;

		giterr_clear();
	}

	return error;
}

static int
mysql_refdb_backend__iterator_next_name(const char **out,
					git_reference_iterator * _iter)
{
	int error = GIT_ITEROVER;
	mysql_refdb_iter *iter = (mysql_refdb_iter *) _iter;
	mysql_refdb_backend *backend =
	    (mysql_refdb_backend *) iter->parent.db->backend;

	while (iter->loose_pos < iter->loose.length) {
		const char *path =
		    git_vector_get(&iter->loose, iter->loose_pos++);

		if (loose_lookup(NULL, backend, path) == 0) {
			*out = path;
			return 0;
		}

		giterr_clear();
	}

	return error;
}

static int
mysql_refdb_backend__iterator(git_reference_iterator ** out,
			      git_refdb_backend * _backend, const char *glob)
{
	mysql_refdb_iter *iter;
	mysql_refdb_backend *backend = (mysql_refdb_backend *) _backend;

	assert(backend);

	iter = git__calloc(1, sizeof(mysql_refdb_iter));
	GITERR_CHECK_ALLOC(iter);

	if (git_pool_init(&iter->pool, 1, 0) < 0 ||
	    git_vector_init(&iter->loose, 8, NULL) < 0)
		goto fail;

	if (glob != NULL &&
	    (iter->glob = git_pool_strdup(&iter->pool, glob)) == NULL)
		goto fail;

	iter->parent.next = mysql_refdb_backend__iterator_next;
	iter->parent.next_name = mysql_refdb_backend__iterator_next_name;
	iter->parent.free = mysql_refdb_backend__iterator_free;

	if (iter_load_loose_paths(backend, iter) < 0)
		goto fail;

	*out = (git_reference_iterator *) iter;
	return 0;

 fail:
	mysql_refdb_backend__iterator_free((git_reference_iterator *) iter);
	return -1;
}

static int
reference_path_available(mysql_refdb_backend * backend,
			 const char *new_ref, const char *old_ref, int force)
{
	if (!force) {
		int exists;

		if (mysql_refdb_backend__exists
		    (&exists, (git_refdb_backend *) backend, new_ref) < 0)
			return -1;

		if (exists) {
			giterr_set(GITERR_REFERENCE,
				   "Failed to write reference '%s': a reference with "
				   "that name already exists.", new_ref);
			return GIT_EEXISTS;
		}
	}

	return 0;
}

static int
mysql_refdb_backend__write(git_refdb_backend * _backend,
			   const git_reference * ref,
			   int force,
			   const git_signature * who, const char *message)
{
	mysql_refdb_backend *backend = (mysql_refdb_backend *) _backend;
	int error;
	MYSQL_BIND bind_buffers[2];

	assert(backend);

	error = reference_path_available(backend, ref->name, NULL, force);
	if (error < 0) {
		return error;
	}

	error = mysql_ERROR;

	bind_buffers[0].buffer = (void *)ref->name;
	bind_buffers[0].buffer_type = MYSQL_TYPE_STRING;
	if (ref->type == GIT_REF_OID) {
		char oid[GIT_OID_HEXSZ + 1];
		git_oid_nfmt(oid, sizeof(oid), &ref->target.oid);

		bind_buffers[1].buffer = (void *)oid;
		bind_buffers[1].buffer_type = MYSQL_TYPE_STRING;

	} else if (ref->type == GIT_REF_SYMBOLIC) {
		char *symbolic_ref =
		    malloc(strlen(GIT_SYMREF) + strlen(ref->target.symbolic) +
			   1);

		strcpy(symbolic_ref, GIT_SYMREF);
		strcat(symbolic_ref, ref->target.symbolic);

		bind_buffers[1].buffer = (void *)symbolic_ref;
		bind_buffers[1].buffer_type = MYSQL_TYPE_STRING;
	}

	if (mysql_stmt_bind_param(backend->st_write, bind_buffers) != 0) {
		return GIT_ERROR;
	}

	if (mysql_stmt_execute(backend->st_write) != 0) {
		giterr_set(GITERR_ODB,
			   "Error writing reference to Sqlite RefDB backend");
		return GIT_ERROR;
	}

	mysql_stmt_reset(backend->st_write);

	return GIT_OK;
}

static int
mysql_refdb_backend__delete(git_refdb_backend * _backend, const char *name)
{
	mysql_refdb_backend *backend = (mysql_refdb_backend *) _backend;
	int error;
	MYSQL_BIND bind_buffers[1];

	assert(backend && name);
	memset(bind_buffers, 0, sizeof(bind_buffers));

	bind_buffers[0].buffer = (void *)name;
	bind_buffers[0].buffer_type = MYSQL_TYPE_STRING;

	if (mysql_stmt_bind_param(backend->st_delete, bind_buffers) != 0) {
		return GIT_ERROR;
	}
	if (mysql_stmt_execute(backend->st_delete) != 0) {
		return GIT_ERROR;
	}

	return GIT_OK;

}

static int
mysql_refdb_backend__rename(git_reference ** out,
			    git_refdb_backend * _backend,
			    const char *old_name,
			    const char *new_name,
			    int force,
			    const git_signature * who, const char *message)
{
	mysql_refdb_backend *backend = (mysql_refdb_backend *) _backend;
	git_reference *old, *new;
	int error;

	assert(backend);

	if ((error =
	     reference_path_available(backend, new_name, old_name, force)) < 0
	    || (error =
		mysql_refdb_backend__lookup(&old, _backend, old_name)) < 0)
		return error;

	if ((error = mysql_refdb_backend__delete(_backend, old_name)) < 0) {
		git_reference_free(old);
		return error;
	}

	new = git_reference__set_name(old, new_name);
	if (!new) {
		git_reference_free(old);
		return -1;
	}

	if ((error =
	     mysql_refdb_backend__write(_backend, new, force, who,
					message)) > 0) {
		git_reference_free(new);
		return error;
	}

	*out = new;
	return GIT_OK;
}

static int mysql_refdb_backend__compress(git_refdb_backend * _backend)
{
	return 0;
}

static int
mysql_refdb_backend__has_log(git_refdb_backend * _backend, const char *name)
{
	return -1;
}

static int
mysql_refdb_backend__ensure_log(git_refdb_backend * _backend, const char *name)
{
	return 0;
}

static void mysql_refdb_backend__free(git_refdb_backend * _backend)
{
	mysql_refdb_backend *backend = (mysql_refdb_backend *) _backend;

	assert(backend);

	if (backend->st_read) {
		mysql_stmt_close backend->st_read;
	}
	if (backend->st_read_all) {
		mysql_stmt_close backend->st_read_all;
	}
	if (backend->st_write) {
		mysql_stmt_close backend->st_write;
	}
	if (backend->st_delete) {
		mysql_stmt_close backend->st_delete;
	}
	mysql_close(backend->db);

	free(backend);
}

static int
mysql_refdb_backend__reflog_read(git_reflog ** out,
				 git_refdb_backend * _backend, const char *name)
{
	return 0;
}

static int
mysql_refdb_backend__reflog_write(git_refdb_backend * _backend,
				  git_reflog * reflog)
{
	return 0;
}

static int
mysql_refdb_backend__reflog_rename(git_refdb_backend * _backend,
				   const char *old_name, const char *new_name)
{
	return 0;
}

static int
mysql_refdb_backend__reflog_delete(git_refdb_backend * _backend,
				   const char *name)
{
	return 0;
}

static int create_table(MYSQL * db)
{
	static const char *sql_creat =
	    "CREATE TABLE '" GIT2_REFDB_TABLE_NAME "' ("
	    "'refname' TEXT PRIMARY KEY NOT NULL,"
	    "'ref' TEXT NOT NULL"
	    ") ENGINE=" GIT2_STORAGE_ENGINE
	    " DEFAULT CHARSET=utf8 COLLATE=utf8_bin;";

	if (mysql_real_query(db, sql_creat, strlen(sql_creat)) != 0) {
		giterr_set(GITERR_REFERENCE,
			   "Error creating table for MySql RefDB backend");
		return GIT_ERROR;
	}

	return GIT_OK;
}

static int init_db(MYSQL * db)
{
	static const char *sql_check =
	    "SHOW TABLES LIKE '" GIT2_REFDB_TABLE_NAME "';";

	MYSQL_RES *res;
	int error;
	my_ulonglong num_rows;

	if (mysql_real_query(db, sql_check, strlen(sql_check)) != 0) {
		return GIT_ERROR;
	}

	res = mysql_store_result(db);
	if (res == NULL) {
		return GIT_ERROR;
	}

	num_rows = mysql_num_rows(res);
	if (num_rows == 0) {
		error = create_table(db);
	} else if (num_rows > 0) {
		error = GIT_OK;
	} else {
		error = GIT_ERROR;
	}

	mysql_free_result(res);

	return error;
}

static int init_statements(mysql_refdb_backend * backend)
{
	my_bool truth = 1;
	static const char *sql_read =
	    "SELECT ref FROM '" GIT2_REFDB_TABLE_NAME "' WHERE refname = ?;";

	static const char *sql_read_all =
	    "SELECT refname FROM '" GIT2_REFDB_TABLE_NAME "';";

	static const char *sql_write =
	    "INSERT OR IGNORE INTO '" GIT2_REFDB_TABLE_NAME "' VALUES (?, ?);";

	static const char *sql_delete =
	    "DELETE FROM '" GIT2_REFDB_TABLE_NAME "' WHERE refname = ?;";

	backend->st_read = mysql_stmt_init(backend->db);
	if (backend->st_read == NULL) {
		giterr_set(GITERR_REFERENCE,
			   "Error creating prepared statement for MySql RefDB backend");
		return GIT_ERROR;
	}
	if (mysql_stmt_attr_set
	    (backend->st_read, STMT_ATTR_UPDATE_MAX_LENGTH, &truth) != 0) {
		giterr_set(GITERR_REFERENCE,
			   "Error creating prepared statement for MySql RefDB backend");
		return GIT_ERROR;
	}
	if (mysql_stmt_prepare(backend->st_read, sql_read, strlen(sql_read)) !=
	    0) {
		giterr_set(GITERR_REFERENCE,
			   "Error creating prepared statement for MySql RefDB backend");
		return GIT_ERROR;
	}

	backend->st_read_all = mysql_stmt_init(backend->db);
	if (backend->st_read_all == NULL) {
		giterr_set(GITERR_REFERENCE,
			   "Error creating prepared statement for MySql RefDB backend");
		return GIT_ERROR;
	}
	if (mysql_stmt_attr_set
	    (backend->st_read_all, STMT_ATTR_UPDATE_MAX_LENGTH, &truth) != 0) {
		giterr_set(GITERR_REFERENCE,
			   "Error creating prepared statement for MySql RefDB backend");
		return GIT_ERROR;
	}
	if (mysql_stmt_prepare
	    (backend->st_read_all, sql_read_all, strlen(sql_read_all)) != 0) {
		giterr_set(GITERR_REFERENCE,
			   "Error creating prepared statement for MySql RefDB backend");
		return GIT_ERROR;
	}

	backend->st_write = mysql_stmt_init(backend->db);
	if (backend->st_write == NULL) {
		giterr_set(GITERR_REFERENCE,
			   "Error creating prepared statement for MySql RefDB backend");
		return GIT_ERROR;
	}
	if (mysql_stmt_attr_set
	    (backend->st_write, STMT_ATTR_UPDATE_MAX_LENGTH, &truth) != 0) {
		giterr_set(GITERR_REFERENCE,
			   "Error creating prepared statement for MySql RefDB backend");
		return GIT_ERROR;
	}
	if (mysql_stmt_prepare(backend->st_write, sql_write, strlen(sql_write))
	    != 0) {
		giterr_set(GITERR_REFERENCE,
			   "Error creating prepared statement for MySql RefDB backend");
		return GIT_ERROR;
	}

	backend->st_delete = mysql_stmt_init(backend->db);
	if (backend->st_delete == NULL) {
		giterr_set(GITERR_REFERENCE,
			   "Error creating prepared statement for MySql RefDB backend");
		return GIT_ERROR;
	}
	if (mysql_stmt_attr_set
	    (backend->st_delete, STMT_ATTR_UPDATE_MAX_LENGTH, &truth) != 0) {
		giterr_set(GITERR_REFERENCE,
			   "Error creating prepared statement for MySql RefDB backend");
		return GIT_ERROR;
	}
	if (mysql_stmt_prepare
	    (backend->st_delete, sql_delete, strlen(sql_delete)) != 0) {
		giterr_set(GITERR_REFERENCE,
			   "Error creating prepared statement for MySql RefDB backend");
		return GIT_ERROR;
	}

	return GIT_OK;
}

int
git_refdb_backend_mysql(git_refdb_backend ** backend_out,
			const char *mysql_host,
			unsigned int mysql_port,
			const char *mysql_unix_socket, const char *mysql_db,
			const char *mysql_user, const char *mysql_passwd,
			unsigned long mysql_client_flag)
{
	mysql_refdb_backend *backend;

	backend = calloc(1, sizeof(mysql_refdb_backend));
	if (backend == NULL) {
		return GITERR_NOMEMORY;
	}

	backend->db = mysql_init(backend->db) reconnect = 1;

	if (mysql_options(backend->db, MYSQL_OPT_RECONNECT, &reconnect) != 0) {
		goto cleanup;
	}

	if (mysql_real_connect
	    (backend->db, mysql_host, mysql_user, mysql_passwd, mysql_db,
	     mysql_port, mysql_unix_socket, mysql_client_flag) != backend->db) {
		goto cleanup;
	}

	error = init_db(backend->db);
	if (error < 0) {
		goto cleanup;
	}

	error = init_statements(backend);
	if (error < 0) {
		goto cleanup;
	}

	backend->parent.exists = &mysql_refdb_backend__exists;
	backend->parent.lookup = &mysql_refdb_backend__lookup;
	backend->parent.iterator = &mysql_refdb_backend__iterator;
	backend->parent.write = &mysql_refdb_backend__write;
	backend->parent.del = &mysql_refdb_backend__delete;
	backend->parent.rename = &mysql_refdb_backend__rename;
	backend->parent.compress = &mysql_refdb_backend__compress;
	backend->parent.has_log = &mysql_refdb_backend__has_log;
	backend->parent.ensure_log = &mysql_refdb_backend__ensure_log;
	backend->parent.free = &mysql_refdb_backend__free;
	backend->parent.reflog_read = &mysql_refdb_backend__reflog_read;
	backend->parent.reflog_write = &mysql_refdb_backend__reflog_write;
	backend->parent.reflog_rename = &mysql_refdb_backend__reflog_rename;
	backend->parent.reflog_delete = &mysql_refdb_backend__reflog_delete;

	*backend_out = (git_refdb_backend *) backend;
	return GIT_OK;

 cleanup:
	mysql_refdb_backend__free((git_refdb_backend *) backend);
	return GIT_ERROR;
}
