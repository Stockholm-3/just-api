#ifndef CACHE_HPP
#define CACHE_HPP

#include <string>
#include <chrono>
#include <cstddef>
#include <unordered_map>

class Cache {
    public:
        Cache();
        Cache(const Cache& other);
        Cache& operator=(const Cache& other);
        ~Cache();

        void put(const std::string& key, const std::string& value, int ttlSeconds);
        bool get(const std::string& key, std::string* outValue);
        void purgeExpired();
        std::size_t size() const;
    
    private:
        struct Entry {
            std::string value;
            std::chrono::steady_clock::time_point expiresAt;
        };

        std::unordered_map<std::string, Entry> entries_;
};

#endif /* CACHE_HPP */