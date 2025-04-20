#pragma once

#include <memory>
#include <vector>
#include "common/common_types.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/engines/maxwell_3d.h"

namespace Core {
class System;
}

namespace Tegra {
class GPU;
class MemoryManager;
}

namespace VideoCore {

class ShaderCache;
class QueryCache;

class OptimizedRasterizer final : public RasterizerInterface {
public:
    explicit OptimizedRasterizer(Core::System& system, Tegra::GPU& gpu);
    ~OptimizedRasterizer() override;

    void Draw(bool is_indexed, u32 instance_count) override;
    void Clear(u32 layer_count) override;
    void DispatchCompute() override;
    void ResetCounter(VideoCommon::QueryType type) override;
    void Query(GPUVAddr gpu_addr, VideoCommon::QueryType type,
               VideoCommon::QueryPropertiesFlags flags, u32 payload, u32 subreport) override;
    void FlushAll() override;
    void FlushRegion(DAddr addr, u64 size, VideoCommon::CacheType which) override;
    bool MustFlushRegion(DAddr addr, u64 size, VideoCommon::CacheType which) override;
    RasterizerDownloadArea GetFlushArea(DAddr addr, u64 size) override;
    void InvalidateRegion(DAddr addr, u64 size, VideoCommon::CacheType which) override;
    void OnCacheInvalidation(PAddr addr, u64 size) override;
    bool OnCPUWrite(PAddr addr, u64 size) override;
    void InvalidateGPUCache() override;
    void UnmapMemory(DAddr addr, u64 size) override;
    void ModifyGPUMemory(size_t as_id, GPUVAddr addr, u64 size) override;
    void FlushAndInvalidateRegion(DAddr addr, u64 size, VideoCommon::CacheType which) override;
    void WaitForIdle() override;
    void FragmentBarrier() override;
    void TiledCacheBarrier() override;
    void FlushCommands() override;
    void TickFrame() override;

private:
    void PrepareRendertarget();
    void UpdateDynamicState();
    void DrawIndexed(u32 instance_count);
    void DrawArrays(u32 instance_count);
    void ClearFramebuffer(u32 layer_count);
    void PrepareCompute();
    void LaunchComputeShader();

    Core::System& system;
    Tegra::GPU& gpu;
    Tegra::MemoryManager& memory_manager;

    std::unique_ptr<ShaderCache> shader_cache;
    std::unique_ptr<QueryCache> query_cache;

    std::vector<RenderTargetConfig> render_targets;
    DepthStencilConfig depth_stencil;

    // Add any additional member variables needed for the optimized rasterizer
};

} // namespace VideoCore