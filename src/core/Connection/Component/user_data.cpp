#include <string>
#include <any>
#include <unordered_map>
#include <shared_mutex>

namespace httpserver::core {

class UserData {
public:
    void Set(const std::string& key, std::any value) {
        std::unique_lock lock(mutex_);
        data_[key] = std::move(value);
    }

    std::any Get(const std::string& key) const {
        std::shared_lock lock(mutex_);
        auto it = data_.find(key);
        if (it != data_.end()) return it->second;
        return std::any();
    }

    bool Has(const std::string& key) const {
        std::shared_lock lock(mutex_);
        return data_.find(key) != data_.end();
    }

    void Remove(const std::string& key) {
        std::unique_lock lock(mutex_);
        data_.erase(key);
    }

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::any> data_;
};

} // namespace