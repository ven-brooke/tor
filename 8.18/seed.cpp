/*RUN: 
g++ seed.cpp -o seed -lpthread
./seed      # Terminal 1*/
//Enhanced with file sizes, chunk-based downloads, simultaneous multi-port downloading, and progress tracking

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
#include <chrono>
#include <iomanip>
#include <memory>

// =====FILE AND CHUNK INFORMATION=====
struct FileInfo {           // file info stored by a seeder
    std::string filepath;
    std::string key_id;
    size_t file_size;       // File size in bytes
    std::string seed_info;  // Seed info
};

struct DownloadProgress {
    std::string filename;
    size_t total_bytes;
    std::atomic<size_t> downloaded_bytes;
    std::atomic<int> completed_chunks;
    int total_chunks;
    std::chrono::steady_clock::time_point start_time;
    std::atomic<bool> is_complete;
    std::string final_status;
    
    DownloadProgress(const std::string& name, size_t total, int chunks) 
        : filename(name), total_bytes(total), downloaded_bytes(0), 
          completed_chunks(0), total_chunks(chunks), 
          start_time(std::chrono::steady_clock::now()), is_complete(false) {}
    
    void update_progress(size_t chunk_bytes) {
        downloaded_bytes += chunk_bytes;
        completed_chunks++;
    }
    
    void mark_complete(const std::string& status) {
        final_status = status;
        is_complete = true;
    }
};

struct ChunkDownload {      // chunk info stored by a seeder
    int chunk_id;           
    int port;               
    size_t start_offset;    // byte offset of chunk in file
    size_t chunk_size;      // size of chunk in bytes (32 bytes)
    std::vector<char> data; // data of chunk
    bool completed;         // true if chunk is downloaded
    
    ChunkDownload(int id, int p, size_t start, size_t size)     
            : chunk_id(id), port(p), start_offset(start), chunk_size(size), completed(false) {
        data.resize(size);
    }
};

// Utility to extract just the filename from a full path
static std::string extract_filename(const std::string& full_path) {
    size_t last_slash = full_path.find_last_of("/");
    if (last_slash == std::string::npos) {
        return full_path;
    }
    return full_path.substr(last_slash + 1);
}

// Global variables for background download management
std::map<std::string, std::shared_ptr<DownloadProgress>> active_progress; // Track active downloads
std::mutex progress_mutex; // Mutex for progress tracking

// ==CONFIG CONSTANTS=====
#define START_PORT 9005
#define END_PORT 9009
const size_t CHUNK_SIZE = 32; // safer than #define, defines size of each chunk in bytes

std::string base_seed_path = "/mnt/c/Users/USER/Downloads/8.18/8.18/files";
// std::string base_seed_path = "/data1/p4work/awidvb/Exercise/ProjDraft/files";

std::map<int, std::string> port_to_seed = { // maps port -> seed folder
    {9005, "seed1"},
    {9006, "seed2"}, 
    {9007, "seed3"},
    {9008, "seed4"},
    {9009, "seed5"}
};

std::map<std::string, std::string> download_status;  // file -> status message  
std::map<std::string, bool> active_downloads;        // file -> true if download is active
std::mutex download_mutex;                           // mutex for thread-safe download operations
std::mutex console_mutex;                            // mutex for thread-safe console output

// =====PORT HANDLING FUNCTIONS=====
int find_available_port(int& server_fd, sockaddr_in& address) {
    int opt = 1;
    for (int port = START_PORT; port <= END_PORT; ++port) {
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) continue;

        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) { //Reuse address to avoid "port already in use"
            close(server_fd);
            continue;
        }

        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);
        if (bind(server_fd, (sockaddr*)&address, sizeof(address)) == 0) { //Bind to port if free
            return port;
        }
        close(server_fd);
    }
    return -1; // No available port found
}

bool is_port_active(int port) { // Check if port is active
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;
    
    sockaddr_in addr{}; 
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1"); // Localhost
    addr.sin_port = htons(port);
    
    bool active = (connect(sock, (sockaddr*)&addr, sizeof(addr)) == 0); // Connect to port
    close(sock);
    return active; 
}

