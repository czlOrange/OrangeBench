#include <cstdint>
#include <memory>

namespace httpserver::core {

class IConnectionManager;

class Identity {
public:
    void SetId(uint64_t id) { id_ = id; }
    uint64_t GetId() const { return id_; }

    void SetManager(std::shared_ptr<IConnectionManager> mgr) { manager_ = std::move(mgr); }
    std::shared_ptr<IConnectionManager> GetManager() const { return manager_; }

private:
    uint64_t id_ = 0;
    std::shared_ptr<IConnectionManager> manager_;
};

} // namespace