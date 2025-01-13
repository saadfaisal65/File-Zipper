#include <iostream>
#include <fstream>
#include <queue>
#include <unordered_map>
#include <string>
#include <filesystem>
#include <sstream>
#include <ctime>
#include <chrono>
#include <map>
#include <algorithm>

#include <GL/glew.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <Windows.h>
#include <ShObjIdl.h>

using namespace std;
namespace fs = std::filesystem;

// Node structure for Huffman tree
class Node
{
public:
    char data;
    unsigned freq;
    Node *left;
    Node *right;

    Node(char data, unsigned freq) : data(data), freq(freq), left(NULL), right(NULL) {}
};

// Comparison function for priority queue
class Compare
{
public:
    bool operator()(Node *l, Node *r)
    {
        return l->freq > r->freq;
    }
};

class FrequencyCounter
{
private:
    map<char, int> frequencyMap;
    chrono::system_clock::time_point timestamp;

public:
    void countFrequencies(const vector<char> &data)
    {
        frequencyMap.clear();
        timestamp = chrono::system_clock::now();
        for (char c : data)
        {
            frequencyMap[c]++;
        }
    }

    const map<char, int> &getFrequencyMap() const
    {
        return frequencyMap;
    }

    void setFrequencyMap(const map<char, int> &freq)
    {
        frequencyMap = freq;
        timestamp = chrono::system_clock::now();
    }

    chrono::system_clock::time_point getTimestamp() const
    {
        return timestamp;
    }
};

class HuffmanCoding
{
private:
    Node *root;
    unordered_map<char, string> huffmanCode;
    int bitCount;
    char byte;
    string originalFileExtension;
    chrono::system_clock::time_point lastOperation;

    // Helper function to generate Huffman codes
    void generateCodes(Node *node, const string &str)
    {
        if (!node)
            return;

        if (node->data != '\0')
        {
            huffmanCode[node->data] = str;
        }

        generateCodes(node->left, str + "0");
        generateCodes(node->right, str + "1");
    }

    // Helper function to write bits to file
    void writeBits(ofstream &outFile, const string &bits)
    {
        for (char bit : bits)
        {
            byte = (byte << 1) | (bit - '0');
            bitCount++;

            if (bitCount == 8)
            {
                outFile.put(byte);
                bitCount = 0;
                byte = 0;
            }
        }
    }

    // Helper function to read bits from file
    string readBits(ifstream &inFile)
    {
        string bits;
        char byte;
        while (inFile.get(byte))
        {
            for (int i = 7; i >= 0; --i)
            {
                bits += ((byte >> i) & 1) ? '1' : '0';
            }
        }
        return bits;
    }

    void cleanup(Node *node)
    {
        if (node)
        {
            cleanup(node->left);
            cleanup(node->right);
            delete node;
        }
    }

    bool readOriginalExtension(ifstream &inFile, string &ext)
    {
        size_t extLen;
        inFile.read(reinterpret_cast<char *>(&extLen), sizeof(extLen));
        if (extLen > 10)
            return false; // Sanity check

        ext.resize(extLen);
        inFile.read(&ext[0], extLen);
        return true;
    }

    // Add padding info to compressed file
    void writePadding(ofstream &outFile, int padding)
    {
        outFile.write(reinterpret_cast<const char *>(&padding), sizeof(padding));
    }

    // Read padding info from compressed file
    int readPadding(ifstream &inFile)
    {
        int padding;
        inFile.read(reinterpret_cast<char *>(&padding), sizeof(padding));
        return padding;
    }

    // Add new helper method for bit processing
    void processBit(bool bit, Node *&current, ofstream &outFile, size_t &processedChars)
    {
        current = bit ? current->right : current->left;
        if (current && current->data != '\0')
        {
            outFile.put(current->data);
            processedChars++;
            current = root;
        }
    }

