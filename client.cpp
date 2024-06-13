#include <iostream>
#include <print>
#include <fstream>
#include <filesystem>
#include <asio.hpp>

namespace fs = std::filesystem;

bool server_messages(asio::ip::tcp::socket& socket) {
    asio::streambuf response;
    asio::error_code error;
    size_t bytes_transferred;
    std::istream response_stream(&response);
    std::string response_data = "";

    while (!error && bytes_transferred > 1) {
        bytes_transferred = asio::read_until(socket, response, '\n', error);

        if (bytes_transferred > 1) {
            std::getline(response_stream, response_data);
            std::println("{}", response_data);            
        }
    }

    if (response_data.starts_with("[-]")) {
        return false;
    }

    if (error != asio::error::eof && error != asio::error::connection_reset && bytes_transferred < 1) {
        std::println("[-] ERROR CLIENT");
        return false;
    }

    return true;
}

void send_file(asio::ip::tcp::socket& socket, const std::string& file_name) {
    if (!server_messages(socket)) { 
        return;
    }

    if (!fs::exists(file_name)) {
        std::println("[-] Error opening file for uploading.");
        std::size_t file_size = 0;
        asio::write(socket, asio::buffer(&file_size, sizeof(file_size)));
        return;
    }

    std::ifstream source(file_name, std::ios::binary | std::ios::ate);
    source.seekg(0, std::ios::end);
    std::size_t file_size = source.tellg();
    source.seekg(0, std::ios::beg);
    asio::write(socket, asio::buffer(&file_size, sizeof(file_size)));

    asio::error_code error;
    std::size_t sent = 0;
    std::vector<char> buffer(1024);
    while (!source.eof() && !error) {
        source.read(buffer.data(), buffer.size());
        sent += asio::write(socket, asio::buffer(buffer.data(), source.gcount()), error);
        std::print("Sending... {}%\r", static_cast<int32_t>((static_cast<double>(sent) / file_size) * 100));
    }

    if (error) {
        std::println("[-] Error sending data: {}", error.message());
    } else {
        std::println("[+] File sent successfully.");
    }
}

void download_file(asio::ip::tcp::socket& socket, std::string_view file) {
    asio::error_code error;
    std::size_t file_size = 0;
    asio::read(socket, asio::buffer(&file_size, sizeof(file_size)));

    if (file_size <= 0) {
        std::println("[-] REQUESTED Invalid file!");
        return;
    }

    std::size_t received = 0;
    std::println("Receiving file of {} bytes", file_size);

    std::ofstream destination(file.data(), std::ios::binary);
    if (!destination) {
        std::println("[-] Error opening file for writing");
        return;
    }

    std::vector<char> file_buffer(1024);
    while (received < file_size && !error) {
        std::size_t length = socket.read_some(asio::buffer(file_buffer), error);
        destination.write(file_buffer.data(), length);
        received += length;
        std::print("Downloading {}%\r", static_cast<int32_t>((static_cast<double>(received) / file_size) * 100));
    }

    if (received >= file_size) {
        std::println("[+] File received successfully");
    } else {
        destination.close();
        fs::remove(file);
        std::println("[-] File transfer incomplete");
    }
}

void receive_string(asio::ip::tcp::socket& socket, std::u8string& str) {
    std::error_code error;
    std::size_t size;

    // Read size of the incoming string
    asio::read(socket, asio::buffer(&size, sizeof(size)), error);
    if (error) {
        return;
    }
    // Read the actual string
    std::vector<char> buffer(size);
    asio::read(socket, asio::buffer(buffer), error);
    if (error) {
        return;
    }

    str.assign(buffer.begin(), buffer.end());
}

