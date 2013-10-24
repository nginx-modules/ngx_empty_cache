About
===============

`ngx_empty_cache` is a module to quicky empty the cache of a server. Currently it only supports fastcgi caching. It has been successfully tested on nginx 1.4.1, with more tests coming.

Configuration
===============

This module is run on a location of the server:

    http {
        proxy_cache_path  /tmp/cache  keys_zone=tmpcache:10m;
    
        location = /purge/ {
            empty_cache  tmpcache;
        }
    }


On successful purge it will display a message and return 200 OK HTTP status. If the cache is already empty, it will return a 404 NOT FOUND status.