    // Add method to write frequency table
    void writeFrequencyTable(ofstream &outFile, const unordered_map<char, int> &frequency)
    {
        size_t freqSize = frequency.size();
        outFile.write(reinterpret_cast<const char *>(&freqSize), sizeof(freqSize));

        for (const auto &pair : frequency)
        {
            outFile.write(&pair.first, sizeof(char));
            outFile.write(reinterpret_cast<const char *>(&pair.second), sizeof(int));
        }
    }

    // Add method to read frequency table
    bool readFrequencyTable(ifstream &inFile, unordered_map<char, int> &frequency, size_t &totalChars)
    {
        size_t freqSize;
        inFile.read(reinterpret_cast<char *>(&freqSize), sizeof(freqSize));
        if (freqSize == 0 || freqSize > 256)
            return false;

        frequency.clear();
        totalChars = 0;

        for (size_t i = 0; i < freqSize; i++)
        {
            char ch;
            int freq;
            inFile.read(&ch, sizeof(char));
            inFile.read(reinterpret_cast<char *>(&freq), sizeof(int));
            if (freq <= 0)
                return false;
            frequency[ch] = freq;
            totalChars += freq;
        }
        return true;
    }

    void processBits(char byteRead, Node *&current, ofstream &outFile, size_t &processedChars, size_t totalChars, bool isLastByte, int padding)
    {
        int bitsToProcess = isLastByte ? (8 - padding) : 8;

        for (int i = 7; i >= (8 - bitsToProcess); --i)
        {
            if (processedChars >= totalChars)
                break;

            bool bit = (byteRead >> i) & 1;
            current = bit ? current->right : current->left;

            if (current && current->data != '\0')
            {
                outFile.put(current->data);
                processedChars++;
                current = root; // Reset to root after writing character
            }
        }
    }

    // Add this helper method to get file size
    size_t getFileSize(ifstream &file)
    {
        streampos currentPos = file.tellg();
        file.seekg(0, ios::end);
        size_t size = file.tellg();
        file.seekg(currentPos);
        return size;
    }

public:
    HuffmanCoding() : root(NULL), bitCount(0), byte(0) {}

    ~HuffmanCoding()
    {
        cleanup(root);
    }

    // Build Huffman tree from character frequencies
    void buildTree(const unordered_map<char, int> &frequency)
    {
        priority_queue<Node *, vector<Node *>, Compare> pq;

        // Create sorted vector of frequency pairs
        vector<pair<char, int>> sortedFreq;
        for (const auto &pair : frequency)
        {
            sortedFreq.push_back(pair);
        }

        // Sort by frequency first, then by character for ties
        sort(sortedFreq.begin(), sortedFreq.end(),
             [](const pair<char, int> &a, const pair<char, int> &b)
             {
                 if (a.second == b.second)
                 {
                     return a.first < b.first; // Sort by character if frequencies are equal
                 }
                 return a.second < b.second; // Sort by frequency
             });

        // Create nodes in sorted order
        for (const auto &pair : sortedFreq)
        {
            pq.push(new Node(pair.first, pair.second));
        }

        while (pq.size() > 1)
        {
            Node *right = pq.top();
            pq.pop(); // Higher frequency goes right
            Node *left = pq.top();
            pq.pop(); // Lower frequency goes left

            Node *internal = new Node('\0', left->freq + right->freq);
            internal->left = left;
            internal->right = right;
            pq.push(internal);
        }

        root = pq.empty() ? nullptr : pq.top();
    }

    // Generate Huffman codes
    void generateHuffmanCodes()
    {
        if (root)
        {
            generateCodes(root, "");
        }
        else
        {
            cerr << "Error: Huffman tree is empty. Cannot generate codes.\n";
        }
    }

    // Add these new methods
    void huffer(const map<char, int> &frequencyMap)
    {
        lastOperation = chrono::system_clock::now();
        unordered_map<char, int> freq(frequencyMap.begin(), frequencyMap.end());
        buildTree(freq);
        generateHuffmanCodes();
    }