void receive_file(asio::ip::tcp::socket& socket, const fs::path& base_path) {
    std::error_code error;
    std::u8string file_path_str;

    receive_string(socket, file_path_str);
    if (error) {
        return;
    }

    fs::path file_path = base_path / file_path_str;

    // Create directories if they do not exist
    fs::create_directories(file_path.parent_path());

    // Receive file size
    std::size_t file_size;
    asio::read(socket, asio::buffer(&file_size, sizeof(file_size)), error);
    if (error) {
        return;
    }

    // Open file for writing
    std::ofstream file(file_path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to open file for writing");
    }

    // Receive file content in chunks
    const std::size_t buffer_size = 1024;
    std::vector<char> buffer(buffer_size);
    std::size_t bytes_received = 0;

    while (bytes_received < file_size && !error) {
        std::size_t bytes_to_read = std::min(buffer_size, file_size - bytes_received);
        std::size_t len = asio::read(socket, asio::buffer(buffer.data(), bytes_to_read), error);
        if (error) {
            break;
        }
        file.write(buffer.data(), len);
        bytes_received += len;

        // Calculate and display progress
        std::print("Progress: {}%\r",static_cast<int32_t>((static_cast<double>(bytes_received) / file_size) * 100));
    }
    std::cout << std::endl;
}

void receive_directory(asio::ip::tcp::socket& socket, const fs::path& base_path) {
    std::error_code error;

    while (true) {
        // Read the type of the incoming entry
        char type;
        asio::read(socket, asio::buffer(&type, sizeof(type)), error);
        if (error) {
            std::println("[-] {}", error.message());
            break;
        }

        if (type == 'E') {
            break; // End of transmission
        } else if (type == 'D') {
            // Receive directory path
            std::u8string dir_path_str;
            receive_string(socket, dir_path_str);
            if (error) {
                break;
            }
            fs::path dir_path = base_path / dir_path_str;

            // Create directory
            fs::create_directories(dir_path);
        } else if (type == 'F') {
            // Receive file
            receive_file(socket, base_path);
            if (error) {
                break;
            }
        }
    }
}

int32_t main() {
    asio::io_context io_context;
    asio::ip::tcp::socket socket(io_context);
    asio::ip::tcp::resolver resolver(io_context);
    asio::error_code error;
    asio::connect(socket, resolver.resolve("192.168.2.7", "12345"), error);

    std::string command = "";
    while (true) {
        if (error) {
            std::println("[-] Trying to reconnect...");
            std::this_thread::sleep_for(std::chrono::seconds(1));
            asio::connect(socket, resolver.resolve("192.168.2.7", "12345"), error);
            continue;
        }

        std::print("Enter command (or 'exit' to quit): ");
        std::getline(std::cin, command);

        if (command == "exit") {
            break;
        } else if (command.starts_with("download ")) {
            std::string file_name = command.substr(9);
            if (fs::exists(file_name) && fs::file_size(file_name) > 0) {
                std::srand(std::time(nullptr));
                file_name = std::to_string(std::rand()) + file_name;
                asio::write(socket, asio::buffer(command + "\n"), error);
                download_file(socket, file_name);
            } else if(file_name.starts_with("-r ")) {
                file_name = file_name.substr(3);
                asio::write(socket, asio::buffer("download " + file_name + "\n"), error);
                download_file(socket, file_name);
            } else {
                asio::write(socket, asio::buffer("download " + file_name + "\n"), error);
                download_file(socket, file_name);
            }
        } else if (command.starts_with("upload ")) {
            std::string file_name = command.substr(7);
            if (fs::exists(file_name)) {
                asio::write(socket, asio::buffer("upload " + file_name + "\n"));
                send_file(socket, file_name);
            }else if (fs::exists(file_name.substr(3))  && file_name.starts_with("-r ")) {
                asio::write(socket, asio::buffer("upload " + file_name + "\n"));
                send_file(socket, file_name.substr(3));
            } else {
                std::println("[-] File does not exist.");
            }
        } else if (command.starts_with("get ")) {
            std::string file_name = command.substr(4);
            asio::write(socket, asio::buffer("get " + file_name + "\n"), error);
            receive_directory(socket, file_name);
        } else {
            asio::write(socket, asio::buffer(command + "\n"), error);
            std::this_thread::sleep_for(std::chrono::milliseconds(1)); //Latency on %% validations requires this delay
            server_messages(socket);
        }
    }
}
//clang++ -Wall -Wextra -Wpedantic -Wconversion -fsanitize=address client.cpp -o client -std=c++23 -lws2_32