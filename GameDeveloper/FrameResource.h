#pragma once

#include "d3dUtil.h"
#include "UploadBuffer.h"
#include "MathHelper.h"
#include <DirectXPackedVector.h>

using namespace DirectX::PackedVector;
using namespace DirectX;


//���а���ÿ֡��������Ҫ�����ݣ�ÿ֡�����������������ÿ֡�����ĳ�����������ÿ֡��Χ��ֵ��
//˳���ģ���ÿ֡��Ҫ�Ķ��������Լ��������ݽṹ��Ҳһ���ƹ�����
//ͬʱCommandList��CommandAllocator��Щ����Ҳ������Դ�����ǵ��������ڹ�������������Դ���޶��¡�

//���嶥��ṹ��
struct Vertex
{
	Vertex() = default;
	Vertex(float x, float y, float z, float nx, float ny, float nz, float u, float v) :
		Pos(x, y, z),
		Normal(nx,ny,nz),
		TexC(u,v){}

	XMFLOAT3 Pos;
	XMFLOAT3 Normal;
	XMFLOAT2 TexC;
};
//������������峣�����ݣ�����ģ�
struct ObjectConstants
{
	XMFLOAT4X4 World = MathHelper::Identity4x4();
	// ����ÿ�������UV����
	// ��Ϊ��ͼƽ���ܶ��ǲ�ͬ�ģ����Ա�����Բ�ͬ�������ò�ͬ��UV���ž���
	XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();
};
//��������Ĺ��̳�������(ÿ֡�仯)
struct PassConstants
{
	XMFLOAT4X4 viewProj = MathHelper::Identity4x4();

	XMFLOAT3 EyePosW = { 0.0f,0.0f,0.0f };
	//Ϊ��4ά�����Ĵ�����䣬�������4D������Ҳ����˵3D��eyePosW���������һ��1D�����ݣ�������ʱ��float���͵�totalTime��ռλ
	//float TotalTime = 0.0f;
	bool CartoonShader = false;

	XMFLOAT4 FogColor = { 0.7f, 0.7f, 0.7f, 1.0f };
	float gFogStart = 5.0f;// �����ʼ��Χ
	float gFogRange = 150.0f;// ������÷�Χ
	XMFLOAT2 pad2 = { 0.0f, 0.0f };//ռλ	

	float alpha = 0.3f;
	XMFLOAT3 cbPerObjectPad3 = { 0.0f,0.0f,0.0f };//��λ�� ������ά��������

	// �ƹ�����
	XMFLOAT4 AmbientLight = { 0.0f,0.0f,0.0f,1.0f };
	Light Lights[MaxLights];
};

struct FrameResource
{
public:
	FrameResource(ID3D12Device* device, UINT passCount, UINT objectCount, UINT materialCount);
	FrameResource(const FrameResource& rhs) = delete;
	FrameResource& operator=(const FrameResource& rhs) = delete;
	~FrameResource();

	//��GPU����������֮ǰ�������޷����÷�������
	//��ˣ�ÿ��֡����Ҫ�Լ��ķ�������
	//ÿ֡��Դ����Ҫ���������������
	ComPtr<ID3D12CommandAllocator> CmdListAlloc;


	//��GPU����������֮ǰ�������޷�����cbuffer
	//����ÿ��֡����Ҫ�Լ���cbuffer��
	//ÿ֡����Ҫ��������Դ���������˰�����Ϊ2��������������
	std::unique_ptr<UploadBuffer<PassConstants>> PassCB = nullptr;
	std::unique_ptr<UploadBuffer<ObjectConstants>> ObjectCB = nullptr;
	// �������ʻ�����������objCB��passCB��
	std::unique_ptr<UploadBuffer<MaterialConstants>> MaterialCB = nullptr;

	//��GPU��ɴ���֮ǰ�������޷����¶�̬���㻺����
	//���������������ÿ��֡����Ҫ�Լ��ġ�
	//std::unique_ptr<UploadBuffer<Vertex>> WavesVB = nullptr;

	//���ڱ�ǵ���Χ����������Χ��ֵ����������
	//���GPU�Ƿ�����ʹ����Щ֡��Դ��
	//CPU�˵�Χ��ֵ
	UINT64 fenceCPU = 0;
};