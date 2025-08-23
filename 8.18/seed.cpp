/*RUN: 
g++ seed.cpp -o seed -lpthread
./seed      # Terminal 1*/
//Enhanced with file sizes, chunk-based downloads, and simultaneous multi-port downloading

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
#include <unordered_set>
#include <thread>
#include <mutex>
#include <atomic>

#define START_PORT 9005
#define END_PORT 9009
const size_t CHUNK_SIZE = 32;  // Changed from #define to const size_t for type safety

std::string base_seed_path = "/mnt/c/Users/USER/Downloads/8.18/8.18/files";
std::map<int, std::string> port_to_seed = {
    {9005, "seed1"},
    {9006, "seed2"}, 
    {9007, "seed3"},
    {9008, "seed4"},
    {9009, "seed5"}
};

std::map<std::string, std::string> download_status;
std::map<std::string, bool> active_downloads;
std::mutex download_mutex; // Added mutex for thread-safe download operations

struct FileInfo {
    std::string filepath;
    std::string key_id;
    size_t file_size;
    std::string seed_info;
};

struct ChunkDownload {
    int chunk_id;
    int port;
    size_t start_offset;
    size_t chunk_size;
    std::vector<char> data;
    bool completed;
    
    ChunkDownload(int id, int p, size_t start, size_t size) 
        : chunk_id(id), port(p), start_offset(start), chunk_size(size), completed(false) {
        data.resize(size);
    }
};

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

void list_files_recursive(const std::string& path, std::vector<FileInfo>& files, const std::string& seed_info = "", const std::string& key_id = "") {
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
            // directory - use folder name as Key ID
            std::string current_key_id = name;
            std::string enhanced_seed_info = "[" + current_key_id + "] " + seed_info; 
            list_files_recursive(full_path, files, enhanced_seed_info, current_key_id);
        } else if (S_ISREG(st.st_mode)) {
            // regular file - create FileInfo with size
            FileInfo file_info;
            file_info.filepath = full_path;
            file_info.key_id = key_id;
            file_info.file_size = st.st_size;
            file_info.seed_info = seed_info;
            files.push_back(file_info);
        }
    }
    closedir(dir);
}

std::vector<FileInfo> get_local_files(int current_port) {
    std::vector<FileInfo> files;
    
    auto it = port_to_seed.find(current_port);
    if (it == port_to_seed.end()) {
        std::cout << "[DEBUG] Port " << current_port << " not found in port_to_seed map" << std::endl;
        return files;
    }
    
    std::string seed_folder = it->second;
    std::string full_seed_path = base_seed_path + "/" + seed_folder;
    
    DIR* test_dir = opendir(full_seed_path.c_str());
    if (!test_dir) {
        std::cout << "[DEBUG] Failed to open directory: " << full_seed_path << " (errno: " << errno << ")" << std::endl;
        return files;
    }
    closedir(test_dir);
    
    std::string seed_info;
    list_files_recursive(full_seed_path, files, seed_info, "");
    
    return files;
}

std::vector<FileInfo> request_files_from_port(int port) {
    std::vector<FileInfo> files;
    
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return files;
    }
    
    struct timeval timeout;
    timeout.tv_sec = 2;
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
                // Parse the enhanced file info: filepath|key_id|size|seed_info
                std::istringstream line_stream(line);
                std::string filepath, key_id, size_str, seed_info;
                
                if (std::getline(line_stream, filepath, '|') &&
                    std::getline(line_stream, key_id, '|') &&
                    std::getline(line_stream, size_str, '|') &&
                    std::getline(line_stream, seed_info)) {
                    
                    FileInfo file_info;
                    file_info.filepath = filepath;
                    file_info.key_id = key_id;
                    file_info.file_size = std::stoull(size_str);
                    file_info.seed_info = seed_info;
                    files.push_back(file_info);
                }
            }
        }
    }
    
    return files;
}

