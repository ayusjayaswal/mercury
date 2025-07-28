#include <iostream>
#include <string>
#include <sstream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <signal.h>
#include <ctime>
#include <vector>
#include <algorithm>

class TextStorageServer {
private:
    int server_fd;
    std::string stored_data;
    bool running;
    
    std::string get_timestamp() {
        time_t now = time(0);
        char* timestr = ctime(&now);
        std::string result(timestr);
        result.pop_back();
        return result;
    }
    
    void send_response(int client_fd, const std::string& status, const std::string& content_type, const std::string& body) {
        std::ostringstream response;
        response << "HTTP/1.1 " << status << "\r\n";
        response << "Content-Type: " << content_type << "\r\n";
        response << "Access-Control-Allow-Origin: *\r\n";
        response << "Content-Length: " << body.length() << "\r\n";
        response << "\r\n";
        response << body;
        
        std::string response_str = response.str();
        size_t total_sent = 0;
        size_t response_size = response_str.length();
        
        // Send response in chunks to handle large data
        while (total_sent < response_size) {
            ssize_t sent = send(client_fd, response_str.c_str() + total_sent, 
                              response_size - total_sent, 0);
            if (sent <= 0) break;
            total_sent += sent;
        }
    }
    
    std::string read_complete_request(int client_fd) {
        std::string request;
        std::vector<char> buffer(65536); // 64KB buffer
        
        // Read headers first
        std::string headers;
        while (true) {
            ssize_t bytes_read = recv(client_fd, buffer.data(), buffer.size(), 0);
            if (bytes_read <= 0) break;
            
            headers.append(buffer.data(), bytes_read);
            size_t header_end = headers.find("\r\n\r\n");
            if (header_end != std::string::npos) {
                request = headers;
                break;
            }
        }
        
        if (request.empty()) return request;
        
        // Check if this is a POST request
        if (request.substr(0, 4) != "POST") {
            return request; // GET/OPTIONS don't need body
        }
        
        // Extract Content-Length
        size_t content_length = 0;
        size_t cl_pos = request.find("Content-Length:");
        if (cl_pos != std::string::npos) {
            size_t start = cl_pos + 15;
            size_t end = request.find("\r\n", start);
            if (end != std::string::npos) {
                std::string length_str = request.substr(start, end - start);
                // Trim whitespace
                size_t first = length_str.find_first_not_of(" \t");
                size_t last = length_str.find_last_not_of(" \t");
                if (first != std::string::npos) {
                    length_str = length_str.substr(first, last - first + 1);
                    try {
                        content_length = std::stoull(length_str);
                    } catch (...) {
                        content_length = 0;
                    }
                }
            }
        }
        
        // Read the body if Content-Length is specified
        if (content_length > 0) {
            size_t header_end = request.find("\r\n\r\n") + 4;
            size_t body_already_read = request.length() - header_end;
            size_t remaining = content_length - body_already_read;
            
            // Reserve space for efficiency
            request.reserve(header_end + content_length);
            
            // Read remaining body data
            while (remaining > 0) {
                size_t to_read = std::min(remaining, buffer.size());
                ssize_t bytes_read = recv(client_fd, buffer.data(), to_read, 0);
                if (bytes_read <= 0) break;
                
                request.append(buffer.data(), bytes_read);
                remaining -= bytes_read;
            }
        }
        
        return request;
    }
    
    void handle_get(int client_fd) {
        send_response(client_fd, "200 OK", "text/plain; charset=utf-8", stored_data);
        std::cout << "[" << get_timestamp() << "] GET - Returned " 
                  << stored_data.length() << " characters" << std::endl;
    }
    
