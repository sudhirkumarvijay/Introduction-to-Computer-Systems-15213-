/* ----------------------------------------------------------------------------
 * File: proxy.c
 * Name: Sudhir Kumar Vijay
 * Andrew ID: svijay@andrew.cmu.edu
 * Private dependencies - csapp.c csapp.h cache.c cache.h
 * ----------------------------------------------------------------------------
 * Implements a multi-threaded web proxy that serves a web browser with real 
 * content from servers. In addition to serving web content to the web-browser 
 * client, the proxy also has the provision to store objects elements that are 
 * less than 100KB and with a total size of 1024MB. 
 * 
 * The web-proxy is designed only to sevice HTTP GET requests and will force
 * a HTTP/1.0 request, even if the request from the client was a HTTP/1.1. A 
 * pre-defined set of HTTP headers are sent to the web-server.
 *
 * The proxy spawns off new threads to handle requests, whenever a new client
 * connection is accepted. This provides the advantage of serving requests to
 * clients more responsively - a key feature that is required when the modern
 * websites are growing increasingly complex in terms of the content that they
 * display.
 *
 * To combat the kernel's SIGPIPE connection (that will be delivered to handle
 * a socket which has been broken), a SIGPIPE handler is installed to prevent
 * the proxy from being terminated. Also, 'read' and 'write' functions tend to
 * return -1 with errno being set to ECONNRESET and EPIPE while reading/writing
 * data from/to a closed-connection socket. The proxy is designed to handle 
 * this and will not exit under any circumstance.
 *
 * Cache is implemented using a Least-Recently-Used (LRU) policy, where-in a 
 * linked-list based queue scheme is implemented. The least-recently used node, 
 * consisting of the oldest cached buffer is evicted, when there is a need to
 * make room for newer buffer entries. To maintain size of the cache, the size 
 * of the input buffer is first checked to be below MAX_OBJECT_SIZE and then
 * if the total size, after addition is lesser than MAX_CACHE_SIZE. If the 
 * expected size is more than the MAX_CACHE_SIZE then the LRU blocks are 
 * evicted to make room for the new entries .
 * 
 * The installed proxy was tested to work succesfully to deliver content from
 * several websites, including:
 * - http://www.cs.cmu.edu/~213
 * - http://csapp.cs.cmu.edu
 * - http://www.cmu.edu
 * - http://www.amazon.com
 * - http://www.9gag.com
 *
 * ----------------------------------------------------------------------------
*/
/* Include files */
#include <stdio.h>
#include "csapp.h"
#include "cache.h"

/* Max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* Function definitions */
void forward_to_server(int clientfd);
void read_from_client(char*port);
void clienterror(int fd, char *cause, char *errnum, 
     char *shortmsg, char *longmsg) ;
void sigpipe_handler(int sig); 
static void init_forward_to_server(void);
void read_request_header(rio_t *rio_in, char* header_body, 
      char* host_header, int* host_header_found);
void write_request_header(int proxyfd, int host_header_found,
       char* host_header, char* header_body, char* host);
void parse_uri(char* uri, char* host, char* query, char* port);
void forward_from_server(int clientfd, char* uri, char* host, char* query, 
    char* port, int host_header_found, char* host_header, char* header_body);

/* Debug define */
/* Uncomment to enable print messages */
// #define DEBUG_VERBOSE

/* Default header content */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *accept_hdr = "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n";
static const char *accept_encoding_hdr = "Accept-Encoding: gzip, deflate\r\n";
static const char *connection_hdr = "Connection: close\r\n";
static const char *proxy_connection_hdr = "Proxy-Connection: close\r\n";
void *thread(void *vargp);

/* Global variables */
static sem_t mutex, w;
static int readcnt;
int host_header_found; 

/* ----------------------------------------------------------------------------
 * Function: main
 * Input parameters: argc, argv from commandline.
 * Return parameters: int return value on exit.
 * ----------------------------------------------------------------------------
 * Description: 
 * Used to start the proxy on a specific port, as specified using the command-
 * line. Installs the main sigpipe handler and restores the default handler on
 * exit. 
 * ----------------------------------------------------------------------------
 */
int main(int argc, char **argv){
    char* port;
    readcnt = 0;
    if (argc != 2){
        fprintf(stderr," usage:  %s <port>\n", argv[0]);
        exit(1);
    }
    port = argv[1]; 
    Signal(SIGPIPE,  sigpipe_handler);
    read_from_client(port);
    Signal(SIGPIPE,  SIG_DFL); /* Restoring default handler */
    return 0;
}

