#include "gst-video-context.hpp"
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <gst/cuda/gstcudacontext.h>
#include <gst/cuda/gstcudaloader.h>
#include <gst/cuda/gstcudautils.h>
#include <helpers/logger.hpp>
#include <sys/stat.h>
#include <sys/sysmacros.h>

namespace gst_video_context {

using cuda_context_ptr = std::shared_ptr<GstCudaContext>;

struct GstVideoContext {
  cuda_context_ptr cuda_context;
  GstContext *context;

  ~GstVideoContext() {
    if (context) {
      gst_context_unref(context);
    }
  }
};

bool init() {
  return gst_cuda_load_library();
}

namespace fs = std::filesystem;

std::optional<std::string> getPciBusIdFromDri(const fs::path &driPath) {
  struct stat st {};
  if (stat(driPath.c_str(), &st) != 0) {
    return std::nullopt;
  }

  std::ostringstream sysfsPath;
  sysfsPath << "/sys/dev/char/" << major(st.st_rdev) << ":" << minor(st.st_rdev) << "/device";

  std::error_code ec;
  fs::path deviceLink = fs::read_symlink(sysfsPath.str(), ec);
  if (ec) {
    return std::nullopt;
  }

  // ex 0000:01:00.0
  std::string busId = deviceLink.filename().string();

  // Validate format (should be domain:bus:device.function)
  if (busId.length() < 7 || busId.find(':') == std::string::npos) {
    return std::nullopt;
  }

  return busId;
}

bool isNvidiaGpu(const std::string &pciBusId) {
  fs::path vendorPath = fs::path("/sys/bus/pci/devices") / pciBusId / "vendor";

  std::ifstream vendorFile(vendorPath);
  if (!vendorFile.is_open()) {
    return false;
  }

  std::string vendor;
  std::getline(vendorFile, vendor);
  // NVIDIA vendor ID is 0x10de
  return vendor == "0x10de";
}

std::optional<int> getCudaDeviceIndexFromPciBusId(const std::string &pciBusId) {
  // /proc/driver/nvidia/gpus/*/information reports the *kernel device minor*,
  // not the CUDA ordinal used by gst_cuda_context_new and nvh265deviceNenc.
  // Ask the CUDA driver for its ordinal using the DRM device's PCI identity.
  using cuda_init = int (*)(unsigned int);
  using cuda_device_from_pci = int (*)(int *, const char *);
  void *library = dlopen("libcuda.so.1", RTLD_NOW | RTLD_LOCAL);
  if (!library) {
    logs::log(logs::error, "Unable to load CUDA driver for GPU {}: {}", pciBusId, dlerror());
    return std::nullopt;
  }
  auto close_library = std::unique_ptr<void, decltype(&dlclose)>(library, dlclose);
  auto cu_init = reinterpret_cast<cuda_init>(dlsym(library, "cuInit"));
  auto cu_device_from_pci = reinterpret_cast<cuda_device_from_pci>(dlsym(library, "cuDeviceGetByPCIBusId"));
  int device_index = -1;
  if (!cu_init || !cu_device_from_pci || cu_init(0) != 0 ||
      cu_device_from_pci(&device_index, pciBusId.c_str()) != 0 || device_index < 0) {
    logs::log(logs::error, "CUDA cannot map PCI GPU {} to a CUDA device", pciBusId);
    return std::nullopt;
  }
  logs::log(logs::info, "PCI bus ID {} mapped to CUDA device ordinal {}", pciBusId, device_index);
  return device_index;
}

std::optional<int> getCudaDeviceFromDri(const std::string &device_path) {
  const fs::path driPath{device_path};
  auto pciBusId = getPciBusIdFromDri(driPath);
  if (!pciBusId) {
    logs::log(logs::warning, "Failed to get PCI bus ID for device: {}", driPath.string());
    return std::nullopt;
  }

  if (!isNvidiaGpu(*pciBusId)) {
    logs::log(logs::warning, "Device: {} is not a NVIDIA GPU", driPath.string());
    return std::nullopt;
  }

  return getCudaDeviceIndexFromPciBusId(*pciBusId);
}

bool set_context(gst_context_ptr context, GstMessage *msg) {
  if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_NEED_CONTEXT) {
    const gchar *context_type;
    gst_message_parse_context_type(msg, &context_type);

    if (g_strcmp0(context_type, GST_CUDA_CONTEXT_TYPE) == 0) {
      gst_element_set_context(GST_ELEMENT(GST_MESSAGE_SRC(msg)), context->context);
      return true;
    }
    logs::log(logs::debug, "Received NEED_CONTEXT for type {}, but it is not supported", context_type);
  }
  return false;
}

bool set_context(gst_context_ptr context, GstElement *element) {
  if (!context || !element) {
    return false;
  }
  gst_element_set_context(element, context->context);
  return true;
}

cuda_context_ptr create_cuda_context(const std::string &device_path) {
  auto detected_device = getCudaDeviceFromDri(device_path);
  if (!detected_device) {
    logs::log(logs::error, "Refusing to create a CUDA context on the default GPU for {}", device_path);
    return nullptr;
  }
  auto device_id = *detected_device;
  logs::log(logs::info, "Creating CUDA context for device {} (detected CUDA device ID: {})", device_path, device_id);
  auto cuda_ctx = gst_cuda_context_new(device_id);
  if (cuda_ctx) {
    return std::shared_ptr<GstCudaContext>(cuda_ctx, gst_object_unref);
  }
  logs::log(logs::warning, "Failed to create CUDA context for device: {}", device_path);
  return nullptr;
}

gst_context_ptr GstVideoContextProvider::get_or_create(const std::string &device_path) {
  std::lock_guard lock(mutex_);
  const auto key = getPciBusIdFromDri(device_path).value_or(device_path);
  if (const auto it = contexts_.find(key); it != contexts_.end()) {
    logs::log(logs::debug, "Reusing CUDA context for render node {} (GPU key {})", device_path, key);
    return it->second;
  }

  auto cuda_context = create_cuda_context(device_path);
  if (!cuda_context) {
    return nullptr;
  }
  auto context = std::make_shared<GstVideoContext>();
  context->cuda_context = std::move(cuda_context);
  context->context = gst_context_new_cuda_context(context->cuda_context.get());
  contexts_.emplace(key, context);
  logs::log(logs::info, "Created CUDA context for render node {} (GPU key {})", device_path, key);
  return context;
}

gst_context_ptr need_context_for_device(const std::string &device_path, GstMessage *msg) {
  if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_NEED_CONTEXT) {
    const gchar *context_type;
    gst_message_parse_context_type(msg, &context_type);

    logs::log(logs::debug, "Received NEED_CONTEXT for type {}", context_type);
    if (g_strcmp0(context_type, GST_CUDA_CONTEXT_TYPE) == 0) {
      if (auto cuda_context = create_cuda_context(device_path)) {
        auto context = gst_context_new_cuda_context(cuda_context.get());
        gst_element_set_context(GST_ELEMENT(GST_MESSAGE_SRC(msg)), context);
        logs::log(logs::debug, "Created CUDA context for device: {}", device_path);
        return std::make_shared<GstVideoContext>(GstVideoContext{
            .cuda_context = std::move(cuda_context),
            .context = context,
        });
      }
    }
  }

  return nullptr;
}

} // namespace gst_video_context
