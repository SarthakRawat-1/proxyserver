// Implementing a Proxy Server with LRU cache, MultiThreading and support for the four basic HTTP methods.
// To test this, open Browser in Incognito mode (as broswer has it's own cache, incognito will not have that so we can test our own cache).
// Then go to Setting -> Network Settings -> Manual Proxy Configuration and put HTTP Proxy :- localhost and Port :- 8080 and Check the box for "Use this proxy server for all protocols"
// Finally, to send requests to the same website, you need to open different browser windows as HTTP/1.1 by default uses persistent connections.
// This means the browser keeps the TCP connection open to reuse it for multiple requests to the same server instead of creating a new connection for every request.

// Endpoints used for Testing :-
// GET :- http://httpbin.org/get
// POST :- http://httpbin.org/forms/post
// PUT :- http://httpbin.org/put
// DELETE :- https://jsonplaceholder.typicode.com/posts/1

// Import Required Header files
#include "proxy_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/wait.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>

#define h_addr h_addr_list[0] // For Backward Compatibility */

#define MAX_BYTES 4096    // Maximum Size of Request/Response
#define MAX_CLIENTS 400     // Maximum number of Client Requests Served at a time
#define MAX_SIZE 200*(1<<20)     // Size of the cache
#define MAX_ELEMENT_SIZE 10*(1<<20)     // Max Size of an element in cache

typedef struct cache_element cache_element; // This structure defines an individual entry in a cache.
struct cache_element{ 
    char* data;         // Response
    int len;          // Length of data 
    char* url;        // Request URL
	time_t lru_time_track;    // Latest Time the element is accessed
    cache_element* next;    // Pointer to next element
};

cache_element* find(char* url); // This function will help us find element from our cache.
int add_cache_element(char* data,int size,char* url); // This function will help us add an element to the cache.
void remove_cache_element(); // This function will be used to remove elements from cache when it is full and there is a new request.

int port_number = 8080;				// Default Port
int proxy_socketId;					// Socket descriptor of Proxy Server (not Web Server)
pthread_t tid[MAX_CLIENTS];         // Array to store the thread ids of clients
sem_t seamaphore;	                // Semaphore to keep in check the client requests do not exceed max number of requests 

//sem_t cache_lock;			       
pthread_mutex_t lock;               // For locking the cache


cache_element* head;                // Pointer to cache as it is a Linked List
int cache_size;             // Denotes the current size of the cache

