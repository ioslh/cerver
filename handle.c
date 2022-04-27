#include "handle.h"

char *mime_table[][2] = {
    {"html", "text/html"},
    {"css", "text/css"},
    {"js", "application/javascript"},
    {"png", "image/png"},
};
// Supported methods
char *methods[] = {
    "GET",
    "HEAD",
};


int read_startline(rio_t *rp, req_t *req, res_t *res) {
    char line[HDR_MAX];
    char url[URI_MAX];
    if (rio_readline(rp, line, HDR_MAX) < 0) {
        res->status = 400;
        return FAILED;
    }
    trimright_line(line);
    if ((sscanf(line, "%7s %s %*s", req->method, url)) != 2) {
        res->status = 400;
        return FAILED;
    }

    req->location = (location_t *)malloc(sizeof(location_t));
    if (parse_location(url, req->location) != OK) {
        res->status = 400;
        return FAILED; 
    }

    if (check_method(req->method) != OK) {
        res->status = 405;
        return FAILED;
    }

    return OK;
}




int read_request_headers(rio_t *rp, req_t *req, res_t *res) {
    char line[HDR_MAX];
    size_t n;
    char *nameptr, *valueptr, *colonptr;
    header_t *prev_header = NULL, *temp_header = NULL;
    while((n = rio_readline(rp, line, HDR_MAX)) > 0) {
        n -= trimright_line(line);
        if (n == 0) break; // empty line, end of headers
        // if less than 3, we can surely know that's not a valid header
        if (n < 3) continue;

        nameptr = line;
        if ((colonptr = strnstr(line, ":", n - 1)) == NULL) {
            continue; // invalid header line
        }
        *colonptr = 0;
        valueptr = colonptr + 1;
        while(isspace(*valueptr)) {
            valueptr++;
            if (*valueptr == 0) break;
        }
        temp_header = new_header(nameptr, valueptr);
        if (prev_header) {
            prev_header->next = temp_header;
        } else {
            req->header = temp_header;
        }
        prev_header = temp_header;
    }
    return OK;
}

int trimright_line(char *line) {
    size_t len = strlen(line);
    if (len == 0) return 0;
    // both CRLF and LF are valid as line terminator
    if (line[len - 1] == 10) {
        line[len - 1] = 0;
        if (len > 1 && line[len - 2] == 13) {
            line[len - 2] = 0;
            return 2;
        }
        return 1;
    }
    return 0;
}

int handle_request(req_t *req, res_t *res) {
    // serve static file
    char filename[URI_MAX];
    strcpy(filename, public);
    strcat(filename, req->location->path);
    struct stat st;
    char *pos, *mime;
    if (stat(filename, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
             pos = &filename[strlen(filename) - 1];
             if (*pos != '/') {
                 *(pos + 1) = '/';
                 *(pos + 2) = 0;
             }
             // Default page
             strcat(filename, "index.html");
         }
    }
    if (stat(filename, &st) < 0) {
        res->status = 404;
        return FAILED;
    }

    res->status = 200;
    append_header(res, new_header("Last-Modified", stringify_time(st.st_mtime)));
    mime = get_mime(get_extension(filename));
    if (mime) append_header(res, new_header("Content-Type", mime));
    if (strncasecmp(req->method, "HEAD", 4) == 0) {
        res->body = NULL;
        res->length = 0;
        return OK;
    }
    int fd = open(filename, O_RDONLY, 0);
    char *bodybuf = (char *)mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    res->body = (char *)malloc(st.st_size);
    memcpy(res->body, bodybuf, st.st_size);
    res->length = st.st_size;
    close(fd);
    munmap(bodybuf, st.st_size);
    return OK;
}

void web_handle(int connfd) {
    rio_t rio;
    req_t req;
    res_t res;
    rio_init(&rio, connfd);
    req_init(&req);
    res_init(&res);
    if (read_startline(&rio, &req, &res) == OK) {
        if (read_request_headers(&rio, &req, &res) == OK) {
            if (handle_request(&req, &res) == OK) {
                httpsend(connfd, &res);
            } else {
                httpsend_error(connfd, &res);
            }
        } else {
            httpsend_error(connfd, &res);
        }
    } else {
        httpsend_error(connfd, &res);
    }
    free_request(&req);
    free_response(&res);
}


void httpsend_error(int connfd, res_t *res) {
    char bodybuf[BUF_MAX];
    char *message = get_http_message(res->status);
    sprintf(bodybuf, "<html>");
    sprintf(bodybuf, "%s  <head><title>%d %s</title></head>", bodybuf, res->status, message);
    sprintf(bodybuf, "%s  <body>", bodybuf);
    sprintf(bodybuf, "%s    <h3>%d %s</h3>", bodybuf, res->status, message);
    sprintf(bodybuf, "%s  </body></html>", bodybuf);
    size_t bodylen = strlen(bodybuf);
    char *body = (char *)malloc(bodylen + 1);
    strncpy(body, bodybuf, bodylen + 1);
    res->body = body;
    res->length = bodylen;
    append_header(res, new_header("Content-Type", "text/html"));
    httpsend(connfd, res);
}

void append_header(res_t *res, header_t *header) {
    if (res->last) {
        res->last->next = header;
        res->last = header;
    } else {
        res->header = res->last = header;
    }
}

