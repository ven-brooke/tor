// // /*RUN: 
// g++ seed.cpp -o seed -lpthread
// ./seed      # Terminal 1*/
// //to do: file download

// #include <iostream>
// #include <fstream>
// #include <vector>
// #include <string>
// #include <cstring>
// #include <map>
// #include <dirent.h>
// #include <pthread.h>
// #include <arpa/inet.h>
// #include <netinet/in.h>
// #include <sys/socket.h>
// #include <sys/stat.h>
// #include <unistd.h>

// #define START_PORT 9005
// #define END_PORT 9009

// std::string base_seed_path = "C:/Users/USER/Downloads/8.18/8.18/files";
// std::map<int, std::string> port_to_seed = {
//     {9005, "seed1"},
//     {9006, "seed2"}, 
//     {9007, "seed3"},
//     {9008, "seed4"},
//     {9009, "seed5"}
// };

// std::map<std::string, std::string> download_status;
// std::map<std::string, bool> active_downloads;

// int find_available_port(int& server_fd, sockaddr_in& address) {
//     int opt = 1;
//     for (int port = START_PORT; port <= END_PORT; ++port) {
//         server_fd = socket(AF_INET, SOCK_STREAM, 0);
//         if (server_fd < 0) continue;

//         if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
//             close(server_fd);
//             continue;
//         }

//         address.sin_family = AF_INET;
//         address.sin_addr.s_addr = INADDR_ANY;
//         address.sin_port = htons(port);

//         if (bind(server_fd, (sockaddr*)&address, sizeof(address)) == 0) {
//             return port;
//         }

//         close(server_fd);
//     }
//     return -1;
// }

// // RECURSIVE LIST FILES
// void list_files_recursive(const std::string& path, std::vector<std::string>& files) {
//     DIR* dir = opendir(path.c_str()); 
//     if (!dir) {
//         std::cerr << "Failed to open directory: " << path << "\n";
//         return;
//     }

//     struct dirent* entry;
//     while ((entry = readdir(dir)) != nullptr) {  
//         std::string name = entry->d_name;
//         if (name == "." || name == "..") continue; // skip special entries

//         std::string full_path = path + "/" + name;

//         struct stat st;
//         if (stat(full_path.c_str(), &st) == -1) continue; //get file info

//         if (S_ISDIR(st.st_mode)) {
//             list_files_recursive(full_path, files); //subdirectory
//         } else if (S_ISREG(st.st_mode)) {
//            // std::cout << "Found file: " << full_path << "\n"; // Debug
//             files.push_back(full_path); //add regular file
//         }
//     }
//     closedir(dir);
// }

// std::vector<std::string> list_files_from_other_ports(int current_port) {
//     std::vector<std::string> files;
    
//     for (const auto& port_seed_pair : port_to_seed) {
//         int port = port_seed_pair.first;
//         std::string seed_folder = port_seed_pair.second;
        
//         // Skip current port's folder - only show files from OTHER ports
//         if (port == current_port) {
//             continue;
//         }
        
//         std::string full_seed_path = base_seed_path + "/" + seed_folder;
//         std::cout << "Scanning port " << port << " folder: " << full_seed_path << "\n";
        
//         list_files_recursive(full_seed_path, files);
//     }
    
//     return files;
// }

// // SEND entire buffer over socket
// bool send_all(int sock, const char* buffer, size_t length) {
//     size_t total_sent = 0;
//     while (total_sent < length) {
//         ssize_t sent = send(sock, buffer + total_sent, length - total_sent, 0);
//         if (sent <= 0) return false; //error or connection closed
//         total_sent += sent;
//     }
//     return true;
// }

// // RECEIVE data from socket
// bool recv_all(int sock, char* buffer, size_t length) {
//     size_t total_received = 0;
//     while (total_received < length) {
//         ssize_t bytes = recv(sock, buffer + total_received, length - total_received, 0);
//         if (bytes <= 0) return false;
//         total_received += bytes;
//     }
//     return true;
// }

// void handle_client(int client_socket, int current_port) {
//     std::vector<std::string> available_files = list_files_from_other_ports(current_port); //get file list from other ports
//     std::string file_list_msg = "Files available from other ports:\n";
//     for (size_t i = 0; i < available_files.size(); ++i) {
//         // Extract just the filename for display
//         std::string display_name = available_files[i].substr(available_files[i].find_last_of("/") + 1);
//         file_list_msg += "[" + std::to_string(i + 1) + "] " + display_name + " (from " + available_files[i] + ")\n";
//     }
//     send(client_socket, file_list_msg.c_str(), file_list_msg.size(), 0); //send file list

//     int file_id;
//     recv(client_socket, &file_id, sizeof(file_id), 0); // rcv file id

//     if (file_id < 1 || file_id > available_files.size()) {
//         std::string msg = "Invalid file ID.\n";
//         send(client_socket, msg.c_str(), msg.size(), 0); 
//         return;
//     }

