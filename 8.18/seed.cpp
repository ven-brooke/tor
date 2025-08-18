/*RUN: 
g++ seed.cpp -o seed -lpthread
./seed      # Terminal 1*/
//must connect  to different port when new terminal is opened

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <dirent.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <map>
#include <sys/stat.h>
#include <pthread.h>
#include <set>

#define START_PORT 9005
#define END_PORT 9010

std::string seed_folder = "/data1/p4work/awidvb/Exercise/ProjDraft/bin"; // Default subfolder for files
std::map<std::string, std::string> download_status;
std::map<std::string, bool> active_downloads;
std::set<int> used_ports;

int find_available_port(int& server_fd, sockaddr_in& address) {
    int opt = 1;
    for (int port = START_PORT; port <= END_PORT; ++port) {
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) continue;

        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
            close(server_fd);
            continue;
        }

        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);

        // Try to bind
        if (bind(server_fd, (sockaddr*)&address, sizeof(address)) == 0) {
            // Immediately listen so OS reserves it
            if (listen(server_fd, 5) == 0) {
                return port;
            }
            close(server_fd);
        } else {
            close(server_fd);
        }
    }
    return -1;
}

void list_files_recursive(const std::string& path, std::vector<std::string>& files) {
    DIR* dir = opendir(path.c_str());
    if (!dir) {
        std::cerr << "Failed to open directory: " << path << "\n";
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
            list_files_recursive(full_path, files);
        } else if (S_ISREG(st.st_mode)) {
           // std::cout << "Found file: " << full_path << "\n"; // Debug
            files.push_back(full_path);
        }
    }

    closedir(dir);
}

std::vector<std::string> list_files(const std::string& root_path) {
    std::vector<std::string> files;
    list_files_recursive(root_path, files);
    return files;
}

// std::vector<std::string> list_files(const std::string& path = seed_folder) {
//     std::vector<std::string> files;
//     DIR* dir = opendir(path.c_str());
//     if (dir == nullptr) {
//         perror("opendir");
//         return files;
//     }

//     struct dirent* entry;
//     while ((entry = readdir(dir)) != nullptr) {
//         if (entry->d_type == DT_REG) { // Only regular files
//             files.emplace_back(entry->d_name);
//         }
//     }
//     closedir(dir);
//     return files;
// }


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

void handle_client(int client_socket, const std::string& seed_folder) {
    std::vector<std::string> available_files = list_files(seed_folder);
    std::string file_list_msg = "Files available:\n";
    for (size_t i = 0; i < available_files.size(); ++i) {
        file_list_msg += "[" + std::to_string(i + 1) + "] " + available_files[i] + "\n";
    }
    send(client_socket, file_list_msg.c_str(), file_list_msg.size(), 0);

    int file_id;
    recv(client_socket, &file_id, sizeof(file_id), 0);

    if (file_id < 1 || file_id > available_files.size()) {
        std::string msg = "Invalid file ID.\n";
        send(client_socket, msg.c_str(), msg.size(), 0);
        return;
    }

    std::string filename = available_files[file_id - 1];
    std::string filepath = seed_folder + "/" + filename;

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

    char buffer[1024];
    size_t total_sent = 0;
    while (infile.read(buffer, sizeof(buffer))) {
        if (!send_all(client_socket, buffer, sizeof(buffer))) {
            download_status[filename] = "Failed during transfer";
            active_downloads[filename] = false;
            return;
        }
        total_sent += sizeof(buffer);
    }

    if (infile.gcount() > 0) {
        send_all(client_socket, buffer, infile.gcount());
        total_sent += infile.gcount();
    }

    infile.close();
    download_status[filename] = "Success (" + std::to_string(total_sent) + "/" + std::to_string(filesize) + " bytes)";
    active_downloads[filename] = false;
}

void show_menu() {
    std::cout << "\n>./seed\n";
    std::cout << "[1] List available files.\n";
    std::cout << "[2] Download file.\n";
    std::cout << "[3] Download status.\n";
    std::cout << "[4] Exit.\n";
    std::cout << "Choose an option: ";
}

void* connection_thread(void* arg) {
    int server_fd = *(int*)arg;
    while (true) {
        sockaddr_in client_addr{};
        socklen_t addrlen = sizeof(client_addr);
        int client_socket = accept(server_fd, (sockaddr*)&client_addr, &addrlen);
        if (client_socket < 0) {
            std::cerr << "Accept failed.\n";
            continue;
        }
        handle_client(client_socket, seed_folder);
        close(client_socket);
    }
    return nullptr;
}

int main() {
    int server_fd;
    sockaddr_in address{};
    int port = find_available_port(server_fd, address);

    if (port == -1) {
        std::cerr << "No available ports found.\n";
        return 1;
    }

    std::cout << "\nFinding available ports ... Found port " << port << "\n";
    std::cout << "Listening at port " << port << "\n";

    listen(server_fd, 5);

    int choice;
    while (true) {
        show_menu();
        std::cin >> choice;
        std::cin.ignore();

        //list files
        if (choice == 1) {
            std::cout << "\nSearching files ... done.\n";
            auto files = list_files(seed_folder);
            if (files.empty()) {
                std::cout << "No files found.\n";
            } else {
                std::cout << "Files available:\n";
                for (size_t i = 0; i < files.size(); ++i) {
                    std::string filename = files[i].substr(files[i].find_last_of("/") + 1);
                    std::cout << "[" << i + 1 << "] " << filename << "\n";
                }
            }
        }else if (choice == 2) { //accept clients and handle requests
            std::cout << "Waiting for clients to request files...\n";
            std::cout << "(Press Ctrl+C to stop waiting and kill process, or choose Exit from menu later)\n";
        
            while (true) {
                sockaddr_in client_addr{};
                socklen_t addrlen = sizeof(client_addr);
                int client_socket = accept(server_fd, (sockaddr*)&client_addr, &addrlen);
                if (client_socket < 0) {
                    std::cerr << "Accept failed.\n";
                    continue;
                }
        
                std::cout << "Client connected. Handling request...\n";
                handle_client(client_socket, seed_folder);
                close(client_socket);
        
                std::cout << "Client disconnected. Waiting for next client...\n";
        
                // optional: let user break out with a key
                char again;
                std::cout << "Press 'q' to return to menu, or any other key to keep waiting: ";
                std::cin >> again;
                std::cin.ignore();
                if (again == 'q' || again == 'Q') break;
            }
        }else if (choice == 3) { //download status
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

