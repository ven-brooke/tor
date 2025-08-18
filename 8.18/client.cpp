/*RUN: 
g++ client.cpp -o client
./client      # Terminal 2 */
//#include <arpa/inet.h>

//ver2

#include <iostream>
#include <fstream>
#include <cstring>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/stat.h>

#define PORT 8080

size_t get_file_size(const std::string& filename) {
    struct stat stat_buf;
    return (stat(filename.c_str(), &stat_buf) == 0) ? stat_buf.st_size : 0;
}

bool file_exists(const std::string& filename) {
    std::ifstream infile(filename.c_str());
    return infile.good();
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

void send_file(int sock, const std::string& filename) {
    if (!file_exists(filename)) {
        std::cerr << "File not found: " << filename << "\n";
        return;
    }

    std::ifstream infile(filename, std::ios::binary);
    size_t filesize = get_file_size(filename);

    // Send filename (256 bytes fixed)
    char fname_buf[256] = {0};
    strncpy(fname_buf, filename.c_str(), sizeof(fname_buf) - 1);
    if (!send_all(sock, fname_buf, sizeof(fname_buf))) {
        std::cerr << "Failed to send filename.\n";
        return;
    }

    // Send filesize
    if (!send_all(sock, reinterpret_cast<char*>(&filesize), sizeof(filesize))) {
        std::cerr << "Failed to send filesize.\n";
        return;
    }

    // Send file content
    char buffer[1024];
    size_t total_sent = 0;
    while (infile.read(buffer, sizeof(buffer))) {
        if (!send_all(sock, buffer, sizeof(buffer))) {
            std::cerr << "Failed to send file data.\n";
            return;
        }
        total_sent += sizeof(buffer);
        std::cout << "\rProgress: " << (100 * total_sent / filesize) << "%" << std::flush;
    }

    if (infile.gcount() > 0) {
        if (!send_all(sock, buffer, infile.gcount())) {
            std::cerr << "Failed to send final chunk.\n";
            return;
        }
        total_sent += infile.gcount();
    }

    std::cout << "\rProgress: 100%\n";
    infile.close();
}

int main() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "Socket creation failed.\n";
        return 1;
    }

    sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

    if (connect(sock, (sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cerr << "Connection failed.\n";
        return 1;
    }

    //display files in directory
    char buffer[2048] = {0}; // Adjust size if needed
    ssize_t bytes = recv(sock, buffer, sizeof(buffer) - 1, 0); //changed client_socket to sock
    if (bytes > 0) {
        buffer[bytes] = '\0'; // Null-terminate
        std::cout << "\nFiles available on server:\n" << buffer << std::endl;
    }

    //separate this
    std::string filename;
    while (true) {
        std::cout << "Enter filename to send (or 'exit' to quit): ";
        std::getline(std::cin, filename);
        if (filename == "exit") break;
        send_file(sock, filename);
    }

    close(sock);
    return 0;
} 

// #include <iostream>
// #include <fstream>
// #include <string>
// #include <cstring>
// #include <netinet/in.h>
// #include <unistd.h>
// #include <arpa/inet.h>

// #define SERVER_IP "127.0.0.1"
// #define SERVER_PORT 9005

// bool recv_all(int sock, char* buffer, size_t length) {
//     size_t total_received = 0;
//     while (total_received < length) {
//         ssize_t bytes = recv(sock, buffer + total_received, length - total_received, 0);
//         if (bytes <= 0) return false;
//         total_received += bytes;
//     }
//     return true;
// }

// bool send_all(int sock, const char* buffer, size_t length) {
//     size_t total_sent = 0;
//     while (total_sent < length) {
//         ssize_t sent = send(sock, buffer + total_sent, length - total_sent, 0);
//         if (sent <= 0) return false;
//         total_sent += sent;
//     }
//     return true;
// }

// int main() {
//     int sock = socket(AF_INET, SOCK_STREAM, 0);
//     if (sock < 0) {
//         std::cerr << "Socket creation failed.\n";
//         return 1;
//     }

//     sockaddr_in server_addr{};
//     server_addr.sin_family = AF_INET;
//     server_addr.sin_port = htons(SERVER_PORT);
//     inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

//     if (connect(sock, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
//         std::cerr << "Connection failed.\n";
//         return 1;
//     }

//     // Receive file list
//     char buffer[1024];
//     ssize_t bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);
//     if (bytes <= 0) {
//         std::cerr << "Failed to receive file list.\n";
//         close(sock);
//         return 1;
//     }
//     buffer[bytes] = '\0';
//     std::cout << buffer;

//     // Ask user for file ID
//     int file_id;
//     std::cout << "Enter file ID to download: ";
//     std::cin >> file_id;

//     // Send file ID
//     send_all(sock, reinterpret_cast<char*>(&file_id), sizeof(file_id));

//     // Simulate file existence check
//     std::string filename = "sample.txt"; // Replace with actual logic if needed
//     bool file_exists = std::ifstream(filename).good();
//     char exists_flag = file_exists ? '1' : '0';
//     send(sock, &exists_flag, sizeof(exists_flag), 0);

//     // Receive server response
//     bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);
//     if (bytes <= 0) {
//         std::cerr << "No response from server.\n";
//         close(sock);
//         return 1;
//     }
//     buffer[bytes] = '\0';
//     std::cout << buffer;

//     // If download starts, receive file
//     if (std::string(buffer).find("Download started") != std::string::npos) {
//         size_t filesize;
//         recv_all(sock, reinterpret_cast<char*>(&filesize), sizeof(filesize));

//         std::ofstream outfile(filename, std::ios::binary);
//         size_t received = 0;
//         while (received < filesize) {
//             ssize_t chunk = recv(sock, buffer, sizeof(buffer), 0);
//             if (chunk <= 0) break;
//             outfile.write(buffer, chunk);
//             received += chunk;
//         }
//         outfile.close();
//         std::cout << "Download complete. Received " << received << " bytes.\n";
//     }

//     close(sock);
//     return 0;
// }