std::vector<int> get_active_ports(int current_port) {
    std::vector<int> active_ports;                          // List of active ports
    for (const auto& port_seed_pair : port_to_seed) {       
        int port = port_seed_pair.first;                    // Get port number
        if (port != current_port && is_port_active(port)) {
            active_ports.push_back(port);                   // Add active port to list
        }
    }
    return active_ports;
}
//== FILE LISTING FUNCTIONS=====
void list_files_recursive(const std::string& path, std::vector<FileInfo>& files, const std::string& seed_info = "", const std::string& key_id = "") {
    DIR* dir = opendir(path.c_str());  
    if (!dir) {
        return; // Return if directory cannot be opened
    }
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {  
        std::string name = entry->d_name;
        if (name == "." || name == "..") continue; // Skip current and parent directories

        std::string full_path = path + "/" + name; 

        struct stat st;
        if (stat(full_path.c_str(), &st) == -1) continue; // Skip if file cannot be opened

        if (S_ISDIR(st.st_mode)) {
            // directory - use folder name as Key ID
            std::string current_key_id = name;
            std::string enhanced_seed_info = "[" + current_key_id + "] " + seed_info; 
            list_files_recursive(full_path, files, enhanced_seed_info, current_key_id); // Recursively list files in subdirectories
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

std::vector<FileInfo> get_local_files(int current_port) { // Get files from CURRENT port
    std::vector<FileInfo> files;
    auto it = port_to_seed.find(current_port); // Find port in port_to_seed map
    if (it == port_to_seed.end()) {
        std::cout << "[DEBUG] Port " << current_port << " not found in port_to_seed map" << std::endl;
        return files; // Return if port not found
    }
    
    std::string seed_folder = it->second; // Get SEED# folder from port_to_seed map
    std::string full_seed_path = base_seed_path + "/" + seed_folder; 
    DIR* test_dir = opendir(full_seed_path.c_str()); // Open seed directory
    if (!test_dir) {
        std::cout << "[DEBUG] Failed to open directory: " << full_seed_path << " (errno: " << errno << ")" << std::endl;
        return files;
    }
    closedir(test_dir);
    
    std::string seed_info;
    list_files_recursive(full_seed_path, files, seed_info, ""); // List files in seed directory
    return files;
}

std::vector<FileInfo> request_files_from_port(int port) { // Request files from port
    std::vector<FileInfo> files;
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return files;
    }
    
    struct timeval timeout;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)); // Set timeout for socket

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(port); 
    
    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) { // Connect to port
        close(sock);
        return files;
    }
    std::string request = "LIST_FILES";
    if (send(sock, request.c_str(), request.size(), 0) < 0) { // request file list to port
        close(sock);
        return files;
    }
    
    std::string response;   // Response from port
    char buffer[1024];      // Buffer to store response
    int bytes_received;     // Number of bytes received
    
    while ((bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) { // Receive response from port
        buffer[bytes_received] = '\0'; // Add null terminator to end of buffer
        response += std::string(buffer); 
    }
    close(sock);
    
    // Parse response 
    if (!response.empty()) { // If response is not empty
        std::istringstream iss(response);
        std::string line;
        while (std::getline(iss, line)) {
            if (!line.empty() && line != "END_LIST") {
                // Parse the enhanced file info: filepath|key_id|size|seed_info
                std::istringstream line_stream(line);               // Create stream from line
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
                    files.push_back(file_info); // Add file info to files vector
                }
            }
        }
    }
    
    return files;
}
//== CHUNK DOWNLOAD FUNCTIONS=====
bool send_all(int sock, const char* buffer, size_t length) { // Send all data to port
    size_t total_sent = 0;            // Total bytes sent
    while (total_sent < length) {     // Send data in chunks
        ssize_t sent = send(sock, buffer + total_sent, length - total_sent, 0);
        if (sent <= 0) return false;  
        total_sent += sent;           // Addto total bytes sent
    }
    return true; 
}

