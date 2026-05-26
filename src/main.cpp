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
    string stored_content_type;
    bool running;

    string get_timestamp() {
        time_t now = time(0);
        char* timestr = ctime(&now);
        string result(timestr);
        result.pop_back();
        return result;
    }

    string detect_content_type(const string& data) {
        if (data.size() >= 4) {
            const unsigned char* b = reinterpret_cast<const unsigned char*>(data.data());
            if (b[0] == 0xFF && b[1] == 0xD8 && b[2] == 0xFF)
                return "image/jpeg";
            if (b[0] == 0x89 && b[1] == 0x50 && b[2] == 0x4E && b[3] == 0x47)
                return "image/png";
            if (b[0] == 0x47 && b[1] == 0x49 && b[2] == 0x46 && b[3] == 0x38)
                return "image/gif";
            if (b[0] == 0x52 && b[1] == 0x49 && b[2] == 0x46 && b[3] == 0x46)
                return "image/webp";
        }
        return "text/plain";
    }

    void send_response(int client_fd, const string& status, const string& content_type, const string& body) {
        ostringstream headers;
        headers << "HTTP/1.1 " << status << "\r\n";
        headers << "Content-Type: " << content_type << "\r\n";
        headers << "Access-Control-Allow-Origin: *\r\n";
        headers << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
        headers << "Access-Control-Allow-Headers: Content-Type\r\n";
        headers << "Content-Length: " << body.size() << "\r\n";
        headers << "\r\n";
        string header_str = headers.str();
        send(client_fd, header_str.c_str(), header_str.size(), 0);
        send(client_fd, body.data(), body.size(), 0);
    }

    string read_full_request(int client_fd) {
        string request;
        char buffer[8192];
        int content_length = 0;
        bool headers_complete = false;
        size_t headers_end_pos = 0;

        while (true) {
            int bytes_read = recv(client_fd, buffer, sizeof(buffer), 0);
            if (bytes_read <= 0) break;

            request.append(buffer, bytes_read);

            if (!headers_complete) {
                headers_end_pos = request.find("\r\n\r\n");
                if (headers_end_pos != string::npos) {
                    headers_complete = true;
                    headers_end_pos += 4;

                    size_t cl_pos = request.find("Content-Length:");
                    if (cl_pos != string::npos) {
                        size_t start = cl_pos + 15;
                        size_t end = request.find("\r\n", start);
                        if (end != string::npos) {
                            string len_str = request.substr(start, end - start);
                            len_str.erase(0, len_str.find_first_not_of(" \t"));
                            len_str.erase(len_str.find_last_not_of(" \t") + 1);
                            try { content_length = stoi(len_str); } catch (...) {}
                        }
                    }
                }
            }

            if (headers_complete) {
                int body_so_far = request.size() - headers_end_pos;
                if (content_length == 0 || body_so_far >= content_length) break;
            }

            if (request.size() > 100 * 1024 * 1024) {
                cerr << "[" << get_timestamp() << "] Request too large" << endl;
                break;
            }
        }

        return request;
    }

    void handle_get(int client_fd) {
        send_response(client_fd, "200 OK", stored_content_type, stored_data);
        cout << "[" << get_timestamp() << "] GET - " << stored_data.size()
             << " bytes (" << stored_content_type << ")" << endl;
    }

    void handle_post(int client_fd, const string& request) {
        int content_length = 0;
        size_t cl_pos = request.find("Content-Length:");
        if (cl_pos != string::npos) {
            size_t start = cl_pos + 15;
            size_t end = request.find("\r\n", start);
            if (end != string::npos) {
                string len_str = request.substr(start, end - start);
                len_str.erase(0, len_str.find_first_not_of(" \t"));
                len_str.erase(len_str.find_last_not_of(" \t") + 1);
                try { content_length = stoi(len_str); } catch (...) {}
            }
        }

        size_t body_start = request.find("\r\n\r\n");
        if (body_start != string::npos) {
            body_start += 4;
            stored_data = content_length > 0
                ? request.substr(body_start, content_length)
                : request.substr(body_start);
        } else {
            stored_data = "";
        }

        stored_content_type = detect_content_type(stored_data);

        string response_body = "Stored " + to_string(stored_data.size()) + " bytes (" + stored_content_type + ")";
        send_response(client_fd, "200 OK", "text/plain", response_body);
        cout << "[" << get_timestamp() << "] POST - stored " << stored_data.size()
             << " bytes (" << stored_content_type << ")" << endl;
    }

    void handle_options(int client_fd) {
        send_response(client_fd, "200 OK", "text/plain", "");
    }

public:
    TextStorageServer() : server_fd(-1), stored_content_type("text/plain"), running(false) {}

    void start() {
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd == -1) { cerr << "Failed to create socket" << endl; return; }

        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        int recv_buf = 64 * 1024 * 1024;
        setsockopt(server_fd, SOL_SOCKET, SO_RCVBUF, &recv_buf, sizeof(recv_buf));

        struct sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(9999);

        if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
            cerr << "Failed to bind to port 9999" << endl; close(server_fd); return;
        }
        if (listen(server_fd, 10) < 0) {
            cerr << "Failed to listen" << endl; close(server_fd); return;
        }

        cout << "Server running on port 9999" << endl;
        running = true;

        while (running) {
            struct sockaddr_in client_address{};
            socklen_t client_len = sizeof(client_address);
            int client_fd = accept(server_fd, (struct sockaddr*)&client_address, &client_len);
            if (client_fd < 0) { if (running) cerr << "Accept failed" << endl; continue; }

            struct timeval timeout{ 30, 0 };
            setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

            string request = read_full_request(client_fd);
            if (!request.empty()) {
                if      (request.substr(0, 3) == "GET")     handle_get(client_fd);
                else if (request.substr(0, 4) == "POST")    handle_post(client_fd, request);
                else if (request.substr(0, 7) == "OPTIONS") handle_options(client_fd);
                else send_response(client_fd, "405 Method Not Allowed", "text/plain", "Method not allowed");
            }

            close(client_fd);
        }
    }

    void stop() {
        running = false;
        if (server_fd != -1) { close(server_fd); server_fd = -1; }
        cout << "Server stopped." << endl;
    }
};

TextStorageServer* global_server = nullptr;
void signal_handler(int) { if (global_server) global_server->stop(); }

int main() {
    TextStorageServer server;
    global_server = &server;
    signal(SIGINT, signal_handler);
    server.start();
    return 0;
}
