#include <iostream>
#include <filesystem>
#include <functional>
#include <algorithm>
#include <thread>
#include <mutex>
#include <string>
#include <format>
#include <cstdlib>
#include <cstdint>
#include <vector>
#include <map>
#include <sstream>
#include <fstream>
#include "nlohmann/json.hpp"
#include "SQLog/sqlog.hpp"

using namespace std;
namespace fs = std::filesystem;
using json = nlohmann::json;

fs::path MotherPath;
fs::path CurrentPath;
map<string,vector<string>> emitConfigMap = {
    {"-d",{"bool","debug"}},
    {"-port",{"int","port"}},
    {"-mode",{"char","mode"}},
    {"-name",{"string","name"}}
};
json MainConfig;

string input(string prompt) {
    string bfr;
    cout << prompt;
    getline(cin,bfr);
    return bfr;
}

vector<string> split(string data,char target) {
    vector<string> result;
    stringstream ss(data);
    string word;
    while (getline(ss,word,target)) {
        result.push_back(word);
    }
    return result;
}

void ReadConfig() {
    fs::path ConfigPath = MotherPath / "config" / "config.json";
    ifstream file(ConfigPath);
    file >> MainConfig;
}

vector<string> emitConfig(int argc,char* args[]) {
    vector<string> arg;
    for (int i=1; i<argc; i++) {
        vector<string> _parse = split(args[i],'=');
        if (!emitConfigMap.contains(_parse[0])) {
            arg.push_back(_parse[0]);
            continue;
        }
        string type = emitConfigMap[_parse[0]][0];
        string key = emitConfigMap[_parse[0]][1];
        if (_parse.size() < 2) {
            if (type != "bool") {
                cout << "[Error] When providing parameters that are not Boolean, the format must be <key>=<value>" << endl;
                cout << format("[Error] {}",args[i]) << endl;
                exit(1);
            }
            MainConfig[key] = true;
        } else {
            string val = _parse[1];
            if (type == "bool") {
                MainConfig[key] = (val == "true" || val == "1");
            } else if (type == "int") {
                MainConfig[key] = stoi(val);
            } else if (type == "char") {
                if (!val.empty()) {
                    MainConfig[key] = val[0];
                }
            } else if (type == "string") {
                MainConfig[key] = val;
            }
        }
    }
    return arg;
}

string readFile(string path) {
    ifstream file(path);
    stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

void mainExecute(vector<string> args) {
    SQLOG_HPP::LoadLogs(fs::path("./test/logs"));
    SQLOG_HPP::loadProfile("main.log");
    SQLOG_ENGINE_HPP::parse(SQLOG_HPP::getLogPath());
    cout << SQLOG_ENGINE_HPP::doString("print \"Hello from SQLog\"") << endl;;
}

int main(int argc,char* args[]) {
    MotherPath = fs::canonical(fs::absolute(args[0])).parent_path().parent_path();
    CurrentPath = fs::current_path();
    try {
        ReadConfig();
        vector<string> arg = emitConfig(argc,args);
        if (MainConfig.contains("debug") && MainConfig["debug"]) {
            cout << "[Debug] Debug Mode" << endl;
        }
        mainExecute(arg);
    } catch (const std::exception& e) {
        std::cerr << "[Error] " << e.what() << "\n";
        return 1;
    }
    return 0;
}