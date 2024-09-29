<img width="25%" alt="cpp23-filesystem-asio" src="https://m.media-amazon.com/images/I/710gHgD1BHL._SL1360_.jpg">
<p>Exploring server-client networking concepts using modern C++23 features.</p>
<p>Implementing a synchronous single-threaded server and client application.</p>
<p>Utilizing C++ <code>std::filesystem</code> for file operations and standalone <a href="https://github.com/chriskohlhoff/asio/">ASIO</a> library for TCP/IP communication, and pattern matching with <a href="https://github.com/mpark/patterns">mpark/patterns</a>.</p>

<p>This project is a TCP-based file transfer server which allows users to browse directories, upload, download, and manage files over a TCP connection.</p>

<h2>Features</h2>
<ul>
  <li>Browse the current directory and list its contents</li>
  <li>Change directories, including handling environment variables for paths</li>
  <li>Download and upload files</li>
  <li>Send entire directories with file transfer support</li>
</ul>

<h2>Functionality</h2>
<ul>
  <li><code>curr_path()</code> - Sends the current directory path to the connected client.</li>
  <li><code>directory_listing()</code> - Lists the files and directories in the current directory and sends the information to the client.</li>
  <li><code>change_directory()</code> - Changes the server's working directory based on the command from the client, supporting environment variables in the path.</li>
  <li><code>send_file()</code> - Sends a requested file to the client, ensuring the file exists before transferring.</li>
  <li><code>receive_file()</code> - Receives a file from the client and saves it to the server, with support for file replacement.</li>
  <li><code>send_directory()</code> - Sends an entire directory recursively to the client, including file contents and directory structure.</li>
</ul>

<h2>Command Handling</h2>
<p>The server listens for and processes commands sent by the client:</p>
<ul>
  <li><strong>cd &lt;path&gt;</strong> - Change the server's current directory to the specified path.</li>
  <li><strong>ls</strong> - List the files and directories in the current directory.</li>
  <li><strong>pwd</strong> - Show the server's current directory path.</li>
  <li><strong>download &lt;file&gt;</strong> - Request the server to send a file.</li>
  <li><strong>upload &lt;file&gt;</strong> - Send a file to the server.</li>
  <li><strong>get &lt;directory&gt;</strong> - Download an entire directory from the server.</li>
</ul>

<h2>Technologies Used</h2>
<ul>
  <li><strong>Asio</strong> - C++ library for networking and asynchronous programming</li>
  <li><strong>C++23</strong> - Utilizes modern C++23 features</li>
  <li><strong>Filesystem</strong> - C++ standard library for file and directory manipulation</li>
  <li><strong>mpark/patterns</strong> - Pattern matching library for cleaner command handling</li>
</ul>

<h2>Compilation</h2>
<p>To compile the server, you can use the following command:</p>
<pre><code>clang++ -Wall -Wextra -Wpedantic -Wconversion -fsanitize=address server.cpp -o server -std=c++23 -lws2_32</code></pre>

<h2>Usage</h2>
<p>Once the server is running, it listens on port <code>12345</code> for incoming TCP connections. Clients can send commands to interact with the server, browse directories, and transfer files.</p>
