

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_stream.h>

/**********************************/
/***  Definitions                **/
/**********************************/
typedef struct _ngx_stream_shadowsocks_srv_conf_s {
    ngx_flag_t shadowsocks;
    ngx_str_t method;
    ngx_str_t password;
} ngx_stream_shadowsocks_srv_conf_t;


static ngx_int_t ngx_stream_shadowsocks_addr_variable(ngx_stream_session_t *s,
        ngx_stream_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_stream_shadowsocks_port_variable(ngx_stream_session_t *s,
        ngx_stream_variable_value_t *v, uintptr_t data);

static ngx_int_t ngx_stream_shadowsocks_add_variables(ngx_conf_t *cf);
static ngx_int_t ngx_stream_shadowsocks_init_post_config(ngx_conf_t *cf);
static void * ngx_stream_shadowsocks_create_srv_conf(ngx_conf_t *cf);

static ngx_int_t ngx_stream_shadowsocks_content_handler(ngx_stream_session_t *s);
static void ngx_stream_shadowsocks_read_handler(ngx_event_t *ev);


/**********************************/
/**********************************/

static ngx_command_t  ngx_stream_shadowsocks_commands[] = {
    { ngx_string("shadowsocks"),
        NGX_STREAM_SRV_CONF|NGX_CONF_FLAG,
        ngx_conf_set_flag_slot,
        NGX_STREAM_SRV_CONF_OFFSET,
        offsetof(ngx_stream_shadowsocks_srv_conf_t, shadowsocks),
        NULL},
    { ngx_string("shadowsocks_method"),
        NGX_STREAM_SRV_CONF|NGX_CONF_FLAG,
        ngx_conf_set_str_slot,
        NGX_STREAM_SRV_CONF_OFFSET,
        offsetof(ngx_stream_shadowsocks_srv_conf_t, method),
        NULL},
    { ngx_string("shadowsocks_password"),
        NGX_STREAM_SRV_CONF|NGX_CONF_FLAG,
        ngx_conf_set_str_slot,
        NGX_STREAM_SRV_CONF_OFFSET,
        offsetof(ngx_stream_shadowsocks_srv_conf_t, password),
        NULL},
};

static ngx_stream_module_t ngx_stream_shadowsocks_module_ctx = {
    ngx_stream_shadowsocks_add_variables,/* preconfiguration */
    ngx_stream_shadowsocks_init_post_config,/* postconfiguration */

    NULL,                           /* create main configuration */
    NULL,                           /* init main configuration */

    ngx_stream_shadowsocks_create_srv_conf,/* create server configuration */
    NULL,                           /* merge server configuration */
};


ngx_module_t ngx_stream_shadowsocks_module = {
    NGX_MODULE_V1,
    &ngx_stream_shadowsocks_module_ctx, /* module context */
    ngx_stream_shadowsocks_commands,    /* module directives */
    NGX_STREAM_MODULE,                  /* module type */
    NULL,                               /* init master */
    NULL,                               /* init module */
    NULL,                               /* init process */
    NULL,                               /* init thread */
    NULL,                               /* exit thread */
    NULL,                               /* exit process */
    NULL,                               /* exit master */
    NGX_MODULE_V1_PADDING
};

static ngx_stream_variable_t  ngx_stream_shadowsocks_vars[] = {

    { ngx_string("shadowsocks_addr"), NULL,
        ngx_stream_shadowsocks_addr_variable, 0,
        NGX_STREAM_VAR_NOCACHEABLE, 0 },

    { ngx_string("shadowsocks_port"), NULL,
        ngx_stream_shadowsocks_port_variable, 0,
        NGX_STREAM_VAR_NOCACHEABLE, 0 },

    { ngx_null_string, NULL, NULL, 0, 0, 0 }
};

static ngx_int_t ngx_stream_shadowsocks_addr_variable(ngx_stream_session_t *s,
        ngx_stream_variable_value_t *v, uintptr_t data)
{
    v->valid = 0;
    v->no_cacheable = 1;
    v->not_found = 1;
    return NGX_OK;
}

static ngx_int_t ngx_stream_shadowsocks_port_variable(ngx_stream_session_t *s,
        ngx_stream_variable_value_t *v, uintptr_t data)
{
    v->valid = 0;
    v->no_cacheable = 1;
    v->not_found = 1;
    return NGX_OK;
}


static void * ngx_stream_shadowsocks_create_srv_conf(ngx_conf_t *cf)
{
    ngx_stream_shadowsocks_srv_conf_t *sscf;

    if ((sscf = ngx_pcalloc(cf->pool,
                    sizeof(ngx_stream_shadowsocks_srv_conf_t))) == NULL) {
        return NULL;
    }
    sscf->shadowsocks = NGX_CONF_UNSET;
    return sscf;
}


static ngx_int_t ngx_stream_shadowsocks_add_variables(ngx_conf_t *cf)
{
    ngx_stream_variable_t  *var, *v;

    for (v = ngx_stream_shadowsocks_vars; v->name.len; v++) {
        var = ngx_stream_add_variable(cf, &v->name, v->flags);
        if (var == NULL) {
            return NGX_ERROR;
        }

        var->get_handler = v->get_handler;
        var->data = v->data;
    }

    return NGX_OK;
}


static ngx_int_t ngx_stream_shadowsocks_init_post_config(ngx_conf_t *cf)
{
    ngx_stream_handler_pt        *h;
    ngx_stream_core_main_conf_t  *cmcf;

    cmcf = ngx_stream_conf_get_module_main_conf(cf, ngx_stream_core_module);

    h = ngx_array_push(&cmcf->phases[NGX_STREAM_PREREAD_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = ngx_stream_shadowsocks_content_handler;
    return NGX_OK;
}

static ngx_int_t ngx_stream_shadowsocks_content_handler(ngx_stream_session_t *s)
{
    ngx_stream_shadowsocks_srv_conf_t *sscf;
    ngx_connection_t                   *c;

    c = s->connection;

    sscf = ngx_stream_get_module_srv_conf(s, ngx_stream_shadowsocks_module);
    if (!sscf->shadowsocks) {
        return NGX_DECLINED;
    }

    if (c->type != SOCK_STREAM) {
        return NGX_DECLINED;
    }

    c->read->handler = ngx_stream_shadowsocks_read_handler;
    c->write->handler = NULL;

    ngx_add_event(c->read, NGX_READ_EVENT, 0);

    return NGX_DONE;    // keep this connection
    //return NGX_OK;      // do content-phase
}


static void ngx_stream_shadowsocks_read_handler(ngx_event_t *ev)
{
    char buff[BUFSIZ];
    int ret;

    ngx_connection_t *c = ev->data;
    ngx_stream_session_t  *s = c->data;

    ngx_log_error(NGX_LOG_ERR, ev->log, 0, "[%s:%d]", __FUNCTION__, __LINE__);

    while (1) {
        if ((ret = read(c->fd, buff, BUFSIZ)) <= 0) {
            if (ret < 0 && errno == EAGAIN) {
                return;
            }
            ngx_stream_finalize_session(s, NGX_STREAM_BAD_REQUEST);
            return;
        }
        ngx_log_error(NGX_LOG_ERR, ev->log, 0, "[%s:%d] %s",
                __FUNCTION__, __LINE__, buff);
    }
}