/* ----------------------------------------------------------------------------
 * Function: sigpipe_handler
 * Input parameters: signal to be handler 
 * Return parameters: -- None -- 
 * ----------------------------------------------------------------------------
 * Description: 
 * Bypasses the default sigpipe operation by kernel.
 * ----------------------------------------------------------------------------
 */
void sigpipe_handler(int sig) 
{   
    return;
}

/* ----------------------------------------------------------------------------
 * Function: read_from_client 
 * Input parameters: Port where the proxy is hosted current. 
 * Return parameters: --None-- 
 * ----------------------------------------------------------------------------
 * Description: 
 * This function listens for connection requests from the client(our handy web
 * browser) and spawns off different threads - each for a new connection that
 * is accepted.
 *
 * The connfdp is a pointer to the connfd (connection file-descriptor) which is
 * passed to each of the threads. 
 * ----------------------------------------------------------------------------
 */
void read_from_client(char* input_port) 
{
    int  listenfd, *connfdp;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    /* Proxy server binds and listens at port */
    listenfd = Open_listenfd(input_port);

    while (1) {
        clientlen = sizeof(clientaddr);
    
        /* Proxy accepts connection from client and spawns threads */
        connfdp = Malloc(sizeof(int));
        *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Pthread_create(&tid, NULL ,thread, connfdp);
    }
}

/* ----------------------------------------------------------------------------
 * Function: thread 
 * Input parameters: Thread parameters 
 * Return parameters: --None-- 
 * ----------------------------------------------------------------------------
 * Description:
 * Individual thread for each new accepted connection from the client. 
 * ----------------------------------------------------------------------------
 */
void *thread(void *vargp){
    int connfd = *((int*) vargp); 
    
    #ifdef DEBUG_VERBOSE
    static int i = 0;
    printf("Inside thread : %d\n", i++);
    #endif

    /* Making thread detachable so that its resources are freed when done */
    Pthread_detach(pthread_self());

    /* Freeing the malloced resource from main function*/
    Free(vargp); 
    
    /* Calling forward_to_server to service client's requests */
    forward_to_server(connfd);        
    Close(connfd);
    return NULL;
}

/* ----------------------------------------------------------------------------
 * Function: init_forward_to_server 
 * Input parameters: --None-- 
 * Return parameters: --None-- 
 * ----------------------------------------------------------------------------
 * Description:
 * This function is used to initialize the readers/writers mutexes.
 * Function is called only once during threads' intialization.
 * ----------------------------------------------------------------------------
 */
static void init_forward_to_server(void){
    Sem_init(&mutex, 0, 1);
    Sem_init(&w, 0, 1);
}

/* ----------------------------------------------------------------------------
 * Function: forward_to_server 
 * Input parameters: client's file descriptor. 
 * Return parameters: 
 * ----------------------------------------------------------------------------
 * Description:
 * Function that processes data received from client and forwards to server.
 * The function also searches for cached requests and returns the corresponding
 * data if found in the cache. Else a new connection is opened to the webserver
 * new data is then sent to the client, along with a cache update.
 * ----------------------------------------------------------------------------
 */
