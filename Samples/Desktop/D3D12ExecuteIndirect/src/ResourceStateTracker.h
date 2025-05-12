#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <unordered_map>
#include <vector>

class ResourceStateTracker
{
public:
    void TrackResourceState(ID3D12Resource* resource, D3D12_RESOURCE_STATES state);
    void Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES newState, bool should_insert_uav_barrier);
    void FlushBarriers(ID3D12GraphicsCommandList* cmdList);
    void Reset();

private:
    struct ResourceInfo
    {
        D3D12_RESOURCE_STATES currentState;
    };

    std::unordered_map<ID3D12Resource*, ResourceInfo> m_TrackedResources;
    std::vector<D3D12_RESOURCE_BARRIER> m_PendingBarriers;
};

inline std::wstring ResourceStateToString(D3D12_RESOURCE_STATES state) {
    switch (state) {
        case D3D12_RESOURCE_STATE_COMMON: return L"COMMON";
        case D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER: return L"VERTEX_AND_CONSTANT_BUFFER";
        case D3D12_RESOURCE_STATE_INDEX_BUFFER: return L"INDEX_BUFFER";
        case D3D12_RESOURCE_STATE_RENDER_TARGET: return L"RENDER_TARGET";
        case D3D12_RESOURCE_STATE_UNORDERED_ACCESS: return L"UNORDERED_ACCESS";
        case D3D12_RESOURCE_STATE_DEPTH_WRITE: return L"DEPTH_WRITE";
        case D3D12_RESOURCE_STATE_DEPTH_READ: return L"DEPTH_READ";
        case D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE: return L"NON_PIXEL_SHADER_RESOURCE";
        case D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE: return L"PIXEL_SHADER_RESOURCE";
        case D3D12_RESOURCE_STATE_STREAM_OUT: return L"STREAM_OUT";
        case D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT: return L"INDIRECT_ARGUMENT";
        case D3D12_RESOURCE_STATE_COPY_DEST: return L"COPY_DEST";
        case D3D12_RESOURCE_STATE_COPY_SOURCE: return L"COPY_SOURCE";
        case D3D12_RESOURCE_STATE_RESOLVE_DEST: return L"RESOLVE_DEST";
        case D3D12_RESOURCE_STATE_RESOLVE_SOURCE: return L"RESOLVE_SOURCE";
        case D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE: return L"RAYTRACING_ACCELERATION_STRUCTURE";
        case D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE: return L"SHADING_RATE_SOURCE";
        case D3D12_RESOURCE_STATE_VIDEO_DECODE_READ: return L"VIDEO_DECODE_READ";
        case D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE: return L"VIDEO_DECODE_WRITE";
        case D3D12_RESOURCE_STATE_VIDEO_PROCESS_READ: return L"VIDEO_PROCESS_READ";
        case D3D12_RESOURCE_STATE_VIDEO_PROCESS_WRITE: return L"VIDEO_PROCESS_WRITE";
        case D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ: return L"VIDEO_ENCODE_READ";
        case D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE: return L"VIDEO_ENCODE_WRITE";
        default: return L"UNKNOWN_STATE_" + std::to_wstring(static_cast<int>(state));
    }
}

// ==================== Implementation ====================

inline void ResourceStateTracker::TrackResourceState(ID3D12Resource* resource, D3D12_RESOURCE_STATES state)
{

    {
        std::wstring msg = L"Tracking resource state: " + ResourceStateToString(state) + 
            L" [Resource: 0x" + std::to_wstring(reinterpret_cast<uintptr_t>(resource)) + L"]\n";
        // OutputDebugStringW(msg.c_str());
        m_TrackedResources[resource] = { state };
    }
}

inline void ResourceStateTracker::Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES newState, bool should_insert_uav_barrier = true)
{
    if (m_TrackedResources.find(resource) == m_TrackedResources.end())
    {
        std::wstring msg = L"Resource state transition from UNKNOWN to " + ResourceStateToString(newState) + 
            L" [Resource: 0x" + std::to_wstring(reinterpret_cast<uintptr_t>(resource)) + L"]\n";
        // OutputDebugStringW(msg.c_str());
        m_TrackedResources[resource] = { newState };
        return;
    }
    std::wstring msg = L"Resource state transition from " + 
        ResourceStateToString(m_TrackedResources[resource].currentState) + 
        L" to " + ResourceStateToString(newState) + 
        L" [Resource: 0x" + std::to_wstring(reinterpret_cast<uintptr_t>(resource)) + L"]\n";
    // OutputDebugStringW(msg.c_str());

    auto& entry = m_TrackedResources[resource];
    if (entry.currentState != newState)
    {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = resource;
        barrier.Transition.StateBefore = entry.currentState;
        barrier.Transition.StateAfter = newState;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        m_PendingBarriers.push_back(barrier);
        entry.currentState = newState;
    }
    else if (newState == D3D12_RESOURCE_STATE_UNORDERED_ACCESS && should_insert_uav_barrier)
    {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.UAV.pResource = resource;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;

        m_PendingBarriers.push_back(barrier);
    }
}

inline void ResourceStateTracker::FlushBarriers(ID3D12GraphicsCommandList* cmdList)
{
    if (!m_PendingBarriers.empty())
    {
        cmdList->ResourceBarrier(static_cast<UINT>(m_PendingBarriers.size()), m_PendingBarriers.data());
        m_PendingBarriers.clear();
    }
}

inline void ResourceStateTracker::Reset()
{
    m_TrackedResources.clear();
    m_PendingBarriers.clear();
}
