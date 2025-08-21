/*RUN: 
g++ seed.cpp -o seed -lpthread
./seed      # Terminal 1*/
//to do: file download

#include <iostream>
#include <fstream>
#include <algorithm>
#include <vector>
#include <string>
#include <cstring>
#include <map>
#include <dirent.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <sstream>

#define START_PORT 9005
#define END_PORT 9009

std::string base_seed_path = "/mnt/c/Users/USER/Downloads/8.18/8.18/files";
//std::string base_seed_path = "/data1/p4work/awidvb/Exercise/ProjDraft/files";
std::map<int, std::string> port_to_seed = {
    {9005, "seed1"},
    {9006, "seed2"}, 
    {9007, "seed3"},
    {9008, "seed4"},
    {9009, "seed5"}
};

std::map<std::string, std::string> download_status;
std::map<std::string, bool> active_downloads;

int find_available_port(int& server_fd, sockaddr_in& address) {
    int opt = 1;
    for (int port = START_PORT; port <= END_PORT; ++port) {
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) continue;

        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            close(server_fd);
            continue;
        }

        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);

        if (bind(server_fd, (sockaddr*)&address, sizeof(address)) == 0) {
            return port;
        }

        close(server_fd);
    }
    return -1;
}

bool is_port_active(int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(port);
    
    bool active = (connect(sock, (sockaddr*)&addr, sizeof(addr)) == 0);
    close(sock);
    return active;
}

std::vector<int> get_active_ports(int current_port) {
    std::vector<int> active_ports;
    for (const auto& port_seed_pair : port_to_seed) {
        int port = port_seed_pair.first;
        if (port != current_port && is_port_active(port)) {
            active_ports.push_back(port);
        }
    }
    return active_ports;
}

// RECURSIVE LIST FILES - Fixed for nested structure
void list_files_recursive(const std::string& path, std::vector<std::string>& files, const std::string& seed_info = "", const std::string& key_id = "") {
    DIR* dir = opendir(path.c_str()); 
    if (!dir) {
        return;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {  
        std::string name = entry->d_name;
        if (name == "." || name == "..") continue;

        std::string full_path = path + "/" + name; 

        struct stat st;
        if (stat(full_path.c_str(), &st) == -1) continue;

        if (S_ISDIR(st.st_mode)) {
            // directory 
            std::string current_key_id = name; // Use the folder name as Key ID
            std::string enhanced_seed_info = "["+ current_key_id + "] " + seed_info; 
            list_files_recursive(full_path, files, enhanced_seed_info, current_key_id);
        } else if (S_ISREG(st.st_mode)) {
            //  file
            std::string final_seed_info = seed_info;
            if (!key_id.empty()) {
                final_seed_info += " [Key ID: " + key_id + "]";
            }
            std::string file_with_info = full_path + "|" + final_seed_info;
            files.push_back(file_with_info);
        }
    }
    closedir(dir);
}

std::vector<std::string> get_local_files(int current_port) {
    std::vector<std::string> files;
    
    std::cout << "[DEBUG] get_local_files called for port " << current_port << std::endl;
    
    auto it = port_to_seed.find(current_port);
    if (it == port_to_seed.end()) {
        std::cout << "[DEBUG] Port " << current_port << " not found in port_to_seed map" << std::endl;
        return files;
    }
    
    std::string seed_folder = it->second;
    std::string full_seed_path = base_seed_path + "/" + seed_folder;
    
    std::cout << "[DEBUG] base_seed_path: " << base_seed_path << std::endl;
    std::cout << "[DEBUG] seed_folder: " << seed_folder << std::endl;
    std::cout << "[DEBUG] full_seed_path: " << full_seed_path << std::endl;
    
    // Check if seed folder exists
    DIR* test_dir = opendir(full_seed_path.c_str());
    if (!test_dir) {
        std::cout << "[DEBUG] Failed to open directory: " << full_seed_path << " (errno: " << errno << ")" << std::endl;
        return files;
    }
    closedir(test_dir);
    
    std::cout << "[DEBUG] Directory exists, scanning for files..." << std::endl;
    
    std::string seed_info = "Port " + std::to_string(current_port);
    list_files_recursive(full_seed_path, files, seed_info, "");
    
    std::cout << "[DEBUG] Found " << files.size() << " files in " << full_seed_path << std::endl;
    for (const auto& file : files) {
        std::cout << "[DEBUG] File: " << file << std::endl;
    }
    
    return files;
}

std::vector<std::string> request_files_from_port(int port) {
    std::vector<std::string> files;
    
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return files;
    }
    
    struct timeval timeout;
    timeout.tv_sec = 2;  // 2 second timeout
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(port);
    
    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        return files;
    }
    
    std::string request = "LIST_FILES";
    if (send(sock, request.c_str(), request.size(), 0) < 0) {
        close(sock);
        return files;
    }
    
    std::string response;
    char buffer[1024];
    int bytes_received;
    
    while ((bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytes_received] = '\0';
        response += std::string(buffer);
    }
    
    close(sock);
    
    if (!response.empty()) {
        std::istringstream iss(response);
        std::string line;
        while (std::getline(iss, line)) {
            if (!line.empty() && line != "END_LIST") {
                files.push_back(line);
            }
        }
    }
    
    return files;
}