void httpsend(int connfd, res_t *res) {
    char valuebuf[16];
    char headbuf[BUF_MAX];
    header_t *header = NULL;
    sprintf(valuebuf, "%ld", res->length);
    append_header(res, new_header("Server", SERVER_NAME));
    append_header(res, new_header("Date", stringify_time(time(NULL))));
    append_header(res, new_header("Content-Length", valuebuf));
    sprintf(headbuf, "HTTP/1.1 %d %s%s", res->status, get_http_message(res->status), CRLF);
    header = res->header;

    while(header) {
        sprintf(headbuf, "%s%s: %s%s", headbuf, header->name, header->value, CRLF);
        header = header->next;
    }

    sprintf(headbuf, "%s%s", headbuf, CRLF);
    rio_writen(connfd, headbuf, strlen(headbuf));
    rio_writen(connfd, res->body, res->length);
}

void report_client(struct sockaddr_in *client) {
    char *ip = inet_ntoa(client->sin_addr);
    printf("Connected from %s\n", ip);
}

header_t * new_header(const char *name, const char *value) {
    header_t *header = (header_t *)malloc(sizeof(header_t));
    size_t namelen = strlen(name), valuelen = strlen(value);
    char *nameptr = (char *)malloc(namelen + 1);
    char *valueptr = (char *)malloc(valuelen + 1);
    if (!header || !nameptr || !valueptr) fatal(1, "Failed allocate memory for header");
    strncpy(nameptr, name, namelen + 1);
    strncpy(valueptr, value, valuelen + 1);
    header->next = NULL;
    header->name = nameptr;
    header->value = valueptr;
    return header;
}

void print_headers(header_t *header) {
    while(header) {
        printf("%s: %s\n", header->name, header->value);
        header = header->next;
    }
}

// These would be enough for now
char *get_http_message(int code) {
    // split handle different code ranges, thus minimize jump table
    if (code < 300) {
        switch(code) {
            case 200:
                return "OK";
            case 204:
                return "No Content";
        }
    }

    if (code < 500) {
        switch(code) {
            case 400:
                return "Bad Request";
            case 403:
                return "Forbidden";
            case 404:
                return "Not Found";
            case 405:
                return "Method Not Allowed";
        }
    }
    if (code == 500) return "Internal Server Error";
    return "";
}

void free_headers(header_t *header) {
    header_t *curr = header, *next;
    while(curr) {
        next = curr->next;
        free(curr->name);
        free(curr->value);
        free(curr);
        curr = next;
    }
}

void free_request(req_t *req) {
    if (req->location) {
        if (req->location->path) free(req->location->path);
        if (req->location->hash) free(req->location->hash);
        if (req->location->query) free(req->location->query);
        free(req->location);
    }
    if (req->header) free_headers(req->header);
}

void free_response(res_t *res) {
    if (res->body) free(res->body);
    if (res->header) free_headers(res->header);
}

void req_init(req_t *req) {
    req->header = NULL;
    req->location = NULL;
}

void res_init(res_t *res) {
    res->body = NULL;
    res->length = 0;
    res->header = NULL;
    res->last = NULL;
    res->status = 0;
}

char *stringify_time(time_t t) {
    char *ret = ctime(&t);
    trimright_line(ret);
    return ret;
}

char *get_extension(const char *name) {
    char *ret = rindex(name, '.');
    return ret ? ret + 1 : NULL;
}

char *get_mime(const char *ext) {
    if (!ext) return NULL;
    size_t len = sizeof(mime_table) / sizeof(mime_table[0]), i;
    for(i = 0; i < len; i++) {
        if (strncasecmp(mime_table[i][0], ext, strlen(ext)) == 0) {
            return mime_table[i][1];
        }
    }
    return NULL;
}


int parse_location(char *url, location_t* loc) {
    char *pathptr = NULL, *queryptr = NULL, *hashptr = NULL;
    loc->path = NULL;
    loc->hash = NULL;
    loc->query = NULL;
    size_t len = strlen(url);
    if (len == 0) return FAILED;

    if (url[0] == '/') {
        pathptr = url;
    } else {
        // If request come from a proxy, host is also included  in startline URI part
        // https://www.w3.org/Protocols/rfc2616/rfc2616-sec5.html#sec5.1.2
        if (strncasecmp(url, "https://", 8) == 0) {
            pathptr = index(url + 8, '/');
        } else if (strncasecmp(url, "http://", 7) == 0) {
            pathptr = index(url + 7, '/');
        }
    }
    if (pathptr == NULL || (len = strlen(pathptr)) == 0) {
        return FAILED;
    }

    if ((queryptr = index(pathptr, '?')) != NULL) {
        *queryptr = 0;
        queryptr++;
    }
    if ((hashptr = index(pathptr, '#')) != NULL) {
        *hashptr = 0;
        hashptr++;
    }

    loc->path = (char *)malloc(len + 1);
    strncpy(loc->path, pathptr, len + 1);

    if (queryptr && (len = strlen(queryptr)) > 0) {
        loc->query = (char *)malloc(len + 1);
        strncpy(loc->query, queryptr, len + 1);
    }

    if (hashptr && (len = strlen(hashptr)) > 0) {
        loc->hash = (char *)malloc(len + 1);
        strncpy(loc->hash, hashptr, len + 1);
    }
    return OK;
}

int check_method(char *method) {
    size_t i, len = sizeof(methods) / sizeof(methods[0]);
    for(i = 0; i < len; i++) {
        if (strncasecmp(methods[i], method, strlen(method)) == 0) {
            return OK;
        }
    }
    return FAILED;
}
