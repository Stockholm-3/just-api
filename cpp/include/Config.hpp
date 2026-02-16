#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <string>

class Config {
    public:
        Config();
        Config(std::string baseUrl, int cacheTtlSeconds);
        Config(const Config& other);
        Config& operator=(const Config& other);
        ~Config();

        const std::string& baseUrl() const;
        int cacheTtlSeconds() const;

        void setBaseUrl(const std::string& baseUrl);
        void setCacheTtlSeconds(int cacheTtlSeconds);

    private:
        std::string baseUrl_;
        int cacheTtlSeconds_;
};

#endif /* CONFIG_HPP */