bool send_all(int sock, const char* buffer, size_t length) {
    size_t total_sent = 0;
    while (total_sent < length) {
        ssize_t sent = send(sock, buffer + total_sent, length - total_sent, 0);
        if (sent <= 0) return false; //error or connection closed
        total_sent += sent;
    }
    return true;
}

bool recv_all(int sock, char* buffer, size_t length) {
    size_t total_received = 0;
    while (total_received < length) {
        ssize_t bytes = recv(sock, buffer + total_received, length - total_received, 0);
        if (bytes <= 0) return false;
        total_received += bytes;
    }
    return true;
}

void handle_client(int client_socket, int current_port) {
    char buffer[1024];
    int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received <= 0) {
        close(client_socket);
        return;
    }
    
    buffer[bytes_received] = '\0';
    std::string request(buffer);
    
    if (request == "LIST_FILES") {
        std::vector<std::string> local_files = get_local_files(current_port);
        
        std::string response;
        for (const auto& file_entry : local_files) {
            response += file_entry + "\n";
        }
        response += "END_LIST\n";
        
        send_all(client_socket, response.c_str(), response.size());
        close(client_socket);
        return;
    }
    
    std::vector<std::string> available_files;
    std::vector<int> active_ports = get_active_ports(current_port);
    for (int active_port : active_ports) {
        std::vector<std::string> port_files = request_files_from_port(active_port);
        available_files.insert(available_files.end(), port_files.begin(), port_files.end());
    }
    
    std::string file_list_msg = "Files available:\n";
    for (size_t i = 0; i < available_files.size(); ++i) {
        std::string file_entry = available_files[i];
        size_t separator = file_entry.find("|");
        std::string filepath = file_entry.substr(0, separator);
        std::string info = (separator != std::string::npos) ? file_entry.substr(separator + 1) : "";
        
        std::string display_name = filepath;
        size_t last_slash = filepath.find_last_of("/");
        if (last_slash != std::string::npos) {
            display_name = filepath.substr(last_slash + 1);
        }
        
        file_list_msg += "[" + std::to_string(i + 1) + "] " + display_name + " (" + info + ")\n";
    }
    send(client_socket, file_list_msg.c_str(), file_list_msg.size(), 0);

    int file_id;
    recv(client_socket, &file_id, sizeof(file_id), 0);

    if (file_id < 1 || file_id > available_files.size()) {
        std::string msg = "Invalid file ID.\n";
        send(client_socket, msg.c_str(), msg.size(), 0); 
        return;
    }

    std::string file_entry = available_files[file_id - 1];
    size_t separator = file_entry.find("|");
    std::string filepath = file_entry.substr(0, separator);
    std::string filename = filepath;
    size_t last_slash = filepath.find_last_of("/");
    if (last_slash != std::string::npos) {
        filename = filepath.substr(last_slash + 1);
    }

    char exists_flag;
    recv(client_socket, &exists_flag, sizeof(exists_flag), 0);
    if (exists_flag == '1') {
        std::string msg = "File " + filename + " already exists\n";
        send(client_socket, msg.c_str(), msg.size(), 0);
        return;
    }

    if (active_downloads[filename]) {
        std::string msg = "Download for File: " + filename + " is already started\n";
        send(client_socket, msg.c_str(), msg.size(), 0);
        return;
    }

    active_downloads[filename] = true; 

    std::string seeder_msg = "Enter file ID:\nLocating seeders... Found 2 seeders\nDownload started. File: " + filename + "\n";
    send(client_socket, seeder_msg.c_str(), seeder_msg.size(), 0);

    std::ifstream infile(filepath, std::ios::binary); 
    if (!infile) {
        std::string msg = "File not found.\n";
        send(client_socket, msg.c_str(), msg.size(), 0);
        download_status[filename] = "Failed: File not found";
        active_downloads[filename] = false;
        return;
    }

    infile.seekg(0, std::ios::end);
    size_t filesize = infile.tellg(); 
    infile.seekg(0, std::ios::beg);

    send_all(client_socket, reinterpret_cast<char*>(&filesize), sizeof(filesize)); 

    char file_buffer[1024];
    size_t total_sent = 0;
    while (infile.read(file_buffer, sizeof(file_buffer))) {
        if (!send_all(client_socket, file_buffer, sizeof(file_buffer))) {
            download_status[filename] = "Failed during transfer";
            active_downloads[filename] = false;
            return;
        }
        total_sent += sizeof(file_buffer);
    }

    if (infile.gcount() > 0) {
        send_all(client_socket, file_buffer, infile.gcount()); 
        total_sent += infile.gcount();
    }

    infile.close();
    download_status[filename] = "Success (" + std::to_string(total_sent) + "/" + std::to_string(filesize) + " bytes)";
    active_downloads[filename] = false; 
}