void forward_to_server(int clientfd) 
{
    /* Buffers used to store various data */
    char method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char host[MAXLINE], port[MAXLINE], query[MAXLINE];
    char proxy_buf[MAXLINE], host_header[MAXLINE], uri_bkup[MAXLINE];
    /* The thread's cache_buffer */
    char new_cache_buf[MAX_OBJECT_SIZE];
    char header_body[MAXBUF];

    rio_t rio_in;
    size_t new_size = 0;
    int host_header_found = 0; 
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    
    cache_element* new_cache_element = NULL;
    Pthread_once(&once, init_forward_to_server);

    /* Read request line and headers */
    Rio_readinitb(&rio_in, clientfd);
    /* Part1: Request line */
    if (!Rio_readlineb(&rio_in, proxy_buf, MAXLINE)) 
    return;
    sscanf(proxy_buf, "%s http://%s %s", method, uri, version);      
    #ifdef DEBUG_VERBOSE
    printf("Client Request: %s", proxy_buf);
    #endif
    /* Part2: Header body */
    read_request_header (&rio_in, header_body, host_header, &host_header_found);
    
    if (strcasecmp(method, "GET")) {             
        clienterror(clientfd, method, "501", "Not Implemented",
                "Tiny does not implement this method");
        return;
    }
    
    /* Backing up URI, to be used as a cache-indexer */      
    strcpy(uri_bkup, uri);
    /* Parsing URI for host, query and port */
    parse_uri(uri, host, query, port);
    /* Overwriting HTTP/1.1, if any other HTTP* requests */
    strcpy(version, "HTTP/1.0");
    
    /* ----- Start of Readers' Section ----- */
    /* Implementing a Readers' lock, with priority given to readers */
    P(&mutex);
    readcnt++;
    if(readcnt ==1)
        P(&w);
    V(&mutex);

    /* Critical Reading section: Searching for previous cache entries */
    new_cache_element = find_node(uri_bkup);
    /* If previous node in cache is found, service it to the client */
    if(new_cache_element != NULL){
        new_size = new_cache_element->size;
        memcpy(new_cache_buf, new_cache_element->cache_buf, new_size);
        Rio_writen(clientfd, new_cache_buf, new_size);
    }

    P(&mutex);
    readcnt--;
    if(readcnt == 0)
        V(&w);
    V(&mutex);
    /* ----- End of Readers' Section ----- */
 
    /* ----- Start Writers' section ----- */
    /* Implementing a Writers' lock, with priority given to readers */
    /* Writers Part 1: Previous cache entry exists and has been serviced to
     * the client. Need to modify cache to implement LRU - so a queue
     * based policy is implemented. */
    P(&w); /* Locking writers mutex */
    if((new_cache_element = find_node(uri_bkup))!= NULL){
        #ifdef DEBUG_VERBOSE
        printf("Sending cache data to client\n");
        #endif
        new_size = new_cache_element->size;
        memcpy(new_cache_buf, new_cache_element->cache_buf, new_size);
        delete_from_cache(new_cache_element);
        add_to_cache(uri_bkup, new_cache_buf, new_size);
    }
    V(&w); /* Unlocking writers mutex */

    /* Writers Part 2: Previous cache entry does not exist. Need to write new
     * data from server into the cache.*/
    forward_from_server(clientfd, uri_bkup, host, query, port, 
                        host_header_found, host_header, header_body);
    return;
}

/* ----------------------------------------------------------------------------
 * Function: Client error 
 * Input parameters: client's file descriptor. 
 * Return parameters: 
 * ----------------------------------------------------------------------------
 * Description:
 * Used to display error messages to the client
 * ----------------------------------------------------------------------------
 */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, 
                char *longmsg) 
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}
/* ----------------------------------------------------------------------------
 * Function: read_request_header 
 * Input parameters: pointer to rio_in, header_body, host_header,
 * 		     host_header_found
 * Return parameters: -none-
 * ----------------------------------------------------------------------------
 * Description:
 * Used to read the header data coming from the client. Sets a flag if the 
 * client sends its own "Host:" parameter as part of its header data- this
 * is set in the host_header_found.
 * ----------------------------------------------------------------------------
 */
void read_request_header(rio_t *rio_in, char* header_body,
	char* host_header, int* host_header_found){
    char proxy_buf[MAXLINE];
    strcpy(header_body, "");
    strcpy(host_header, "");
    strcpy(proxy_buf, "");
    *host_header_found = 0;
    size_t n;
    
    Rio_readlineb(rio_in, proxy_buf, MAXLINE);
    while(strcmp(proxy_buf, "\r\n")){ 
       n = Rio_readlineb(rio_in, proxy_buf, MAXLINE);
       if(strstr(proxy_buf, "Host:")){
           *host_header_found = 1;
            strncpy(host_header, proxy_buf, n);
       }
       else if( (strstr(proxy_buf, "User-Agent:")==0) &&
            (strstr(proxy_buf, "Accept:")==0) &&
            (strstr(proxy_buf, "Accept-Encoding:")==0) &&
            (strstr(proxy_buf, "Connection:")==0) &&
            (strstr(proxy_buf, "Proxy-Connection")==0)){
            strncat(header_body, proxy_buf, n);
        }
   }
   return; 
}

/* ----------------------------------------------------------------------------
 * Function: write_request_header
 * Input parameters: proxyfd, host_header_found, host_header, header_body,
 *                   host 
 * Return parameters: -none-
 * ----------------------------------------------------------------------------
 * Description:
 * Writes the required HTTP headers to the webserver. host_header_found is 
 * used to indicate that the client is sending its own "Host:" as part of
 * the header.
 * ----------------------------------------------------------------------------
 */
