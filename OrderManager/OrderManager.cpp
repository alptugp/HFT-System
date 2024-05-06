#include "OrderManager.hpp"
#include "OrderManagerUtils.hpp"
#include <liburing.h>
#include <iostream>
#include <chrono>
#include <ctime>
#include <iomanip>

#define TX_DEFAULT_BUF_SIZE 128

using namespace std::chrono;

static const char *const  apiKey = "63ObNQpYqaCVrjTuBbhgFm2p";
static const char *const apiSecret = "D2OBzpfW-i6FfgmqGnrhpYqKPrxCvIYnu5KZKsZQW_09XkF-";
static const char *const  verb = "POST";
static const char *const  path = "/api/v1/order";

void orderManager(int cpu, SPSCQueue<std::string>& strategyToOrderManagerQueue) {
    pinThread(cpu);

    int port = 443;
    const char* host_ip = "104.18.32.75";
    const char * host_name = "testnet.bitmex.com";
    int ip_family = AF_INET;
    /*int port = 4443;
    const char* host_ip = "127.0.0.1";
    const char * host_name = NULL;
    int ip_family = AF_INET;*/
    int sockfds[BATCH_SIZE];
    struct ssl_client clients[BATCH_SIZE];

    for (int i = 0; i < BATCH_SIZE; ++i) {
        sockfds[i] = socket(ip_family, SOCK_STREAM, 0);

        if (sockfds[i] < 0)
            die("socket()");

        if (ip_family == AF_INET) {
            struct sockaddr_in addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family = ip_family;
            addr.sin_port = htons(port);

            if (inet_pton(ip_family, host_ip, &(addr.sin_addr)) <= 0)
                die("inet_pton()");

            if (connect(sockfds[i], (struct sockaddr *) &addr, sizeof(addr)) < 0)
                die("connect()");
        }
    }

    printf("sockets connected\n");

    struct pollfd fdset[BATCH_SIZE];
    memset(&fdset, 0, sizeof(fdset));
    for (int i = 0; i < BATCH_SIZE; i++) {
        fdset[i].fd = sockfds[i];
        fdset[i].events = POLLERR | POLLHUP | POLLNVAL | POLLIN;
    }

    ssl_init(0,0);
    for (int i = 0; i < BATCH_SIZE; i++) {
        ssl_client_init(&clients[i], sockfds[i], SSLMODE_CLIENT);
        if (host_name)
            SSL_set_tlsext_host_name(clients[i].ssl, host_name); // TLS SNI
    }

    for (int i = 0; i < BATCH_SIZE; i++) {
        do_ssl_handshake(&clients[i]);
        printf("Performing SSL handshake\n");
        while (true) {
            if (SSL_is_init_finished(clients[i].ssl))
                break;

            fdset[i].events &= ~POLLOUT;
            fdset[i].events |= ssl_client_want_write(&clients[i]) ? POLLOUT : 0;

            int nready = poll(&fdset[0], BATCH_SIZE, -1);

            if (nready == 0)
                continue; /* no fd ready */

            int revents = fdset[i].revents;
            if (revents & POLLIN)
                if (do_sock_read(&clients[i], true) == -1)
                    break;

            if (revents & POLLOUT)
                if (do_sock_write(&clients[i]) == -1)
                    break;

            if (revents & (POLLERR | POLLHUP | POLLNVAL))
                break;

            if (clients[i].encrypt_len > 0)
                if (do_encrypt(&clients[i]) < 0)
                    break;
        }
    }

    printf("SSL handshake done for all sockets\n");

    struct io_uring ring;
    struct io_uring_params params;

    print_sq_poll_kernel_thread_status();

    memset(&params, 0, sizeof(params));
    params.flags |= IORING_SETUP_SQPOLL;
    params.sq_thread_idle = 20000;

    // Initialize io_uring
    if (io_uring_queue_init_params(8, &ring, &params) < 0) {
        perror("io_uring_queue_init");
        return;
    }

    // int ret = io_uring_queue_init(8, &ring, 0);
    // if (ret) {
    //     perror("io_uring_queue_init");
    //     return;
    // }

    if (io_uring_register_files(&ring, sockfds, BATCH_SIZE) < 0) {
        perror("io_uring_register_files");
        return;
    }

    int stop = 0;
    while (true) {
        std::string orderData[BATCH_SIZE];

        for (int i = 0; i < BATCH_SIZE; ++i) {
            // std::string orderData_i;
            // while (!strategyToOrderManagerQueue.pop(orderData_i)) {};
            // orderData[i] = orderData_i.substr(0, orderData_i.length() - 39);
            // const char *postData = orderData[i].c_str();
            /*std::string updateExchangeTimepoint = orderData[i].substr(orderData[i].length() - 39, 13);
            std::string updateReceiveTimepoint = orderData[i].substr(orderData[i].length() - 26, 13);
            std::string strategyTimepoint = orderData[i].substr(orderData[i].length() - 13);*/

            const char * postData = "symbol=XBTUSDT&side=Sell&orderQty=1000&price=1&ordType=Limit";
            orderData[i] = std::string(postData);
        }

        for (int i = 0; i < BATCH_SIZE; ++i) {
            char expires[20];
            char unencrypted_signature[256];

            time_t now = time(NULL);
            time_t tenSecondsLater = now + 10;
            strftime(expires, sizeof(expires), "%s", localtime(&tenSecondsLater));

            snprintf(unencrypted_signature, sizeof(unencrypted_signature), "%s%s%s%s", verb, path, expires, orderData[i].c_str());
            char *signature = api_get_signature(apiSecret, strlen(apiSecret), unencrypted_signature, strlen(unencrypted_signature));

            printf("Unix Timestamp (expires): %s\n", expires);
            printf("Unencrypted signature form: %s\n", unencrypted_signature);
            printf("Encrypted hexadecimal signature: %s\n", signature);

            char unencrypted_request[1024];
            sprintf(unencrypted_request, "POST /api/v1/order HTTP/1.1\r\n"
                                         "Host: testnet.bitmex.com\r\n"
                                         "api-key: %s\r\n"
                                         "api-expires: %s\r\n"
                                         "api-signature: %s\r\n"
                                         "Content-Type: application/x-www-form-urlencoded\r\n"
                                         "Content-Length: %zu\r\n"
                                         "\r\n"
                                         "%s", apiKey, expires, signature, orderData[i].length(), orderData[i].c_str());

            send_unencrypted_bytes(&clients[i], unencrypted_request, strlen(unencrypted_request));
            do_encrypt(&clients[i]);
        }

        struct io_uring_sqe *sqe;
        struct io_uring_cqe *cqe;

        for (int i = 0; i < BATCH_SIZE; ++i) {
            sqe = io_uring_get_sqe(&ring);

            if (!sqe) {
                perror("io_uring_get_sqe");
                io_uring_queue_exit(&ring);
                return;
            }

            printf("Sending for sockfd %d\n", sockfds[i]);
            io_uring_prep_write(sqe, 0, clients[i].write_buf, clients[i].write_len, 0);
            sqe->flags |= IOSQE_FIXED_FILE;
        }

        if (io_uring_submit(&ring) < 0) {
            perror("io_uring_submit");
            io_uring_queue_exit(&ring);
            return;
        }
        
        std::cout << "IO_URING SUBMISSION TIME: " << getCurrentTime() << std::endl;
        system_clock::time_point submissionTimestamp = high_resolution_clock::now();
        std::string submissionTimepoint = std::to_string(duration_cast<milliseconds>(submissionTimestamp.time_since_epoch()).count());

        // Wait for completions
        for (int i = 0; i < BATCH_SIZE; ++i) {
            int ret = io_uring_wait_cqe(&ring, &cqe);
            if (ret < 0) {
                perror("Error waiting for completion: %s\n");
                return;
            }

            // Read response
            if (cqe->res <= 0) {
                perror("io_uring completion error");
                return;
            }

            if (cqe->res > 0) {
                if ((size_t)cqe->res < clients[i].write_len)
                    memmove(clients[i].write_buf, clients[i].write_buf+cqe->res, clients[i].write_len-cqe->res);
                clients[i].write_len -= cqe->res;
                clients[i].write_buf = (char*)realloc(clients[i].write_buf, clients[i].write_len);
            }
            else
                return;

            io_uring_cqe_seen(&ring, cqe);
        }

        print_sq_poll_kernel_thread_status();

        int break_polling = 0;
        while (true) {
            int nready = poll(&fdset[0], BATCH_SIZE, -1);
            /*printf("nready: %d %d ", nready, break_polling);*/

            if (nready == 0)
                continue; /* no fd ready */

            for (int i = 0; i < BATCH_SIZE; ++i) {
                int revents = fdset[i].revents;

                if (revents & POLLIN) {
                    int bytes_read = do_sock_read(&clients[i], false);
                    size_t last_char_index = strlen(clients[i].response_buf) - 1;
                    /*printf("exits %d %c\n", bytes_read, clients[i].response_buf[strlen(clients[i].response_buf) - 1]);*/

                    if (clients[i].response_buf[last_char_index] == '}') {
                        long exchangeExecutionTimestamp = convertTimestampToTimePoint(extract_json(std::string(clients[i].response_buf)).FindMember("transactTime")->value.GetString());

                        std::cout
                        << "\n===========================================================================================\n"
                        << "NEW ORDER EXECUTED\n"
                        << exchangeExecutionTimestamp
                        << "\nSubmission to Execution (ms): "
                        << getTimeDifferenceInMillis(submissionTimepoint, std::to_string(exchangeExecutionTimestamp)) << "      "
                        << "\n===========================================================================================\n"
                        << std::endl;

                        memset(clients[i].response_buf, 0, sizeof(clients[i].response_buf));
                        break_polling++;
                    }
                }

                if (revents & (POLLERR | POLLHUP | POLLNVAL))
                    break;

            }

            if (break_polling >= BATCH_SIZE)
                break;
        }

        stop++;

        if (stop == 2)
            break;
    }


    for (int i = 0; i < BATCH_SIZE; ++i) {
        close(fdset[i].fd);
        print_ssl_state(&clients[i]);
        print_ssl_error();
        ssl_client_cleanup(&clients[i]);
    }
}

