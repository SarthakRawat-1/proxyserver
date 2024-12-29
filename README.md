
# Proxy Server with Cache 

## Overview

This project implements a **Proxy Server** with the following features:

-   **LRU Cache**: Enhances performance by caching frequently accessed resources.
-   **Multithreading Support**: Handles multiple client requests concurrently.
-   **HTTP Methods Support**: Implements support for `GET`, `POST`, `PUT`, and `DELETE` requests. `CONNECT` request is acknowledged and logs nothing to the output.
-   **Error Handling**: Returns appropriate HTTP error responses for invalid or unsupported requests.

## Features

1.  **LRU Cache**:
    -   Stores responses for `GET` requests to minimize network traffic.
    -   Implements an eviction policy based on Least Recently Used (LRU) strategy.
2.  **Multi-threading**:
    -   Uses pthreads to handle multiple client requests simultaneously.
3.  **HTTP Methods**:
    -   Supports HTTP `GET`, `POST`, `PUT`, and `DELETE` methods.
    -   Sends requests to remote servers and relays responses to clients.
4.  **Error Responses**:
    -   Returns HTTP status codes like 400, 403, 404, 500, and 501 for different errors.
5.  **Port Configurability**:
    -   Allows configuration of the proxy server port via command-line arguments.

## Setup Instructions

1.  **Compilation**:
    
    ```bash
    make
    ```
    
2.  **Execution**:
    
    ```bash
    ./proxy <port_number>
    ```
    
    Example:
    
    ```bash
    ./proxy 8080
    ```
    
3.  **Browser Configuration**:
    -   Open the browser in **Incognito mode**.
    -   Navigate to **Settings -> Network Settings -> Manual Proxy Configuration**.
    -   Set HTTP Proxy: `localhost` and Port: `8080`.
    -   Check the box for "Use this proxy server for all protocols."

## Testing Endpoints Examples

1.  **GET Request**:
    
    ```
    http://httpbin.org/get
    ```
    
2.  **POST Request**:
    
    ```
    http://httpbin.org/forms/post
    ```
    
3.  **PUT Request**:
    
    ```
    http://httpbin.org/put
    ```
    
4.  **DELETE Request**:
    
    ```
    https://jsonplaceholder.typicode.com/posts/1
    ```
    

## Code Overview

1.  **Main Components**:
    -   `cache_element`: Structure for storing cache entries.
    -   `find()`: Searches for an item in the cache.
    -   `add_cache_element()`: Adds a new item to the cache.
    -   `remove_cache_element()`: Removes the least recently used item from the cache.
2.  **Thread Handling**:
    -   Uses `pthread_create` to create separate threads for each client connection.
3.  **Error Handling**:
    -   `sendErrorMessage()`: Sends HTTP error messages based on status codes.

## Notes

-   The cache size is defined as `MAX_SIZE = 200MB`.
-   Each cache element can store up to `10MB` of data.
-   The server uses port 8080 by default but can be customized.

## Dependencies

-   GCC compiler
-   POSIX Threads (pthreads)

## License

This project is licensed under the MIT License.

