module;

#include <cstdint>
#include <filesystem>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <span>
#include <string_view>
#include <thread>
#include <variant>
#include <version>

export module dreamrender:resource_loader;

import :texture;
import :model;

import spdlog;
import vulkan_hpp;
import vma;

namespace dreamrender {

class texture;
class model;

enum class LoadType
{
    Texture,
    Image,
    Buffer,
    Model
};

#if __cpp_lib_move_only_function >= 202110L
export using LoaderFunction = std::move_only_function<void(uint8_t*, size_t)>;
#else
export using LoaderFunction = std::function<void(uint8_t*, size_t)>;
#endif
export struct LoadDataView {
    std::span<const uint8_t> data;
    std::string type;

    LoadDataView(std::span<const uint8_t> data, std::string_view type = "") : data(data), type(type) {}
};
struct LoadTask
{
    LoadType type;
    std::variant<std::filesystem::path, LoaderFunction, LoadDataView> src;
    std::variant<texture*, model*, vk::Image, vk::Buffer> dst;
    std::promise<void> promise;

    std::string source_name() const;
};

bool load_model(
    int index, LoadTask& task,
    vk::Device device, vma::Allocator allocator, vma::Allocation allocation,
    vk::CommandBuffer commandBuffer,
    size_t stagingSize, vk::Buffer stagingBuffer);
bool load_texture(
    int index, LoadTask& task, std::mutex& lock,
    vk::Device device, vma::Allocator allocator, vma::Allocation allocation,
    vk::CommandBuffer commandBuffer,
    uint8_t* decodeBuffer, size_t stagingSize, vk::Buffer stagingBuffer);

export class resource_loader
{
    public:
        resource_loader(vk::Device device, vma::Allocator allocator,
            uint32_t transferFamily, uint32_t graphicsFamily,
            std::vector<vk::Queue> queues) : device(device), allocator(allocator), transferFamily(transferFamily), graphicsFamily(graphicsFamily)
        {
            int index = 0;
            for(auto& queue : queues)
            {
                threads.emplace_back(&resource_loader::loadThread, this, index, queue);
                index++;
            }
        }

        ~resource_loader() {
            quit = true;
            cv.notify_all();
            for(auto& t : threads) {
                if(t.joinable()) {
                    t.join();
                }
            }
        }

        std::future<void> loadTexture(texture* texture, std::filesystem::path path) {
            std::future<void> f;
            {
                std::scoped_lock<std::mutex> l(lock);
                tasks.push(LoadTask{.type = LoadType::Texture, .src = path, .dst = texture, .promise = std::promise<void>()});
                f = tasks.back().promise.get_future();
            }
            cv.notify_one();
            return f;
        }
        std::future<void> loadTexture(texture* texture, LoaderFunction loader) {
            std::future<void> f;
            {
                std::scoped_lock<std::mutex> l(lock);
                tasks.push(LoadTask{.type = LoadType::Texture, .src = std::move(loader), .dst = texture, .promise = std::promise<void>()});
                f = tasks.back().promise.get_future();
            }
            cv.notify_one();
            return f;
        }
        std::future<void> loadTexture(texture* texture, LoadDataView data) {
            std::future<void> f;
            {
                std::scoped_lock<std::mutex> l(lock);
                tasks.push(LoadTask{.type = LoadType::Texture, .src = data, .dst = texture, .promise = std::promise<void>()});
                f = tasks.back().promise.get_future();
            }
            cv.notify_one();
            return f;
        }

        std::future<void> loadModel(model* model, std::filesystem::path filename) {
            std::future<void> f;
            {
                std::scoped_lock<std::mutex> l(lock);
                tasks.push(LoadTask{.type = LoadType::Model, .src = filename, .dst = model, .promise = std::promise<void>()});
                f = tasks.back().promise.get_future();
            }
            cv.notify_one();
            return f;
        }
        std::future<void> loadModel(model* model, LoadDataView data) {
            std::future<void> f;
            {
                std::scoped_lock<std::mutex> l(lock);
                tasks.push(LoadTask{.type = LoadType::Model, .src = data, .dst = model, .promise = std::promise<void>()});
                f = tasks.back().promise.get_future();
            }
            cv.notify_one();
            return f;
        }

