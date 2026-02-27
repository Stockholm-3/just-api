#include "../include/Config.hpp"

#include <utility>

Config::Config() : baseUrl_(""), cacheTtlSeconds_(0) {}

Config::Config(std::string baseUrl, int cacheTtlSeconds)
    : baseUrl_(std::move(baseUrl)), cacheTtlSeconds_(cacheTtlSeconds) {}

Config::Config(const Config& other)
    : baseUrl_(other.baseUrl_), cacheTtlSeconds_(other.cacheTtlSeconds_) {}

Config& Config::operator=(const Config& other) {
    if (this != &other) {
        baseUrl_         = other.baseUrl_;
        cacheTtlSeconds_ = other.cacheTtlSeconds_;
    }
    return *this;
}

Config::~Config() = default;

const std::string& Config::baseUrl() const { return baseUrl_; }

int Config::cacheTtlSeconds() const { return cacheTtlSeconds_; }

void Config::setBaseUrl(const std::string& baseUrl) { baseUrl_ = baseUrl; }

void Config::setCacheTtlSeconds(int cacheTtlSeconds) {
    cacheTtlSeconds_ = cacheTtlSeconds;
}