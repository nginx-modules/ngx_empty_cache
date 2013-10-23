/**
 * Nginx clean all cache module
 * @author Oscar MARGINEAN
 * @version 0.1
 */

#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <stdio.h>
#include <ftw.h>
#include <unistd.h>

////////////////
ngx_int_t do_the_shit( ngx_http_request_t *r );
/////////////////
char *ngx_http_empty_cache_conf(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static void * ngx_http_empty_cache_loc_conf( ngx_conf_t *cf );
static ngx_int_t ngx_http_empty_cache_handler(ngx_http_request_t *r);
int rmrf(char *path);
int unlink_cb(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf);

// static ngx_conf_post_handler_pt ngx_http_empty_cache_p = ngx_http_empty_cache;

extern ngx_module_t ngx_http_fastcgi_module;

/**
 * Redefinition of ngx_http_fastcgi_loc_conf_t
 * @see ngx_http_fastcgi_module
 */
typedef struct {
    ngx_http_upstream_conf_t       upstream;

    ngx_str_t                      index;

    ngx_array_t                   *flushes;
    ngx_array_t                   *params_len;
    ngx_array_t                   *params;
    ngx_array_t                   *params_source;
    ngx_array_t                   *catch_stderr;

    ngx_array_t                   *fastcgi_lengths;
    ngx_array_t                   *fastcgi_values;

#  if defined(nginx_version) && (nginx_version >= 8040)
    ngx_hash_t                     headers_hash;
    ngx_uint_t                     header_params;
#  endif /* nginx_version >= 8040 */

#  if defined(nginx_version) && (nginx_version >= 1001004)
    ngx_flag_t                     keep_conn;
#  endif /* nginx_version >= 1001004 */

    ngx_http_complex_value_t       cache_key;

#  if (NGX_PCRE)
    ngx_regex_t                   *split_regex;
    ngx_str_t                      split_name;
#  endif /* NGX_PCRE */
} ngx_http_fastcgi_loc_conf_t;

/**
 * module settings
 */
typedef struct {
    ngx_str_t      keyzone;
} ngx_http_empty_cache_loc_conf_t;


static ngx_command_t  ngx_http_empty_cache_commands[] = {

    { ngx_string("empty_cache"),
      NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_TAKE2,
      ngx_http_empty_cache_conf,
      NGX_HTTP_LOC_CONF_OFFSET,
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
    ngx_http_empty_cache_loc_conf_t  *conf;

    conf = ngx_palloc(cf->pool, sizeof(ngx_http_empty_cache_loc_conf_t));
    if (conf == NULL) {
        puts("could not allocate memory for the conf!");
    }

    return conf;
}

char *ngx_http_empty_cache_conf(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    puts("empty cache_conf");

    // nginx core local configuration
    ngx_http_core_loc_conf_t         *clcf;

    // nginx fastcgi local configuration
    ngx_http_fastcgi_loc_conf_t      *flcf;

    // variable used for the cache key
    ngx_http_compile_complex_value_t ccv;

    // getting values from the config
    ngx_str_t *value;

    flcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_fastcgi_module);

    if (flcf->upstream.cache != NGX_CONF_UNSET_PTR && flcf->upstream.cache != NULL) {
        return "is incompatible with \"fastcgi_cache\"";
    }

    if (flcf->upstream.upstream || flcf->fastcgi_lengths) {
        return "is incompatible with \"fastcgi_pass\"";
    }

    if (flcf->upstream.store > 0 || flcf->upstream.store_lengths) {
        return "is incompatible with \"fastcgi_store\"";
    }

    value = cf->args->elts;

    printf("%s keyzone is: %s\n", value->data, (value+1)->data);

    /* set fastcgi_cache part */
    flcf->upstream.cache = ngx_shared_memory_add(cf, &value[1], 0, &ngx_http_fastcgi_module);
    
    if (flcf->upstream.cache == NULL) {
        return NGX_CONF_ERROR;
    }

    /* set fastcgi_cache_key part */
    ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

    ccv.cf = cf;
    ccv.value = &value[2];
    ccv.complex_value = &flcf->cache_key;

    if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    // set handler
    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);

    clcf->handler = ngx_http_empty_cache_handler;
    puts("empty cache_conf end");

    return NGX_CONF_OK;
}