bool recv_all(int sock, char* buffer, size_t length) { // Receive all data from port
    size_t total_received = 0;         // Total bytes received
    while (total_received < length) {  // Receive data in chunks
        ssize_t bytes = recv(sock, buffer + total_received, length - total_received, 0);
        if (bytes <= 0) return false; 
        total_received += bytes;      // Add to total bytes received
    }
    return true; 
}

bool download_chunk_from_port(int port, const std::string& filepath, ChunkDownload& chunk, DownloadProgress* progress = nullptr) {
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
    
    if (progress) {
        progress->update_progress(chunk.chunk_size);
    }
    
    return true;
}
//== CLIENT HANDLING FUNCTIONS=====
void handle_client(int client_socket, int current_port) {
    char buffer[1024];
    int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received <= 0) {
        close(client_socket);
        return;
    }
    
    buffer[bytes_received] = '\0';
    std::string request(buffer);
    if (request == "LIST_FILES") { // List files from current port
        std::vector<FileInfo> local_files = get_local_files(current_port);
        
        std::string response;
        for (const auto& file_info : local_files) { // Send file info to client
            // Send: filepath|key_id|size|seed_info
            response += file_info.filepath + "|" + file_info.key_id + "|" + 
                       std::to_string(file_info.file_size) + "|" + file_info.seed_info + "\n";
        }
        response += "END_LIST\n";
        
        send_all(client_socket, response.c_str(), response.size()); // Send response to client
        close(client_socket);
        return;
    }
    if (request.substr(0, 14) == "DOWNLOAD_CHUNK") { // Download chunk from client
        std::istringstream iss(request);
        std::string command, filepath, start_str, size_str;
        
        if (std::getline(iss, command, '|') &&
            std::getline(iss, filepath, '|') &&
            std::getline(iss, start_str, '|') &&
            std::getline(iss, size_str)) {
            
            size_t start_offset = std::stoull(start_str);
            size_t chunk_size = std::stoull(size_str);
            
            std::ifstream file(filepath, std::ios::binary); // Open file
            if (file) {
                file.seekg(start_offset);
                std::vector<char> chunk_data(chunk_size);
                file.read(chunk_data.data(), chunk_size);
                size_t bytes_read = file.gcount();
                
                send_all(client_socket, chunk_data.data(), bytes_read); // Send chunk data to client
            }
            file.close();
        }
        close(client_socket);
        return;
    }

    std::vector<FileInfo> available_files; // List of available files
    std::vector<int> active_ports = get_active_ports(current_port); // Get active ports
    for (int active_port : active_ports) { // Iterate through active ports
        std::vector<FileInfo> port_files = request_files_from_port(active_port); // Request files from port
        available_files.insert(available_files.end(), port_files.begin(), port_files.end()); // Add files to available files
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

    char exists_flag; // Check if file exists
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

    for (auto& thread : download_threads) {    // Wait for all downloads to complete
        thread.join();
    }
    std::ofstream outfile(filename, std::ios::binary); // Reassemble file from chunks
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
//== SERVER THREAD FUNCTIONS=====
void* server_thread(void* arg) { // Dedicated thread for each server
    int server_fd = *(int*)arg;
    int current_port;
    socklen_t addrlen;
    sockaddr_in addr;

    socklen_t len = sizeof(addr);
    getsockname(server_fd, (sockaddr*)&addr, &len);
    current_port = ntohs(addr.sin_port);

    while (true) { // Accept connections from clients
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

bool create_directory_recursive(const std::string& path) {
    struct stat st = {0};
    if (stat(path.c_str(), &st) == 0) {
        return S_ISDIR(st.st_mode); // Already exists and is a directory
    }
    
    // Find parent directory
    size_t pos = path.find_last_of('/');
    if (pos != std::string::npos) {
        std::string parent = path.substr(0, pos);
        if (!create_directory_recursive(parent)) {
            return false;
        }
    }
    
    return mkdir(path.c_str(), 0755) == 0;
}

void download_files_background(const std::vector<FileInfo>& selected_files, 
                              const std::vector<int>& active_ports,
                              const std::string& current_seed_folder,
                              const std::string& selected_key_id) {
    
    std::vector<std::thread> file_download_threads;
    
    for (const auto& selected_file : selected_files) {
        file_download_threads.emplace_back([selected_file, active_ports, current_seed_folder, selected_key_id]() {
            std::string filename = selected_file.filepath;
            size_t last_slash = selected_file.filepath.find_last_of("/");
            if (last_slash != std::string::npos) {
                filename = selected_file.filepath.substr(last_slash + 1);
            }
            
            std::string local_folder_path = current_seed_folder + "/" + selected_key_id;
            std::string local_file_path = local_folder_path + "/" + filename;
            
            if (!create_directory_recursive(local_folder_path)) {
                std::lock_guard<std::mutex> lock(progress_mutex);
                if (active_progress[filename]) {
                    active_progress[filename]->mark_complete("Failed: Could not create directory");
                }
                return;
            }
            
            std::ifstream check_file(local_file_path);
            bool file_exists = check_file.good();
            check_file.close();
            
            {
                std::lock_guard<std::mutex> lock(download_mutex);
                if (active_downloads[filename]) {
                    std::lock_guard<std::mutex> progress_lock(progress_mutex);
                    if (active_progress[filename]) {
                        active_progress[filename]->mark_complete("Download already in progress");
                    }
                    return;
                }
                active_downloads[filename] = true;
            }
            
            size_t file_size = selected_file.file_size;
            size_t total_chunks = (file_size + CHUNK_SIZE - 1) / CHUNK_SIZE;
            
            std::vector<ChunkDownload> chunks;
            for (size_t i = 0; i < total_chunks; ++i) {
                size_t start_offset = i * CHUNK_SIZE;
                size_t chunk_size = std::min(CHUNK_SIZE, file_size - start_offset);
                int target_port = active_ports[i % active_ports.size()];
                
                chunks.emplace_back(i, target_port, start_offset, chunk_size);
            }
            
            std::vector<std::thread> chunk_threads;
            std::atomic<int> completed_chunks(0);
            
            std::shared_ptr<DownloadProgress> progress;
            {
                std::lock_guard<std::mutex> lock(progress_mutex);
                if (active_progress[filename]) {
                    progress = active_progress[filename];
                }
            }
            
            const size_t MAX_CONCURRENT_CHUNKS = std::min((size_t)10, total_chunks);
            size_t chunks_processed = 0;
            
            while (chunks_processed < total_chunks) {
                size_t batch_size = std::min(MAX_CONCURRENT_CHUNKS, total_chunks - chunks_processed);
                
                for (size_t i = 0; i < batch_size; ++i) {
                    auto& chunk = chunks[chunks_processed + i];
                    chunk_threads.emplace_back([&chunk, &selected_file, &completed_chunks, progress]() {
                        if (download_chunk_from_port(chunk.port, selected_file.filepath, chunk)) {
                            completed_chunks++;
                            if (progress) {
                                progress->update_progress(chunk.chunk_size);
                            }
                        }
                    });
                }
                
                for (auto& thread : chunk_threads) {
                    thread.join();
                }
                chunk_threads.clear();
                
                chunks_processed += batch_size;
            }
            
            std::string final_path = local_file_path;
            if (file_exists) {
                // Create a unique filename for duplicates
                size_t dot_pos = filename.find_last_of('.');
                std::string base_name = (dot_pos != std::string::npos) ? filename.substr(0, dot_pos) : filename;
                std::string extension = (dot_pos != std::string::npos) ? filename.substr(dot_pos) : "";
                
                int counter = 1;
                do {
                    std::string new_filename = base_name + "_" + std::to_string(counter) + extension;
                    final_path = local_folder_path + "/" + new_filename;
                    counter++;
                } while (std::ifstream(final_path).good());
            }
            
            std::ofstream outfile(final_path, std::ios::binary);
            if (outfile) {
                for (const auto& chunk : chunks) {
                    if (chunk.completed) {
                        outfile.write(chunk.data.data(), chunk.data.size());
                    }
                }
                outfile.close();
                
                std::string success_status;
                if (file_exists) {
                    success_status = "Success (" + std::to_string(completed_chunks.load()) + 
                                   "/" + std::to_string(total_chunks) + " chunks) -> " + final_path + " (downloaded from all duplicate sources)";
                } else {
                    success_status = "Success (" + std::to_string(completed_chunks.load()) + 
                                   "/" + std::to_string(total_chunks) + " chunks) -> " + final_path;
                }
                
                std::lock_guard<std::mutex> lock(progress_mutex);
                if (active_progress[filename]) {
                    active_progress[filename]->mark_complete(success_status);
                }
                
                download_status[filename] = success_status;
            } else {
                std::string error_status = "Failed: Could not create output file at " + final_path;
                
                std::lock_guard<std::mutex> lock(progress_mutex);
                if (active_progress[filename]) {
                    active_progress[filename]->mark_complete(error_status);
                }
                
                download_status[filename] = error_status;
            }
            
            {
                std::lock_guard<std::mutex> lock(download_mutex);
                active_downloads[filename] = false;
            }
        });
    }
    
    for (auto& thread : file_download_threads) {
        thread.join();
    }
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
                    // Deduplicate by filename + size so only the first seeder's entry is shown
                    std::string file_key = extract_filename(file.filepath) + "|" + std::to_string(file.file_size);
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
            
            std::vector<FileInfo> files;
            std::unordered_set<std::string> seen;
            std::map<std::string, std::vector<FileInfo>> files_by_key_id; 

            for (int active_port : active_ports) {
                std::vector<FileInfo> port_files = request_files_from_port(active_port);
                
                for (const FileInfo& file : port_files) {
                    // Deduplicate by filename + size so only the first seeder's entry is kept
                    std::string file_key = extract_filename(file.filepath) + "|" + std::to_string(file.file_size);
                    if (seen.count(file_key) == 0) {
                        files.push_back(file);
                        seen.insert(file_key);                                             
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
                        
            std::cout << "\nAvailable Files:\n";
            std::vector<std::string> key_ids;
            for (const auto& entry : files_by_key_id) {
                key_ids.push_back(entry.first);
                std::cout << "[" << entry.first << "]";
                for (const auto& file : entry.second) {
                    std::string filename = file.filepath;
                    size_t last_slash = file.filepath.find_last_of("/");
                    if (last_slash != std::string::npos) {
                        filename = file.filepath.substr(last_slash + 1);
                    }
                    std::cout << " " << filename << " (" << file.file_size << " bytes)\n";
                }
            }
            
            std::cout << "\nEnter File ID: ";
            std::string file_id_input;
            std::getline(std::cin, file_id_input);
            
            std::string selected_key_id;
            bool found_key_id = false;
                        
            if (files_by_key_id.find(file_id_input) != files_by_key_id.end()) {
                selected_key_id = file_id_input;
                found_key_id = true;
            } else {
                try {
                    int file_id_choice = std::stoi(file_id_input);
                    if (file_id_choice >= 1 && file_id_choice <= key_ids.size()) {
                        selected_key_id = key_ids[file_id_choice - 1];
                        found_key_id = true;
                    }
                } catch (const std::exception&) { }
            }
            
            if (!found_key_id) {
                std::cout << "Invalid File ID.\n";
                continue;
            }
            
            std::vector<FileInfo>& selected_files = files_by_key_id[selected_key_id];
            std::cout << "Locating seeders..";
            std::flush(std::cout); 
            
            int seeders_with_files = 0;
            for (int active_port : active_ports) {
                std::vector<FileInfo> port_files = request_files_from_port(active_port);
                bool has_key_id_files = false;
                for (const auto& file : port_files) {
                    if (file.key_id == selected_key_id) {
                        has_key_id_files = true;
                        break;
                    }
                }
                if (has_key_id_files) {
                    seeders_with_files++;
                }
            }
            
            if (seeders_with_files == 0) {
                std::cout << " No seeders for File ID " << selected_key_id << "\n";
                continue;
            }
            
            std::cout << " Found " << seeders_with_files << " seeders\n";  
            
            bool any_download_active = false;
            std::vector<std::string> active_file_names;
            {
                std::lock_guard<std::mutex> lock(download_mutex);
                for (const auto& file : selected_files) {
                    std::string filename = file.filepath;
                    size_t last_slash = file.filepath.find_last_of("/");
                    if (last_slash != std::string::npos) {
                        filename = file.filepath.substr(last_slash + 1);
                    }
                    
                    if (active_downloads[filename]) {
                        any_download_active = true;
                        active_file_names.push_back(filename);
                    }
                }
            }
            
            if (any_download_active) {
                for (const auto& filename : active_file_names) {
                    std::cout << "Download for file: " << filename << " is already started\n";
                }
                continue;
            }
            
            for (const auto& file : selected_files) {
                std::string filename = file.filepath;
                size_t last_slash = file.filepath.find_last_of("/");
                if (last_slash != std::string::npos) {
                    filename = file.filepath.substr(last_slash + 1);
                }
                std::cout << "Download started. File: " << filename << "\n";
                
                size_t file_size = file.file_size;
                size_t total_chunks = (file_size + CHUNK_SIZE - 1) / CHUNK_SIZE;
                
                std::lock_guard<std::mutex> lock(progress_mutex);
                active_progress[filename] = std::make_shared<DownloadProgress>(filename, file_size, total_chunks);
            }
            
            auto it = port_to_seed.find(port);
            if (it == port_to_seed.end()) {
                std::cout << "Error: Current port not found in port mapping.\n";
                continue;
            }
            std::string current_seed_folder = base_seed_path + "/" + it->second;
            
            std::thread background_download(download_files_background, selected_files, active_ports, current_seed_folder, selected_key_id);
            background_download.detach(); // Let it run in background
            
            std::cout << "\nReturning to menu...\n";
            
        } else if (choice == 3) {
            std::cout << "Download status:\n";
            
            bool has_active_downloads = false;
            std::vector<std::shared_ptr<DownloadProgress>> active_downloads_list;
            
            {
                std::lock_guard<std::mutex> lock(progress_mutex);
                for (const auto& entry : active_progress) {
                    if (!entry.second->is_complete) {
                        has_active_downloads = true;
                        active_downloads_list.push_back(entry.second);
                    }
                }
            }
            
            if (has_active_downloads) {
                std::cout << "Downloading 1 files from Key ID: (active)\n";
                
                while (true) {
                    bool still_active = false;
                    {
                        std::lock_guard<std::mutex> lock(progress_mutex);
                        for (const auto& progress : active_downloads_list) {
                            if (!progress->is_complete) {
                                still_active = true;
                                double percentage = (progress->total_bytes > 0) ? 
                                    (double)progress->downloaded_bytes / progress->total_bytes * 100.0 : 0.0;
                                
                                std::cout << "\r" << progress->filename << " (" << progress->total_chunks << " chunks) - "
                                          << "\"" << progress->filename << "\" " 
                                          << progress->downloaded_bytes << "b/" << progress->total_bytes << "b (" 
                                          << std::fixed << std::setprecision(1) << percentage << "%)";
                                std::cout.flush();
                                break; // Show one at a time for clarity
                            }
                        }
                    }
                    
                    if (!still_active) {
                        std::cout << "\n"; // Move to next line when done
                        break;
                    }
                    
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }
            
            {
                std::lock_guard<std::mutex> lock(progress_mutex);
                for (const auto& entry : active_progress) {
                    if (entry.second->is_complete) {
                        std::cout << "Completed: " << entry.second->filename << " - " << entry.second->final_status << "\n";
                    }
                }
            }
            
            std::cout << "\nReturning to menu...\n";
        }

        else if (choice == 4) {
            std::cout << "Exiting...\n";
            break;
        } else {
            std::cout << "Invalid option. Try again.\n";
        }
    }
    close(server_fd);
    return 0;
}