//     std::string filepath = available_files[file_id - 1]; // Full path already included
//     std::string filename = filepath.substr(filepath.find_last_of("/") + 1);

//     char exists_flag;
//     recv(client_socket, &exists_flag, sizeof(exists_flag), 0);
//     if (exists_flag == '1') {
//         std::string msg = "File " + filename + " already exists\n";
//         send(client_socket, msg.c_str(), msg.size(), 0);
//         return;
//     }

//     if (active_downloads[filename]) {
//         std::string msg = "Download for File: " + filename + " is already started\n";
//         send(client_socket, msg.c_str(), msg.size(), 0);
//         return;
//     }

//     active_downloads[filename] = true; // mark as downloading

//     std::string seeder_msg = "Enter file ID:\nLocating seeders... Found 2 seeders\nDownload started. File: " + filename + "\n";
//     send(client_socket, seeder_msg.c_str(), seeder_msg.size(), 0);

//     std::ifstream infile(filepath, std::ios::binary); //open file
//     if (!infile) {
//         std::string msg = "File not found.\n";
//         send(client_socket, msg.c_str(), msg.size(), 0);
//         download_status[filename] = "Failed: File not found";
//         active_downloads[filename] = false;
//         return;
//     }

//     infile.seekg(0, std::ios::end);
//     size_t filesize = infile.tellg(); // get file size
//     infile.seekg(0, std::ios::beg);

//     send_all(client_socket, reinterpret_cast<char*>(&filesize), sizeof(filesize)); //send file size

//     char buffer[1024];
//     size_t total_sent = 0;
//     while (infile.read(buffer, sizeof(buffer))) {
//         if (!send_all(client_socket, buffer, sizeof(buffer))) {
//             download_status[filename] = "Failed during transfer";
//             active_downloads[filename] = false;
//             return;
//         }
//         total_sent += sizeof(buffer);
//     }

//     if (infile.gcount() > 0) {
//         send_all(client_socket, buffer, infile.gcount()); //sennd remaining bytes
//         total_sent += infile.gcount();
//     }

//     infile.close();
//     download_status[filename] = "Success (" + std::to_string(total_sent) + "/" + std::to_string(filesize) + " bytes)";
//     active_downloads[filename] = false; //mark complete DL
// }

// // MENU
// void show_menu() {
//     std::cout << "\n==========================================================\n";
//     std::cout << "\n>./seed\n";
//     std::cout << "[1] List available files.\n";
//     std::cout << "[2] Download file.\n";
//     std::cout << "[3] Download status.\n";
//     std::cout << "[4] Exit.\n";
//     std::cout << "Choose an option: ";
// }

// int main() {
//     int server_fd;
//     sockaddr_in address{};
//     int port = find_available_port(server_fd, address);

//     if (port == -1) {
//         std::cerr << "No available ports found.\n";
//         return 1;
//     }

//     std::cout << "\n==========================================================";
//     std::cout << "\nFinding available ports ... Found port " << port << "\n";
//     std::cout << "This is " << port_to_seed[port] << " - showing files from all OTHER seed folders\n";
//     std::cout << "Listening at port " << port << "\n";

//     listen(server_fd, 5);

//     int choice;
//     while (true) {
//         show_menu();
//         std::cin >> choice;
//         std::cin.ignore();

//         if (choice == 1) {
//             std::cout << "\nSearching files from other ports ... done.\n";
//             auto files = list_files_from_other_ports(port);
//             if (files.empty()) {
//                 std::cout << "No files found in other seed folders.\n";
//             } else {
//                 std::cout << "Files available from other ports:\n";
//                 for (size_t i = 0; i < files.size(); ++i) {
//                     std::string filename = files[i].substr(files[i].find_last_of("/") + 1);
//                     // Show which seed folder the file comes from
//                     std::string folder_info = files[i];
//                     for (const auto& port_seed_pair : port_to_seed) {
//                         if (folder_info.find(port_seed_pair.second) != std::string::npos) {
//                             folder_info = "from " + port_seed_pair.second + " (port " + std::to_string(port_seed_pair.first) + ")";
//                             break;
//                         }
//                     }
//                     std::cout << "[" << i + 1 << "] " << filename << " " << folder_info << "\n";
//                 }
//             }
//         } else if (choice == 2) {
//             std::cout << "Waiting for client to request file...\n";
//             sockaddr_in client_addr{};
//             socklen_t addrlen = sizeof(client_addr);
//             int client_socket = accept(server_fd, (sockaddr*)&client_addr, &addrlen);
//             if (client_socket < 0) {
//                 std::cerr << "Accept failed.\n";
//                 continue;
//             }
//             handle_client(client_socket, port);
//             close(client_socket);
//         } else if (choice == 3) {
//             std::cout << "\nDownload status:\n";
//             if (download_status.empty()) {
//                 std::cout << "No downloads yet.\n";
//             } else {
//                 for (const auto& entry : download_status) {
//                     std::cout << entry.first << ": " << entry.second << "\n";
//                 }
//             }
//         } else if (choice == 4) {
//             std::cout << "Exiting...\n";
//             break;
//         } else {
//             std::cout << "Invalid option. Try again.\n";
//         }
//     }

