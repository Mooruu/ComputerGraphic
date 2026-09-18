#include "ParticleSystem.h"
using Microsoft::WRL::ComPtr;
using namespace DirectX;

void ParticleSystem::Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* list)
{
    mDevice = device;
    mDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const UINT64 dataSize = sizeof(Particle) * MaxParticles;
    for (UINT i = 0; i < 2; ++i) {
        ThrowIfFailed(device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(dataSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&mBuffers[i])));
        ThrowIfFailed(device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(sizeof(UINT), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&mCounters[i])));
    }
    ThrowIfFailed(device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(sizeof(UINT) * 2), D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mResetUpload)));
    UINT* reset = nullptr; ThrowIfFailed(mResetUpload->Map(0, nullptr, reinterpret_cast<void**>(&reset))); reset[0] = MaxParticles; reset[1] = 0; mResetUpload->Unmap(0, nullptr);
    list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mCounters[0].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST));
    list->CopyBufferRegion(mCounters[0].Get(), 0, mResetUpload.Get(), 0, sizeof(UINT));
    list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mCounters[0].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
    D3D12_DESCRIPTOR_HEAP_DESC heap = {}; heap.NumDescriptors = 6; heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&mHeap)));
    mConstants = std::make_unique<UploadBuffer<ParticleRenderConstants>>(device, 1, true);
    CreateViews(device);
    auto clearCpu = mHeap->GetCPUDescriptorHandleForHeapStart();
    auto clearGpu = mHeap->GetGPUDescriptorHandleForHeapStart();
    const UINT clearValues[] = { 0, 0, 0, 0 };
    list->ClearUnorderedAccessViewUint(clearGpu, clearCpu, mBuffers[0].Get(), clearValues, 0, nullptr);
    clearCpu.ptr += mDescriptorSize; clearGpu.ptr += mDescriptorSize;
    list->ClearUnorderedAccessViewUint(clearGpu, clearCpu, mBuffers[1].Get(), clearValues, 0, nullptr);
    list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mCounters[0].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST));
    list->CopyBufferRegion(mCounters[0].Get(), 0, mResetUpload.Get(), 0, sizeof(UINT));
    list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mCounters[0].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
}

void ParticleSystem::CreateViews(ID3D12Device* device)
{
    const UINT step = mDescriptorSize;
    auto cpu = mHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {}; uav.Format = DXGI_FORMAT_UNKNOWN; uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER; uav.Buffer.NumElements = MaxParticles; uav.Buffer.StructureByteStride = sizeof(Particle);
    device->CreateUnorderedAccessView(mBuffers[0].Get(), mCounters[0].Get(), &uav, cpu);
    cpu.ptr += step; device->CreateUnorderedAccessView(mBuffers[1].Get(), mCounters[1].Get(), &uav, cpu);
    cpu.ptr += step; device->CreateUnorderedAccessView(mBuffers[1].Get(), mCounters[1].Get(), &uav, cpu);
    cpu.ptr += step; device->CreateUnorderedAccessView(mBuffers[0].Get(), mCounters[0].Get(), &uav, cpu);
    cpu.ptr += step; D3D12_SHADER_RESOURCE_VIEW_DESC srv = {}; srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srv.Format = DXGI_FORMAT_UNKNOWN; srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER; srv.Buffer.NumElements = MaxParticles; srv.Buffer.StructureByteStride = sizeof(Particle);
    device->CreateShaderResourceView(mBuffers[0].Get(), &srv, cpu);
    cpu.ptr += step; device->CreateShaderResourceView(mBuffers[1].Get(), &srv, cpu);
}

