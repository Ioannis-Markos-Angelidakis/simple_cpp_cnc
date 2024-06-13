#include <iostream>
#include <print>
#include <ranges>
#include <filesystem>
#include <fstream>
#include <asio.hpp>
#include <patterns.hpp>

using mpark::patterns::match;
using mpark::patterns::pattern;
using mpark::patterns::_;
namespace fs = std::filesystem;

void curr_path(asio::ip::tcp::socket& socket) {
    asio::write(socket, asio::buffer(fs::current_path().u8string() + u8"\n\n"));
}

void directory_listing(asio::ip::tcp::socket& socket) {
    std::error_code error;

    for (const fs::directory_entry& entry : fs::directory_iterator(".", error)) {
        if (error) {
            asio::write(socket, asio::buffer(u8"<ERROR>\t" + entry.path().filename().u8string() + u8"\n"));
            error.clear();
            continue;
        }

        if (fs::is_directory(entry, error)) {
            asio::write(socket, asio::buffer(u8"<DIR>\t" + entry.path().filename().u8string() + u8"\n"));
        } else if (fs::is_regular_file(entry, error)) {
            asio::write(socket, asio::buffer(u8"\t" + entry.path().filename().u8string() + u8"\n"));
        }
    }

    asio::write(socket, asio::buffer(u8"\n"));
}

void change_directory(asio::ip::tcp::socket &socket, std::string&& directory) {
    using namespace std::literals;

    if (directory.starts_with("%")) [[unlikely]] {
        auto words = std::views::split(directory, "%"sv)
                    | std::ranges::to<std::vector<std::string>>();

        std::string path = std::getenv(words.at(1).data()) != nullptr? std::getenv(words.at(1).data()) : "ERROR";
        if (path != "ERROR") [[likely]] {
            directory = path + words.at(2);
        }
    }

    std::error_code error;
    if (fs::is_directory(directory, error)) {
        fs::current_path(directory, error);
        asio::write(socket, asio::buffer(u8"\n"));
        return;
    } 

    asio::write(socket, asio::buffer("[-] Invalid directory!\n\n"));
}

void send_file(asio::ip::tcp::socket &socket, const std::string& file_name) {
    std::string file = fs::current_path().string() + "/" + file_name;
    std::println("REQUESTED: {}", file);

    if (!fs::exists(file)) {
        std::println("[-] Error opening file for reading.");
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

void receive_file(asio::ip::tcp::socket& socket, std::string&& file) {
    if (fs::exists(fs::current_path() / file) && !file.starts_with("-r ")) {
        std::println("[-] File exists, abording...");
        asio::write(socket, asio::buffer("[-] File exists! Use `-r` flag to replace.\n\n"));
        return;
    } else if (file.starts_with("-r ")) {
        file = file.substr(3);
    }

    asio::write(socket, asio::buffer("\n\n"));
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
        std::println("[+] File received successfully.");
    } else {
        destination.close();
        fs::remove(file);
        std::println("[-] File transfer incomplete.");
    }
}

void send_string(asio::ip::tcp::socket& socket, const std::u8string& str) {
    std::error_code error;
    std::size_t size = str.size();
    
    asio::write(socket, asio::buffer(&size, sizeof(size)), error);
    asio::write(socket, asio::buffer(str), error);
}

void send_dir_file(asio::ip::tcp::socket& socket, const fs::path& file_path) {
    std::error_code error;
    std::ifstream file(file_path, std::ios::binary);
    
    if (file) {
        // Indicate file type
        char type = 'F';
        asio::write(socket, asio::buffer(&type, sizeof(type)), error);

        // Send file path relative to the base directory
        send_string(socket, fs::relative(file_path).u8string());
        // Get file size
        file.seekg(0, std::ios::end);
        std::size_t file_size = file.tellg();
        file.seekg(0, std::ios::beg);

        // Send file size
        asio::write(socket, asio::buffer(&file_size, sizeof(file_size)), error);

        // Send file content in chunks
        const std::size_t buffer_size = 1024;  // 8 KB chunks
        std::vector<char> buffer(buffer_size);
        std::size_t bytes_sent = 0;

        while (bytes_sent < file_size && !error) {
            std::size_t bytes_to_read = std::min(buffer_size, file_size - bytes_sent);
            file.read(buffer.data(), bytes_to_read);
            std::size_t len = asio::write(socket, asio::buffer(buffer.data(), bytes_to_read), error);
            bytes_sent += len;

            // Calculate and display progress
            std::print("Progress: {}%\r",static_cast<int32_t>((static_cast<double>(bytes_sent) / file_size) * 100));
        }
        std::println("");
    }
}

void send_directory(asio::ip::tcp::socket& socket, const fs::path& dir_path) {
    asio::error_code error;

    for (const fs::directory_entry& entry : fs::recursive_directory_iterator(dir_path, error)) {
        if (fs::is_regular_file(entry.status()) && !error) {
            send_dir_file(socket, entry.path());
        } else if (fs::is_directory(entry.status()) && !error) {
            // Indicate directory type
            char type = 'D';
            asio::write(socket, asio::buffer(&type, sizeof(type)), error);

            // Send directory path relative to the base directory
            send_string(socket, fs::relative(entry.path()).u8string());
        }
    }
    // Send a signal to indicate end of transmission
    char end_signal = 'E';
    asio::write(socket, asio::buffer(&end_signal, sizeof(end_signal)), error);
}

void server_messages(asio::ip::tcp::socket& socket) {
    asio::streambuf response;
    asio::error_code error;
    size_t bytes_transferred;

    while (!error && (bytes_transferred = asio::read_until(socket, response, '\n', error)) > 0) {
        std::istream response_stream(&response);
        std::string command;
        std::getline(response_stream, command);
        std::println("{}", command);

        match (command, command.starts_with("cd "), command.starts_with("download "), command.starts_with("upload "), command.starts_with("get ")) (
            pattern("ls", _, _, _, _) = [&] { directory_listing(socket); },
            pattern("pwd", _, _, _, _) = [&] { curr_path(socket); },
            pattern(_, 1, _, _, _) = [&] { change_directory(socket, command.substr(3)); },
            pattern(_, _, 1, _, _) = [&] { send_file(socket, command.substr(9)); },
            pattern(_, _, _, 1, _) = [&] { receive_file(socket, command.substr(7)); },
            pattern(_, _, _, _, 1) = [&] { send_directory(socket, command.substr(4)); },
            pattern(_, _, _, _, _) = [&] { asio::write(socket, asio::buffer("[-] Invalid command!\n\n")); }
        );
    }

    if (error != asio::error::eof && error != asio::error::connection_reset) {
        std::println("[-] ERROR SERVER");
    }
}

int32_t main() {
    asio::io_context io_context;
    asio::ip::tcp::acceptor acceptor(io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 12345));

    std::println("Server started. Waiting for connections...");

    while (true) {
        asio::ip::tcp::socket socket(io_context);
        acceptor.accept(socket);

        std::println("[+] New connection established");
        server_messages(socket);
    }
}
//clang++ -Wall -Wextra -Wpedantic -Wconversion -fsanitize=address server.cpp -o server -std=c++23 -lws2_32