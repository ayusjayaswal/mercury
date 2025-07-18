#include <iostream>
#include <string>
#include <sstream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <signal.h>
#include <ctime>

class TextStorageServer {
private:
    int server_fd;
    std::string stored_data;
    bool running;
    
    std::string get_timestamp() {
        time_t now = time(0);
        char* timestr = ctime(&now);
        std::string result(timestr);
        result.pop_back(); // Remove newline
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
        
        send(client_fd, response.str().c_str(), response.str().length(), 0);
    }
    
    void handle_get(int client_fd) {
        send_response(client_fd, "200 OK", "text/plain", stored_data);
        std::cout << "[" << get_timestamp() << "] GET request - Returned: " << stored_data << std::endl;
    }
    
    void handle_post(int client_fd, const std::string& request) {
        // Find Content-Length header
        size_t content_length_pos = request.find("Content-Length:");
        int content_length = 0;
        
        if (content_length_pos != std::string::npos) {
            size_t start = content_length_pos + 15; // Skip "Content-Length:"
            size_t end = request.find("\r\n", start);
            if (end != std::string::npos) {
                std::string length_str = request.substr(start, end - start);
                // Trim whitespace
                length_str.erase(0, length_str.find_first_not_of(" \t"));
                length_str.erase(length_str.find_last_not_of(" \t") + 1);
                content_length = std::stoi(length_str);
            }
        }
        
        // Find the body (after double CRLF)
        size_t body_start = request.find("\r\n\r\n");
        if (body_start != std::string::npos && content_length > 0) {
            body_start += 4; // Skip the "\r\n\r\n"
            stored_data = request.substr(body_start, content_length);
        } else {
            stored_data = "";
        }
        
        std::string response_body = "Data stored successfully: " + stored_data;
        send_response(client_fd, "200 OK", "text/plain", response_body);
        std::cout << "[" << get_timestamp() << "] POST request - Stored: " << stored_data << std::endl;
    }
    
    void handle_options(int client_fd) {
        std::ostringstream response;
        response << "HTTP/1.1 200 OK\r\n";
        response << "Access-Control-Allow-Origin: *\r\n";
        response << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
        response << "Access-Control-Allow-Headers: Content-Type\r\n";
        response << "Content-Length: 0\r\n";
        response << "\r\n";
        
        send(client_fd, response.str().c_str(), response.str().length(), 0);
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
        
        // Set socket options
        int opt = 1;
        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
            std::cerr << "Failed to set socket options" << std::endl;
            close(server_fd);
            return;
        }
        
        struct sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(9999);
        
        if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
            std::cerr << "Failed to bind to port 9999" << std::endl;
            close(server_fd);
            return;
        }
        
        // Start listening
        if (listen(server_fd, 3) < 0) {
            std::cerr << "Failed to listen on socket" << std::endl;
            close(server_fd);
            return;
        }
        
        std::cout << "Text Storage Server Started" << std::endl;
        std::cout << "==================================================" << std::endl;
        std::cout << "Server running on port 9999" << std::endl;
        std::cout << "Access via: http://localhost:9999" << std::endl;
        std::cout << "POST text data to store it" << std::endl;
        std::cout << "GET to retrieve stored data" << std::endl;
        std::cout << "Press Ctrl+C to stop the server" << std::endl;
        std::cout << "==================================================" << std::endl;
        
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
            
            // Read the request
            char buffer[4096] = {0};
            int bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
            
            if (bytes_read > 0) {
                std::string request(buffer, bytes_read);
                
                // Parse HTTP method
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
        std::cout << "\nShutting down server..." << std::endl;
        std::cout << "Server stopped." << std::endl;
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
