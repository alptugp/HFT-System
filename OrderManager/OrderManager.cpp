#include "OrderManager.hpp"

using json = nlohmann::json;
using namespace std::chrono;


size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
    size_t total_size = size * nmemb;
    std::cout << (char*)contents << std::endl;
    output->append((char*)contents, total_size);
    json jsonResponse = json::parse(*output);
    std::cout << jsonResponse["timestamp"] << std::endl;

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
    int i = 0;

    // Initialize libcurl
    curl_global_init(CURL_GLOBAL_ALL);

    // Create multi handle
    CURLM* multiHandle = curl_multi_init();

    // Loop indefinitely
    while (true) {
        std::string data;
        // Pop the data from the queue synchronously
        while (!strategyToOrderManagerQueue.pop(data));

        // Send the order asynchronously
        sendOrderAsync(multiHandle, data);

        i++;
        if (i == 3) {
            break;
        }
    }

    int still_running = 0;
    curl_multi_perform(multiHandle, &still_running);
    while(still_running) {
        CURLMcode mc = curl_multi_perform(multiHandle, &still_running);

        if(still_running) /*wait for activity, timeout or "nothing"*/
            mc = curl_multi_poll(multiHandle, NULL, 0, 1000, NULL);

        if(mc)
            break;
    }

    // Clean up
    curl_multi_cleanup(multiHandle);

    curl_global_cleanup();
}

void sendOrderAsync(CURLM* multiHandle, const std::string& data) {
    std::string apiKey = "63ObNQpYqaCVrjTuBbhgFm2p";
    std::string apiSecret = "D2OBzpfW-i6FfgmqGnrhpYqKPrxCvIYnu5KZKsZQW_09XkF-";

    CURL* easyHandle;

    std::string orderData = data.substr(0, data.length() - 39);
    std::cout << orderData << std::endl;
    std::string updateExchangeTimepoint = data.substr(data.length() - 39, 13);
    std::string updateReceiveTimepoint = data.substr(data.length() - 26, 13);
    std::string strategyTimepoint = data.substr(data.length() - 13);

    /*std::cout << "Order data: " << orderData << std::endl;
            std::cout << "Strategy timepoint: " << strategyTimepoint << std::endl;
            std::cout << "Update Exchange timepoint: " << updateExchangeTimepoint << std::endl;*/
    // Get the current time_point
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    // Add 1 hour to the current time_point
    std::chrono::system_clock::time_point oneHourLater = now + std::chrono::hours(1);
    // Convert the time_point to a Unix timestamp
    std::time_t timestamp = std::chrono::system_clock::to_time_t(oneHourLater);
    // Convert the timestamp to a string
    std::string expires = std::to_string(timestamp);

    // std::cout << "expires: " << expires << std::endl;
    std::string verb = "POST";
    std::string path = "/api/v1/order";
    // Concatenate the string to be hashed
    std::string concatenatedString = verb + path + expires + orderData;

    // Calculate HMAC-SHA256
    std::string signature = CalcHmacSHA256(apiSecret, concatenatedString);
    std::string hexSignature = toHex(signature);

    easyHandle = curl_easy_init();
    if (easyHandle) {

        curl_easy_setopt(easyHandle, CURLOPT_URL, "https://testnet.bitmex.com/api/v1/order");
        /*curl_easy_setopt(easyHandle, CURLOPT_VERBOSE, 1L);*/

        // Build the headers
        struct curl_slist* headers = NULL;
        headers = curl_slist_append(headers, ("api-key: " + apiKey).c_str());
        headers = curl_slist_append(headers, ("api-expires: " + expires).c_str());
        headers = curl_slist_append(headers, ("api-signature: " + hexSignature).c_str());
        headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");

        // Add headers to the easy handle
        curl_easy_setopt(easyHandle, CURLOPT_HTTPHEADER, headers);

        // Set the POST data
        curl_easy_setopt(easyHandle, CURLOPT_POSTFIELDS, orderData.c_str());
        system_clock::time_point submissionTimestamp = high_resolution_clock::now();
        std::string submissionTimepoint = std::to_string(
                duration_cast<milliseconds>(submissionTimestamp.time_since_epoch()).count());

        // Set the write callback function
        std::string response;
        curl_easy_setopt(easyHandle, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(easyHandle, CURLOPT_WRITEDATA, &response);

        // Add the easy handle to the multi handle
        curl_multi_add_handle(multiHandle, easyHandle);

        std::cout
                << "===========================================================================================\n"
                << "NEW ORDER EXECUTED\n"
                << "Exchange to Receival (ms): "
                << getTimeDifferenceInMillis(updateExchangeTimepoint, updateReceiveTimepoint) << "      "
                << "Receival to Detection (ms): "
                << getTimeDifferenceInMillis(updateReceiveTimepoint, strategyTimepoint) << "      "
                << "Detection to Submission (ms): "
                << getTimeDifferenceInMillis(strategyTimepoint, submissionTimepoint) << "      \n"

                << "Update Exch. Ts.: " << updateExchangeTimepoint << "      "
                << "Update Rec. Ts.: " << updateReceiveTimepoint << "      "
                << "Strat. Ts.: " << strategyTimepoint << "      "
                << "Submission. Ts.: " << submissionTimepoint << "      \n"
                << "\nResponse from exchange:\n" << response
                << std::endl;

        /*curl_slist_free_all(headers);*/
        /*curl_easy_cleanup(easyHandle);*/
    }
}