void* server_thread(void* arg) {
    int server_fd = *(int*)arg;
    int current_port;
    socklen_t addrlen;
    sockaddr_in addr;

    // extract port number from server_fd
    socklen_t len = sizeof(addr);
    getsockname(server_fd, (sockaddr*)&addr, &len);
    current_port = ntohs(addr.sin_port);

    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_socket = accept(server_fd, (sockaddr*)&client_addr, &client_len);
        if (client_socket < 0) {
            continue;
        }
        handle_client(client_socket, current_port);
        close(client_socket);
    }
    return nullptr;
}


void show_menu() {
    std::cout << "\n==========================================================\n";
    std::cout << "\n>./seed\n";
    std::cout << "[1] List available files.\n";
    std::cout << "[2] Download file.\n";
    std::cout << "[3] Download status.\n";
    std::cout << "[4] Exit.\n";
    std::cout << "Choose an option: ";
}

int main() {
    int server_fd;
    sockaddr_in address{};
    int port = find_available_port(server_fd, address);

    if (port == -1) {
        std::cerr << "No available ports found.\n";
        return 1;
    }

    auto it = port_to_seed.find(port);
    if (it != port_to_seed.end()) {
        std::string seed_folder = it->second;
        std::string full_seed_path = base_seed_path + "/" + seed_folder;
        std::cout << "[DEBUG] full_seed_path for port " << port << ": " << full_seed_path << std::endl;
    }

    std::cout << "\n==========================================================";
    std::cout << "\nFinding available ports ... Found port " << port << "\n";
    std::cout << "Listening at port " << port << "\n";

    listen(server_fd, 5);
    pthread_t tid;
    pthread_create(&tid, nullptr, server_thread, &server_fd);


    int choice;
    while (true) {
        show_menu();
        std::cin >> choice;
        std::cin.ignore();

        if (choice == 1) {
            std::cout << "[DEBUG MAIN] === LISTING FILES DEBUG ===" << std::endl;
            std::cout << "[DEBUG MAIN] Current port: " << port << std::endl;
            std::cout << "[DEBUG MAIN] Base seed path: " << base_seed_path << std::endl;
            
            auto it = port_to_seed.find(port);
            if (it != port_to_seed.end()) {
                std::string seed_folder = it->second;
                std::string full_seed_path = base_seed_path + "/" + seed_folder;
                std::cout << "[DEBUG MAIN] Current seed folder: " << seed_folder << std::endl;
                std::cout << "[DEBUG MAIN] Full seed path: " << full_seed_path << std::endl;
                
                DIR* test_dir = opendir(full_seed_path.c_str());
                if (test_dir) {
                    std::cout << "[DEBUG MAIN] Directory exists and is accessible" << std::endl;
                    closedir(test_dir);
                } else {
                    std::cout << "[DEBUG MAIN] Directory does NOT exist or is not accessible (errno: " << errno << ")" << std::endl;
                }
            }
            
            std::cout << "[DEBUG MAIN] Checking local files for current port " << port << std::endl;
            std::vector<std::string> local_files = get_local_files(port);
            std::cout << "[DEBUG MAIN] Current port has " << local_files.size() << " local files:" << std::endl;
            for (const auto& file : local_files) {
                std::cout << "[DEBUG MAIN] Local file: " << file << std::endl;
            }
            
            std::cout << "[DEBUG MAIN] Getting active ports..." << std::endl;
            std::vector<int> active_ports = get_active_ports(port);
            std::cout << "[DEBUG MAIN] Found " << active_ports.size() << " active ports: ";
            for (int ap : active_ports) {
                std::cout << ap << " ";
            }
            std::cout << std::endl;
            
            if (active_ports.empty()) {
                std::cout << "\nNo other active ports found. Start more terminals to see available files.\n";
                continue;
            }
            
            std::cout << "\nSearching files from " << active_ports.size() << " active port(s) ... done.\n";
            std::vector<std::string> files;
            for (int active_port : active_ports) {
                std::cout << "[DEBUG MAIN] Requesting files from port " << active_port << std::endl;
                std::vector<std::string> port_files = request_files_from_port(active_port);
                std::cout << "[DEBUG MAIN] Received " << port_files.size() << " files from port " << active_port << std::endl;
                files.insert(files.end(), port_files.begin(), port_files.end());
            }
            
            std::cout << "[DEBUG MAIN] Total files collected: " << files.size() << std::endl;
            
            if (files.empty()) {
                std::cout << "No files found in other seed folders.\n";
            } else {
                std::cout << "Files available from other ports:\n";
                for (size_t i = 0; i < files.size(); ++i) {
                    std::string file_entry = files[i];
                    size_t separator = file_entry.find("|");
                    std::string filepath = file_entry.substr(0, separator);
                    std::string info = (separator != std::string::npos) ? file_entry.substr(separator + 1) : "";
                    
                    std::string filename = filepath;
                    size_t last_slash = filepath.find_last_of("/");
                    if (last_slash != std::string::npos) {
                        filename = filepath.substr(last_slash + 1);
                    }
                    std::cout << info << " " << filename << "\n";
                }
            }
        } else if (choice == 2) {
            std::cout << "Waiting for client to request file...\n";
            sockaddr_in client_addr{};
            socklen_t addrlen = sizeof(client_addr);
            int client_socket = accept(server_fd, (sockaddr*)&client_addr, &addrlen);
            if (client_socket < 0) {
                std::cerr << "Accept failed.\n";
                continue;
            }
            handle_client(client_socket, port);
            close(client_socket);
        } else if (choice == 3) {
            std::cout << "\nDownload status:\n";
            if (download_status.empty()) {
                std::cout << "No downloads yet.\n";
            } else {
                for (const auto& entry : download_status) {
                    std::cout << entry.first << ": " << entry.second << "\n";
                }
            }
        } else if (choice == 4) {
            std::cout << "Exiting...\n";
            break;
        } else {
            std::cout << "Invalid option. Try again.\n";
        }
    }

    close(server_fd);
    return 0;
}