/*void orderManager(int cpu, SPSCQueue<std::string>& strategyToOrderManagerQueue) {
    pinThread(cpu);

    // Initialize libcurl
    curl_global_init(CURL_GLOBAL_DEFAULT);
    static const int HANDLE_COUNT = 3;

    // Create multi handle
    CURL* easyHandles[HANDLE_COUNT];
    ThreadPool pool(HANDLE_COUNT);

    for (int i = 0; i < HANDLE_COUNT; i++) {
        easyHandles[i] = curl_easy_init();
        if (easyHandles[i]) {
            curl_easy_setopt(easyHandles[i], CURLOPT_WRITEFUNCTION, WriteCallback);
            *//*curl_easy_setopt(easyHandle, CURLOPT_VERBOSE, 1L);*//*
        }
    }

    // Initially send unfillable/invalid orders to the exchange to make the submission-execution latency for subsequent orders lower
    static const std::string invalidOrderData = "symbol=XBTUSDT&side=Sell&orderQty=0&ordType=Market";
    for (int i = 0; i < HANDLE_COUNT; i++) {
        pool.enqueue(sendOrderAsync, invalidOrderData, easyHandles[i % HANDLE_COUNT], true);
    }

    {
        ThreadPool rttPool(1);
        CURL *rttEasyHandle = curl_easy_init();
        curl_easy_setopt(rttEasyHandle, CURLOPT_WRITEFUNCTION, WriteCallback);
        rttPool.enqueue(testRoundTripTime, "GET", "/api/v1/position?filter=%7B%22symbol%22%3A%20%22XBTUSDT%22%7D&columns=%5B%22timestamp%22%5D", rttEasyHandle);
        rttPool.enqueue(testRoundTripTime, "GET", "/api/v1/position?filter=%7B%22symbol%22%3A%20%22XBTUSDT%22%7D&columns=%5B%22timestamp%22%5D", rttEasyHandle);
        rttPool.enqueue(testRoundTripTime, "GET", "/api/v1/address", rttEasyHandle);
        rttPool.enqueue(testRoundTripTime, "GET", "/api/v1/apiKey", rttEasyHandle);
        rttPool.enqueue(testRoundTripTime, "GET", "/api/v1/globalNotification", rttEasyHandle);
        rttPool.enqueue(testRoundTripTime, "GET", "/api/v1/globalNotification", rttEasyHandle);
        rttPool.enqueue(testRoundTripTime, "GET", "/api/v1/globalNotification", rttEasyHandle);
        rttPool.enqueue(testRoundTripTime, "GET", "/api/v1/globalNotification", rttEasyHandle);
        rttPool.enqueue(testRoundTripTime, "GET", "/api/v1/globalNotification", rttEasyHandle);
    }

    int handleIndex = 0;
    while (true) {
        std::string data;
        // Pop the data from the queue synchronously
        while (!strategyToOrderManagerQueue.pop(data));

        // Send the order asynchronously
        pool.enqueue(sendOrderAsync, data, easyHandles[handleIndex % HANDLE_COUNT], false);

        handleIndex++;
    }

    *//*for (int i = 0; i < HANDLE_COUNT; i++) {
        curl_easy_cleanup(easyHandles[i]);
    }*//*

    curl_global_cleanup();
}*/

