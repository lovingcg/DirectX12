#include "FrameResource.h"

FrameResource::FrameResource(ID3D12Device* device, UINT passCount, UINT maxInstanceCount,UINT materialCount)
{
	ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&CmdListAlloc)));

	PassCB = std::make_unique<UploadBuffer<PassConstants>>(device, passCount, true);
	//ObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(device, objCount, true);
	//MaterialCB = std::make_unique<UploadBuffer<MaterialConstants>>(device, materialCount, true);
	// 此时的matSB是属于StructuredBuffer，不属于ConstantBuffer，所以isConstant的Bool值为false
	MaterialBuffer = std::make_unique<UploadBuffer<MaterialData>>(device, materialCount, false);

	// 由于它不属于常量缓冲，所以第三个参数是false。
	//WavesVB = std::make_unique<UploadBuffer<Vertex>>(device, waveVertCount, false);
	InstanceBuffer = std::make_unique<UploadBuffer<InstanceData>>(device, maxInstanceCount, false);
}

FrameResource::~FrameResource()
{

}