    void deHuffer(const map<char, int> &frequencyMap)
    {
        lastOperation = chrono::system_clock::now();
        unordered_map<char, int> freq(frequencyMap.begin(), frequencyMap.end());
        buildTree(freq);
    }

    // Compress a file

    bool compressFile(const string &inputFile, const string &outputFile)
    {
        try
        {
            ifstream inFile(inputFile, ios::binary);
            if (!inFile)
                return false;

            ofstream outFile(outputFile, ios::binary);
            if (!outFile)
                return false;

            // Write file extension
            fs::path inputPath(inputFile);
            originalFileExtension = inputPath.extension().string();
            size_t extLen = originalFileExtension.length();
            outFile.write(reinterpret_cast<const char *>(&extLen), sizeof(extLen));
            outFile.write(originalFileExtension.c_str(), extLen);

            // Calculate frequencies
            vector<char> fileContent;
            char ch;
            while (inFile.get(ch))
            {
                fileContent.push_back(ch);
            }

            FrequencyCounter counter;
            counter.countFrequencies(fileContent);

            // Write frequency table
            const auto &freqMap = counter.getFrequencyMap();
            writeFrequencyTable(outFile, unordered_map<char, int>(freqMap.begin(), freqMap.end()));

            // Use huffer with timestamp
            huffer(freqMap);

            // Write data size
            size_t dataSize = fileContent.size();
            outFile.write(reinterpret_cast<const char *>(&dataSize), sizeof(dataSize));

            // Reset bit counting
            bitCount = 0;
            byte = 0;

            // Compress data
            for (char c : fileContent)
            {
                if (huffmanCode.find(c) == huffmanCode.end())
                    return false;
                writeBits(outFile, huffmanCode[c]);
            }

            // Handle final padding
            if (bitCount > 0)
            {
                int padding = 8 - bitCount;
                byte <<= padding;
                outFile.put(byte);
                outFile.write(reinterpret_cast<const char *>(&padding), sizeof(padding));
            }
            else
            {
                int padding = 0;
                outFile.write(reinterpret_cast<const char *>(&padding), sizeof(padding));
            }

            return true;
        }
        catch (const exception &e)
        {
            cerr << "Compression error: " << e.what() << endl;
            return false;
        }
    }

    // Decompress a file

    bool decompressFile(const string &inputFile, const string &outputFile)
    {
        try
        {
            ifstream inFile(inputFile, ios::binary);
            if (!inFile)
                return false;

            // Read and validate file header
            string originalExt;
            if (!readOriginalExtension(inFile, originalExt))
                return false;

            // Create output file with directories
            fs::path outputPath(outputFile);
            fs::create_directories(outputPath.parent_path());
            ofstream outFile(outputFile, ios::binary);
            if (!outFile)
                return false;

            // Read frequency table and validate
            unordered_map<char, int> frequency;
            size_t totalChars = 0;
            if (!readFrequencyTable(inFile, frequency, totalChars))
                return false;

            // Convert to map for FrequencyCounter
            map<char, int> freqMap(frequency.begin(), frequency.end());
            FrequencyCounter counter;
            counter.setFrequencyMap(freqMap);

            // Use deHuffer with timestamp
            deHuffer(freqMap);

            // Read original data size
            size_t originalSize;
            inFile.read(reinterpret_cast<char *>(&originalSize), sizeof(originalSize));
            if (originalSize != totalChars)
                return false;

            // Build Huffman tree
            buildTree(frequency);
            if (!root)
                return false;

            // Read padding information
            streampos dataStart = inFile.tellg();
            size_t fileSize = getFileSize(inFile);
            inFile.seekg(-sizeof(int), ios::end);
            int padding;
            inFile.read(reinterpret_cast<char *>(&padding), sizeof(padding));
            if (padding < 0 || padding > 7)
                return false;

            // Process compressed data
            inFile.seekg(dataStart);
            Node *current = root;
            size_t processedChars = 0;
            size_t dataSize = fileSize - dataStart - sizeof(int);
            char byteRead;

            // Process bytes directly without buffering for accurate ordering
            while (inFile.get(byteRead) && processedChars < totalChars)
            {
                bool isLastByte = (inFile.tellg() == (fileSize - sizeof(int)));
                processBits(byteRead, current, outFile, processedChars, totalChars,
                            isLastByte, isLastByte ? padding : 0);
            }

            return processedChars == totalChars;
        }
        catch (const exception &e)
        {
            cerr << "Decompression error: " << e.what() << endl;
            return false;
        }
    }

