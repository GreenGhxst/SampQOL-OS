#pragma once
#include "native_model.h"
#include "core/file_ops.h"
#include "core/volume_registry.h"
namespace dm::native {
class CoreService final : public Service {
public:
    CoreService(VolumeRegistry& registry, FileOps& files) : registry_(registry), files_(files) {}
    std::vector<Volume> volumes() override;
    bool list(const std::string& volume, const std::string& relative_dir,
              std::size_t offset, std::size_t limit, std::vector<Item>& out,
              bool& has_more, std::string& error) override;
private:
    VolumeRegistry& registry_;
    FileOps& files_;
};
} // namespace dm::native
