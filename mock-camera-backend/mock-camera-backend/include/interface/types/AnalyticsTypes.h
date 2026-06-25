#pragma once
#include <string>
#include <vector>

struct AnalyticsModule {
    std::string type;
    std::string name;
    std::string version;
};

struct AnalyticsRule {
    std::string type;
    std::string name;
    std::string configToken;
};