//     close(server_fd);
//     return 0;
// }
/*RUN: 
g++ seed.cpp -o seed -lpthread
./seed      # Terminal 1*/
//to do: file download

#include <iostream>
#include <fstream>
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

#define START_PORT 9005
#define END_PORT 9009

std::string base_seed_path = "C:/Users/USER/Downloads/8.18/8.18/files";
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

// RECURSIVE LIST FILES
void list_files_recursive(const std::string& path, std::vector<std::string>& files, const std::string& seed_info = "", const std::string& key_id = "") {
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
            std::string current_key_id = key_id.empty() ? name : key_id;
            std::string enhanced_seed_info = seed_info + " [Key ID: " + current_key_id + "]";
            list_files_recursive(full_path, files, enhanced_seed_info, current_key_id);
        } else if (S_ISREG(st.st_mode)) {
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

std::vector<std::string> list_files_from_other_ports(int current_port) {
    std::vector<std::string> files;
    
    for (const auto& port_seed_pair : port_to_seed) {
        int port = port_seed_pair.first;
        std::string seed_folder = port_seed_pair.second;
        
        if (port == current_port) {
            continue;
        }
        
        std::string full_seed_path = base_seed_path + "/" + seed_folder;
        std::cout << "Scanning port " << port << " folder: " << full_seed_path << "\n";
        
        DIR* test_dir = opendir(full_seed_path.c_str());
        if (!test_dir) {
            std::cout << "Directory not found or inaccessible: " << full_seed_path << "\n";
            continue;
        }
        closedir(test_dir);
        
        std::string seed_info = "Port " + std::to_string(port);
        list_files_recursive(full_seed_path, files, seed_info, "");
    }
    
    return files;
}

// SEND entire buffer over socket
bool send_all(int sock, const char* buffer, size_t length) {
    size_t total_sent = 0;
    while (total_sent < length) {
        ssize_t sent = send(sock, buffer + total_sent, length - total_sent, 0);
        if (sent <= 0) return false; //error or connection closed
        total_sent += sent;
    }
    return true;
}

// RECEIVE data from socket
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
    std::vector<std::string> available_files = list_files_from_other_ports(current_port); //get file list from other ports
    std::string file_list_msg = "Files available from other ports:\n";
    for (size_t i = 0; i < available_files.size(); ++i) {
        std::string file_entry = available_files[i];
        size_t separator = file_entry.find("|");
        std::string filepath = file_entry.substr(0, separator);
        std::string info = (separator != std::string::npos) ? file_entry.substr(separator + 1) : "";
        
        std::string display_name = filepath.substr(filepath.find_last_of("/") + 1);
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
    std::string filename = filepath.substr(filepath.find_last_of("/") + 1);

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

    active_downloads[filename] = true; // mark as downloading

    std::string seeder_msg = "Enter file ID:\nLocating seeders... Found 2 seeders\nDownload started. File: " + filename + "\n";
    send(client_socket, seeder_msg.c_str(), seeder_msg.size(), 0);

    std::ifstream infile(filepath, std::ios::binary); //open file
    if (!infile) {
        std::string msg = "File not found.\n";
        send(client_socket, msg.c_str(), msg.size(), 0);
        download_status[filename] = "Failed: File not found";
        active_downloads[filename] = false;
        return;
    }

    infile.seekg(0, std::ios::end);
    size_t filesize = infile.tellg(); // get file size
    infile.seekg(0, std::ios::beg);

    send_all(client_socket, reinterpret_cast<char*>(&filesize), sizeof(filesize)); //send file size

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
        send_all(client_socket, buffer, infile.gcount()); //send remaining bytes
        total_sent += infile.gcount();
    }

    infile.close();
    download_status[filename] = "Success (" + std::to_string(total_sent) + "/" + std::to_string(filesize) + " bytes)";
    active_downloads[filename] = false; //mark complete DL
}

// MENU
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

    int choice;
    while (true) {
        show_menu();
        std::cin >> choice;
        std::cin.ignore();

        if (choice == 1) {
            std::cout << "\nSearching files from other ports ... done.\n";
            auto files = list_files_from_other_ports(port);
            if (files.empty()) {
                std::cout << "No files found in other seed folders.\n";
            } else {
                std::cout << "Files available from other ports:\n";
                for (size_t i = 0; i < files.size(); ++i) {
                    std::string file_entry = files[i];
                    size_t separator = file_entry.find("|");
                    std::string filepath = file_entry.substr(0, separator);
                    std::string info = (separator != std::string::npos) ? file_entry.substr(separator + 1) : "";
                    
                    std::string filename = filepath.substr(filepath.find_last_of("/") + 1);
                    std::cout << "[" << i + 1 << "] " << filename << " (" << info << ")\n";
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
