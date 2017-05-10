

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_stream.h>

/**********************************/
/***  Definitions                **/
/**********************************/
#if 0
typedef struct _ngx_stream_shadowsocks_srv_conf_s {
    ngx_uint_t shadowsocks;
} ngx_stream_shadowsocks_srv_conf_t;
#endif

static void * ngx_stream_shadowsocks_create_srv_conf(ngx_conf_t *cf);
static ngx_int_t ngx_stream_shadowsocks_init_post_config(ngx_conf_t *cf);

static void ngx_stream_shadowsocks_content_handler(ngx_stream_session_t *s);


/**********************************/
/**********************************/
static char *
ngx_conf_set_stream_shadowsocks(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_stream_core_srv_conf_t *cscf;

    if ((cscf = ngx_stream_conf_get_module_srv_conf(cf, ngx_stream_core_module)) == NULL) {
        return NGX_CONF_ERROR;
    }
    cscf->handler = ngx_stream_shadowsocks_content_handler;
    return NGX_CONF_OK;
}

static ngx_command_t  ngx_stream_shadowsocks_commands[] = {
    { ngx_string("socks5"),
        NGX_STREAM_SRV_CONF|NGX_CONF_FLAG,
        ngx_conf_set_stream_shadowsocks,
        0,
        0,
        NULL },
};

static ngx_stream_module_t ngx_stream_shadowsocks_module_ctx = {
    NULL,                           /* preconfiguration */
    NULL,                           /* postconfiguration */

    NULL,                           /* create main configuration */
    NULL,                           /* init main configuration */

    NULL,                               /* create server configuration */
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

static void ngx_stream_shadowsocks_content_handler(ngx_stream_session_t *s)
{
}