void ParticleSystem::BuildPipelineStates(ID3D12Device* device, ID3DBlob* cs, ID3DBlob* vs, ID3DBlob* gs, ID3DBlob* ps, DXGI_FORMAT target, DXGI_FORMAT depth)
{
    CD3DX12_DESCRIPTOR_RANGE uavs; uavs.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0); CD3DX12_ROOT_PARAMETER cp[2]; cp[0].InitAsDescriptorTable(1, &uavs); cp[1].InitAsConstantBufferView(0);
    CD3DX12_ROOT_SIGNATURE_DESC cdesc(2, cp, 0, nullptr); ComPtr<ID3DBlob> blob, err; ThrowIfFailed(D3D12SerializeRootSignature(&cdesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err)); ThrowIfFailed(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&mComputeRoot)));
    D3D12_COMPUTE_PIPELINE_STATE_DESC compute = {}; compute.pRootSignature = mComputeRoot.Get(); compute.CS = { (BYTE*)cs->GetBufferPointer(), cs->GetBufferSize() }; ThrowIfFailed(device->CreateComputePipelineState(&compute, IID_PPV_ARGS(&mComputePSO)));
    CD3DX12_DESCRIPTOR_RANGE srv; srv.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); CD3DX12_ROOT_PARAMETER rp[2]; rp[0].InitAsConstantBufferView(0); rp[1].InitAsDescriptorTable(1, &srv, D3D12_SHADER_VISIBILITY_GEOMETRY);
    CD3DX12_ROOT_SIGNATURE_DESC rdesc(2, rp, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT); ThrowIfFailed(D3D12SerializeRootSignature(&rdesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err)); ThrowIfFailed(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&mRenderRoot)));
    D3D12_GRAPHICS_PIPELINE_STATE_DESC draw = {}; draw.pRootSignature=mRenderRoot.Get(); draw.VS={(BYTE*)vs->GetBufferPointer(),vs->GetBufferSize()};draw.GS={(BYTE*)gs->GetBufferPointer(),gs->GetBufferSize()};draw.PS={(BYTE*)ps->GetBufferPointer(),ps->GetBufferSize()};draw.RasterizerState=CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);draw.BlendState=CD3DX12_BLEND_DESC(D3D12_DEFAULT);draw.DepthStencilState=CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);draw.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ZERO;draw.SampleMask=UINT_MAX;draw.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;draw.NumRenderTargets=1;draw.RTVFormats[0]=target;draw.DSVFormat=depth;draw.SampleDesc.Count=1;ThrowIfFailed(device->CreateGraphicsPipelineState(&draw,IID_PPV_ARGS(&mRenderPSO)));
}

void ParticleSystem::Update(ID3D12GraphicsCommandList* list, float dt, float time)
{
    if(mInputReadable) list->ResourceBarrier(1,&CD3DX12_RESOURCE_BARRIER::Transition(mBuffers[mInput].Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
    const UINT output=1-mInput; list->ResourceBarrier(1,&CD3DX12_RESOURCE_BARRIER::Transition(mCounters[output].Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST));list->CopyBufferRegion(mCounters[output].Get(),0,mResetUpload.Get(),sizeof(UINT),sizeof(UINT));list->ResourceBarrier(1,&CD3DX12_RESOURCE_BARRIER::Transition(mCounters[output].Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
    ParticleRenderConstants c; c.DeltaTime=dt;c.TotalTime=time;mConstants->CopyData(0,c);ID3D12DescriptorHeap* heaps[]={mHeap.Get()};list->SetDescriptorHeaps(1,heaps);list->SetPipelineState(mComputePSO.Get());list->SetComputeRootSignature(mComputeRoot.Get());auto computeTable=mHeap->GetGPUDescriptorHandleForHeapStart();computeTable.ptr+=(mInput==0?0:2)*mDescriptorSize;list->SetComputeRootDescriptorTable(0,computeTable);list->SetComputeRootConstantBufferView(1,mConstants->Resource()->GetGPUVirtualAddress());list->Dispatch(MaxParticles/256,1,1);list->ResourceBarrier(1,&CD3DX12_RESOURCE_BARRIER::UAV(mBuffers[output].Get()));mInput=output;mInputReadable=true;
}

void ParticleSystem::Draw(ID3D12GraphicsCommandList* list,const ParticleRenderConstants& c,D3D12_CPU_DESCRIPTOR_HANDLE rtv,D3D12_CPU_DESCRIPTOR_HANDLE dsv)
{
    mConstants->CopyData(0,c);list->ResourceBarrier(1,&CD3DX12_RESOURCE_BARRIER::Transition(mBuffers[mInput].Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));ID3D12DescriptorHeap* heaps[]={mHeap.Get()};list->SetDescriptorHeaps(1,heaps);list->SetPipelineState(mRenderPSO.Get());list->SetGraphicsRootSignature(mRenderRoot.Get());list->SetGraphicsRootConstantBufferView(0,mConstants->Resource()->GetGPUVirtualAddress());auto gpu=mHeap->GetGPUDescriptorHandleForHeapStart();gpu.ptr+=(4+mInput)*mDescriptorSize; list->SetGraphicsRootDescriptorTable(1,gpu);list->OMSetRenderTargets(1,&rtv,true,&dsv);list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);list->DrawInstanced(MaxParticles,1,0,0);
}
