#pragma once

// Small helpers of the WebGPU renderer, which its parts share: entries of layouts and of groups, layouts, groups.
#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <span>

namespace devex::render::webgpu {

[[nodiscard]] inline wgpu::BindGroupLayoutEntry uniformEntry(std::uint32_t binding, wgpu::ShaderStage visibility, bool dynamic,
                                                      std::uint64_t size)
{
    wgpu::BindGroupLayoutEntry entry;
    entry.binding = binding;
    entry.visibility = visibility;
    entry.buffer.type = wgpu::BufferBindingType::Uniform;
    entry.buffer.hasDynamicOffset = dynamic;
    entry.buffer.minBindingSize = size;
    return entry;
}

[[nodiscard]] inline wgpu::BindGroupLayoutEntry textureEntry(std::uint32_t binding, wgpu::ShaderStage visibility)
{
    wgpu::BindGroupLayoutEntry entry;
    entry.binding = binding;
    entry.visibility = visibility;
    entry.texture.sampleType = wgpu::TextureSampleType::Float;
    entry.texture.viewDimension = wgpu::TextureViewDimension::e2D;
    return entry;
}

[[nodiscard]] inline wgpu::BindGroupLayoutEntry samplerEntry(std::uint32_t binding, wgpu::ShaderStage visibility)
{
    wgpu::BindGroupLayoutEntry entry;
    entry.binding = binding;
    entry.visibility = visibility;
    entry.sampler.type = wgpu::SamplerBindingType::Filtering;
    return entry;
}

[[nodiscard]] inline wgpu::BindGroupLayout bindGroupLayout(const wgpu::Device& device, const char* label,
                                                    std::span<const wgpu::BindGroupLayoutEntry> entries)
{
    wgpu::BindGroupLayoutDescriptor descriptor;
    descriptor.label = label;
    descriptor.entryCount = entries.size();
    descriptor.entries = entries.data();
    return device.CreateBindGroupLayout(&descriptor);
}

[[nodiscard]] inline wgpu::PipelineLayout pipelineLayout(const wgpu::Device& device, std::span<const wgpu::BindGroupLayout> groups)
{
    wgpu::PipelineLayoutDescriptor descriptor;
    descriptor.bindGroupLayoutCount = groups.size();
    descriptor.bindGroupLayouts = groups.data();
    return device.CreatePipelineLayout(&descriptor);
}

[[nodiscard]] inline wgpu::BindGroupEntry bufferBinding(std::uint32_t binding, const wgpu::Buffer& buffer, std::uint64_t size)
{
    wgpu::BindGroupEntry entry;
    entry.binding = binding;
    entry.buffer = buffer;
    entry.size = size;
    return entry;
}

[[nodiscard]] inline wgpu::BindGroupEntry viewBinding(std::uint32_t binding, const wgpu::TextureView& view)
{
    wgpu::BindGroupEntry entry;
    entry.binding = binding;
    entry.textureView = view;
    return entry;
}

[[nodiscard]] inline wgpu::BindGroupEntry samplerBinding(std::uint32_t binding, const wgpu::Sampler& sampler)
{
    wgpu::BindGroupEntry entry;
    entry.binding = binding;
    entry.sampler = sampler;
    return entry;
}

[[nodiscard]] inline wgpu::BindGroup bindGroup(const wgpu::Device& device, const wgpu::BindGroupLayout& layout,
                                        std::span<const wgpu::BindGroupEntry> entries)
{
    wgpu::BindGroupDescriptor descriptor;
    descriptor.layout = layout;
    descriptor.entryCount = entries.size();
    descriptor.entries = entries.data();
    return device.CreateBindGroup(&descriptor);
}

} // namespace devex::render::webgpu
