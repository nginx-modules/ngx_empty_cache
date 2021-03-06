/**
 * Nginx clean all cache module
 * @author Oscar MARGINEAN
 * @version 0.2
 */

#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <stdio.h>
#include <ftw.h>
#include <unistd.h>


ngx_int_t ngx_http_empty_cache_respond( ngx_http_request_t *r );
char     *ngx_http_empty_cache_conf( ngx_conf_t *cf, ngx_command_t *cmd, void *conf );
ngx_int_t ngx_http_empty_cache_handler( ngx_http_request_t *r);
ngx_int_t ngx_http_empty_cache_remove_folder( char *path );


/**
 * The top of the page which is displayed on a successful purge
 * @var static char
 */
static char ngx_http_cache_purge_success_page_top[] =
    "<html>" CRLF
    "<head><title>Cache emptied</title></head>" CRLF
    "<body bgcolor=\"white\">" CRLF
    "<center><h1>Cache emptied</h1>" CRLF
;

static char ngx_http_cache_purge_failure_page_top[] =
    "<html>" CRLF
    "<head><title>Cache could not be emptied</title></head>" CRLF
    "<body bgcolor=\"white\">" CRLF
    "<center><h1>Cache could not be emptied</h1>" CRLF
;

/**
 * The top of the page which is displayed on a successful purge
 * @var static char
 */
static char ngx_http_cache_purge_page_tail[] =
    CRLF "</center>" CRLF
    "<hr><center>" NGINX_VER " with ngx_empty_cache module</center>" CRLF
    "</body>" CRLF
    "</html>" CRLF
;

/**
 * load the ngx_http_fastcgi_module
 */
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
 * The module directives
 * the directive empty_cache can be called in a server and location config and takes
 * exactly one argument
 */