    void handle_post(int client_fd, const std::string& request) {
        // Find the start of the body
        size_t body_start = request.find("\r\n\r\n");
        if (body_start != std::string::npos) {
            body_start += 4; // Skip "\r\n\r\n"
            
            // Extract Content-Length to get exact body size
            size_t content_length = 0;
            size_t cl_pos = request.find("Content-Length:");
            if (cl_pos != std::string::npos) {
                size_t start = cl_pos + 15;
                size_t end = request.find("\r\n", start);
                if (end != std::string::npos) {
                    std::string length_str = request.substr(start, end - start);
                    size_t first = length_str.find_first_not_of(" \t");
                    size_t last = length_str.find_last_not_of(" \t");
                    if (first != std::string::npos) {
                        length_str = length_str.substr(first, last - first + 1);
                        try {
                            content_length = std::stoull(length_str);
                        } catch (...) {
                            content_length = 0;
                        }
                    }
                }
            }
            
            if (content_length > 0) {
                stored_data = request.substr(body_start, content_length);
            } else {
                // Fallback: take everything after headers
                stored_data = request.substr(body_start);
            }
        } else {
            stored_data = "";
        }
        
        std::string response_body = "OK";
        send_response(client_fd, "200 OK", "text/plain", response_body);
        
        std::cout << "[" << get_timestamp() << "] POST - Stored " 
                  << stored_data.length() << " characters" << std::endl;
    }
    
    void handle_options(int client_fd) {
        std::ostringstream response;
        response << "HTTP/1.1 200 OK\r\n";
        response << "Access-Control-Allow-Origin: *\r\n";
        response << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
        response << "Access-Control-Allow-Headers: Content-Type\r\n";
        response << "Content-Length: 0\r\n";
        response << "\r\n";
        
        std::string response_str = response.str();
        send(client_fd, response_str.c_str(), response_str.length(), 0);
    }
    
public:
    TextStorageServer() : server_fd(-1), running(false) {}
    
    void start() {
        // Create socket
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd == -1) {
            std::cerr << "Failed to create socket" << std::endl;
            return;
        }
        
        // Set socket options for large data handling
        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
        // Increase buffer sizes for large data
        int buffer_size = 10 * 1024 * 1024; // 10MB
        setsockopt(server_fd, SOL_SOCKET, SO_RCVBUF, &buffer_size, sizeof(buffer_size));
        setsockopt(server_fd, SOL_SOCKET, SO_SNDBUF, &buffer_size, sizeof(buffer_size));
        
        struct sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(9999);
        
        if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
            std::cerr << "Failed to bind to port 9999" << std::endl;
            close(server_fd);
            return;
        }
        
        if (listen(server_fd, 3) < 0) {
            std::cerr << "Failed to listen on socket" << std::endl;
            close(server_fd);
            return;
        }
        
        std::cout << "Unlimited Text Storage Server" << std::endl;
        std::cout << "=====================================" << std::endl;
        std::cout << "Server running on port 9999" << std::endl;
        std::cout << "POST: Store unlimited size text data" << std::endl;
        std::cout << "GET:  Retrieve stored data as-is" << std::endl;
        std::cout << "Press Ctrl+C to stop" << std::endl;
        std::cout << "=====================================" << std::endl;
        
        running = true;
        
        while (running) {
            struct sockaddr_in client_address;
            socklen_t client_len = sizeof(client_address);
            
            int client_fd = accept(server_fd, (struct sockaddr*)&client_address, &client_len);
            if (client_fd < 0) {
                if (running) {
                    std::cerr << "Failed to accept connection" << std::endl;
                }
                continue;
            }
            
            // Set generous timeouts for large data transfers
            struct timeval timeout;
            timeout.tv_sec = 300;  // 5 minutes
            timeout.tv_usec = 0;
            setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
            
            // Read the complete request
            std::string request = read_complete_request(client_fd);
            
            if (!request.empty()) {
                if (request.substr(0, 3) == "GET") {
                    handle_get(client_fd);
                } else if (request.substr(0, 4) == "POST") {
                    handle_post(client_fd, request);
                } else if (request.substr(0, 7) == "OPTIONS") {
                    handle_options(client_fd);
                } else {
                    send_response(client_fd, "405 Method Not Allowed", "text/plain", "Method not allowed");
                }
            }
            
            close(client_fd);
        }
    }
    
    void stop() {
        running = false;
        if (server_fd != -1) {
            close(server_fd);
            server_fd = -1;
        }
        std::cout << "\nServer stopped. Final data size: " 
                  << stored_data.length() << " characters" << std::endl;
    }
};

// Global server instance for signal handler
TextStorageServer* global_server = nullptr;

void signal_handler(int) {
    if (global_server) {
        global_server->stop();
    }
}

int main() {
    TextStorageServer server;
    global_server = &server;
    
    // Set up signal handler for Ctrl+C
    signal(SIGINT, signal_handler);
    
    server.start();
    
    return 0;
}
