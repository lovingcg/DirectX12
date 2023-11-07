#include "FrameResource.h"

FrameResource::FrameResource(ID3D12Device* device, UINT passCount, UINT maxInstanceCount,UINT materialCount)
{
	ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&CmdListAlloc)));

	PassCB = std::make_unique<UploadBuffer<PassConstants>>(device, passCount, true);
	//ObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(device, objCount, true);
	//MaterialCB = std::make_unique<UploadBuffer<MaterialConstants>>(device, materialCount, true);
	// ��ʱ��matSB������StructuredBuffer��������ConstantBuffer������isConstant��BoolֵΪfalse
	MaterialBuffer = std::make_unique<UploadBuffer<MaterialData>>(device, materialCount, false);

	// �����������ڳ������壬���Ե�����������false��
	//WavesVB = std::make_unique<UploadBuffer<Vertex>>(device, waveVertCount, false);
	InstanceBuffer = std::make_unique<UploadBuffer<InstanceData>>(device, maxInstanceCount, false);
}

FrameResource::~FrameResource()
{

}