        vk::Device getDevice() const { return device; }
        vma::Allocator getAllocator() const { return allocator; }
    private:
        vk::Device device;
        vma::Allocator allocator;

        uint32_t transferFamily;
        uint32_t graphicsFamily;

        std::mutex lock;
        std::vector<std::thread> threads;
        std::queue<LoadTask> tasks;
        std::condition_variable cv;
        bool quit = false;

        void loadThread(int index, vk::Queue queue) {
            vk::UniqueCommandPool pool;
            vk::UniqueCommandBuffer commandBuffer;
            vk::UniqueFence fence;
            vk::Buffer stagingBuffer;
            vma::Allocation allocation;
            {
                std::scoped_lock<std::mutex> l(lock);

                pool = device.createCommandPoolUnique(
                        vk::CommandPoolCreateInfo({}, transferFamily));
                commandBuffer = std::move(device.allocateCommandBuffersUnique(
                        vk::CommandBufferAllocateInfo(pool.get(), vk::CommandBufferLevel::ePrimary, 1)).back());
                fence = device.createFenceUnique(vk::FenceCreateInfo());

                vk::BufferCreateInfo buffer_info({}, stagingSize, vk::BufferUsageFlagBits::eTransferSrc, vk::SharingMode::eExclusive);
                vma::AllocationCreateInfo alloc_info({}, vma::MemoryUsage::eCpuToGpu);
                std::tie(stagingBuffer, allocation) = allocator.createBuffer(buffer_info, alloc_info);
            }

            std::unique_ptr<uint8_t[]> cpuBuffer = std::make_unique<uint8_t[]>(stagingSize);

            spdlog::info("[Resource Loader {}]: Started", index);
            std::unique_lock<std::mutex> l(lock);
            do
            {
                cv.wait(l, [this]{
                    return (tasks.size() || quit);
                });

                if(!quit && tasks.size())
                {
                    auto task = std::move(tasks.front());
                    tasks.pop();
                    l.unlock();

                    spdlog::debug("[Resource Loader {}] Loading {}", index,
                        std::holds_alternative<std::filesystem::path>(task.src) ? std::get<std::filesystem::path>(task.src).string() : "dynamic resource");
                    auto t0 = std::chrono::high_resolution_clock::now();
                    {
                        bool okay;
                        if(task.type == LoadType::Texture)
                        {
                            okay = load_texture(index, task, lock, device, allocator, allocation, commandBuffer.get(), cpuBuffer.get(), stagingSize, stagingBuffer);
                        }
                        else if(task.type == LoadType::Model)
                        {
                            okay = load_model(index, task, device, allocator, allocation, commandBuffer.get(), stagingSize, stagingBuffer);
                        }
                        if(okay) {
                            std::array<vk::SubmitInfo, 1> submits = {
                                vk::SubmitInfo({}, {}, commandBuffer.get(), {})
                            };
                            queue.submit(submits, fence.get());
                            vk::Result result = device.waitForFences(fence.get(), true, UINT64_MAX);
                            if(result != vk::Result::eSuccess)
                            {
                                spdlog::error("[Resource Loader {}] Waiting for fence failed: {}", index, vk::to_string(result));
                            }
                            device.resetCommandPool(pool.get());
                            device.resetFences(fence.get());

                            if(std::holds_alternative<texture*>(task.dst))
                                std::get<texture*>(task.dst)->loaded = true;
                            else if(std::holds_alternative<model*>(task.dst))
                                std::get<model*>(task.dst)->loaded = true;
                        }
                        task.promise.set_value();
                    }
                    auto t1 = std::chrono::high_resolution_clock::now();
                    auto time = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
                    spdlog::debug("[Resource Loader {}] Loaded {} into {} in {} ms", index,
                        task.source_name(),
                        std::holds_alternative<texture*>(task.dst) ? static_cast<void*>(std::get<texture*>(task.dst)->image) :
                            (std::holds_alternative<model*>(task.dst) ? std::get<model*>(task.dst)->vertexBuffer : nullptr),
                        time);

                    l.lock();
                }
            } while(!quit);

            allocator.destroyBuffer(stagingBuffer, allocation);
            spdlog::info("[Resource Loader {}]: Quit", index);
        }

        constexpr static vk::DeviceSize stagingSize = 16*1024*1024;
};

}
