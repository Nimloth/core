/* Copyright (C) 2002 Timo Sirainen */

#include "common.h"
#include "istream.h"
#include "safe-mkdir.h"
#include "unlink-directory.h"
#include "settings.h"

#include <stdio.h>
#include <stddef.h>
#include <unistd.h>
#include <fcntl.h>
#include <pwd.h>

enum settings_type {
	SETTINGS_TYPE_ROOT,
	SETTINGS_TYPE_SERVER,
	SETTINGS_TYPE_AUTH,
	SETTINGS_TYPE_AUTH_SOCKET,
        SETTINGS_TYPE_NAMESPACE,
	SETTINGS_TYPE_SOCKET
};

struct settings_parse_ctx {
	enum settings_type type, parent_type;
	enum mail_protocol protocol;

	struct server_settings *root, *server;
	struct auth_settings *auth;
	struct socket_settings *socket;
	struct auth_socket_settings *auth_socket;
        struct namespace_settings *namespace;

	int level;
};

#define DEF(type, name) \
	{ type, #name, offsetof(struct settings, name) }

static struct setting_def setting_defs[] = {
	/* common */
	DEF(SET_STR, base_dir),
	DEF(SET_STR, log_path),
	DEF(SET_STR, info_log_path),
	DEF(SET_STR, log_timestamp),

	/* general */
	DEF(SET_STR, protocols),
	DEF(SET_STR, listen),
	DEF(SET_STR, ssl_listen),

	DEF(SET_BOOL, ssl_disable),
	DEF(SET_STR, ssl_ca_file),
	DEF(SET_STR, ssl_cert_file),
	DEF(SET_STR, ssl_key_file),
	DEF(SET_STR, ssl_parameters_file),
	DEF(SET_STR, ssl_parameters_regenerate),
	DEF(SET_STR, ssl_cipher_list),
	DEF(SET_BOOL, ssl_verify_client_cert),
	DEF(SET_BOOL, disable_plaintext_auth),
	DEF(SET_BOOL, verbose_ssl),

	/* login */
	DEF(SET_STR, login_dir),
	DEF(SET_STR, login_executable),
	DEF(SET_STR, login_user),
	DEF(SET_STR, login_greeting),

	DEF(SET_BOOL, login_process_per_connection),
	DEF(SET_BOOL, login_chroot),
	DEF(SET_BOOL, login_greeting_capability),

	DEF(SET_INT, login_process_size),
	DEF(SET_INT, login_processes_count),
	DEF(SET_INT, login_max_processes_count),
	DEF(SET_INT, login_max_logging_users),

	/* mail */
	DEF(SET_STR, valid_chroot_dirs),
	DEF(SET_STR, mail_chroot),
	DEF(SET_INT, max_mail_processes),
	DEF(SET_BOOL, verbose_proctitle),

	DEF(SET_INT, first_valid_uid),
	DEF(SET_INT, last_valid_uid),
	DEF(SET_INT, first_valid_gid),
	DEF(SET_INT, last_valid_gid),
	DEF(SET_STR, mail_extra_groups),

	DEF(SET_STR, default_mail_env),
	DEF(SET_STR, mail_cache_fields),
	DEF(SET_STR, mail_never_cache_fields),
	DEF(SET_INT, mailbox_idle_check_interval),
	DEF(SET_BOOL, mail_full_filesystem_access),
	DEF(SET_INT, mail_max_keyword_length),
	DEF(SET_BOOL, mail_save_crlf),
	DEF(SET_BOOL, mail_read_mmaped),
	DEF(SET_BOOL, mmap_disable),
	DEF(SET_BOOL, mmap_no_write),
	DEF(SET_STR, lock_method),
	DEF(SET_BOOL, maildir_stat_dirs),
	DEF(SET_BOOL, maildir_copy_with_hardlinks),
	DEF(SET_BOOL, maildir_check_content_changes),
	DEF(SET_STR, mbox_read_locks),
	DEF(SET_STR, mbox_write_locks),
	DEF(SET_INT, mbox_lock_timeout),
	DEF(SET_INT, mbox_dotlock_change_timeout),
	DEF(SET_BOOL, mbox_dirty_syncs),
	DEF(SET_BOOL, mbox_lazy_writes),
	DEF(SET_INT, umask),
	DEF(SET_BOOL, mail_drop_priv_before_exec),

	DEF(SET_STR, mail_executable),
	DEF(SET_INT, mail_process_size),
	DEF(SET_BOOL, mail_use_modules),
	DEF(SET_STR, mail_modules),
	DEF(SET_STR, mail_log_prefix),

	/* imap */
	DEF(SET_INT, imap_max_line_length),
	DEF(SET_STR, imap_capability),
	DEF(SET_STR, imap_client_workarounds),

	/* pop3 */
	DEF(SET_BOOL, pop3_no_flag_updates),
	DEF(SET_BOOL, pop3_enable_last),
	DEF(SET_STR, pop3_client_workarounds),

	{ 0, NULL, 0 }
};

#undef DEF
#define DEF(type, name) \
	{ type, #name, offsetof(struct auth_settings, name) }

static struct setting_def auth_setting_defs[] = {
	DEF(SET_STR, mechanisms),
	DEF(SET_STR, realms),
	DEF(SET_STR, default_realm),
	DEF(SET_STR, userdb),
	DEF(SET_STR, passdb),
	DEF(SET_INT, cache_size),
	DEF(SET_INT, cache_ttl),
	DEF(SET_STR, executable),
	DEF(SET_STR, user),
	DEF(SET_STR, chroot),
	DEF(SET_STR, username_chars),
	DEF(SET_STR, username_translation),
	DEF(SET_STR, anonymous_username),

	DEF(SET_BOOL, verbose),
	DEF(SET_BOOL, debug),
	DEF(SET_BOOL, ssl_require_client_cert),

	DEF(SET_INT, count),
	DEF(SET_INT, process_size),

	{ 0, NULL, 0 }
};

#undef DEF
#define DEF(type, name) \
	{ type, #name, offsetof(struct socket_settings, name) }

static struct setting_def socket_setting_defs[] = {
	DEF(SET_STR, path),
	DEF(SET_INT, mode),
	DEF(SET_STR, user),
	DEF(SET_STR, group),

	{ 0, NULL, 0 }
};

#undef DEF
#define DEF(type, name) \
	{ type, #name, offsetof(struct auth_socket_settings, name) }

static struct setting_def auth_socket_setting_defs[] = {
	DEF(SET_STR, type),

	{ 0, NULL, 0 }
};

#undef DEF
#define DEF(type, name) \
	{ type, #name, offsetof(struct namespace_settings, name) }

static struct setting_def namespace_setting_defs[] = {
	DEF(SET_STR, type),
	DEF(SET_STR, separator),
	DEF(SET_STR, prefix),
	DEF(SET_STR, location),
	DEF(SET_BOOL, inbox),
	DEF(SET_BOOL, hidden),

	{ 0, NULL, 0 }
};

struct settings default_settings = {
	MEMBER(server) NULL,
	MEMBER(protocol) 0,

	/* common */
	MEMBER(base_dir) PKG_RUNDIR,
	MEMBER(log_path) NULL,
	MEMBER(info_log_path) NULL,
	MEMBER(log_timestamp) DEFAULT_FAILURE_STAMP_FORMAT,

	/* general */
	MEMBER(protocols) "imap imaps",
	MEMBER(listen) "*",
	MEMBER(ssl_listen) NULL,

#ifdef HAVE_SSL
	MEMBER(ssl_disable) FALSE,
#else
	MEMBER(ssl_disable) TRUE,
#endif
	MEMBER(ssl_ca_file) NULL,
	MEMBER(ssl_cert_file) SSLDIR"/certs/dovecot.pem",
	MEMBER(ssl_key_file) SSLDIR"/private/dovecot.pem",
	MEMBER(ssl_parameters_file) "ssl-parameters.dat",
	MEMBER(ssl_parameters_regenerate) 24,
	MEMBER(ssl_cipher_list) NULL,
	MEMBER(ssl_verify_client_cert) FALSE,
	MEMBER(disable_plaintext_auth) TRUE,
	MEMBER(verbose_ssl) FALSE,

	/* login */
	MEMBER(login_dir) "login",
	MEMBER(login_executable) NULL,
	MEMBER(login_user) "dovecot",
	MEMBER(login_greeting) "Dovecot ready.",

	MEMBER(login_process_per_connection) TRUE,
	MEMBER(login_chroot) TRUE,
	MEMBER(login_greeting_capability) FALSE,

	MEMBER(login_process_size) 32,
	MEMBER(login_processes_count) 3,
	MEMBER(login_max_processes_count) 128,
	MEMBER(login_max_logging_users) 256,

	/* mail */
	MEMBER(valid_chroot_dirs) NULL,
	MEMBER(mail_chroot) NULL,
	MEMBER(max_mail_processes) 1024,
	MEMBER(verbose_proctitle) FALSE,

	MEMBER(first_valid_uid) 500,
	MEMBER(last_valid_uid) 0,
	MEMBER(first_valid_gid) 1,
	MEMBER(last_valid_gid) 0,
	MEMBER(mail_extra_groups) NULL,

	MEMBER(default_mail_env) NULL,
	MEMBER(mail_cache_fields) "flags",
	MEMBER(mail_never_cache_fields) "imap.envelope",
	MEMBER(mailbox_idle_check_interval) 30,
	MEMBER(mail_full_filesystem_access) FALSE,
	MEMBER(mail_max_keyword_length) 50,
	MEMBER(mail_save_crlf) FALSE,
	MEMBER(mail_read_mmaped) FALSE,
	MEMBER(mmap_disable) FALSE,
#ifdef MMAP_CONFLICTS_WRITE
	MEMBER(mmap_no_write) TRUE,
#else
	MEMBER(mmap_no_write) FALSE,
#endif
	MEMBER(lock_method) "fcntl",
	MEMBER(maildir_stat_dirs) FALSE,
	MEMBER(maildir_copy_with_hardlinks) FALSE,
	MEMBER(maildir_check_content_changes) FALSE,
	MEMBER(mbox_read_locks) "fcntl",
	MEMBER(mbox_write_locks) "dotlock fcntl",
	MEMBER(mbox_lock_timeout) 300,
	MEMBER(mbox_dotlock_change_timeout) 30,
	MEMBER(mbox_dirty_syncs) TRUE,
	MEMBER(mbox_lazy_writes) TRUE,
	MEMBER(umask) 0077,
	MEMBER(mail_drop_priv_before_exec) FALSE,

	MEMBER(mail_executable) PKG_LIBEXECDIR"/imap",
	MEMBER(mail_process_size) 256,
	MEMBER(mail_use_modules) FALSE,
	MEMBER(mail_modules) MODULEDIR"/imap",
	MEMBER(mail_log_prefix) "%Us(%u): ",

	/* imap */
	MEMBER(imap_max_line_length) 65536,
	MEMBER(imap_capability) NULL,
	MEMBER(imap_client_workarounds) "outlook-idle",

	/* pop3 */
	MEMBER(pop3_no_flag_updates) FALSE,
	MEMBER(pop3_enable_last) FALSE,
	MEMBER(pop3_client_workarounds) NULL,

	/* .. */
	MEMBER(login_uid) 0,
	MEMBER(listen_fd) -1,
	MEMBER(ssl_listen_fd) -1
};

struct auth_settings default_auth_settings = {
	MEMBER(parent) NULL,
	MEMBER(next) NULL,

	MEMBER(name) NULL,
	MEMBER(mechanisms) "plain",
	MEMBER(realms) NULL,
	MEMBER(default_realm) NULL,
	MEMBER(userdb) "passwd",
	MEMBER(passdb) "pam",
	MEMBER(cache_size) 0,
	MEMBER(cache_ttl) 3600,
	MEMBER(executable) PKG_LIBEXECDIR"/dovecot-auth",
	MEMBER(user) "root",
	MEMBER(chroot) NULL,
	MEMBER(username_chars) "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890.-_@",
	MEMBER(username_translation) "",
	MEMBER(anonymous_username) "anonymous",

	MEMBER(verbose) FALSE,
	MEMBER(debug) FALSE,
	MEMBER(ssl_require_client_cert) FALSE,

	MEMBER(count) 1,
	MEMBER(process_size) 256,

	/* .. */
	MEMBER(uid) 0,
	MEMBER(gid) 0,
	MEMBER(sockets) NULL
};

static pool_t settings_pool, settings2_pool;
struct server_settings *settings_root = NULL;

static void fix_base_path(struct settings *set, const char **str)
{
	if (*str != NULL && **str != '\0' && **str != '/') {
		*str = p_strconcat(settings_pool,
				   set->base_dir, "/", *str, NULL);
	}
}

static int get_login_uid(struct settings *set)
{
	struct passwd *pw;

	if ((pw = getpwnam(set->login_user)) == NULL) {
		i_error("Login user doesn't exist: %s", set->login_user);
		return FALSE;
	}

	if (set->server->login_gid == 0)
		set->server->login_gid = pw->pw_gid;
	else if (set->server->login_gid != pw->pw_gid) {
		i_error("All login process users must belong to same group "
			"(%s vs %s)", dec2str(set->server->login_gid),
			dec2str(pw->pw_gid));
		return FALSE;
	}

	set->login_uid = pw->pw_uid;
	return TRUE;
}

static int auth_settings_verify(struct auth_settings *auth)
{
	struct passwd *pw;

	if ((pw = getpwnam(auth->user)) == NULL) {
		i_error("Auth user doesn't exist: %s", auth->user);
		return FALSE;
	}

	if (auth->parent->defaults->login_uid == pw->pw_uid &&
	    master_uid != pw->pw_uid) {
		i_error("login_user %s (uid %s) must not be same as auth_user",
			auth->user, dec2str(pw->pw_uid));
		return FALSE;
	}
	auth->uid = pw->pw_uid;
	auth->gid = pw->pw_gid;

	if (access(t_strcut(auth->executable, ' '), X_OK) < 0) {
		i_error("Can't use auth executable %s: %m",
			t_strcut(auth->executable, ' '));
		return FALSE;
	}

	fix_base_path(auth->parent->defaults, &auth->chroot);
	if (auth->chroot != NULL && access(auth->chroot, X_OK) < 0) {
		i_error("Can't access auth chroot directory %s: %m",
			auth->chroot);
		return FALSE;
	}

	if (auth->ssl_require_client_cert) {
		/* if we require valid cert, make sure we also ask for it */
		if (auth->parent->pop3 != NULL)
			auth->parent->pop3->ssl_verify_client_cert = TRUE;
		if (auth->parent->imap != NULL)
			auth->parent->imap->ssl_verify_client_cert = TRUE;
	}

	return TRUE;
}

static int namespace_settings_verify(struct namespace_settings *ns)
{
	const char *name;

	name = ns->prefix != NULL ? ns->prefix : "";

	if (ns->separator != NULL &&
	    ns->separator[0] != '\0' && ns->separator[1] != '\0') {
		i_error("Namespace '%s': "
			"Hierarchy separator must be only one character long",
			name);
		return FALSE;
	}

	return TRUE;
}

static const char *get_directory(const char *path)
{
	char *str, *p;

	str = t_strdup_noconst(path);
	p = strrchr(str, '/');
	if (p == NULL)
		return ".";
	else {
		*p = '\0';
		return str;
	}
}

static int settings_is_active(struct settings *set)
{
	if (set->protocol == MAIL_PROTOCOL_IMAP) {
		if (strstr(set->protocols, "imap") == NULL)
			return FALSE;
	} else {
		if (strstr(set->protocols, "pop3") == NULL)
			return FALSE;
	}

	return TRUE;
}

static int settings_have_connect_sockets(struct settings *set)
{
	struct auth_settings *auth;
	struct server_settings *server;

	for (server = set->server; server != NULL; server = server->next) {
		for (auth = server->auths; auth != NULL; auth = auth->next) {
			if (auth->sockets != NULL &&
			    strcmp(auth->sockets->type, "connect") == 0)
				return TRUE;
		}
	}

	return FALSE;
}

static int settings_verify(struct settings *set)
{
	const char *dir;

	if (!get_login_uid(set))
		return FALSE;

	if (access(t_strcut(set->mail_executable, ' '), X_OK) < 0) {
		i_error("Can't use mail executable %s: %m",
			t_strcut(set->mail_executable, ' '));
		return FALSE;
	}

#ifdef HAVE_MODULES
	if (set->mail_use_modules &&
	    access(set->mail_modules, R_OK | X_OK) < 0) {
		i_error("Can't access mail module directory: %s: %m",
			set->mail_modules);
		return FALSE;
	}
#else
	if (set->mail_use_modules) {
		i_warning("Module support wasn't built into Dovecot, "
			  "ignoring mail_use_modules setting");
	}
#endif

	if (set->log_path != NULL && access(set->log_path, W_OK) < 0) {
		dir = get_directory(set->log_path);
		if (access(dir, W_OK) < 0) {
			i_error("Can't write to log directory %s: %m", dir);
			return FALSE;
		}
	}

	if (set->info_log_path != NULL &&
	    access(set->info_log_path, W_OK) < 0) {
		dir = get_directory(set->info_log_path);
		if (access(dir, W_OK) < 0) {
			i_error("Can't write to info log directory %s: %m",
				dir);
			return FALSE;
		}
	}

#ifdef HAVE_SSL
	if (!set->ssl_disable) {
		if (set->ssl_ca_file != NULL &&
		    access(set->ssl_ca_file, R_OK) < 0) {
			i_fatal("Can't use SSL CA file %s: %m",
				set->ssl_ca_file);
		}

		if (access(set->ssl_cert_file, R_OK) < 0) {
			i_error("Can't use SSL certificate %s: %m",
				set->ssl_cert_file);
			return FALSE;
		}

		if (access(set->ssl_key_file, R_OK) < 0) {
			i_error("Can't use SSL key file %s: %m",
				set->ssl_key_file);
			return FALSE;
		}
	}
#endif

	/* fix relative paths */
	fix_base_path(set, &set->ssl_parameters_file);
	fix_base_path(set, &set->login_dir);

	/* since they're under /var/run by default, they may have been
	   deleted. */
	if (safe_mkdir(set->base_dir, 0700, master_uid, getegid()) == 0) {
		i_warning("Corrected permissions for base directory %s",
			  set->base_dir);
	}

	/* wipe out contents of login directory, if it exists.
	   except if we're using external authentication - then we would
	   otherwise wipe existing auth sockets */
	if (!settings_have_connect_sockets(set)) {
		if (unlink_directory(set->login_dir, FALSE) < 0) {
			i_error("unlink_directory() failed for %s: %m",
				set->login_dir);
			return FALSE;
		}
	}

	if (safe_mkdir(set->login_dir, 0750,
		       master_uid, set->server->login_gid) == 0) {
		i_warning("Corrected permissions for login directory %s",
			  set->login_dir);
	}

	if (set->max_mail_processes < 1) {
		i_error("max_mail_processes must be at least 1");
		return FALSE;
	}

	if (set->last_valid_uid != 0 &&
	    set->first_valid_uid > set->last_valid_uid) {
		i_error("first_valid_uid can't be larger than last_valid_uid");
		return FALSE;
	}
	if (set->last_valid_gid != 0 &&
	    set->first_valid_gid > set->last_valid_gid) {
		i_error("first_valid_gid can't be larger than last_valid_gid");
		return FALSE;
	}

	if (access(t_strcut(set->login_executable, ' '), X_OK) < 0) {
		i_error("Can't use login executable %s: %m",
			t_strcut(set->login_executable, ' '));
		return FALSE;
	}

	if (set->login_processes_count < 1) {
		i_error("login_processes_count must be at least 1");
		return FALSE;
	}
	if (set->login_max_logging_users < 1) {
		i_error("login_max_logging_users must be at least 1");
		return FALSE;
	}

	return TRUE;
}

static struct auth_settings *
auth_settings_new(struct server_settings *server, const char *name)
{
	struct auth_settings *auth;

	auth = p_new(settings_pool, struct auth_settings, 1);

	/* copy defaults */
	*auth = server->auth_defaults;
	auth->parent = server;
	auth->name = p_strdup(settings_pool, name);

	auth->next = server->auths;
	server->auths = auth;

	return auth;
}

static struct auth_settings *
parse_new_auth(struct server_settings *server, const char *name,
	       const char **errormsg)
{
	struct auth_settings *auth;

	if (strchr(name, '/') != NULL) {
		*errormsg = "Authentication process name must not contain '/'";
		return NULL;
	}

	for (auth = server->auths; auth != NULL; auth = auth->next) {
		if (strcmp(auth->name, name) == 0) {
			*errormsg = "Authentication process already exists "
				"with the same name";
			return NULL;
		}
	}

	return auth_settings_new(server, name);
}

static struct auth_socket_settings *
auth_socket_settings_new(struct auth_settings *auth, const char *type)
{
	struct auth_socket_settings *as, **as_p;

	as = p_new(settings_pool, struct auth_socket_settings, 1);

	as->parent = auth;
	as->type = str_lcase(p_strdup(settings_pool, type));

	as_p = &auth->sockets;
	while (*as_p != NULL)
		as_p = &(*as_p)->next;
	*as_p = as;

	return as;
}

static struct auth_socket_settings *
parse_new_auth_socket(struct auth_settings *auth, const char *name,
		      const char **errormsg)
{
	if (strcmp(name, "connect") != 0 && strcmp(name, "listen") != 0) {
		*errormsg = "Unknown auth socket type";
		return NULL;
	}

	if ((auth->sockets != NULL && strcmp(name, "connect") == 0) ||
	    (auth->sockets != NULL &&
	     strcmp(auth->sockets->type, "listen") == 0)) {
		*errormsg = "With connect auth socket no other sockets "
			"can be used in same auth section";
		return NULL;
	}

	return auth_socket_settings_new(auth, name);
}

static struct namespace_settings *
namespace_settings_new(struct server_settings *server, const char *type)
{
	struct namespace_settings *ns, **ns_p;

	ns = p_new(settings_pool, struct namespace_settings, 1);

	ns->parent = server;
	ns->type = str_lcase(p_strdup(settings_pool, type));

	ns_p = &server->namespaces;
	while (*ns_p != NULL)
		ns_p = &(*ns_p)->next;
	*ns_p = ns;

	return ns;
}

static struct namespace_settings *
parse_new_namespace(struct server_settings *server, const char *name,
		    const char **errormsg)
{
	if (strcasecmp(name, "private") != 0 &&
	    strcasecmp(name, "shared") != 0 &&
	    strcasecmp(name, "public") != 0) {
		*errormsg = "Unknown namespace type";
		return NULL;
	}

	return namespace_settings_new(server, name);
}

static const char *parse_setting(const char *key, const char *value,
				 void *context)
{
	struct settings_parse_ctx *ctx = context;
	const char *error;

	/* backwards compatibility */
	if (strcmp(key, "auth") == 0) {
		ctx->auth = parse_new_auth(ctx->server, value, &error);
		return ctx->auth == NULL ? error : NULL;
	}

	if (strcmp(key, "login") == 0) {
		i_warning("Ignoring deprecated 'login' section handling. "
			  "Use protocol imap/pop3 { .. } instead. "
			  "Some settings may have been read incorrectly.");
		return NULL;
	}

	switch (ctx->type) {
	case SETTINGS_TYPE_ROOT:
	case SETTINGS_TYPE_SERVER:
		error = NULL;
		if (ctx->protocol == MAIL_PROTOCOL_ANY ||
		    ctx->protocol == MAIL_PROTOCOL_IMAP) {
			error = parse_setting_from_defs(settings_pool,
							setting_defs,
							ctx->server->imap,
							key, value);
		}

		if (error == NULL &&
		    (ctx->protocol == MAIL_PROTOCOL_ANY ||
		     ctx->protocol == MAIL_PROTOCOL_POP3)) {
			error = parse_setting_from_defs(settings_pool,
							setting_defs,
							ctx->server->pop3,
							key, value);
		}

		if (error == NULL)
			return NULL;

		if (strncmp(key, "auth_", 5) == 0) {
			return parse_setting_from_defs(settings_pool,
						       auth_setting_defs,
						       ctx->auth,
						       key + 5, value);
		}
		return error;
	case SETTINGS_TYPE_AUTH:
		if (strncmp(key, "auth_", 5) == 0)
			key += 5;
		return parse_setting_from_defs(settings_pool, auth_setting_defs,
					       ctx->auth, key, value);
	case SETTINGS_TYPE_AUTH_SOCKET:
		return parse_setting_from_defs(settings_pool,
					       auth_socket_setting_defs,
					       ctx->auth_socket, key, value);
	case SETTINGS_TYPE_NAMESPACE:
		return parse_setting_from_defs(settings_pool,
					       namespace_setting_defs,
					       ctx->namespace, key, value);
	case SETTINGS_TYPE_SOCKET:
		return parse_setting_from_defs(settings_pool,
					       socket_setting_defs,
					       ctx->socket, key, value);
	}

	i_unreached();
}

static struct server_settings *
create_new_server(const char *name,
		  struct settings *imap_defaults,
		  struct settings *pop3_defaults)
{
	struct server_settings *server;

	server = p_new(settings_pool, struct server_settings, 1);
	server->name = p_strdup(settings_pool, name);
	server->imap = p_new(settings_pool, struct settings, 1);
	server->pop3 = p_new(settings_pool, struct settings, 1);
	server->auth_defaults = default_auth_settings;

	*server->imap = *imap_defaults;
	*server->pop3 = *pop3_defaults;

	server->imap->server = server;
	server->imap->protocol = MAIL_PROTOCOL_IMAP;
	server->imap->login_executable = PKG_LIBEXECDIR"/imap-login";
	server->imap->mail_executable = PKG_LIBEXECDIR"/imap";
	server->imap->mail_modules = MODULEDIR"/imap";

	server->pop3->server = server;
	server->pop3->protocol = MAIL_PROTOCOL_POP3;
	server->pop3->login_executable = PKG_LIBEXECDIR"/pop3-login";
	server->pop3->mail_executable = PKG_LIBEXECDIR"/pop3";
	server->pop3->mail_modules = MODULEDIR"/pop3";

	return server;
}

static int parse_section(const char *type, const char *name, void *context,
			 const char **errormsg)
{
	struct settings_parse_ctx *ctx = context;
	struct server_settings *server;

	if (type == NULL) {
		/* section closing */
		if (--ctx->level > 0) {
			ctx->type = ctx->parent_type;
			ctx->protocol = MAIL_PROTOCOL_ANY;

			switch (ctx->type) {
			case SETTINGS_TYPE_AUTH_SOCKET:
				ctx->parent_type = SETTINGS_TYPE_AUTH;
				break;
			default:
				ctx->parent_type = SETTINGS_TYPE_ROOT;
				break;
			}
		} else {
			ctx->type = SETTINGS_TYPE_ROOT;
			ctx->server = ctx->root;
			ctx->auth = &ctx->root->auth_defaults;
			ctx->namespace = NULL;
		}
		return TRUE;
	}

	ctx->level++;
	ctx->parent_type = ctx->type;

	if (strcmp(type, "server") == 0) {
		if (ctx->type != SETTINGS_TYPE_ROOT) {
			*errormsg = "Server section not allowed here";
			return FALSE;
		}

		ctx->type = SETTINGS_TYPE_SERVER;
		ctx->server = create_new_server(name, ctx->server->imap,
						ctx->server->pop3);
                server = ctx->root;
		while (server->next != NULL)
			server = server->next;
		server->next = ctx->server;
		return TRUE;
	}

	if (strcmp(type, "protocol") == 0) {
		if ((ctx->type != SETTINGS_TYPE_ROOT &&
		     ctx->type != SETTINGS_TYPE_SERVER) ||
		    ctx->level != 1) {
			*errormsg = "Protocol section not allowed here";
			return FALSE;
		}

		if (strcmp(name, "imap") == 0)
			ctx->protocol = MAIL_PROTOCOL_IMAP;
		else if (strcmp(name, "pop3") == 0)
			ctx->protocol = MAIL_PROTOCOL_POP3;
		else {
			*errormsg = "Unknown protocol name";
			return FALSE;
		}
		return TRUE;
	}

	if (strcmp(type, "auth") == 0) {
		if (ctx->type != SETTINGS_TYPE_ROOT &&
		    ctx->type != SETTINGS_TYPE_SERVER) {
			*errormsg = "Auth section not allowed here";
			return FALSE;
		}

		ctx->type = SETTINGS_TYPE_AUTH;
		ctx->auth = parse_new_auth(ctx->server, name, errormsg);
		return ctx->auth != NULL;
	}

	if (ctx->type == SETTINGS_TYPE_AUTH &&
	    strcmp(type, "socket") == 0) {
		ctx->type = SETTINGS_TYPE_AUTH_SOCKET;
		ctx->auth_socket = parse_new_auth_socket(ctx->auth,
							 name, errormsg);
		return ctx->auth_socket != NULL;
	}

	if (ctx->type == SETTINGS_TYPE_AUTH_SOCKET) {
		ctx->type = SETTINGS_TYPE_SOCKET;

		if (strcmp(type, "master") == 0) {
			ctx->socket = &ctx->auth_socket->master;
			return TRUE;
		}

		if (strcmp(type, "client") == 0) {
			ctx->socket = &ctx->auth_socket->client;
			return TRUE;
		}
	}

	if (strcmp(type, "namespace") == 0) {
		if (ctx->type != SETTINGS_TYPE_ROOT &&
		    ctx->type != SETTINGS_TYPE_SERVER) {
			*errormsg = "Namespace section not allowed here";
			return FALSE;
		}

		ctx->type = SETTINGS_TYPE_NAMESPACE;
		ctx->namespace = parse_new_namespace(ctx->server, name,
						     errormsg);
		return ctx->namespace != NULL;
	}

	*errormsg = "Unknown section type";
	return FALSE;
}

int master_settings_read(const char *path, int nochecks)
{
	struct settings_parse_ctx ctx;
	struct server_settings *server, *prev;
	struct auth_settings *auth;
	struct namespace_settings *ns;
	pool_t temp;

	memset(&ctx, 0, sizeof(ctx));

	p_clear(settings_pool);

	ctx.type = SETTINGS_TYPE_ROOT;
	ctx.protocol = MAIL_PROTOCOL_ANY;
	ctx.server = ctx.root =
		create_new_server("default",
				  &default_settings, &default_settings);
	ctx.auth = &ctx.server->auth_defaults;

	if (!settings_read(path, NULL, parse_setting, parse_section, &ctx))
		return FALSE;

	if (ctx.level != 0) {
		i_error("Missing '}'");
		return FALSE;
	}

	/* If server sections were defined, skip the root */
	if (ctx.root->next != NULL)
		ctx.root = ctx.root->next;

	prev = NULL;
	for (server = ctx.root; server != NULL; server = server->next) {
		if (server->imap->protocols == NULL ||
		    server->pop3->protocols == NULL) {
			i_error("No protocols given in configuration file");
			return FALSE;
		}
		if (!settings_is_active(server->imap))
			server->imap = NULL;
		else {
			if (!nochecks && !settings_verify(server->imap))
				return FALSE;
			server->defaults = server->imap;
		}

		if (!settings_is_active(server->pop3))
			server->pop3 = NULL;
		else {
			if (!nochecks && !settings_verify(server->pop3))
				return FALSE;
			if (server->defaults == NULL)
				server->defaults = server->pop3;
		}

		if (server->defaults == NULL) {
			if (prev == NULL)
				ctx.root = server->next;
			else
				prev->next = server->next;
		} else {
			auth = server->auths;
			if (auth == NULL) {
				i_error("Missing auth section for server %s",
					server->name);
				return FALSE;
			}

			if (!nochecks) {
				for (; auth != NULL; auth = auth->next) {
					if (!auth_settings_verify(auth))
						return FALSE;
				}
				ns = server->namespaces;
				for (; ns != NULL; ns = ns->next) {
					if (!namespace_settings_verify(ns))
						return FALSE;
				}
			}
			prev = server;
		}
	}

	/* settings ok, swap them */
	temp = settings_pool;
	settings_pool = settings2_pool;
	settings2_pool = temp;

	settings_root = ctx.root;
	return TRUE;
}

void master_settings_init(void)
{
	settings_pool = pool_alloconly_create("settings", 2048);
	settings2_pool = pool_alloconly_create("settings2", 2048);
}

void master_settings_deinit(void)
{
	pool_unref(settings_pool);
	pool_unref(settings2_pool);
}