ngx_int_t ngx_http_empty_cache_handler(ngx_http_request_t *r) {

    ngx_http_fastcgi_loc_conf_t  *flcf;

    flcf = ngx_http_get_module_loc_conf(r, ngx_http_fastcgi_module);

    ngx_http_cache_t  *c;
    ngx_str_t         *key;
    ngx_int_t          rc;

    rc = ngx_http_discard_request_body(r);
    if (rc != NGX_OK) {
        return NGX_ERROR;
    }

    c = ngx_pcalloc(r->pool, sizeof(ngx_http_cache_t));
    if (c == NULL) {
        return NGX_ERROR;
    }

    rc = ngx_array_init(&c->keys, r->pool, 1, sizeof(ngx_str_t));
    if (rc != NGX_OK) {
        return NGX_ERROR;
    }

    key = ngx_array_push(&c->keys);
    if (key == NULL) {
        return NGX_ERROR;
    }

    rc = ngx_http_complex_value(r, &flcf->cache_key, key);
    if (rc != NGX_OK) {
        return NGX_ERROR;
    }

    r->cache = c;
    c->body_start = ngx_pagesize;
    c->file_cache = flcf->upstream.cache->data;
    c->file.log = r->connection->log;

    ngx_http_file_cache_create_key(r);

    ngx_http_file_cache_open(r);

    return ngx_http_empty_cache_respond( r );
}

ngx_int_t ngx_http_empty_cache_respond( ngx_http_request_t *r ) {
    ngx_int_t    rc;
    ngx_buf_t   *b;
    ngx_chain_t  out;
    ngx_str_t    cache_path;
    char       cpath[100];

    cache_path = r->cache->file_cache->path->name;
    sprintf( cpath, "%s", cache_path.data );

    rmrf ( cpath );

   if (!(r->method & (NGX_HTTP_GET|NGX_HTTP_HEAD))) {
        return NGX_HTTP_NOT_ALLOWED;
    }

    // discard the request body
    rc = ngx_http_discard_request_body(r);

    if (rc != NGX_OK) {
        return rc;
    }


    // set the 'Content-type' header
    r->headers_out.content_type_len = sizeof("text/html") - 1;
    r->headers_out.content_type.len = sizeof("text/html") - 1;
    r->headers_out.content_type.data = (u_char *) "text/html";

    // send the header only, if the request type is http 'HEAD'
    if (r->method == NGX_HTTP_HEAD) {
        r->headers_out.status = NGX_HTTP_OK;
        r->headers_out.content_length_n = cache_path.len;
 
        return ngx_http_send_header(r);
    }

    // allocate a buffer for your response body
    b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
    if (b == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    // attach the buffer to the buffer chain
    out.buf = b;
    out.next = NULL;

    // adjust the pointers of the buffer
    b->pos = cache_path.data;
    b->last = cache_path.data + cache_path.len;
    b->memory = 1;    /* this buffer is in memory */
    b->last_buf = 1;  /* this is the last buffer in the buffer chain */

    /* set the status line */
    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = cache_path.len;


    /* send the headers of your response */
    rc = ngx_http_send_header(r);
 
    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        return rc;
    }
 
    /* send the buffer chain of your response */
    return ngx_http_output_filter(r, &out);
}


int unlink_cb(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {
    int rv = remove(fpath);

    if (rv)
        return NGX_ERROR;

    return rv;
}

int rmrf(char *path) {
    return nftw(path, unlink_cb, 64, FTW_DEPTH | FTW_PHYS);
}