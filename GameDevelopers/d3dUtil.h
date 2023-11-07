#pragma once

#include "DxException.h"
#include <wrl.h>
#include <d3d12.h>
#include "d3dx12.h"
#include <unordered_map>
#include <vector>
#include <array>
#include <memory> //��������ָ�� �������
#include <DirectXPackedVector.h>
#include "MathHelper.h"
#include <DirectXCollision.h>


using namespace Microsoft::WRL;

extern const int gNumFrameResources;

class d3dUtil
{
public:
	//�����������Ĵ�С����ΪӲ����С����ռ�(256byte)��������
	//�����������Դ��С�����256����ֵ��������Դ��С255������256����Դ��С257������512��
	static UINT CalcConstantBufferByteSize(UINT byteSize)
	{
		return (byteSize + 255) & ~255;
	}
	// ����Ĭ�϶�
	// GPU��Դ(ID3D12Resource)
	static ComPtr<ID3D12Resource> CreateDefaultBuffer(
		ID3D12Device* device,
		ID3D12GraphicsCommandList* cmdList,
		const void* initData,
		UINT64 byteSize,
		ComPtr<ID3D12Resource>& uploadBuffer
	);

	static ComPtr<ID3DBlob> CompileShader(
		const std::wstring& filename,
		const D3D_SHADER_MACRO* defines,
		const std::string& entrypoint,
		const std::string& target
	);
};


// ������������������� (��DrawIndexedInstanced�����е�1��3��4����)
struct SubmeshGeometry
{
	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	INT BaseVertexLocation = 0;

	DirectX::BoundingBox Bounds;
};

struct MeshGeometry
{
	std::string Name;
	// ��ʵ���Ƿ�װ����
	ComPtr<ID3DBlob> VertexBufferCPU = nullptr;
	ComPtr<ID3DBlob> IndexBufferCPU = nullptr;
	
	//ID3D12Resource����GPU��Դ
	ComPtr<ID3D12Resource> VertexBufferGPU = nullptr;
	ComPtr<ID3D12Resource> IndexBufferGPU = nullptr;

	ComPtr<ID3D12Resource> VertexBufferUploader = nullptr;
	ComPtr<ID3D12Resource> IndexBufferUploader = nullptr;

	UINT VertexBufferByteSize = 0;
	UINT VertexByteStride = 0;
	DXGI_FORMAT IndexFormat = DXGI_FORMAT_R16_UINT;
	UINT IndexBufferByteSize = 0;

	//MeshGeometry������һ������/�����������д洢���������
	//ʹ�ô��������������񼸺�ͼ�Σ��Ա����ǿ��Ի���
	//������������
	// �������ַ�����SubmeshGeometry�ṹ��Ļ�����������һ��ӳ��
	std::unordered_map<std::string, SubmeshGeometry> DrawArgs;
	// ���ض��㻺������ͼ
	D3D12_VERTEX_BUFFER_VIEW VertexBufferView() const
	{
		D3D12_VERTEX_BUFFER_VIEW vbv;
		vbv.BufferLocation = VertexBufferGPU->GetGPUVirtualAddress();//���㻺������Դ�����ַ
		vbv.StrideInBytes = VertexByteStride;//ÿ������Ԫ����ռ�õ��ֽ���
		vbv.SizeInBytes = VertexBufferByteSize;//���㻺������С�����ж������ݴ�С��

		return vbv;
	}
	// ����������������ͼ
	D3D12_INDEX_BUFFER_VIEW IndexBufferView()const
	{
		D3D12_INDEX_BUFFER_VIEW ibv;
		ibv.BufferLocation = IndexBufferGPU->GetGPUVirtualAddress();
		ibv.Format = IndexFormat;
		ibv.SizeInBytes = IndexBufferByteSize;

		return ibv;
	}
	// �ϴ���GPU���ͷ��ڴ�
	void DisposeUploaders()
	{
		VertexBufferUploader = nullptr;
		IndexBufferUploader = nullptr;
	}
};

struct Light
{
	// Ԫ�ص�����˳��һ��Ҫ����������ķ�ʽ����Ԫ�ش��Ϊ4D����
	// �����������з�ʽ�պ��ܴ����3��4D��������һ��XMFLOAT3��һ��float���һ��4D������
	DirectX::XMFLOAT3 Strength = { 0.5f,0.5f,0.5f };//��Դ��ɫ������ͨ�ã�
	float FalloffStart = 1.0f;//���ƺ;۹�ƵĿ�ʼ˥������
	DirectX::XMFLOAT3 Direction = { 0.0f, -1.0f, 0.0f };//�����;۹�Ƶķ�������
	float FalloffEnd = 10.0f;//���;۹�Ƶ�˥����������
	DirectX::XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };//���;۹�Ƶ�����
	float SpotPower = 64.0f;//�۹�������еĲ���
};

#define MaxLights 16

// ������ʳ����ṹ��
struct MaterialConstants
{
	DirectX::XMFLOAT4 DiffuseAlbedo = { 1.0f,1.0f,1.0f,1.0f };
	DirectX::XMFLOAT3 FresnelR0 = { 0.01f,0.01f,0.01f };
	float Roughness = 0.25f;

	DirectX::XMFLOAT4X4 MatTransform = MathHelper::Identity4x4();//������λ�ƾ���
};

struct Material
{
	std::string Name;

	int MatCBIndex = -1;//���ʳ����������е�����

	int DiffuseSrvHeapIndex = -1;//�����������SRV������

	//��־��ʾ�����Ѹ��ģ�������Ҫ���³�����������
	//��Ϊ����Ϊÿ��FrameResource����һ�����ʳ������������������Ǳ���Ӧ��
	//���µ�ÿ��FrameResource����ˣ��������޸Ĳ���ʱ������Ӧ������
	//NumFramesDirty=gNumFrameResources���Ա�ÿ��֡��Դ���ܵõ����¡�
	int NumFramesDirty = gNumFrameResources;

	//������ɫ�Ĳ��ʳ�������������
	DirectX::XMFLOAT4 DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };//���ʷ�����
	DirectX::XMFLOAT3 FresnelR0 = { 0.01f,0.01f,0.01f };//RF(0)ֵ�������ʵķ�������
	float Roughness = 0.25f;//���ʵĴֲڶ�

	DirectX::XMFLOAT4X4 MatTransform = MathHelper::Identity4x4();

};
// ���Կ��� ������ԴҲ���Ⱦ����ϴ��ѣ��ٿ�������Ĭ�϶ѵ�
struct Texture
{
	std::string Name;//������
	std::wstring Filename;//��������·����Ŀ¼��

	// Resource�൱�ڴ��ݵ�����GPU���ʵ���Դ
	ComPtr<ID3D12Resource> Resource = nullptr;//���ص�������Դ
	// UploadHeap�൱���ϴ���
	ComPtr<ID3D12Resource> UploadHeap = nullptr;//���ص��ϴ����е�������Դ
};