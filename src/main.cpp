#include <iostream>
#include <string>
#include <sstream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <signal.h>
#include <ctime>
using namespace std;

class TextStorageServer {
private:
    int server_fd;
    string stored_data;
    bool running;
    string get_timestamp() {
        time_t now = time(0);
        char* timestr = ctime(&now);
        string result(timestr);
        result.pop_back();
        return result;
    }
    
    void send_response(int client_fd, const string& status, const string& content_type, const string& body) {
        ostringstream response;
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
        cout << "[" << get_timestamp() << "] GET request - Returned data (" << stored_data.length() << " bytes)" << endl;
    }
    
    string read_full_request(int client_fd) {
        string request;
        char buffer[8192];
        int content_length = 0;
        bool headers_complete = false;
        size_t headers_end_pos = 0;
        
        while (true) {
            int bytes_read = recv(client_fd, buffer, sizeof(buffer), 0);
            if (bytes_read <= 0) {
                break;
            }
            
            request.append(buffer, bytes_read);
            
            if (!headers_complete) {
                headers_end_pos = request.find("\r\n\r\n");
                if (headers_end_pos != std::string::npos) {
                    headers_complete = true;
                    headers_end_pos += 4; 
                    
                    size_t content_length_pos = request.find("Content-Length:");
                    if (content_length_pos != std::string::npos && content_length_pos < headers_end_pos) {
                        size_t start = content_length_pos + 15;
                        size_t end = request.find("\r\n", start);
                        if (end != std::string::npos && end < headers_end_pos) {
                            std::string length_str = request.substr(start, end - start);
                            // Trim whitespace
                            length_str.erase(0, length_str.find_first_not_of(" \t"));
                            length_str.erase(length_str.find_last_not_of(" \t") + 1);
                            try {
                                content_length = std::stoi(length_str);
                            } catch (const std::exception& e) {
                                content_length = 0;
                            }
                        }
                    }
                }
            }
            
            if (headers_complete) {
                int current_body_length = request.length() - headers_end_pos;
                if (content_length == 0 || current_body_length >= content_length) {
                    break;
                }
            }
            
            if (request.length() > 100 * 1024 * 1024) {
                cerr << "[" << get_timestamp() << "] Request too large, terminating read" << endl;
                break;
            }
        }
        
        return request;
    }
    
    void handle_post(int client_fd, const std::string& request) {
        size_t content_length_pos = request.find("Content-Length:");
        int content_length = 0;
        
        if (content_length_pos != std::string::npos) {
            size_t start = content_length_pos + 15; 
            size_t end = request.find("\r\n", start);
            if (end != string::npos) {
                string length_str = request.substr(start, end - start);
                // Trim whitespace
                length_str.erase(0, length_str.find_first_not_of(" \t"));
                length_str.erase(length_str.find_last_not_of(" \t") + 1);
                try {
                    content_length = stoi(length_str);
                } catch (const exception& e) {
                    content_length = 0;
                }
            }
        }
        
        size_t body_start = request.find("\r\n\r\n");
        if (body_start != string::npos && content_length > 0) {
            body_start += 4; // Skip the "\r\n\r\n"
            stored_data = request.substr(body_start, content_length);
        } else if (body_start != std::string::npos) {
            body_start += 4;
            stored_data = request.substr(body_start);
        } else {
            stored_data = "";
        }
        
        string response_body = "Data stored successfully (" + to_string(stored_data.length()) + " bytes)";
        send_response(client_fd, "200 OK", "text/plain", response_body);
        cout << "[" << get_timestamp() << "] POST request - Stored data (" << stored_data.length() << " bytes)" << endl;
    }
    
    void handle_options(int client_fd) {
        ostringstream response;
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
            cerr << "Failed to create socket" << endl;
            return;
        }
        
        // Set socket options
        int opt = 1;
        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
            cerr << "Failed to set socket options" << endl;
            close(server_fd);
            return;
        }
        
        // Set receive buffer size to maximum
        int recv_buffer_size = 64 * 1024 * 1024; // 64MB
        if (setsockopt(server_fd, SOL_SOCKET, SO_RCVBUF, &recv_buffer_size, sizeof(recv_buffer_size))) {
            cerr << "Warning: Failed to set receive buffer size" << endl;
        }
        
        struct sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(9999);
        
        if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
            cerr << "Failed to bind to port 9999" << endl;
            close(server_fd);
            return;
        }
        
        // Start listening
        if (listen(server_fd, 10) < 0) {
            cerr << "Failed to listen on socket" << endl;
            close(server_fd);
            return;
        }
        
        cout << "Text Storage Server Started (Unlimited Buffer)" << endl;
        cout << "==================================================" << endl;
        cout << "Server running on port 9999" << endl;
        cout << "Access via: http://localhost:9999" << endl;
        cout << "POST text data to store it (unlimited size)" << endl;
        cout << "GET to retrieve stored data" << endl;
        cout << "Press Ctrl+C to stop the server" << endl;
        cout << "==================================================" << endl;
        
        running = true;
        
        while (running) {
            struct sockaddr_in client_address;
            socklen_t client_len = sizeof(client_address);
            
            int client_fd = accept(server_fd, (struct sockaddr*)&client_address, &client_len);
            if (client_fd < 0) {
                if (running) {
                    cerr << "Failed to accept connection" << endl;
                }
                continue;
            }
            
            // Set receive timeout for client socket
            struct timeval timeout;
            timeout.tv_sec = 30;  // 30 seconds timeout
            timeout.tv_usec = 0;
            setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            
            // Read the full request with unlimited buffer
            std::string request = read_full_request(client_fd);
            
            if (!request.empty()) {
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
        cout << "\nShutting down server..." << endl;
        cout << "Server stopped." << endl;
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
