#include "OrderManager.hpp"

using json = nlohmann::json;
using namespace std::chrono;

size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
    size_t total_size = size * nmemb;
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

    CURL* curl;
    CURLcode res;
    std::string apiKey = "urLbZmnnFvTiIEgtUEOPe15k";
    std::string apiSecret = "7fdNCO8Jol1LW3iD67dTcSXFjqYY-LRi1Uvpcevr7Mg6IwFm";

    curl_global_init(CURL_GLOBAL_DEFAULT);

    curl = curl_easy_init();
    if (curl) {
        // Set the BitMEX API endpoint
        curl_easy_setopt(curl, CURLOPT_URL, "https://testnet.bitmex.com/api/v1/order");

        while (true) {
            // Set the necessary POST data
            std::string data;
            while (!strategyToOrderManagerQueue.pop(data));

            std::string strategyTimepoint = data.substr(data.length() - 13);
            std::string orderData = data.substr(0, data.length() - 13);

            std::cout << "Order data: " << orderData << std::endl;
            std::cout << "Strategy timepoint: " << strategyTimepoint << std::endl;
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
            // Print the hexSignature
            // std::cout << "Signature: " << hexSignature << std::endl;
            // Build the headers
            struct curl_slist* headers = NULL;
            headers = curl_slist_append(headers, ("api-key: " + apiKey).c_str());
            headers = curl_slist_append(headers, ("api-expires: " + expires).c_str());
            headers = curl_slist_append(headers, ("api-signature: " + hexSignature).c_str());
            headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            // Set the POST data and other options
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, orderData.c_str());
            // Declare 'response' here
            std::string response;
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
            // Perform the request
            res = curl_easy_perform(curl);
            // Check for errors
            if (res != CURLE_OK)
                fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
            else {
                json jsonResponse = json::parse(response);

                // Access the "orderID" field
                auto exchangeExecutionTimestamp = convertTimestampToTimePoint(jsonResponse["timestamp"]);

                // Do something with the orderID
                std::cout << "Exchange execution timestamp: " << duration_cast<milliseconds>(exchangeExecutionTimestamp.time_since_epoch()).count() << std::endl;
                std::cout << "Response from exchange:\n" << response << std::endl;
                std::cout << "Strategy to Order Execution Latency: " << duration_cast<milliseconds>(exchangeExecutionTimestamp.time_since_epoch() - std::chrono::milliseconds(std::stoll(strategyTimepoint))).count() << " (ms)\n" << std::endl;
            }
            // Cleanup
            curl_slist_free_all(headers);
        }
        
        curl_easy_cleanup(curl);
    }    

    curl_global_cleanup();
}