    // Get the Huffman codes
    const unordered_map<char, string> &getHuffmanCodes() const
    {
        return huffmanCode;
    }
};

// File dialog helper function
string openFileDialog(bool save = false)
{
    string filename;
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (SUCCEEDED(hr))
    {
        IFileDialog *pFileDialog;
        if (save)
        {
            hr = CoCreateInstance(CLSID_FileSaveDialog, NULL, CLSCTX_ALL, IID_PPV_ARGS(&pFileDialog));
        }
        else
        {
            hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_PPV_ARGS(&pFileDialog));
        }

        if (SUCCEEDED(hr))
        {
            hr = pFileDialog->Show(NULL);
            if (SUCCEEDED(hr))
            {
                IShellItem *pItem;
                hr = pFileDialog->GetResult(&pItem);
                if (SUCCEEDED(hr))
                {
                    PWSTR pszFilePath;
                    hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
                    if (SUCCEEDED(hr))
                    {
                        wstring ws(pszFilePath);
                        filename = string(ws.begin(), ws.end());
                        CoTaskMemFree(pszFilePath);
                    }
                    pItem->Release();
                }
            }
            pFileDialog->Release();
        }
        CoUninitialize();
    }
    return filename;
}

// Add this structure after existing classes
struct FileRecord
{
    string originalPath;
    string compressedPath;
    string decompressPath;
    string fileType;
    time_t timestamp;
};

class FileTracker
{
private:
    const string logFile = "compression_history.txt";

    string getCurrentTimestamp()
    {
        time_t now = time(nullptr);
        char buffer[26];
        ctime_s(buffer, sizeof(buffer), &now);
        string timestamp(buffer);
        return timestamp.substr(0, timestamp.length() - 1); // Remove newline
    }

    void ensureDirectoriesExist()
    {
        fs::create_directories("C:\\Users\\Public\\Compressed Files");
        fs::create_directories("C:\\Users\\Public\\Decompressed Files");
    }

public:
    FileTracker()
    {
        ensureDirectoriesExist();
    }

    void saveRecord(const FileRecord &record)
    {
        ofstream out(logFile, ios::app);
        out << "Original: " << record.originalPath << "\n"
            << "Compressed: " << record.compressedPath << "\n"
            << "Decompress: " << record.decompressPath << "\n"
            << "Type: " << record.fileType << "\n"
            << "Time: " << getCurrentTimestamp() << "\n\n";
    }

    string generateCompressedPath(const string &inputPath)
    {
        fs::path path(inputPath);
        string stem = path.stem().string();
        return stem + "_compressed.bin";
    }

    string generateDecompressPath(const string &inputPath)
    {
        fs::path path(inputPath);
        string originalExt = ".txt"; // Default to .txt

        // Read original extension from compressed file header
        ifstream in(inputPath, ios::binary);
        if (in)
        {
            size_t extLen;
            in.read(reinterpret_cast<char *>(&extLen), sizeof(extLen));
            if (extLen < 10)
            { // Sanity check
                string ext(extLen, '\0');
                in.read(&ext[0], extLen);
                originalExt = ext;
            }
            in.close();
        }

        string stem = path.stem().string();
        return stem + "_decompressed" + originalExt;
    }
};

