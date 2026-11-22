#pragma once
// Native UI read-only adapter into the EXISTING authority-controlled core.
// Mutating native operations are deliberately NOT exposed until confirmation,
// journal/job and mount-ownership workflows are wired and tested.
#include "native_model.h"
#include "core/file_ops.h"
#include "core/volume_registry.h"
namespace dm::native {
class CoreService final : public Service {
public:
    CoreService(VolumeRegistry& registry, FileOps& files) : registry_(registry), files_(files) {}
    std::vector<Volume> volumes() override;
    bool list(const std::string& volume, const std::string& relative_dir,
              std::vector<Item>& out, std::string& error) override;
private:
    VolumeRegistry& registry_;
    FileOps& files_;
};
} // namespace dm::native
