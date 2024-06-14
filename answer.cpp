#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <vector>
#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>

using json = nlohmann::json;

std::string encodeBase64(const std::string &filename) {
    std::ifstream file(filename, std::ios::binary);
    std::ostringstream oss;
    oss << file.rdbuf();
    std::string file_content = oss.str();
    std::string base64_encoded;
    const char *base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    int val = 0, valb = -6;
    for (unsigned char c : file_content) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            base64_encoded.push_back(base64_chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) base64_encoded.push_back(base64_chars[((val << 8) >> (valb + 8)) & 0x3F]);
    while (base64_encoded.size() % 4) base64_encoded.push_back('=');
    return base64_encoded;
}

std::string takeScreenshot(const std::string &filename) {
    pid_t pid = fork();
    if (pid == 0) {
        execlp("scrot", "scrot", filename.c_str(), NULL);
        exit(1);  // If exec fails
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            return filename;
        } else {
            throw std::runtime_error("Failed to take screenshot");
        }
    } else {
        throw std::runtime_error("Fork failed");
    }
}

size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

std::string readApiKey(const std::string &filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Unable to open API key file.");
    }
    std::string api_key;
    std::getline(file, api_key);
    file.close();
    return api_key;
}

void sendToOpenAI(const std::string &api_key, const std::string &image_base64, const std::string &prompt) {
    CURL *curl;
    CURLcode res;
    curl = curl_easy_init();

    if(curl) {
        std::string readBuffer;
        std::string api_url = "https://api.openai.com/v1/images/generations";

        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, ("Authorization: Bearer " + api_key).c_str());
        headers = curl_slist_append(headers, "Content-Type: application/json");

        json root;
        root["prompt"] = prompt;
        root["image"] = image_base64;
        root["n"] = 1;
        root["size"] = "1024x1024";

        std::string data = root.dump();

        curl_easy_setopt(curl, CURLOPT_URL, api_url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

        res = curl_easy_perform(curl);

        if(res != CURLE_OK)
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
        else
            std::cout << "Response from OpenAI: " << readBuffer << std::endl;

        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
    }
}

int main() {
    try {
        std::string api_key = readApiKey("OPENAI_API_KEY");
        std::string prompt = "Please identify the question(s) on the screen and answer them to the best of your ability.";
        std::string screenshot_file = "screenshot.png";

        std::string filename = takeScreenshot(screenshot_file);
        std::string image_base64 = encodeBase64(filename);

        sendToOpenAI(api_key, image_base64, prompt);
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }

    return 0;
}
