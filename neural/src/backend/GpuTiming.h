// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "../Support.h"
#include "GpuInterop.h"
#include <cstring>
namespace ttwvrnr {
// Optional sampled NGX GPU time. The serialized host collects after its
// existing D3D11 completion wait; no new wait or image readback is introduced.
// Uncollected query slots are never reused. Measurements exclude input/output
// conversions.
class GpuTiming {
  Microsoft::WRL::ComPtr<ID3D12QueryHeap> heap_;
  Microsoft::WRL::ComPtr<ID3D12Resource> readback_;
  uint64_t frequency_ = 0, frame_ = 0;
  unsigned eye_ = 0, mask_ = 0;
  bool sample_ = false;

public:
  void Init(GpuInterop &gpu) {
    frequency_ = gpu.TimestampFrequency();
    D3D12_QUERY_HEAP_DESC q{};
    q.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    q.Count = 4;
    D3D12_HEAP_PROPERTIES h{};
    h.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC r{};
    r.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    r.Width = 4 * sizeof(uint64_t);
    r.Height = r.DepthOrArraySize = r.MipLevels = r.SampleDesc.Count = 1;
    r.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (!frequency_ ||
        FAILED(gpu.Device12()->CreateQueryHeap(&q, IID_PPV_ARGS(&heap_))) ||
        FAILED(gpu.Device12()->CreateCommittedResource(
            &h, D3D12_HEAP_FLAG_NONE, &r, D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr, IID_PPV_ARGS(&readback_)))) {
      heap_.Reset();
      readback_.Reset();
      frequency_ = 0;
      Log("neural_gpu_timing_unavailable processing_unchanged=1");
    }
  }
  void Sample(uint64_t frame, unsigned eye) {
    sample_ = frequency_ && !mask_ && frame % 30 == 0;
    if (sample_) {
      frame_ = frame;
      eye_ = eye;
    }
  }
  void Begin(ID3D12GraphicsCommandList *list, unsigned feature) {
    if (sample_)
      list->EndQuery(heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, feature * 2);
  }
  void End(ID3D12GraphicsCommandList *list, unsigned feature) {
    if (!sample_)
      return;
    list->EndQuery(heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, feature * 2 + 1);
    list->ResolveQueryData(heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, feature * 2,
                           2, readback_.Get(), feature * 2 * sizeof(uint64_t));
    mask_ |= 1u << feature;
  }
  void Collect(const GpuInterop &gpu) {
    if (!mask_ || !gpu.Completed())
      return;
    void *data = nullptr;
    D3D12_RANGE range{0, 4 * sizeof(uint64_t)};
    if (SUCCEEDED(readback_->Map(0, &range, &data))) {
      double ms[2]{};
      bool valid = true;
      for (unsigned feature = 0; feature < 2; ++feature) {
        if (!(mask_ & (1u << feature)))
          continue;
        uint64_t ticks[2];
        std::memcpy(ticks,
                    static_cast<char *>(data) + feature * 2 * sizeof(uint64_t),
                    sizeof(ticks));
        valid &= ticks[1] >= ticks[0];
        ms[feature] = double(ticks[1] - ticks[0]) * 1000.0 / double(frequency_);
      }
      D3D12_RANGE written{0, 0};
      readback_->Unmap(0, &written);
      if (valid)
        Log("neural_gpu_timing frame=%llu eye=%u dlaa_active=%u nr_active=%u "
            "dlaa_ms=%.4f nr_ms=%.4f",
            (unsigned long long)frame_, eye_, mask_ & 1u, (mask_ >> 1) & 1u,
            ms[0], ms[1]);
    }
    mask_ = 0;
    sample_ = false;
  }
};
} // namespace ttwvrnr
