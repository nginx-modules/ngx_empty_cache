/**
 * Nginx clean all cache module
 * @author Oscar MARGINEAN
 * @version 0.1
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

static char *ngx_http_empty_cache_config(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_http_empty_cache_handler(ngx_http_request_t *r);
static void * ngx_http_empty_cache_loc_conf( ngx_conf_t *cf );
/**
 * module settings
 */
typedef struct {
    char      string[80];
} ngx_http_empty_cache_loc_conf_t;


static ngx_command_t  ngx_http_empty_cache_commands[] = {
    { ngx_string("empty_cache"),
      NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
      ngx_http_empty_cache_config,
      0,
      0,
      NULL },

      ngx_null_command
};

static ngx_http_module_t  ngx_http_empty_cache_module_ctx = {
    NULL,                                  /* preconfiguration */
    NULL,                                  /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    ngx_http_empty_cache_loc_conf,         /* create location configuration */
    NULL                                   /* merge location configuration */
};

ngx_module_t  ngx_http_empty_cache_module = {
    NGX_MODULE_V1,
    &ngx_http_empty_cache_module_ctx,      /* module context */
    ngx_http_empty_cache_commands,              /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};

static void * ngx_http_empty_cache_loc_conf( ngx_conf_t *cf ) {
    puts("entering: ngx_http_empty_cache_loc_conf");
    ngx_http_empty_cache_loc_conf_t  *conf;
    //char *value;

    conf = ngx_palloc(cf->pool, sizeof(ngx_http_empty_cache_loc_conf_t));

  //  value = cf->args->elts;

//    puts(&value[1]); 

    return conf;
}

static char *ngx_http_empty_cache_config(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    puts("entering: ngx_http_empty_cache_config");

  //  ngx_http_empty_cache_loc_conf_t *config = conf;
    char *value;

    value = cf->args->elts;

     printf("%i", value[0]);

    ngx_http_core_loc_conf_t  *clcf;

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_http_empty_cache_handler;

    return NGX_CONF_OK;
}

static ngx_int_t ngx_http_empty_cache_handler(ngx_http_request_t *r) {
    size_t             size;
    ngx_int_t          rc;
    ngx_buf_t         *b;
    ngx_chain_t        out;

    if (r->method != NGX_HTTP_GET && r->method != NGX_HTTP_HEAD) {
        return NGX_HTTP_NOT_ALLOWED;
    }

    rc = ngx_http_discard_request_body(r);

    if (rc != NGX_OK) {
        return rc;
    }

    ngx_str_set(&r->headers_out.content_type, "text/plain");

    size = sizeof("Commence thy cache cleansing!  \n");

    b = ngx_create_temp_buf(r->pool, size);
    if (b == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    out.buf = b;
    out.next = NULL;

    b->last = ngx_sprintf(b->last, "Commence thy cache cleansing!\n");

    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = b->last - b->pos;

    b->last_buf = (r == r->main) ? 1 : 0;

    rc = ngx_http_send_header(r);

    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        return rc;
    }

    return ngx_http_output_filter(r, &out);   
}