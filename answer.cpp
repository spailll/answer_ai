#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <curl/curl.h>
#include "nlohmann/json.hpp"
#include <vector>
#include <unistd.h>
#include <sys/wait.h>
#include <getopt.h>
#include <iterator>

using json = nlohmann::json;

std::string base64_encode(const std::string &in) {
    std::string out;
    int val = 0, valb = -6;
    for (unsigned char c : in) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

std::string fileToBase64(const std::string &filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) throw std::runtime_error("Unable to open file.");

    std::vector<char> fileBuffer((std::istreambuf_iterator<char>(file)), {});
    return base64_encode(std::string(fileBuffer.begin(), fileBuffer.end()));
}

bool deleteFile(const std::string &filename) {
    if (std::remove(filename.c_str()) != 0) {
        std::cerr << "Error deleting file: " << filename << std::endl;
        return false;
    }
    return true;
}

std::string takeScreenshot() {
    std::string filename = "screenshot.png";
    pid_t pid = fork();
    if (pid == 0) {
        execlp("gnome-screenshot", "gnome-screenshot", "-f", filename.c_str(), NULL);
        exit(1);  // If exec fails
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            std::string base64_image = fileToBase64(filename);
            std::cout << "Screenshot taken successfully." << std::endl;
            deleteFile(filename);
            return base64_image;
        } else {
            throw std::runtime_error("Failed to take screenshot");
        }
    } else {
        throw std::runtime_error("Fork failed");
    }
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

std::string readPromptFile(const std::string &filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Unable to open prompt file.");
    }
    std::ostringstream oss;
    oss << file.rdbuf();
    return oss.str();
}

size_t WriteCallback(void *contents, size_t size, size_t nmemb, std::string *userp) {
    size_t totalSize = size * nmemb;
    userp->append((char*)contents, totalSize);
    return totalSize;
}

std::string generateResponseFromOpenAI(const std::string &api_key, const std::string &model, const std::string &prompt, const std::string &base64_image) {
    CURL *curl;
    CURLcode res;
    std::string readBuffer;

    if (prompt.empty()) {
        throw std::runtime_error("Prompt is empty.");
    }
    if (model.empty()) {
        throw std::runtime_error("Model is empty.");
    }

    json payload;
    payload["model"] = model;
    payload["max_tokens"] = 300;
    payload["messages"] = json::array({
        {
            {"role", "user"},
            {"content", json::array({
                {{"type", "text"}, {"text", prompt}},
                {{"type", "image_url"}, {"image_url", {{"url", "data:image/png;base64," + base64_image}}}}
            })}
        }
    });

    std::string payload_str = payload.dump();

    curl = curl_easy_init();
    
    if(curl) {
        std::string api_url = "https://api.openai.com/v1/chat/completions";
        
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, ("Authorization: Bearer " + api_key).c_str());
        headers = curl_slist_append(headers, "Content-Type: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, api_url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload_str.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

        res = curl_easy_perform(curl);

        if(res != CURLE_OK) {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
        } else {
            json response = json::parse(readBuffer);
            if (response.contains("error")) {
                std::cerr << "Error: " << response["error"]["message"] << std::endl;
            } else {
                return response["choices"][0]["message"]["content"].get<std::string>();
            }
        }

        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
    } else {
        throw std::runtime_error("Failed to initialize curl.");
    }

    return "";
}

void printHelp() {
    std::cout << "Usage: answer_ai [options]\n"
              << "Options:\n"
              << "  -p <name>       Use the content of the file with the specified name as the text prompt\n"
              << "  -m <model>      Specify the model to use (default is gpt-4)\n"
              << "  -h              Display this help message\n";
}

int main(int argc, char *argv[]) {
    std::string prompt = "First, recite the question (and answer choices, if any) detailed in the following image. Then, answer the question to the best of your ability: ";
    std::string model = "gpt-4-turbo";
    std::string promptFile;

    int opt;
    while ((opt = getopt(argc, argv, "p:m:h")) != -1) {
        switch (opt) {
            case 'p':
                prompt = readPromptFile(optarg);
                break;
            case 'm':
                model = optarg;
                break;
            case 'h':
                printHelp();
                return 0;
            default:
                printHelp();
                return 1;
        }
    }

    try {
        std::string api_key = readApiKey("OPENAI_API_KEY");

        // Taking screenshot
        std::string base64_image = takeScreenshot();

        // Generating response from OpenAI
        std::string response = generateResponseFromOpenAI(api_key, model, prompt, base64_image);
        std::cout << "Generated Response: " << response << std::endl;

    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }

    return 0;
}