bool send_all(int sock, const char* buffer, size_t length) {
    size_t total_sent = 0;
    while (total_sent < length) {
        ssize_t sent = send(sock, buffer + total_sent, length - total_sent, 0);
        if (sent <= 0) return false;
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

bool download_chunk_from_port(int port, const std::string& filepath, ChunkDownload& chunk) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(port);
    
    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        return false;
    }
    
    // Send chunk request: "DOWNLOAD_CHUNK|filepath|start_offset|chunk_size"
    std::string request = "DOWNLOAD_CHUNK|" + filepath + "|" + 
                         std::to_string(chunk.start_offset) + "|" + 
                         std::to_string(chunk.chunk_size);
    
    if (send(sock, request.c_str(), request.size(), 0) < 0) {
        close(sock);
        return false;
    }
    
    // Receive chunk data
    if (!recv_all(sock, chunk.data.data(), chunk.chunk_size)) {
        close(sock);
        return false;
    }
    
    close(sock);
    chunk.completed = true;
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
        std::vector<FileInfo> local_files = get_local_files(current_port);
        
        std::string response;
        for (const auto& file_info : local_files) {
            // Send: filepath|key_id|size|seed_info
            response += file_info.filepath + "|" + file_info.key_id + "|" + 
                       std::to_string(file_info.file_size) + "|" + file_info.seed_info + "\n";
        }
        response += "END_LIST\n";
        
        send_all(client_socket, response.c_str(), response.size());
        close(client_socket);
        return;
    }
    
    if (request.substr(0, 14) == "DOWNLOAD_CHUNK") {
        std::istringstream iss(request);
        std::string command, filepath, start_str, size_str;
        
        if (std::getline(iss, command, '|') &&
            std::getline(iss, filepath, '|') &&
            std::getline(iss, start_str, '|') &&
            std::getline(iss, size_str)) {
            
            size_t start_offset = std::stoull(start_str);
            size_t chunk_size = std::stoull(size_str);
            
            std::ifstream file(filepath, std::ios::binary);
            if (file) {
                file.seekg(start_offset);
                std::vector<char> chunk_data(chunk_size);
                file.read(chunk_data.data(), chunk_size);
                size_t bytes_read = file.gcount();
                
                send_all(client_socket, chunk_data.data(), bytes_read);
            }
            file.close();
        }
        close(client_socket);
        return;
    }

    // ... existing code for regular file operations ...
    std::vector<FileInfo> available_files;
    std::vector<int> active_ports = get_active_ports(current_port);
    for (int active_port : active_ports) {
        std::vector<FileInfo> port_files = request_files_from_port(active_port);
        available_files.insert(available_files.end(), port_files.begin(), port_files.end());
    }
    
    std::string file_list_msg = "Files available:\n";
    for (size_t i = 0; i < available_files.size(); ++i) {
        const FileInfo& file_info = available_files[i];
        
        std::string display_name = file_info.filepath;
        size_t last_slash = file_info.filepath.find_last_of("/");
        if (last_slash != std::string::npos) {
            display_name = file_info.filepath.substr(last_slash + 1);
        }
        
        file_list_msg += "[" + std::to_string(i + 1) + "] " + display_name + 
                        " (" + std::to_string(file_info.file_size) + " bytes) " +
                        file_info.seed_info + "\n";
    }
    send(client_socket, file_list_msg.c_str(), file_list_msg.size(), 0);

    int file_id;
    recv(client_socket, &file_id, sizeof(file_id), 0);

    if (file_id < 1 || file_id > available_files.size()) {
        std::string msg = "Invalid file ID.\n";
        send(client_socket, msg.c_str(), msg.size(), 0); 
        return;
    }

    const FileInfo& selected_file = available_files[file_id - 1];
    std::string filename = selected_file.filepath;
    size_t last_slash = selected_file.filepath.find_last_of("/");
    if (last_slash != std::string::npos) {
        filename = selected_file.filepath.substr(last_slash + 1);
    }

    char exists_flag;
    recv(client_socket, &exists_flag, sizeof(exists_flag), 0);
    if (exists_flag == '1') {
        std::string msg = "File " + filename + " already exists\n";
        send(client_socket, msg.c_str(), msg.size(), 0);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(download_mutex);
        if (active_downloads[filename]) {
            std::string msg = "Download for File: " + filename + " is already started\n";
            send(client_socket, msg.c_str(), msg.size(), 0);
            return;
        }
        active_downloads[filename] = true;
    }

    size_t file_size = selected_file.file_size;
    size_t total_chunks = (file_size + CHUNK_SIZE - 1) / CHUNK_SIZE;
    
    std::string seeder_msg = "Enter file ID:\nLocating seeders... Found " + 
                            std::to_string(active_ports.size()) + " seeders\n" +
                            "Download started. File: " + filename + " (" + 
                            std::to_string(total_chunks) + " chunks)\n";
    send(client_socket, seeder_msg.c_str(), seeder_msg.size(), 0);

    // Create chunks and distribute across all available ports
    std::vector<ChunkDownload> chunks;
    for (size_t i = 0; i < total_chunks; ++i) {
        size_t start_offset = i * CHUNK_SIZE;
        size_t chunk_size = std::min(CHUNK_SIZE, file_size - start_offset);
        int target_port = active_ports[i % active_ports.size()]; // Round-robin distribution
        
        chunks.emplace_back(i, target_port, start_offset, chunk_size);
    }

    // Download chunks simultaneously using threads
    std::vector<std::thread> download_threads;
    std::atomic<int> completed_chunks(0);
    
    for (auto& chunk : chunks) {
        download_threads.emplace_back([&chunk, &selected_file, &completed_chunks]() {
            if (download_chunk_from_port(chunk.port, selected_file.filepath, chunk)) {
                completed_chunks++;
            }
        });
    }

    // Wait for all downloads to complete
    for (auto& thread : download_threads) {
        thread.join();
    }

    // Reassemble file from chunks
    std::ofstream outfile(filename, std::ios::binary);
    if (outfile) {
        for (const auto& chunk : chunks) {
            if (chunk.completed) {
                outfile.write(chunk.data.data(), chunk.data.size());
            }
        }
        outfile.close();
        
        download_status[filename] = "Success (" + std::to_string(completed_chunks.load()) + 
                                   "/" + std::to_string(total_chunks) + " chunks)";
    } else {
        download_status[filename] = "Failed: Could not create output file";
    }

    {
        std::lock_guard<std::mutex> lock(download_mutex);
        active_downloads[filename] = false;
    }
}

