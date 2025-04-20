#include "video_core/optimized_rasterizer.h"
#include "common/settings.h"
#include "video_core/gpu.h"
#include "video_core/memory_manager.h"
#include "video_core/engines/maxwell_3d.h"

namespace VideoCore {

OptimizedRasterizer::OptimizedRasterizer(Core::System& system, Tegra::GPU& gpu)
    : system{system}, gpu{gpu}, memory_manager{gpu.MemoryManager()} {
    InitializeShaderCache();
}

OptimizedRasterizer::~OptimizedRasterizer() = default;

void OptimizedRasterizer::Draw(bool is_indexed, u32 instance_count) {
    MICROPROFILE_SCOPE(GPU_Rasterization);

    PrepareRendertarget();
    UpdateDynamicState();

    if (is_indexed) {
        DrawIndexed(instance_count);
    } else {
        DrawArrays(instance_count);
    }
}

void OptimizedRasterizer::Clear(u32 layer_count) {
    MICROPROFILE_SCOPE(GPU_Rasterization);

    PrepareRendertarget();
    ClearFramebuffer(layer_count);
}

void OptimizedRasterizer::DispatchCompute() {
    MICROPROFILE_SCOPE(GPU_Compute);

    PrepareCompute();
    LaunchComputeShader();
}

void OptimizedRasterizer::ResetCounter(VideoCommon::QueryType type) {
    query_cache.ResetCounter(type);
}

void OptimizedRasterizer::Query(GPUVAddr gpu_addr, VideoCommon::QueryType type,
                                VideoCommon::QueryPropertiesFlags flags, u32 payload, u32 subreport) {
    query_cache.Query(gpu_addr, type, flags, payload, subreport);
}

void OptimizedRasterizer::FlushAll() {
    MICROPROFILE_SCOPE(GPU_Synchronization);

    FlushShaderCache();
    FlushRenderTargets();
}

void OptimizedRasterizer::FlushRegion(DAddr addr, u64 size, VideoCommon::CacheType which) {
    MICROPROFILE_SCOPE(GPU_Synchronization);

    if (which == VideoCommon::CacheType::All || which == VideoCommon::CacheType::Unified) {
        FlushMemoryRegion(addr, size);
    }
}

bool OptimizedRasterizer::MustFlushRegion(DAddr addr, u64 size, VideoCommon::CacheType which) {
    if (which == VideoCommon::CacheType::All || which == VideoCommon::CacheType::Unified) {
        return IsRegionCached(addr, size);
    }
    return false;
}

RasterizerDownloadArea OptimizedRasterizer::GetFlushArea(DAddr addr, u64 size) {
    return GetFlushableArea(addr, size);
}

void OptimizedRasterizer::InvalidateRegion(DAddr addr, u64 size, VideoCommon::CacheType which) {
    MICROPROFILE_SCOPE(GPU_Synchronization);

    if (which == VideoCommon::CacheType::All || which == VideoCommon::CacheType::Unified) {
        InvalidateMemoryRegion(addr, size);
    }
}

void OptimizedRasterizer::OnCacheInvalidation(PAddr addr, u64 size) {
    MICROPROFILE_SCOPE(GPU_Synchronization);

    InvalidateCachedRegion(addr, size);
}

bool OptimizedRasterizer::OnCPUWrite(PAddr addr, u64 size) {
    return HandleCPUWrite(addr, size);
}

void OptimizedRasterizer::InvalidateGPUCache() {
    MICROPROFILE_SCOPE(GPU_Synchronization);

    InvalidateAllCache();
}

void OptimizedRasterizer::UnmapMemory(DAddr addr, u64 size) {
    MICROPROFILE_SCOPE(GPU_Synchronization);

    UnmapGPUMemoryRegion(addr, size);
}

void OptimizedRasterizer::ModifyGPUMemory(size_t as_id, GPUVAddr addr, u64 size) {
    MICROPROFILE_SCOPE(GPU_Synchronization);

    UpdateMappedGPUMemory(as_id, addr, size);
}

void OptimizedRasterizer::FlushAndInvalidateRegion(DAddr addr, u64 size, VideoCommon::CacheType which) {
    MICROPROFILE_SCOPE(GPU_Synchronization);

    if (which == VideoCommon::CacheType::All || which == VideoCommon::CacheType::Unified) {
        FlushAndInvalidateMemoryRegion(addr, size);
    }
}

void OptimizedRasterizer::WaitForIdle() {
    MICROPROFILE_SCOPE(GPU_Synchronization);

    WaitForGPUIdle();
}

void OptimizedRasterizer::FragmentBarrier() {
    MICROPROFILE_SCOPE(GPU_Synchronization);

    InsertFragmentBarrier();
}

void OptimizedRasterizer::TiledCacheBarrier() {
    MICROPROFILE_SCOPE(GPU_Synchronization);

    InsertTiledCacheBarrier();
}

void OptimizedRasterizer::FlushCommands() {
    MICROPROFILE_SCOPE(GPU_Synchronization);

    SubmitCommands();
}

void OptimizedRasterizer::TickFrame() {
    MICROPROFILE_SCOPE(GPU_Synchronization);

    EndFrame();
}

void OptimizedRasterizer::PrepareRendertarget() {
    const auto& regs{gpu.Maxwell3D().regs};
    const auto& framebuffer{regs.framebuffer};

    render_targets.resize(framebuffer.num_color_buffers);
    for (std::size_t index = 0; index < framebuffer.num_color_buffers; ++index) {
        render_targets[index] = GetColorBuffer(index);
    }

    depth_stencil = GetDepthBuffer();
}

void OptimizedRasterizer::UpdateDynamicState() {
    const auto& regs{gpu.Maxwell3D().regs};

    UpdateViewport(regs.viewport_transform);
    UpdateScissor(regs.scissor_test);
    UpdateDepthBias(regs.polygon_offset_units, regs.polygon_offset_clamp, regs.polygon_offset_factor);
    UpdateBlendConstants(regs.blend_color);
    UpdateStencilFaceMask(regs.stencil_front_func_mask, regs.stencil_back_func_mask);
}

void OptimizedRasterizer::DrawIndexed(u32 instance_count) {
    const auto& draw_state{gpu.Maxwell3D().draw_manager->GetDrawState()};
    const auto& index_buffer{memory_manager.ReadBlockUnsafe(draw_state.index_buffer.Address(),
                                                            draw_state.index_buffer.size)};

    shader_cache.BindComputeShader();
    shader_cache.BindGraphicsShader();

    DrawElementsInstanced(draw_state.topology, draw_state.index_buffer.count,
                          draw_state.index_buffer.format, index_buffer.data(), instance_count);
}

void OptimizedRasterizer::DrawArrays(u32 instance_count) {
    const auto& draw_state{gpu.Maxwell3D().draw_manager->GetDrawState()};

    shader_cache.BindComputeShader();
    shader_cache.BindGraphicsShader();

    DrawArraysInstanced(draw_state.topology, draw_state.vertex_buffer.first,
                        draw_state.vertex_buffer.count, instance_count);
}

void OptimizedRasterizer::ClearFramebuffer(u32 layer_count) {
    const auto& regs{gpu.Maxwell3D().regs};
    const auto& clear_state{regs.clear_buffers};

    if (clear_state.R || clear_state.G || clear_state.B || clear_state.A) {
        ClearColorBuffers(clear_state.R, clear_state.G, clear_state.B, clear_state.A,
                          regs.clear_color[0], regs.clear_color[1], regs.clear_color[2],
                          regs.clear_color[3], layer_count);
    }

    if (clear_state.Z || clear_state.S) {
        ClearDepthStencilBuffer(clear_state.Z, clear_state.S, regs.clear_depth, regs.clear_stencil,
                                layer_count);
    }
}

void OptimizedRasterizer::PrepareCompute() {
    shader_cache.BindComputeShader();
}

void OptimizedRasterizer::LaunchComputeShader() {
    const auto& launch_desc{gpu.KeplerCompute().launch_description};
    DispatchCompute(launch_desc.grid_dim_x, launch_desc.grid_dim_y, launch_desc.grid_dim_z);
}

} // namespace VideoCore