void write_request_header(int proxyfd, int host_header_found,
       char* host_header, char* header_body, char* host) {
    char proxy_buf[MAXLINE];
    
    /* HTTP headers */
    if(host_header_found == 1) {
        /* Webclient sends its own host_header */
        Rio_writen(proxyfd, host_header, strlen(header_body));
    } else {
        /* Webclient does not send own host */
        sprintf(proxy_buf, "Host: %s\r\n", host);
        Rio_writen(proxyfd, proxy_buf, strlen(proxy_buf));
    }

    /* Serving default headers, given in handout */
    sprintf(proxy_buf, "%s", user_agent_hdr);
    Rio_writen(proxyfd, proxy_buf, strlen(proxy_buf));
    sprintf(proxy_buf, "%s", accept_hdr);
    Rio_writen(proxyfd, proxy_buf, strlen(proxy_buf));
    sprintf(proxy_buf, "%s", accept_encoding_hdr);
    Rio_writen(proxyfd, proxy_buf, strlen(proxy_buf));
    sprintf(proxy_buf, "%s", connection_hdr);
    Rio_writen(proxyfd, proxy_buf, strlen(proxy_buf));
    sprintf(proxy_buf, "%s\r\n", proxy_connection_hdr);
    Rio_writen(proxyfd, proxy_buf, strlen(proxy_buf));

    /* Writing other non-default header requests from client */
    Rio_writen(proxyfd, header_body, strlen(header_body));
    return;
}
/* ----------------------------------------------------------------------------
 * Function: parse_uri
 * Input parameters: uri, host, query, port pointers 
 * Return parameters: -none-
 * ----------------------------------------------------------------------------
 * Description:
 * Parses the URI and saves host, query, port values.
 * ----------------------------------------------------------------------------
 */

void parse_uri(char* uri, char* host, char* query, char* port){
    char* index_ptr = NULL;
    char hostname[MAXLINE];
    
    index_ptr = index(uri, '/');
    strcpy(query, index_ptr);
    *index_ptr = '\0';
    strcpy(hostname, uri);

    /* Checking if the client has issued a specific port number */ 
    if(strstr(hostname, ":")){
        index_ptr = index(hostname, ':');
        strcpy(port, index_ptr+1);
        *index_ptr = '\0';
        strcpy(host, hostname);
    } else {
        /* Sticking to default HTTP port in case none is mentioned */
        strcpy(host, hostname);
        strcpy(port, "80");
    }
    return;
}

/* ----------------------------------------------------------------------------
 * Function:forward_from_server
 * Input parameters: clientfd, uri, host, query, port, host_header_found, 
 *                   host_header, header_body 
 * Return parameters: -none-
 * ----------------------------------------------------------------------------
 * Description:
 * Responsible for delivering content from wbeserver to client and updating
 * the cache if applicable.
 * ----------------------------------------------------------------------------
 */

void forward_from_server(int clientfd, char* uri, char* host, char* query,
    char* port, int host_header_found, char* host_header, char* header_body) 
    {
    cache_element* new_cache_element = NULL;
    int buf_entry_invalid = 0, pos = 0;
    char new_cache_buf[MAX_OBJECT_SIZE];
    char proxy_buf[MAXLINE];
    size_t n, new_size = 0;
    rio_t rio_out;
    int proxyfd;

    if((new_cache_element = find_node(uri))== NULL){
        /* Invalid buf entry flag set because of bad read/write operations
         * or if too big an object for the cache */
        buf_entry_invalid = 0; 
        memset(new_cache_buf, 0, MAX_OBJECT_SIZE);
        new_size = 0;
        
        #ifdef DEBUG_VERBOSE
        printf("Server response not in cache: %s\n", uri);
        #endif
        proxyfd = Open_clientfd(host, port);
        Rio_readinitb(&rio_out, proxyfd);

        /* Send HTTP request and header data to main server */
        /* Main request */
        sprintf(proxy_buf, "GET %s HTTP/1.0\r\n", query);
        Rio_writen(proxyfd, proxy_buf, strlen(proxy_buf));
	
        /* Headers */
        write_request_header(proxyfd, host_header_found, host_header, 
                             header_body, host);

        /* Reading data from webserver */
        while((n = Rio_readlineb(&rio_out, proxy_buf, MAXLINE)) > 0) { 
            /* If error on writing to client, break and return */ 
            if(Rio_writen(clientfd, proxy_buf, n)<0){
                buf_entry_invalid = 1;
                break;
            }
            new_size += n;
            if(new_size <= MAX_OBJECT_SIZE){
                memcpy(&new_cache_buf[pos], proxy_buf, n);
                pos +=n;
            } else {
                #ifdef DEBUG_VERBOSE
                printf("Too big cache entry, discarding \n");
                #endif
                buf_entry_invalid = 1;
            }
        }
        /* Write into cache if buffer entry is smaller than MAX_OBJECT_SIZE */
        if(buf_entry_invalid == 0){
            P(&w); /* Locking writers mutex */
            add_to_cache (uri, new_cache_buf, new_size);
            V(&w); /* Unlocking writers mutex */
        }
        Close(proxyfd); 
    }    
    return;
}
