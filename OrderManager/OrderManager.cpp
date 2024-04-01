#include "OrderManager.hpp"

using json = nlohmann::json;
using namespace std::chrono;

static const std::string apiKey = "63ObNQpYqaCVrjTuBbhgFm2p";
static const std::string apiSecret = "D2OBzpfW-i6FfgmqGnrhpYqKPrxCvIYnu5KZKsZQW_09XkF-";
static const std::string verb = "POST";
static const std::string path = "/api/v1/order";

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

void orderManager(int cpu, SPSCQueue<std::string>& strategyToOrderManagerQueue) {
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
            curl_easy_setopt(easyHandles[i], CURLOPT_URL, "https://testnet.bitmex.com/api/v1/order");
            curl_easy_setopt(easyHandles[i], CURLOPT_WRITEFUNCTION, WriteCallback);
            /*curl_easy_setopt(easyHandle, CURLOPT_VERBOSE, 1L);*/
        }
    }

    // Initially send unfillable orders to the exchange to make the submission-execution latency for subsequent orders lower
    static const std::string unfillableOrderData = "symbol=XBTUSDT&side=Sell&orderQty=0&ordType=Market";
    for (int i = 0; i < HANDLE_COUNT; i++) {
        pool.enqueue(sendOrderAsync, unfillableOrderData, easyHandles[i % HANDLE_COUNT], true);
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

    /*for (int i = 0; i < HANDLE_COUNT; i++) {
        curl_easy_cleanup(easyHandles[i]);
    }*/

    curl_global_cleanup();
}

void sendOrderAsync(const std::string& data, CURL*& easyHandle, bool unfillableOrder) {
    std::string orderData;
    std::string updateExchangeTimepoint;
    std::string updateReceiveTimepoint;
    std::string strategyTimepoint;

    if (!unfillableOrder) {
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

        if (unfillableOrder) {
            std::cout
            << "===========================================================================================\n"
            << "Response from exchange for unfillable order:\n" << response
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
}

