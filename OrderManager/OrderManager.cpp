#include "OrderManager.hpp"

using json = nlohmann::json;
using namespace std::chrono;

static const std::string apiKey = "63ObNQpYqaCVrjTuBbhgFm2p";
static const std::string apiSecret = "D2OBzpfW-i6FfgmqGnrhpYqKPrxCvIYnu5KZKsZQW_09XkF-";

std::string CalcHmacSHA256(std::string_view decodedKey, std::string_view msg)
{
    std::array<unsigned char, EVP_MAX_MD_SIZE> hash;
    unsigned int hashLen;

    HMAC(
        EVP_sha256(),
        decodedKey.data(),
        static_cast<int>(decodedKey.size()),
        reinterpret_cast<unsigned char const*>(msg.data()),
        static_cast<int>(msg.size()),
        hash.data(),
        &hashLen
    );

    return std::string{reinterpret_cast<char const*>(hash.data()), hashLen};
}

std::string toHex(const std::string& input) {
    std::ostringstream hexStream;
    hexStream << std::hex << std::setfill('0');
    for (unsigned char c : input) {
        hexStream << std::setw(2) << static_cast<int>(c);
    }
    return hexStream.str();
}

void orderManager(int cpu, SPSCQueue<std::string>& strategyToOrderManagerQueue) {
    pinThread(cpu);

    SSL_CTX *ctx;
    SSL *ssl;
    int sockfd;
    struct sockaddr_in serv_addr;
    struct hostent *server;

    std::string host = "testnet.bitmex.com";
    std::string port = "443";
    std::string resource = "/api/v1/globalNotification";
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    // Add 10 seconds to the current time_point
    std::chrono::system_clock::time_point tenSecondsLater = now + std::chrono::seconds (10);
    // Convert the time_point to a Unix timestamp
    std::time_t timestamp = std::chrono::system_clock::to_time_t(tenSecondsLater);
    // Convert the timestamp to a string
    std::string expires = std::to_string(timestamp);
    // Concatenate the string to be hashed
    std::string concatenatedString = "GET" + resource + expires;
    // Calculate HMAC-SHA256
    std::string signature = CalcHmacSHA256(apiSecret, concatenatedString);
    std::string hexSignature = toHex(signature);

    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();

    // Create SSL context
    ctx = SSL_CTX_new(SSLv23_client_method());
    if (ctx == NULL) {
        std::cout << "aaaa" << std::endl;
        return;
    }

    // Create socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        std::cout << "failed to create socket" << std::endl;
        return;
    }

    server = gethostbyname(host.c_str());
    if (server == NULL) {
        std::cout << "could Not resolve hostname :(" << std::endl;
        close(sockfd);
        return;
    }

    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(std::stoi(port));
    bcopy((char *)server->h_addr, (char *)&serv_addr.sin_addr.s_addr, server->h_length);

    // Connect to server
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cout << "connection failed :(" << std::endl;
        close(sockfd);
        return;
    }

    // Create SSL connection
    if ((ssl = SSL_new(ctx)) == NULL) {
        ERR_print_errors_fp(stderr);
        return;
    }

    // Attach SSL to socket
    SSL_set_fd(ssl, sockfd);

    // Perform SSL handshake
    if (SSL_connect(ssl) == -1) {
        ERR_print_errors_fp(stderr);
        return;
    }

    std::ostringstream request;
    request << "GET " << resource << " HTTP/1.1\r\n";
    request << "Host: " << host << "\r\n";
    request << "api-key: " + apiKey + "\r\n";
    request << "api-expires: " + expires + "\r\n";
    request << "api-signature: " + hexSignature + "\r\n";
    request << "Content-Type: application/x-www-form-urlencoded\r\n";
    request << "Connection: close\r\n";
    request << "\r\n";

    if (SSL_write(ssl, request.str().c_str(), request.str().size()) < 0) {
        std::cout << "failed to send request..." << std::endl;
        SSL_free(ssl);
        close(sockfd);
        return;
    }

    std::cout << "aaa" << std::endl;

    int n;
    char buffer[4096];
    std::string raw_site;
    while ((n = SSL_read(ssl, buffer, sizeof(buffer))) > 0) {
        raw_site.append(buffer, n);
    }

    // Shutdown SSL
    SSL_shutdown(ssl);
    SSL_free(ssl);

    close(sockfd);

    std::cout << raw_site << std::endl;

    // Clean up SSL context
    SSL_CTX_free(ctx);
    EVP_cleanup();
}

/*
void testRoundTripTime2(io_uring* ring, struct io_uring_sqe* sqe, const std::string& requestVerb, const std::string& requestPath) {
    std::string requestUrl = "https://testnet.bitmex.com";
    requestUrl += requestPath;

    // Get the current time_point
    auto now = std::chrono::system_clock::now();
    // Add 10 seconds to the current time_point
    auto tenSecondsLater = now + std::chrono::seconds(10);
    // Convert the time_point to a Unix timestamp
    std::time_t timestamp = std::chrono::system_clock::to_time_t(tenSecondsLater);
    // Convert the timestamp to a string
    std::string expires = std::to_string(timestamp);
    // Concatenate the string to be hashed
    std::string concatenatedString = requestVerb + requestPath + expires;
    // Calculate HMAC-SHA256
    std::string signature = CalcHmacSHA256(apiSecret, concatenatedString);
    std::string hexSignature = toHex(signature);

    // Build the request headers
    std::string headers = "api-key: " + apiKey + "\r\n";
    headers += "api-expires: " + expires + "\r\n";
    headers += "api-signature: " + hexSignature + "\r\n";

    // Prepare io_uring operation
    io_uring_prep_write_fixed(sqe, 1, headers.data(), headers.size(), 0, 0);  // Example write operation

    // Submit io_uring operation
    int ret = io_uring_submit(ring);
    if (ret < 0) {
        std::cerr << "Failed to submit IO operation: " << strerror(-ret) << std::endl;
        return;
    }

    // Wait for completion queue entry (CQE)
    struct io_uring_cqe* cqe;
    ret = io_uring_wait_cqe(ring, &cqe);
    if (ret < 0) {
        std::cerr << "Failed to wait for CQE: " << strerror(-ret) << std::endl;
        return;
    }

    // Check if the operation was successful
    if (cqe->res < 0) {
        std::cerr << "IO operation failed: " << strerror(-cqe->res) << std::endl;
        io_uring_cqe_seen(ring, cqe);
        return;
    }

    if (cqe->res > 0) {
        // Print the response received
        std::string response(reinterpret_cast<char*>(io_uring_cqe_get_data(cqe)), cqe->res);
        std::cout << "Response received: " << response << std::endl;
    }
    // Release the CQE
    io_uring_cqe_seen(ring, cqe);

    std::cout << "===========================================================================================\n"
              << "RESPONSE FOR ADDITIONAL REQUEST RECEIVED\n"
              << "===========================================================================================\n"
              << std::endl;
}
*/

