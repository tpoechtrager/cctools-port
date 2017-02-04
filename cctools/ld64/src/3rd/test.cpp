#include <string>
#include <vector>
#include <map>

#include "helper.h"

// clang++ test.cpp -I ../../../include -I ../../../include/foreign -I . -std=c++11 -O3 && ./a.out

int main()
{
    std::map<std::string, std::vector<std::string>> a;
    std::map<std::string, std::vector<std::string>> b;

    auto it_a = a.emplace("k", std::initializer_list<std::string>{"test1"});
    auto it_b = STD_MAP_EMPLACE(std::string, std::vector<std::string>, b, "k", std::initializer_list<std::string>{"test1"});

    if (it_a.second != true || it_a.second != it_b.second) abort();

    it_a.first->second.emplace_back("test2");
    it_b.first->second.emplace_back("test2");

    it_a = a.emplace("k", std::initializer_list<std::string>{"test ABC"});
    it_b = STD_MAP_EMPLACE(std::string, std::vector<std::string>, b, "k", std::initializer_list<std::string>{"test DEF"});

    it_a.first->second.emplace_back("test3");
    it_b.first->second.emplace_back("test3");

    if (it_a.second != false || it_a.second != it_b.second) abort();
    if (it_a.first->second.size() != 3 || it_b.first->second.size() != 3) abort();
    if (it_a.first->second[0] != it_b.first->second[0]) abort();
    if (it_a.first->second[1] != it_b.first->second[1]) abort();
    if (it_a.first->second[2] != it_b.first->second[2]) abort();
    if (a["k"][0] != b["k"][0]) abort();
    if (a["k"][1] != b["k"][1]) abort();
    if (a["k"][2] != b["k"][2]) abort();

    return 0;
}