void* server_thread(void* arg) {
    int server_fd = *(int*)arg;
    int current_port;
    socklen_t addrlen;
    sockaddr_in addr;

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
            std::vector<FileInfo> local_files = get_local_files(port);
            
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
            std::vector<FileInfo> files;
            std::unordered_set<std::string> seen;

            for (int active_port : active_ports) {
                std::vector<FileInfo> port_files = request_files_from_port(active_port);
                
                for (const FileInfo& file : port_files) {
                    std::string file_key = file.filepath + "|" + std::to_string(file.file_size);
                    if (seen.count(file_key) == 0) {
                        files.push_back(file);
                        seen.insert(file_key);
                    }
                }
            }
            
            if (files.empty()) {
                std::cout << "No files found in other seeders.\n";
            } else {
                std::cout << "Files available from other ports:\n";
                for (size_t i = 0; i < files.size(); ++i) {
                    const FileInfo& file_info = files[i];
                    
                    std::string filename = file_info.filepath;
                    size_t last_slash = file_info.filepath.find_last_of("/");
                    if (last_slash != std::string::npos) {
                        filename = file_info.filepath.substr(last_slash + 1);
                    }
                    
                    std::cout << file_info.seed_info << " " << filename 
                             << " (" << file_info.file_size << " bytes)";
                    if (!file_info.key_id.empty()) {
                        std::cout << " [Key: " << file_info.key_id << "]";
                    }
                    std::cout << "\n";
                }
            } 
            std::cout << "\nReturning to menu...\n";

        } else if (choice == 2) {
            std::vector<int> active_ports = get_active_ports(port);
            
            if (active_ports.empty()) {
                std::cout << "\nNo other active ports found. Start more terminals to download files.\n";
                continue;
            }
            
            // Get all available files from active ports
            std::vector<FileInfo> files;
            std::unordered_set<std::string> seen;
            std::map<std::string, std::vector<FileInfo>> files_by_key_id;

            for (int active_port : active_ports) {
                std::vector<FileInfo> port_files = request_files_from_port(active_port);
                
                for (const FileInfo& file : port_files) {
                    std::string file_key = file.filepath + "|" + std::to_string(file.file_size);
                    if (seen.count(file_key) == 0) {
                        files.push_back(file);
                        seen.insert(file_key);
                        
                        // Group files by key_id (folder number)
                        if (!file.key_id.empty()) {
                            files_by_key_id[file.key_id].push_back(file);
                        }
                    }
                }
            }
            
            if (files_by_key_id.empty()) {
                std::cout << "No files found with Key IDs.\n";
                continue;
            }
            
            // Display available Key IDs
            std::cout << "\nAvailable File IDs:\n";
            std::vector<std::string> key_ids;
            for (const auto& entry : files_by_key_id) {
                key_ids.push_back(entry.first);
                std::cout << "[" << key_ids.size() << "] Key ID: " << entry.first 
                         << " (" << entry.second.size() << " files)\n";
                
                // Show files in this key_id
                for (const auto& file : entry.second) {
                    std::string filename = file.filepath;
                    size_t last_slash = file.filepath.find_last_of("/");
                    if (last_slash != std::string::npos) {
                        filename = file.filepath.substr(last_slash + 1);
                    }
                    std::cout << "    - " << filename << " (" << file.file_size << " bytes)\n";
                }
            }
            
            std::cout << "\nChoose File ID: ";
            int file_id_choice;
            std::cin >> file_id_choice;
            std::cin.ignore();
            
            if (file_id_choice < 1 || file_id_choice > key_ids.size()) {
                std::cout << "Invalid File ID.\n";
                continue;
            }
            
            std::string selected_key_id = key_ids[file_id_choice - 1];
            std::vector<FileInfo>& selected_files = files_by_key_id[selected_key_id];
            
            auto it = port_to_seed.find(port);
            if (it == port_to_seed.end()) {
                std::cout << "Error: Current port not found in port mapping.\n";
                continue;
            }
            std::string current_seed_folder = base_seed_path + "/" + it->second;
            
            // Download all files in the selected Key ID simultaneously
            std::cout << "\nDownloading " << selected_files.size() << " files from Key ID: " << selected_key_id << "\n";
            std::cout << "Saving to: " << current_seed_folder << "/" << selected_key_id << "/\n";
            
            std::vector<std::thread> file_download_threads;
            
            for (const auto& selected_file : selected_files) {
                file_download_threads.emplace_back([&selected_file, &active_ports, current_seed_folder, selected_key_id]() {
                    std::string filename = selected_file.filepath;
                    size_t last_slash = selected_file.filepath.find_last_of("/");
                    if (last_slash != std::string::npos) {
                        filename = selected_file.filepath.substr(last_slash + 1);
                    }
                    
                    std::string local_folder_path = current_seed_folder + "/" + selected_key_id;
                    std::string local_file_path = local_folder_path + "/" + filename;
                    
                    struct stat st = {0};
                    if (stat(local_folder_path.c_str(), &st) == -1) {
                        if (mkdir(local_folder_path.c_str(), 0755) != 0) {
                            std::cout << "Failed to create directory: " << local_folder_path << "\n";
                            return;
                        }
                    }
                    
                    // Check if file already exists
                    std::ifstream check_file(local_file_path);
                    if (check_file.good()) {
                        check_file.close();
                        std::cout << "File " << local_file_path << " already exists, skipping.\n";
                        return;
                    }
                    
                    {
                        std::lock_guard<std::mutex> lock(download_mutex);
                        if (active_downloads[filename]) {
                            std::cout << "Download for " << filename << " already in progress, skipping.\n";
                            return;
                        }
                        active_downloads[filename] = true;
                    }
                    
                    size_t file_size = selected_file.file_size;
                    size_t total_chunks = (file_size + CHUNK_SIZE - 1) / CHUNK_SIZE;
                    
                    std::cout << "Starting download: " << filename << " -> " << local_file_path << " (" << total_chunks << " chunks)\n";
                    
                    // Create chunks and distribute across ALL available ports
                    std::vector<ChunkDownload> chunks;
                    for (size_t i = 0; i < total_chunks; ++i) {
                        size_t start_offset = i * CHUNK_SIZE;
                        size_t chunk_size = std::min(CHUNK_SIZE, file_size - start_offset);
                        int target_port = active_ports[i % active_ports.size()]; // Round-robin across ALL ports
                        
                        chunks.emplace_back(i, target_port, start_offset, chunk_size);
                    }
                    
                    // Download chunks simultaneously from ALL ports
                    std::vector<std::thread> chunk_threads;
                    std::atomic<int> completed_chunks(0);
                    
                    for (auto& chunk : chunks) {
                        chunk_threads.emplace_back([&chunk, &selected_file, &completed_chunks]() {
                            if (download_chunk_from_port(chunk.port, selected_file.filepath, chunk)) {
                                completed_chunks++;
                            }
                        });
                    }
                    
                    // Wait for all chunk downloads to complete
                    for (auto& thread : chunk_threads) {
                        thread.join();
                    }
                    
                    std::ofstream outfile(local_file_path, std::ios::binary);
                    if (outfile) {
                        for (const auto& chunk : chunks) {
                            if (chunk.completed) {
                                outfile.write(chunk.data.data(), chunk.data.size());
                            }
                        }
                        outfile.close();
                        
                        download_status[filename] = "Success (" + std::to_string(completed_chunks.load()) + 
                                                   "/" + std::to_string(total_chunks) + " chunks) -> " + local_file_path;
                        std::cout << "Completed: " << filename << " -> " << local_file_path << " (" << completed_chunks.load() << 
                                    "/" << total_chunks << " chunks)\n";
                    } else {
                        download_status[filename] = "Failed: Could not create output file at " + local_file_path;
                        std::cout << "Failed to create: " << local_file_path << "\n";
                    }
                    
                    {
                        std::lock_guard<std::mutex> lock(download_mutex);
                        active_downloads[filename] = false;
                    }
                });
            }
            
            // Wait for all file downloads to complete
            for (auto& thread : file_download_threads) {
                thread.join();
            }
            
            std::cout << "\nAll downloads for Key ID " << selected_key_id << " completed.\n";
            std::cout << "Files saved to: " << current_seed_folder << "/" << selected_key_id << "/\n";
            
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