// This function is designed to send an HTTP error response to a client over a network socket.
int sendErrorMessage(int socket, int status_code)
{
	char str[1024];
	char currentTime[50];
	time_t now = time(0);

	struct tm data = *gmtime(&now);
	strftime(currentTime,sizeof(currentTime),"%a, %d %b %Y %H:%M:%S %Z", &data);

	switch(status_code)
	{
		case 400: snprintf(str, sizeof(str), "HTTP/1.1 400 Bad Request\r\nContent-Length: 95\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: SarthakN/14785\r\n\r\n<HTML><HEAD><TITLE>400 Bad Request</TITLE></HEAD>\n<BODY><H1>400 Bad Rqeuest</H1>\n</BODY></HTML>", currentTime);
				  printf("400 Bad Request\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 403: snprintf(str, sizeof(str), "HTTP/1.1 403 Forbidden\r\nContent-Length: 112\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: SarthakN/14785\r\n\r\n<HTML><HEAD><TITLE>403 Forbidden</TITLE></HEAD>\n<BODY><H1>403 Forbidden</H1><br>Permission Denied\n</BODY></HTML>", currentTime);
				  printf("403 Forbidden\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 404: snprintf(str, sizeof(str), "HTTP/1.1 404 Not Found\r\nContent-Length: 91\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: SarthakN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD>\n<BODY><H1>404 Not Found</H1>\n</BODY></HTML>", currentTime);
				  printf("404 Not Found\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 500: snprintf(str, sizeof(str), "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 115\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: SarthakN/14785\r\n\r\n<HTML><HEAD><TITLE>500 Internal Server Error</TITLE></HEAD>\n<BODY><H1>500 Internal Server Error</H1>\n</BODY></HTML>", currentTime);
				  //printf("500 Internal Server Error\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 501: snprintf(str, sizeof(str), "HTTP/1.1 501 Not Implemented\r\nContent-Length: 103\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: SarthakN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Implemented</TITLE></HEAD>\n<BODY><H1>501 Not Implemented</H1>\n</BODY></HTML>", currentTime);
				  printf("501 Not Implemented\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 505: snprintf(str, sizeof(str), "HTTP/1.1 505 HTTP Version Not Supported\r\nContent-Length: 125\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: SarthakN/14785\r\n\r\n<HTML><HEAD><TITLE>505 HTTP Version Not Supported</TITLE></HEAD>\n<BODY><H1>505 HTTP Version Not Supported</H1>\n</BODY></HTML>", currentTime);
				  printf("505 HTTP Version Not Supported\n");
				  send(socket, str, strlen(str), 0);
				  break;

		default:  return -1;

	}
	return 1;
}

// This function establishes a TCP connection to a remote server.
int connectRemoteServer(char* host_addr, int port_num)
{
	// Socket for Remote server, not Proxy Server
	int remoteSocket = socket(AF_INET, SOCK_STREAM, 0);

	if( remoteSocket < 0)
	{
		printf("Error in Creating Socket.\n");
		return -1;
	}
	
	// Get Host by IP address or name
	struct hostent *host = gethostbyname(host_addr);	
	if(host == NULL)
	{
		fprintf(stderr, "No such Host exists.\n");	
		return -1;
	}

	// Inserts IP address and Port number of host in struct server_addr
	struct sockaddr_in server_addr;

	bzero((char*)&server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port_num);

	bcopy((char *)host->h_addr,(char *)&server_addr.sin_addr.s_addr,host->h_length);

	// Connect to Remote Server as it will be serving us response
	if( connect(remoteSocket, (struct sockaddr*)&server_addr, (socklen_t)sizeof(server_addr)) < 0 )
	{
		fprintf(stderr, "Error in Connecting with Remote Server!\n"); 
		return -1;
	}

	// free(host_addr);
	return remoteSocket;
}

// Function to handle GET Requests
int handle_request(int clientSocket, ParsedRequest *request, char *tempReq) {
    char *buf = (char*)malloc(sizeof(char) * MAX_BYTES);
    strcpy(buf, "GET ");
    strcat(buf, request->path);
    strcat(buf, " ");
    strcat(buf, request->version);
    strcat(buf, "\r\n");
    
    size_t len = strlen(buf);

    if (ParsedHeader_set(request, "Connection", "close") < 0) {
        printf("Set Header key not working\n");
    }

    const char* host_header_value = ParsedHeader_get(request, "Host") ? ParsedHeader_get(request, "Host")->value : NULL;
    printf("Parsed request host: %s\n", request->host);
    if (host_header_value == NULL || strcmp(host_header_value, "localhost:8080") == 0) {
        if (request->host != NULL) {
            printf("Setting Host header to: %s\n", request->host);
            if (ParsedHeader_set(request, "Host", request->host) < 0) {
                printf("Set \"Host\" Header key not working\n");
            }
        } else {
            printf("No valid host found in request.\n");
            return -1;
        }
    }

    // Debugging Line to check if Host header is set correctly
    printf("Host header after setting: %s\n", ParsedHeader_get(request, "Host")->value);
    printf("Request Headers:\n");

    // Assuming headers is a null-terminated array of headers
    for (ParsedHeader* header = request->headers; header != NULL && header->key != NULL; header++) {
        printf("%s: %s\n", header->key, header->value);
    }

    if (ParsedRequest_unparse_headers(request, buf + len, (size_t)MAX_BYTES - len) < 0) {
        printf("Unparse failed\n");
        return -1;
    }

    printf("Request:\n%s\n", buf);  // Debugging line to print the full request

    int server_port = 80;  // Default port for HTTP
    if (request->port != NULL) {
        server_port = atoi(request->port);  // Use the port if provided in the request
    }

    int remoteSocketID = connectRemoteServer(request->host, server_port);
    if (remoteSocketID < 0) {
        printf("Failed to connect to remote server\n");
        return -1;
    }

    int bytes_send = send(remoteSocketID, buf, strlen(buf), 0);
    bzero(buf, MAX_BYTES);
    bytes_send = recv(remoteSocketID, buf, MAX_BYTES - 1, 0);

    char *temp_buffer = (char*)malloc(sizeof(char) * MAX_BYTES);  // Temp buffer to store response
    int temp_buffer_size = MAX_BYTES;
    int temp_buffer_index = 0;

    while (bytes_send > 0) {
        int bytes_sent_to_client = send(clientSocket, buf, bytes_send, 0);

        for (int i = 0; i < bytes_send / sizeof(char); i++) {
            temp_buffer[temp_buffer_index] = buf[i];
            temp_buffer_index++;
        }
        temp_buffer_size += MAX_BYTES;
        temp_buffer = (char*)realloc(temp_buffer, temp_buffer_size);

        if (bytes_sent_to_client < 0) {
            perror("Error in sending data to client socket.\n");
            break;
        }
        bzero(buf, MAX_BYTES);
        bytes_send = recv(remoteSocketID, buf, MAX_BYTES - 1, 0);
    }

    temp_buffer[temp_buffer_index] = '\0';
    free(buf);

    add_cache_element(temp_buffer, strlen(temp_buffer), tempReq);
    printf("Done\n");
    free(temp_buffer);
    close(remoteSocketID);
    return 0;
}

// Function to handle POST Requests
int handle_post_request(int clientSocket, ParsedRequest *request, char *body, char *tempReq) {
    char *buf = (char*)malloc(sizeof(char) * MAX_BYTES);
    sprintf(buf, "POST %s %s\r\n", request->path, request->version);

    size_t len = strlen(buf);

    if (ParsedHeader_set(request, "Connection", "close") < 0) {
        printf("Set header key not working\n");
    }

    const char* host_header_value = ParsedHeader_get(request, "Host") ? ParsedHeader_get(request, "Host")->value : NULL;
    if (host_header_value == NULL || strcmp(host_header_value, "localhost:8080") == 0) {
        if (request->host != NULL) {
            printf("Setting Host header to: %s\n", request->host);
            if (ParsedHeader_set(request, "Host", request->host) < 0) {
                printf("Set \"Host\" header key not working\n");
            }
        } else {
            printf("No valid host found in request.\n");
            return -1;
        }
    }

    if (ParsedRequest_unparse_headers(request, buf + len, (size_t)MAX_BYTES - len) < 0) {
        printf("Unparse failed\n");
    }

    // Append headers and body
    strcat(buf, "\r\n");
    strcat(buf, body);

    printf("Full request being sent to remote server:\n%s\n", buf);  // Debugging Line to check if Host header is set correctly in Full Request

    int server_port = 80;  // Default Remote Server Port
    if (request->port != NULL) {
        server_port = atoi(request->port);
    }

    int remoteSocketID = connectRemoteServer(request->host, server_port);
    if (remoteSocketID < 0) {
        free(buf);
        return -1;
    }

    int bytes_sent = send(remoteSocketID, buf, strlen(buf), 0);
    if (bytes_sent < 0) {
        perror("Error sending data to remote server");
        free(buf);
        return -1;
    }

    bzero(buf, MAX_BYTES);
    int bytes_received = recv(remoteSocketID, buf, MAX_BYTES - 1, 0);

    char *temp_buffer = (char*)malloc(sizeof(char) * MAX_BYTES);  // Temp buffer to store response
    int temp_buffer_size = MAX_BYTES;
    int temp_buffer_index = 0;

    while (bytes_received > 0) {
        // Append received data to temp_buffer
        for (int i = 0; i < bytes_received; i++) {
            temp_buffer[temp_buffer_index++] = buf[i];
            if (temp_buffer_index >= temp_buffer_size) {
                temp_buffer_size += MAX_BYTES;
                temp_buffer = (char*)realloc(temp_buffer, temp_buffer_size);
            }
        }

        int bytes_sent_to_client = send(clientSocket, buf, bytes_received, 0);

        if (bytes_sent_to_client < 0) {
            perror("Error in sending data to client socket.\n");
            break;
        }
        bzero(buf, MAX_BYTES);
        bytes_received = recv(remoteSocketID, buf, MAX_BYTES - 1, 0);
    }

    temp_buffer[temp_buffer_index] = '\0';

    // Print JSON content
    char *json_start = strstr(temp_buffer, "\r\n\r\n");
    if (json_start) {
        json_start += 4;  // Skip "\r\n\r\n"
        printf("JSON Data:\n%s\n", json_start);
    } else {
        printf("Failed to locate JSON content\n");
    }

    free(buf);
    free(temp_buffer);
    printf("Done POST\n");

    close(remoteSocketID);
    return 0;
}

// Function to handle PUT Requests
int handle_put_request(int clientSocket, ParsedRequest *request, char *body, char *tempReq) {
    char *buf = (char*)malloc(sizeof(char) * MAX_BYTES);
    sprintf(buf, "PUT %s %s\r\n", request->path, request->version);

    size_t len = strlen(buf);

    // Set headers
    if (ParsedHeader_set(request, "Connection", "close") < 0) {
        printf("set header key not working\n");
    }

    const char* host_header_value = ParsedHeader_get(request, "Host") ? ParsedHeader_get(request, "Host")->value : NULL;
    if (host_header_value == NULL || strcmp(host_header_value, "localhost:8080") == 0) {
        if (request->host != NULL) {
            printf("Setting Host header to: %s\n", request->host);
            if (ParsedHeader_set(request, "Host", request->host) < 0) {
                printf("Set \"Host\" header key not working\n");
            }
        } else {
            printf("No valid host found in request.\n");
            return -1;
        }
    }

    if (ParsedRequest_unparse_headers(request, buf + len, (size_t)MAX_BYTES - len) < 0) {
        printf("unparse failed\n");
    }

    // Append body
    strcat(buf, "\r\n");
    strcat(buf, body);

    printf("Full request being sent to remote server:\n%s\n", buf);  // Debugging Line to check if Host header is set correctly in Full Request

    int server_port = 80; // Default Remote Server Port
    if (request->port != NULL) {
        server_port = atoi(request->port);
    }

    int remoteSocketID = connectRemoteServer(request->host, server_port);
    if (remoteSocketID < 0) {
        free(buf);
        return -1;
    }

    int bytes_sent = send(remoteSocketID, buf, strlen(buf), 0);
    if (bytes_sent < 0) {
        perror("Error sending data to remote server");
        free(buf);
        return -1;
    }

    bzero(buf, MAX_BYTES);
    int bytes_received = recv(remoteSocketID, buf, MAX_BYTES - 1, 0);

    char *temp_buffer = (char*)malloc(sizeof(char) * MAX_BYTES);  // Temp buffer to store response
    int temp_buffer_size = MAX_BYTES;
    int temp_buffer_index = 0;

    while (bytes_received > 0) {
        // Append received data to temp_buffer
        for (int i = 0; i < bytes_received; i++) {
            temp_buffer[temp_buffer_index++] = buf[i];
            if (temp_buffer_index >= temp_buffer_size) {
                temp_buffer_size += MAX_BYTES;
                temp_buffer = (char*)realloc(temp_buffer, temp_buffer_size);
            }
        }

        int bytes_sent_to_client = send(clientSocket, buf, bytes_received, 0);

        if (bytes_sent_to_client < 0) {
            perror("Error in sending data to client socket.\n");
            break;
        }
        bzero(buf, MAX_BYTES);
        bytes_received = recv(remoteSocketID, buf, MAX_BYTES - 1, 0);
    }

    temp_buffer[temp_buffer_index] = '\0';

    // Print JSON content
    char *json_start = strstr(temp_buffer, "\r\n\r\n");
    if (json_start) {
        json_start += 4;  // Skip "\r\n\r\n"
        printf("JSON Data:\n%s\n", json_start);
    } else {
        printf("Failed to locate JSON content\n");
    }

    free(buf);
    free(temp_buffer);
    printf("Done PUT\n");

    close(remoteSocketID);
    return 0;
}

// Function to handle DELETE Requests
int handle_delete_request(int clientSocket, ParsedRequest *request, char *buffer, char *tempReq) {
    char *buf = (char*)malloc(sizeof(char) * MAX_BYTES);
    sprintf(buf, "DELETE %s %s\r\n", request->path, request->version);

    size_t len = strlen(buf);

    // Set headers
    if (ParsedHeader_set(request, "Connection", "close") < 0) {
        printf("set header key not work\n");
    }

    const char* host_header_value = ParsedHeader_get(request, "Host") ? ParsedHeader_get(request, "Host")->value : NULL;
    if (host_header_value == NULL || strcmp(host_header_value, "localhost:8080") == 0) {
        if (request->host != NULL) {
            printf("Setting Host header to: %s\n", request->host);
            if (ParsedHeader_set(request, "Host", request->host) < 0) {
                printf("Set \"Host\" header key not working\n");
            }
        } else {
            printf("No valid host found in request.\n");
            return -1;
        }
    }

    if (ParsedRequest_unparse_headers(request, buf + len, (size_t)MAX_BYTES - len) < 0) {
        printf("unparse failed\n");
    }

    strcat(buf, "\r\n");

    printf("Full request being sent to remote server:\n%s\n", buf);  // Debugging Line to check if Host header is set correctly in Full Request

    int server_port = 80; // Default Remote Server Port
    if (request->port != NULL) {
        server_port = atoi(request->port);
    }

    int remoteSocketID = connectRemoteServer(request->host, server_port);
    if (remoteSocketID < 0) {
        free(buf);
        return -1;
    }

    int bytes_sent = send(remoteSocketID, buf, strlen(buf), 0);
    if (bytes_sent < 0) {
        perror("Error sending data to remote server");
        free(buf);
        return -1;
    }

    bzero(buf, MAX_BYTES);
    int bytes_received = recv(remoteSocketID, buf, MAX_BYTES - 1, 0);

    char *temp_buffer = (char*)malloc(sizeof(char) * MAX_BYTES);  // Temp buffer to store response
    int temp_buffer_size = MAX_BYTES;
    int temp_buffer_index = 0;

    while (bytes_received > 0) {
        // Append received data to temp_buffer
        for (int i = 0; i < bytes_received; i++) {
            temp_buffer[temp_buffer_index++] = buf[i];
            if (temp_buffer_index >= temp_buffer_size) {
                temp_buffer_size += MAX_BYTES;
                temp_buffer = (char*)realloc(temp_buffer, temp_buffer_size);
            }
        }

        printf("Response from remote server: %s\n", buf); // Print the response received

        int bytes_sent_to_client = send(clientSocket, buf, bytes_received, 0);

        if (bytes_sent_to_client < 0) {
            perror("Error in sending data to client socket.\n");
            break;
        }
        bzero(buf, MAX_BYTES);
        bytes_received = recv(remoteSocketID, buf, MAX_BYTES - 1, 0);
    }

    temp_buffer[temp_buffer_index] = '\0';

    free(buf);
    free(temp_buffer);
    printf("Done DELETE\n");

    close(remoteSocketID);
    return 0;
}


// This function determines the HTTP version of a given message.
int checkHTTPversion(char *msg)
{
	int version = -1;

	if(strncmp(msg, "HTTP/1.1", 8) == 0)
	{
		version = 1;
	}
	else if(strncmp(msg, "HTTP/1.0", 8) == 0)			
	{
		version = 1;										// Handling this similar to version 1.1
	}
	else
		version = -1;

	return version;
}

// The function thread_fn() is a thread handler for a proxy server that processes incoming HTTP requests from a connected client.
void* thread_fn(void* socketNew)
{
	sem_wait(&seamaphore); // Take the semaphore
	int p;
	sem_getvalue(&seamaphore,&p);
	printf("Semaphore Value :- %d\n",p);
    int* t= (int*)(socketNew);
	int socket=*t;           // Socket descriptor of the Connected Client
	int bytes_send_client,len;	  // Bytes Transferred

	
	char *buffer = (char*)calloc(MAX_BYTES,sizeof(char));	// Creating Buffer of 4KB for a Client
	
	
	bzero(buffer, MAX_BYTES);								// Making Buffer zero
	bytes_send_client = recv(socket, buffer, MAX_BYTES, 0); // Receiving the Request of Client by proxy server
	
	while(bytes_send_client > 0)
	{
		len = strlen(buffer);
        //loop until u find "\r\n\r\n" in the buffer
		if(strstr(buffer, "\r\n\r\n") == NULL)
		{	
			bytes_send_client = recv(socket, buffer + len, MAX_BYTES - len, 0);
		}
		else{
			break;
		}
	}

	char *tempReq = (char*)malloc(strlen(buffer)*sizeof(char)+1);
    // tempReq, buffer both store the HTTP request sent by client
	for (int i = 0; i < strlen(buffer); i++)
	{
		tempReq[i] = buffer[i];
	}
	
	// Checking for the request in cache (only GET Requests are stored in Cache)
	struct cache_element* temp = find(tempReq);

	if( temp != NULL){
        // Request found in cache, so sending the response to client from proxy's cache
		int size=temp->len/sizeof(char);
		int pos=0;
		char response[MAX_BYTES];
		while(pos<size){
			bzero(response,MAX_BYTES);
			for(int i=0;i<MAX_BYTES;i++){
				response[i]=temp->data[pos];
				pos++;
			}
			send(socket,response,MAX_BYTES,0);
		}
		printf("Data retrived from the Cache\n\n");
		printf("%s\n\n",response);
		// close(socketNew);
		// sem_post(&seamaphore);
		// return NULL;
	}
	
	
	else if(bytes_send_client > 0)
	{
		len = strlen(buffer); 
		// Parsing the request
		ParsedRequest* request = ParsedRequest_create();
		
        // ParsedRequest_parse returns 0 on success and -1 on failure.On success it stores parsed request in the request
		if (ParsedRequest_parse(request, buffer, len) < 0) 
		{
		   	printf("Parsing failed\n");
		}
		else
		{	
			bzero(buffer, MAX_BYTES);
			if(!strcmp(request->method,"GET"))							
			{
                
				if( request->host && request->path && (checkHTTPversion(request->version) == 1) )
				{
					bytes_send_client = handle_request(socket, request, tempReq);		// Handle GET request
					if(bytes_send_client == -1)
					{	
						sendErrorMessage(socket, 500);
					}

				}
				else
					sendErrorMessage(socket, 500);			// 500 Internal Error

			} else if(!strcmp(request->method,"POST")) {
    if( request->host && request->path && (checkHTTPversion(request->version) == 1) ){
        
        // Read the POST body if Content-Length is specified
        char *content_length_header = ParsedHeader_get(request, "Content-Length")->value;
        if (content_length_header) {
            int content_length = atoi(content_length_header);
            int total_received = len; // Already received bytes (headers)

            while (total_received < content_length) {
                int bytes_received = recv(socket, buffer + total_received, MAX_BYTES - total_received, 0);
                if (bytes_received <= 0) break;
                total_received += bytes_received;
            }
        }


        bytes_send_client = handle_post_request(socket, request, buffer, tempReq);

        if(bytes_send_client == -1){   
            sendErrorMessage(socket, 500);
        }
    }
    else
        sendErrorMessage(socket, 500); // 500 Internal Error
} else if (!strcmp(request->method, "PUT")) {
    bytes_send_client = handle_put_request(socket, request, buffer, tempReq);
    if (bytes_send_client == -1) {
        sendErrorMessage(socket, 500);
    }
} else if (!strcmp(request->method, "DELETE")) {
    bytes_send_client = handle_delete_request(socket, request, buffer, tempReq);
    if (bytes_send_client == -1) {
        sendErrorMessage(socket, 500);
    }
}else if(!strcmp(request->method,"CONNECT")) {
	// Do nothing
}
 else
            {
                printf("This code doesn't support this method\n");
            }
    
		}
        // Freeing up the request pointer
		ParsedRequest_destroy(request);

	}

	else if( bytes_send_client < 0)
	{
		perror("Error in receiving from client.\n");
	}
	else if(bytes_send_client == 0)
	{
		printf("Client disconnected!\n");
	}

	shutdown(socket, SHUT_RDWR);
	close(socket);
	free(buffer);
	sem_post(&seamaphore);	
	
	sem_getvalue(&seamaphore,&p);
	printf("Semaphore post value:%d\n",p);
	free(tempReq);
	return NULL;
}

int main(int argc, char * argv[]) {

	int client_socketId, client_len; // To store the client socket id
	struct sockaddr_in server_addr, client_addr; // Address of client and server to be assigned

    sem_init(&seamaphore,0,MAX_CLIENTS); // Initializing seamaphore and lock
    pthread_mutex_init(&lock,NULL); // Initializing lock for cache
    

	if(argc == 2)        // Two arguments are necessary
	{
		port_number = atoi(argv[1]);
	}
	else
	{
		printf("Too few arguments\n");
		exit(1);
	}

	printf("Setting Proxy Server Port : %d\n",port_number);

    // Creating the Proxy Socket
	proxy_socketId = socket(AF_INET, SOCK_STREAM, 0);

	if( proxy_socketId < 0)
	{
		perror("Failed to create a Socket.\n");
		exit(1);
	}

	int reuse =1;
	if (setsockopt(proxy_socketId, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) < 0) 
        perror("setsockopt(SO_REUSEADDR) failed\n");

	bzero((char*)&server_addr, sizeof(server_addr));  
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port_number); // Assigning port to the Proxy
	server_addr.sin_addr.s_addr = INADDR_ANY; // Any available address assigned

    // Binding the socket
	if( bind(proxy_socketId, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0 )
	{
		perror("Port is not free\n");
		exit(1);
	}
	printf("Binding on port: %d\n",port_number);

    // Proxy Socket listening to the requests
	int listen_status = listen(proxy_socketId, MAX_CLIENTS);

	if(listen_status < 0 )
	{
		perror("Error while Listening !\n");
		exit(1);
	}

	int i = 0; // Iterator for thread_id (tid) and Accepted Client_Socket for each thread
	int Connected_socketId[MAX_CLIENTS];   // This array stores socket descriptors of connected clients

    // Infinite Loop for accepting connections
	while(1)
	{
		
		bzero((char*)&client_addr, sizeof(client_addr));			// Clears struct client_addr
		client_len = sizeof(client_addr); 

        // Accepting the connections
		client_socketId = accept(proxy_socketId, (struct sockaddr*)&client_addr,(socklen_t*)&client_len);	// Accepts connection
		if(client_socketId < 0)
		{
			fprintf(stderr, "Error in Accepting connection !\n");
			exit(1);
		}
		else{
			Connected_socketId[i] = client_socketId; // Storing accepted client into array
		}

		// Getting IP address and port number of client
		struct sockaddr_in* client_pt = (struct sockaddr_in*)&client_addr;
		struct in_addr ip_addr = client_pt->sin_addr;
		char str[INET_ADDRSTRLEN];										// INET_ADDRSTRLEN: Default ip address size
		inet_ntop( AF_INET, &ip_addr, str, INET_ADDRSTRLEN );
		printf("Client is connected with port number: %d and ip address: %s \n",ntohs(client_addr.sin_port), str);
		pthread_create(&tid[i],NULL,thread_fn, (void*)&Connected_socketId[i]); // Creating a thread for each client accepted
		i++; 
	}
	close(proxy_socketId);									// Close socket
 	return 0;
}

cache_element* find(char* url){
	// Checks for url in the cache if found returns pointer to the respective cache element or else returns NULL
    cache_element* site=NULL;
	//sem_wait(&cache_lock);
    int temp_lock_val = pthread_mutex_lock(&lock);
	printf("Remove Cache Lock Acquired %d\n",temp_lock_val); 
    if(head!=NULL){
        site = head;
        while (site!=NULL)
        {
            if(!strcmp(site->url,url)){
				printf("LRU Time Track Before : %ld", site->lru_time_track);
                printf("\nurl found\n");
				// Updating the time_track
				site->lru_time_track = time(NULL);
				printf("LRU Time Track After : %ld", site->lru_time_track);
				break;
            }
            site=site->next;
        }       
    }
	else {
    printf("\nURL not found\n");
	}

    temp_lock_val = pthread_mutex_unlock(&lock);
	printf("Remove Cache Lock Unlocked %d\n",temp_lock_val); 
    return site;
}

// It’s functionality is very similar to how we delete an element from a Linked List.
void remove_cache_element(){
    // If cache is not empty searches for the node which has the least lru_time_track and deletes it
    cache_element * p ;  	// Cache_element Pointer (Prev. Pointer)
	cache_element * q ;		// Cache_element Pointer (Next Pointer)
	cache_element * temp;	// Cache element to remove
    //sem_wait(&cache_lock);
    int temp_lock_val = pthread_mutex_lock(&lock);
	printf("Remove Cache Lock Acquired %d\n",temp_lock_val); 
	if( head != NULL) { // Cache != empty
		for (q = head, p = head, temp =head ; q -> next != NULL; 
			q = q -> next) { // Iterate through entire cache and search for oldest time track
			if(( (q -> next) -> lru_time_track) < (temp -> lru_time_track)) {
				temp = q -> next;
				p = q;
			}
		}
		if(temp == head) { 
			head = head -> next; /*Handle the base case*/
		} else {
			p->next = temp->next;	
		}
		cache_size = cache_size - (temp -> len) - sizeof(cache_element) - 
		strlen(temp -> url) - 1;     //updating the cache size
		free(temp->data);     		
		free(temp->url); // Free the removed element 
		free(temp);
	} 
	//sem_post(&cache_lock);
    temp_lock_val = pthread_mutex_unlock(&lock);
	printf("Remove Cache Lock Unlocked %d\n",temp_lock_val); 
}

int add_cache_element(char* data,int size,char* url){
	// sem_wait(&cache_lock);
    int temp_lock_val = pthread_mutex_lock(&lock);
	printf("Add Cache Lock Acquired %d\n", temp_lock_val);
    int element_size=size+1+strlen(url)+sizeof(cache_element); // Size of the new element which will be added to the cache
    if(element_size>MAX_ELEMENT_SIZE){
		//sem_post(&cache_lock);
        // If element size is greater than MAX_ELEMENT_SIZE we don't add the element to the cache
        temp_lock_val = pthread_mutex_unlock(&lock);
		printf("Add Cache Lock Unlocked %d\n", temp_lock_val);
		// free(data);
		// printf("--\n");
		// free(url);
        return 0;
    }
    else
    {   while(cache_size+element_size>MAX_SIZE){
            // We keep removing elements from cache until we get enough space to add the element
            remove_cache_element();
        }
        cache_element* element = (cache_element*) malloc(sizeof(cache_element)); // Allocating memory for the new cache element
        element->data= (char*)malloc(size+1); // Allocating memory for the response to be stored in the cache element
		strcpy(element->data,data); 
        element -> url = (char*)malloc(1+( strlen( url )*sizeof(char)  )); // Allocating memory for the request to be stored in the cache element (as a key)
		strcpy( element -> url, url );
		element->lru_time_track=time(NULL);    // Updating the time_track
        element->next=head; 
        element->len=size;
        head=element;
        cache_size+=element_size;
        temp_lock_val = pthread_mutex_unlock(&lock);
		printf("Add Cache Lock Unlocked %d\n", temp_lock_val);
		//sem_post(&cache_lock);
		// free(data);
		// printf("--\n");
		// free(url);
        return 1;
    }
    return 0;
}