static ngx_command_t  ngx_http_empty_cache_commands[] = {
    /* empty cache directive */
    { ngx_string("empty_cache"),
      /* directive configurations */
      NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
      /* function to be called on config */
      ngx_http_empty_cache_conf,
      /* the memory offset */
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

      ngx_null_command
};

/**
 * the module context.
 */
static ngx_http_module_t  ngx_http_empty_cache_module_ctx = {
    NULL,                                  /* preconfiguration */
    NULL,                                  /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    NULL,                                  /* create location configuration */
    NULL                                   /* merge location configuration */
};


/**
 * the actual module config
 */
ngx_module_t  ngx_http_empty_cache_module = {
    NGX_MODULE_V1,
    &ngx_http_empty_cache_module_ctx,      /* module context */
    ngx_http_empty_cache_commands,         /* module directives */
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

/**
 * This function is called when the module is set up
 * @return      success or failure
 */
char *ngx_http_empty_cache_conf(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    // nginx core local configuration
    ngx_http_core_loc_conf_t         *clcf;

    // nginx fastcgi local configuration
    ngx_http_fastcgi_loc_conf_t      *flcf;

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

    // set fastcgi_cache part
    flcf->upstream.cache = ngx_shared_memory_add(cf, &value[1], 0, &ngx_http_fastcgi_module);
    
    if (flcf->upstream.cache == NULL) {
        return NGX_CONF_ERROR;
    }

    // set handler
    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_http_empty_cache_handler;

    return NGX_CONF_OK;
}

/**
 * This function is called when the empty_cache directive is run ( usually in the purge location )
 * @param  r the request
 * @return   NGX_ERROR or HTML status
 */
ngx_int_t ngx_http_empty_cache_handler(ngx_http_request_t *r) {

    ngx_http_fastcgi_loc_conf_t  *flcf;
    ngx_http_cache_t  *c;
    ngx_str_t         *key;
    ngx_int_t          rc;

    flcf = ngx_http_get_module_loc_conf(r, ngx_http_fastcgi_module);

    // discard the request body
    rc = ngx_http_discard_request_body(r);
    if (rc != NGX_OK) {
        return NGX_ERROR;
    }

    // allocate memory for the cache
    c = ngx_pcalloc(r->pool, sizeof(ngx_http_cache_t));
    if (c == NULL) {
        return NGX_ERROR;
    }

    // initialize the cache key array
    rc = ngx_array_init(&c->keys, r->pool, 1, sizeof(ngx_str_t));
    if (rc != NGX_OK) {
        return NGX_ERROR;
    }

    key = ngx_array_push(&c->keys);
    if (key == NULL) {
        return NGX_ERROR;
    }

    // create the complex value for the key
    rc = ngx_http_complex_value(r, &flcf->cache_key, key);
    if (rc != NGX_OK) {
        return NGX_ERROR;
    }

    r->cache = c;
    c->body_start = ngx_pagesize;
    c->file_cache = flcf->upstream.cache->data;
    c->file.log = r->connection->log;

    ngx_http_file_cache_open(r);

    return ngx_http_empty_cache_respond( r );
}


/**
 * Clears the cache and returns the result
 * @param  r the request
 * @return   HTTP status - 500, 404 or NGX status
 */
ngx_int_t ngx_http_empty_cache_respond( ngx_http_request_t *r ) {
    ngx_int_t    rc;
    ngx_buf_t   *b;
    ngx_chain_t  out;
    ngx_str_t    cache_path;
    size_t       len;
    char         *cpath;

    // only allow GET and HEAD requests
    if (!(r->method & (NGX_HTTP_GET|NGX_HTTP_HEAD))) {
        return NGX_HTTP_NOT_ALLOWED;
    }

    cache_path = r->cache->file_cache->path->name;

    // create the cache path
    cpath = ngx_pcalloc( r->pool, r->cache->file_cache->path->name.len );
    sprintf( cpath, "%s", r->cache->file_cache->path->name.data );

    // clear the cache
    rc = ngx_http_empty_cache_remove_folder ( cpath );

    switch ( rc ) {
        case NGX_HTTP_OK:

            // the length of the displayed markup
            len = sizeof(ngx_http_cache_purge_success_page_top) - 1
                  + sizeof(ngx_http_cache_purge_page_tail) - 1
                  + sizeof(CRLF "<br>Path: ") - 1
                  + cache_path.len;
            break;
        case NGX_HTTP_INTERNAL_SERVER_ERROR:

            // the length of the displayed markup
            len = sizeof(ngx_http_cache_purge_failure_page_top) - 1
                  + sizeof(ngx_http_cache_purge_page_tail) - 1
                  + sizeof(CRLF "<br>Path: ") - 1
                  + cache_path.len
                  + sizeof(CRLF "<br>Error: ") - 1
                  + strlen( strerror( errno ) );
            break;
        case NGX_HTTP_NOT_FOUND:
            return rc;
            break;
        default:
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    // set the headers
    r->headers_out.content_type.len  = sizeof("text/html") - 1;
    r->headers_out.content_type.data = (u_char *) "text/html";
    r->headers_out.status            = rc;
    r->headers_out.content_length_n  = len;

    // send the header only, if the request type is http 'HEAD'
    if (r->method == NGX_HTTP_HEAD) {
        return ngx_http_send_header(r);        
    }

    // create a temporary buffer for the output body
    b = ngx_create_temp_buf(r->pool, len);

    if (b == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    // attach the buffer to the buffer chain
    out.buf = b;
    out.next = NULL;

    // add the content to the buffer
    
    if( rc == NGX_HTTP_OK) {
        b->last = ngx_cpymem(b->last, ngx_http_cache_purge_success_page_top,
                             sizeof(ngx_http_cache_purge_success_page_top) - 1);
    } else {
        b->last = ngx_cpymem(b->last, ngx_http_cache_purge_failure_page_top,
                             sizeof(ngx_http_cache_purge_failure_page_top) - 1);
        b->last = ngx_cpymem(b->last, CRLF "<br>Error: ",
                             sizeof(CRLF "<br>Error: ") - 1);
        b->last = ngx_cpymem(b->last, strerror( errno ),
                             strlen( strerror( errno ) ));
    }

    b->last = ngx_cpymem(b->last, CRLF "<br>Path: ",
                         sizeof(CRLF "<br>Path: ") - 1);
    b->last = ngx_cpymem(b->last, cache_path.data,
                         cache_path.len);

    b->last = ngx_cpymem(b->last, ngx_http_cache_purge_page_tail,
                         sizeof(ngx_http_cache_purge_page_tail) - 1);
    b->last_buf = 1;

    /* send the headers of your response */
    rc = ngx_http_send_header(r);
 
    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        return rc;
    }
 
    /* send the buffer chain of your response */
    return ngx_http_output_filter(r, &out);
}

/**
 * Function to recursively delete the cache folder
 * @param  path the path of the cache folder
 * @return      HTTP status code on failure, NGX_OK on success
 */
ngx_int_t ngx_http_empty_cache_remove_folder( char* path ) {
    DIR   *dp;
    struct dirent *ep;
    char   filepath[100];
    struct stat buf;
    int    rv, empty = 1;

    // reset errno
    errno = 0;

    dp = opendir ( path );

    if (dp != NULL) {
        // go through the folder
        while ( (ep = readdir( dp )) ) {
            // skip the . and ..
            if( strcmp( ep->d_name, ".") && strcmp (ep->d_name, "..") ) {            
               empty = 0;

                // compose path
                strcpy( filepath, path );
                if ( filepath[ strlen( filepath ) - 1 ])
                    strcat ( filepath, "/");

                strcat( filepath, ep->d_name );

                // check if the target is a folder or a file
                if( stat( filepath, &buf) == 0 && S_ISDIR( buf.st_mode ) ) {

                    // if it's a folder, attempt to delete it's content
                    rv = ngx_http_empty_cache_remove_folder( filepath );

                    if (  rv == NGX_HTTP_INTERNAL_SERVER_ERROR) {
                        return NGX_HTTP_INTERNAL_SERVER_ERROR;
                    }

                    // after emptying the folder, delete it
                    rv = rmdir( filepath );

                    if( rv )
                        return NGX_HTTP_INTERNAL_SERVER_ERROR;
                } else {
                    // if it's a file, delete it
                    rv = unlink( filepath );
    
                    if( rv )
                        return NGX_HTTP_INTERNAL_SERVER_ERROR;
                }
            }
        }

        // if the folder was empty, return 404
        if( empty )
            return NGX_HTTP_NOT_FOUND;
    } else {
        // if the folder doesn't exist, return 404
        return NGX_HTTP_NOT_FOUND;
    }

    return NGX_HTTP_OK;
}