/*void sendOrderAsyncWithCurl(const std::string& data, CURL*& easyHandle, const bool isInvalidOrder) {
    curl_easy_setopt(easyHandle, CURLOPT_URL, "https://testnet.bitmex.com/api/v1/order");

    std::string orderData;
    std::string updateExchangeTimepoint;
    std::string updateReceiveTimepoint;
    std::string strategyTimepoint;

    if (!isInvalidOrder) {
        orderData = data.substr(0, data.length() - 39);
        std::cout << orderData << std::endl;
        updateExchangeTimepoint = data.substr(data.length() - 39, 13);
        updateReceiveTimepoint = data.substr(data.length() - 26, 13);
        strategyTimepoint = data.substr(data.length() - 13);
    } else {
        orderData = data;
    }

    // Get the current time_point
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    // Add 10 seconds to the current time_point
    std::chrono::system_clock::time_point tenSecondsLater = now + std::chrono::seconds (10);
    // Convert the time_point to a Unix timestamp
    std::time_t timestamp = std::chrono::system_clock::to_time_t(tenSecondsLater);
    // Convert the timestamp to a string
    std::string expires = std::to_string(timestamp);
    // Concatenate the string to be hashed
    std::string concatenatedString = verb + path + expires + orderData;
    // Calculate HMAC-SHA256
    std::string signature = CalcHmacSHA256(apiSecret, concatenatedString);
    std::string hexSignature = toHex(signature);

    if (easyHandle) {
        // Build the headers
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, ("api-key: " + apiKey).c_str());
        headers = curl_slist_append(headers, ("api-expires: " + expires).c_str());
        headers = curl_slist_append(headers, ("api-signature: " + hexSignature).c_str());
        headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
        // Add headers to the easy handle
        curl_easy_setopt(easyHandle, CURLOPT_HTTPHEADER, headers);
        // Set the POST data
        curl_easy_setopt(easyHandle, CURLOPT_POSTFIELDS, orderData.c_str());
        // Set the write callback function
        std::string response;
        curl_easy_setopt(easyHandle, CURLOPT_WRITEDATA, &response);

        system_clock::time_point submissionTimestamp = high_resolution_clock::now();
        std::string submissionTimepoint = std::to_string(duration_cast<milliseconds>(submissionTimestamp.time_since_epoch()).count());

        // Perform the request
        CURLcode res = curl_easy_perform(easyHandle);

        if (isInvalidOrder) {
            std::string responseTimestamp = std::to_string(duration_cast<milliseconds>(high_resolution_clock::now().time_since_epoch()).count());

            std::cout
            << "===========================================================================================\n"
            << "Response from exchange for unfillable order:\n" << response << "\n"
            << "RTT (ms): "
            << getTimeDifferenceInMillis(submissionTimepoint, responseTimestamp)
            << "\n===========================================================================================\n"
            << std::endl;

            curl_slist_free_all(headers);
            return;
        }

        // Check for errors
        if (res != CURLE_OK)
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        else {
            json jsonResponse = json::parse(response);
            // Access the "orderID" field
            if (jsonResponse["error"]["name"] == "RateLimitError") {
                std::cout << "Rate Limit Exceeded" << std::endl;
                std::this_thread::sleep_for(milliseconds(15000));
                sendOrderAsync(data, easyHandle, isInvalidOrder);
            }
            long exchangeExecutionTimestamp = convertTimestampToTimePoint(jsonResponse["timestamp"]);
            // Do something with the orderID
            std::cout
            << "===========================================================================================\n"
            << "NEW ORDER EXECUTED\n"
            << "Exchange to Receival (ms): "
            << getTimeDifferenceInMillis(updateExchangeTimepoint, updateReceiveTimepoint) << "      "
            << "Receival to Detection (ms): "
            << getTimeDifferenceInMillis(updateReceiveTimepoint, strategyTimepoint) << "      "
            << "Detection to Submission (ms): "
            << getTimeDifferenceInMillis(strategyTimepoint, submissionTimepoint) << "      "
            << "Submission to Execution (ms): "
            << getTimeDifferenceInMillis(submissionTimepoint, std::to_string(exchangeExecutionTimestamp)) << "      "
            << "Total Latency: " << getTimeDifferenceInMillis(updateExchangeTimepoint,std::to_string(exchangeExecutionTimestamp))
            << "      \n"
            << "Update Exch. Ts.: " << updateExchangeTimepoint << "      "
            << "Update Rec. Ts.: " << updateReceiveTimepoint << "      "
            << "Strat. Ts.: " << strategyTimepoint << "      "
            << "Submission. Ts.: " << submissionTimepoint << "      "
            << "Execution. Ts.: " << exchangeExecutionTimestamp << "      \n"
            << "Response from exchange:\n" << response
            << "\n===========================================================================================\n"
            << std::endl;
        }

        curl_slist_free_all(headers);
    }
}*/

