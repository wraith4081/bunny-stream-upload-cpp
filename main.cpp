#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <filesystem>
#include <regex>
#include <curl/curl.h>
#include "./json.hpp" // nlohmann/json
#include <chrono>

using json = nlohmann::json;

// Helper function to write the response data
static size_t WriteCallback(void *contents, size_t size, size_t nmemb, std::string *s)
{
    size_t newLength = size * nmemb;
    try
    {
        s->append((char *)contents, newLength);
    }
    catch (std::bad_alloc &e)
    {
        // handle memory problem
        return 0;
    }
    return newLength;
}

bool createVideo(int libraryId, const std::string &title, const std::string &collectionId, const std::string &accessKey, std::string &outGuid)
{
    CURL *curl;
    CURLcode res;
    std::string readBuffer;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();

    if (curl)
    {
        std::string url = "https://video.bunnycdn.com/library/" + std::to_string(libraryId) + "/videos";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POST, 1L);

        // Construct JSON data
        json dataJson = {
            {"title", title},
            {"collectionId", collectionId}};
        std::string postData = dataJson.dump();

        // Set headers
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "accept: application/json");
        headers = curl_slist_append(headers, "content-type: application/*+json");
        headers = curl_slist_append(headers, ("AccessKey: " + accessKey).c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        // Set data to send
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());

        // Set callback function to receive response data
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

        // Perform the request, res will get the return code
        res = curl_easy_perform(curl);

        // Check for errors
        if (res != CURLE_OK)
        {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        }
        else
        {
            // Parse JSON response
            auto jsonResponse = json::parse(readBuffer);
            if (jsonResponse.contains("guid"))
            {
                outGuid = jsonResponse["guid"];
                // Be sure to clean up
                curl_easy_cleanup(curl);
                curl_global_cleanup();
                return true;
            }
        }

        // Be sure to clean up
        curl_easy_cleanup(curl);
    }

    curl_global_cleanup();
    return false;
}

// Helper function to clear the line by overwriting with spaces
void clearLine(int length)
{
    for (int i = 0; i < length; ++i)
    {
        std::cout << " ";
    }
    std::cout << "\r";
}

// Progress callback function prototype for libcurl
int progress_callback(void *ptr, curl_off_t totalToDownload, curl_off_t nowDownloaded,
                      curl_off_t totalToUpload, curl_off_t nowUploaded)
{
    // Constants
    const int barWidth = 70;   // Width of the progress bar
    static int lastLength = 0; // Keep track of the last line's length

    if (totalToUpload > 0)
    {
        double percent = static_cast<double>(nowUploaded) / static_cast<double>(totalToUpload);
        int pos = barWidth * percent;

        std::cout << "[";

        for (int i = 0; i < barWidth; ++i)
        {
            if (i < pos)
                std::cout << "=";
            else if (i == pos)
                std::cout << ">";
            else
                std::cout << " ";
        }

        // Print percentage next to the progress bar
        std::stringstream ss;
        ss << "] " << int(percent * 100.0) << " %";
        std::string progressBar = ss.str();
        std::cout << progressBar;

        // Calculate current line length
        int currentLength = barWidth + progressBar.length() + 1; // +1 for '['

        // Clear remaining characters if the current bar is shorter than the last one
        if (currentLength < lastLength)
        {
            clearLine(lastLength - currentLength);
        }

        std::cout << "\r" << std::flush; // Move cursor to the beginning of the line

        // Update last length
        lastLength = currentLength;
    }

    // Similar logic could be applied for download progress if needed

    return 0; // Continue the operation
}

bool uploadVideo(int libraryId, const std::string &guid, const std::string &accessKey, const std::string &filePath)
{
    CURL *curl;
    CURLcode res;

    // Open the file using C's fopen
    FILE *file = fopen(filePath.c_str(), "rb"); // Open in binary mode
    if (!file)
    {
        std::cerr << "Failed to open file: " << filePath << std::endl;
        return false;
    }

    curl = curl_easy_init();
    if (curl)
    {
        std::string url = "https://video.bunnycdn.com/library/" + std::to_string(libraryId) + "/videos/" + guid;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);

        // Set headers
        struct curl_slist *headers = nullptr;
        headers = curl_slist_append(headers, "Accept: application/json");
        headers = curl_slist_append(headers, ("AccessKey: " + accessKey).c_str());
        headers = curl_slist_append(headers, "Content-Type: application/octet-stream");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        // Enable progress function
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, nullptr);

        // Determine the file size
        fseek(file, 0, SEEK_END);
        long fileSize = ftell(file);
        rewind(file); // Or fseek(file, 0, SEEK_SET);
        curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)fileSize);

        // Set file to upload
        curl_easy_setopt(curl, CURLOPT_READDATA, file);

        // Perform the upload
        res = curl_easy_perform(curl);

        // Cleanup
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        fclose(file); // Close the file

        if (res != CURLE_OK)
        {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
            return false;
        }

        return true; // Success
    }

    return false;
}

int main(int argc, char *argv[])
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL *curl = curl_easy_init();
    if (!curl)
    {
        std::cerr << "CURL initialization failed" << std::endl;
        return 1;
    }

    std::map<std::string, std::string> args;

    // Simple command line argument parsing. Not as robust as yargs.
    for (int i = 1; i < argc; i += 2)
    {
        if (i + 1 < argc)
        { // Check that we haven't finished parsing prematurely
            args[argv[i]] = argv[i + 1];
        }
    }

    std::string file = args["--file"];
    std::string key = args["--key"];
    std::string library = args["--library"];
    std::string collection = args["--collection"];
    std::string title = args["--title"];

    // Validate the inputs similarly as the TypeScript version.
    if (file.empty() || key.empty() || library.empty() || collection.empty() || title.empty())
    {
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        std::cerr << "Missing required arguments." << std::endl;
        return 1; // Signal error
    }

    // Convert library ID string to integer.
    int libraryId = std::stoi(library);
    if (libraryId <= 0)
    {
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        std::cerr << "Invalid library ID." << std::endl;
        return 1;
    }

    // Check for valid UUID formats if necessary.
    // Example omitted for brevity.

    std::string filePath = std::filesystem::absolute(file).string();

    if (!std::filesystem::exists(filePath))
    {
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        std::cerr << "File does not exist: " << filePath << std::endl;
        return 1;
    }

    std::cout << "Reading file: " << filePath << std::endl;

    std::string guid;
    bool success = createVideo(libraryId, title, collection, key, guid);
    if (!success)
    {
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        std::cerr << "Could not create video." << std::endl;
        return 1;
    }

    success = uploadVideo(libraryId, guid, key, filePath);
    if (!success)
    {
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        std::cerr << "Could not upload video." << std::endl;
        return 1;
    }

    std::cout << "Video uploaded: " << guid << std::endl;

    curl_easy_cleanup(curl);
    curl_global_cleanup();
    return 0;
}