void guiMenu()
{
    if (!glfwInit())
        return;

    GLFWwindow *window = glfwCreateWindow(800, 600, "File Zipper", NULL, NULL);
    glfwMakeContextCurrent(window);

    // Initialize GLEW
    if (glewInit() != GLEW_OK)
    {
        std::cerr << "Failed to initialize GLEW" << std::endl;
        return;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    HuffmanCoding huffman;
    FileTracker fileTracker;
    char inputPath[256] = "";
    bool showSuccess = false;
    string statusMessage;
    bool showError = false;
    bool isAdmin = false;
    bool loggedIn = false;
    bool compressMode = false;
    bool decompressMode = false;
    char username[256] = "";
    char password[256] = "";
    bool showHistory = false;
    string historyContent;

    // Set ImGui style
    ImGui::GetStyle().WindowRounding = 5.0f;
    ImGui::GetStyle().FrameRounding = 4.0f;
    ImGui::GetStyle().GrabRounding = 3.0f;

    ImVec4 *colors = ImGui::GetStyle().Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.15f, 0.16f, 0.21f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.25f, 0.27f, 0.35f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.32f, 0.34f, 0.45f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.42f, 0.44f, 0.55f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.28f, 0.30f, 0.38f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.32f, 0.34f, 0.45f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.42f, 0.44f, 0.55f, 1.00f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.19f, 0.20f, 0.26f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.23f, 0.24f, 0.31f, 1.00f);

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Center window
        ImGui::SetNextWindowPos(ImVec2(400, 300), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(600, 400));

        if (!loggedIn)
        {
            ImGui::Begin("Login", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 50);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.9f, 1.0f));
            ImGui::Text("Welcome to File Zipper");
            ImGui::PopStyleColor();

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Text("Username:");
            ImGui::InputText("##username", username, sizeof(username));

            ImGui::Text("Password:");
            ImGui::InputText("##password", password, sizeof(password), ImGuiInputTextFlags_Password);

            // CHECKING AND VERIFYING THAT IT IS ADMIN OR USER

            if (ImGui::Button("Login"))
            {
                if (strcmp(username, "admin") == 0 && strcmp(password, "admin") == 0)
                {
                    isAdmin = true;
                    loggedIn = true;
                }
                else if (strcmp(username, "user") == 0 && strcmp(password, "user") == 0)
                {
                    isAdmin = false;
                    loggedIn = true;
                }
                else
                {
                    showError = true;
                    statusMessage = "Invalid credentials!";
                }
            }

            if (showError)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "%s", statusMessage.c_str());
            }

            ImGui::End();
        }
        else if (!compressMode && !decompressMode)
        {
            ImGui::Begin("File Zipper", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 20);
            ImGui::Text("Welcome, %s!", isAdmin ? "Administrator" : "User");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button("Compress File", ImVec2(200, 40)))
            {
                compressMode = true;
            }

            ImGui::Spacing();

            if (ImGui::Button("Decompress File", ImVec2(200, 40)))
            {
                decompressMode = true;
            }

            ImGui::Spacing();

            if (isAdmin)
            {
                ImGui::Separator();
                ImGui::Text("Admin Panel");

                if (ImGui::Button("View Compression History", ImVec2(200, 30)))
                {
                    showHistory = true;
                    // Load history content
                    ifstream inFile("compression_history.txt");
                    if (inFile)
                    {
                        stringstream buffer;
                        buffer << inFile.rdbuf();
                        historyContent = buffer.str();
                    }
                    else
                    {
                        historyContent = "No history available.";
                    }
                }
            }

            if (ImGui::Button("Logout", ImVec2(100, 25)))
            {
                loggedIn = false;
                isAdmin = false;
                compressMode = false;
                decompressMode = false;
            }

            ImGui::End();
        }
        else if (compressMode)
        {
            ImGui::Begin("Compress File", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

            ImGui::Text("Input File:");
            ImGui::InputText("##input", inputPath, sizeof(inputPath));
            ImGui::SameLine();
            if (ImGui::Button("Browse Input"))
            {
                string path = openFileDialog();
                if (!path.empty())
                {
                    strncpy(inputPath, path.c_str(), sizeof(inputPath) - 1);
                }
            }

            // Saving the File in path

            if (ImGui::Button("Compress", ImVec2(120, 0)))
            {
                if (strlen(inputPath) > 0)
                {
                    if (fs::exists(inputPath))
                    {
                        string outputPath = "C:\\Users\\Public\\Compressed Files\\" + fs::path(inputPath).stem().string() + "_compressed.bin";
                        if (huffman.compressFile(inputPath, outputPath))
                        {
                            FileRecord record;
                            record.originalPath = inputPath;
                            record.compressedPath = outputPath;
                            record.fileType = fs::path(inputPath).extension().string();
                            fileTracker.saveRecord(record);

                            showSuccess = true;
                            showError = false;
                            statusMessage = "File compressed successfully to: " + outputPath;
                        }
                        else
                        {
                            showError = true;
                            showSuccess = false;
                            statusMessage = "Compression failed!";
                        }
                    }
                    else
                    {
                        showError = true;
                        statusMessage = "Input file does not exist!";
                    }
                }
                else
                {
                    showError = true;
                    statusMessage = "Please select input file!";
                }
            }

            if (showSuccess || showError)
            {
                ImGui::OpenPopup("Result");
            }

            if (ImGui::BeginPopupModal("Result", NULL, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text("%s", statusMessage.c_str());
                if (ImGui::Button("OK"))
                {
                    ImGui::CloseCurrentPopup();
                    showSuccess = false;
                    showError = false;
                    compressMode = false;
                }
                ImGui::EndPopup();
            }

            ImGui::End();
        }
        else if (decompressMode)
        {
            ImGui::Begin("Decompress File", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

            ImGui::Text("Input File:");
            ImGui::InputText("##input", inputPath, sizeof(inputPath));
            ImGui::SameLine();
            if (ImGui::Button("Browse Input"))
            {
                string path = openFileDialog();
                if (!path.empty())
                {
                    strncpy(inputPath, path.c_str(), sizeof(inputPath) - 1);
                }
            }

            if (ImGui::Button("Decompress", ImVec2(120, 0)))
            {
                if (strlen(inputPath) > 0)
                {
                    if (fs::exists(inputPath))
                    {
                        string outputPath = "C:\\Users\\Public\\Decompressed Files\\" + fileTracker.generateDecompressPath(inputPath);
                        if (huffman.decompressFile(inputPath, outputPath))
                        {
                            FileRecord record;
                            record.compressedPath = inputPath;
                            record.decompressPath = outputPath;
                            fileTracker.saveRecord(record);

                            showSuccess = true;
                            showError = false;
                            statusMessage = "File decompressed successfully to: " + outputPath;
                        }
                        else
                        {
                            showError = true;
                            showSuccess = false;
                            statusMessage = "Decompression failed!";
                        }
                    }
                    else
                    {
                        showError = true;
                        statusMessage = "Input file does not exist!";
                    }
                }
                else
                {
                    showError = true;
                    statusMessage = "Please select input file!";
                }
            }

            if (showSuccess || showError)
            {
                ImGui::OpenPopup("Result");
            }

            if (ImGui::BeginPopupModal("Result", NULL, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text("%s", statusMessage.c_str());
                if (ImGui::Button("OK"))
                {
                    ImGui::CloseCurrentPopup();
                    showSuccess = false;
                    showError = false;
                    decompressMode = false;
                }
                ImGui::EndPopup();
            }

            ImGui::End();
        }

        // Show history in a separate window
        if (showHistory)
        {
            ImGui::Begin("Compression History", &showHistory);
            ImGui::TextWrapped("%s", historyContent.c_str());
            if (ImGui::Button("Close"))
            {
                showHistory = false;
            }
            ImGui::End();
        }

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
}

int main()
{
    guiMenu();
    return 0;
}