/*void testRoundTripTimeWithCurl(const std::string& requestVerb, const std::string& requestPath, CURL*& easyHandle) {
    std::string requestUrl = "https://testnet.bitmex.com";
    requestUrl += requestPath;
    curl_easy_setopt(easyHandle, CURLOPT_URL, requestUrl.c_str());

    // Get the current time_point
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    // Add 10 seconds to the current time_point
    std::chrono::system_clock::time_point tenSecondsLater = now + std::chrono::seconds (10);
    // Convert the time_point to a Unix timestamp
    std::time_t timestamp = std::chrono::system_clock::to_time_t(tenSecondsLater);
    // Convert the timestamp to a string
    std::string expires = std::to_string(timestamp);
    // Concatenate the string to be hashed
    std::string concatenatedString = requestVerb + requestPath + expires;
    // Calculate HMAC-SHA256
    std::string signature = CalcHmacSHA256(apiSecret, concatenatedString);
    std::string hexSignature = toHex(signature);
    if (easyHandle) {
        // Build the headers
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, ("api-key: " + apiKey).c_str());
        headers = curl_slist_append(headers, ("api-expires: " + expires).c_str());
        headers = curl_slist_append(headers, ("api-signature: " + hexSignature).c_str());
        *//*headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");*//*
        // Add headers to the easy handle
        curl_easy_setopt(easyHandle, CURLOPT_HTTPHEADER, headers);
        // Set the write callback function
        std::string response;
        curl_easy_setopt(easyHandle, CURLOPT_WRITEDATA, &response);

        std::string requestTimepoint = std::to_string(duration_cast<milliseconds>(high_resolution_clock::now().time_since_epoch()).count());
        // Perform the request
        CURLcode res = curl_easy_perform(easyHandle);
        // Check for errors
        if (res != CURLE_OK)
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        else {
            std::string responseTimestamp = std::to_string(duration_cast<milliseconds>(high_resolution_clock::now().time_since_epoch()).count());

            std::cout
                    << "===========================================================================================\n"
                    << "RESPONSE FOR ADDITIONAL REQUEST RECEIVED\n"
                    << "RTT (ms): "
                    << getTimeDifferenceInMillis(requestTimepoint, responseTimestamp) << "      \n"
                    << "Request Ts.: " << requestTimepoint << "      "
                    << "Response Ts.: " << responseTimestamp
                    << "\n===========================================================================================\n"
                    << std::endl;
        }

        curl_slist_free_all(headers);
    }
}*/

size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
    size_t total_size = size * nmemb;
    /*std::cout << (char*)contents << std::endl;*/
    output->append((char*)contents, total_size);
    return total_size;
}

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




