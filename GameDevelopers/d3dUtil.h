#pragma once

#include "DxException.h"
#include <wrl.h>
#include <d3d12.h>
#include "d3dx12.h"
#include <unordered_map>
#include <vector>
#include <array>
#include <memory> //用于智能指针 必须加上
#include <DirectXPackedVector.h>
#include "MathHelper.h"
#include <DirectXCollision.h>


using namespace Microsoft::WRL;

extern const int gNumFrameResources;

class d3dUtil
{
public:
	//常量缓冲区的大小必须为硬件最小分配空间(256byte)整数倍。
	//返回离这个资源大小最近的256倍数值（比如资源大小255，返回256，资源大小257，返回512）
	static UINT CalcConstantBufferByteSize(UINT byteSize)
	{
		return (byteSize + 255) & ~255;
	}
	// 创建默认堆
	// GPU资源(ID3D12Resource)
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


// 绘制子物体的三个属性 (即DrawIndexedInstanced函数中的1、3、4参数)
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
	// 其实就是封装起来
	ComPtr<ID3DBlob> VertexBufferCPU = nullptr;
	ComPtr<ID3DBlob> IndexBufferCPU = nullptr;
	
	//ID3D12Resource代表GPU资源
	ComPtr<ID3D12Resource> VertexBufferGPU = nullptr;
	ComPtr<ID3D12Resource> IndexBufferGPU = nullptr;

	ComPtr<ID3D12Resource> VertexBufferUploader = nullptr;
	ComPtr<ID3D12Resource> IndexBufferUploader = nullptr;

	UINT VertexBufferByteSize = 0;
	UINT VertexByteStride = 0;
	DXGI_FORMAT IndexFormat = DXGI_FORMAT_R16_UINT;
	UINT IndexBufferByteSize = 0;

	//MeshGeometry可以在一个顶点/索引缓冲区中存储多个几何体
	//使用此容器定义子网格几何图形，以便我们可以绘制
	//单独的子网格
	// 将名字字符串和SubmeshGeometry结构里的绘制三参数做一个映射
	std::unordered_map<std::string, SubmeshGeometry> DrawArgs;
	// 返回顶点缓冲区视图
	D3D12_VERTEX_BUFFER_VIEW VertexBufferView() const
	{
		D3D12_VERTEX_BUFFER_VIEW vbv;
		vbv.BufferLocation = VertexBufferGPU->GetGPUVirtualAddress();//顶点缓冲区资源虚拟地址
		vbv.StrideInBytes = VertexByteStride;//每个顶点元素所占用的字节数
		vbv.SizeInBytes = VertexBufferByteSize;//顶点缓冲区大小（所有顶点数据大小）

		return vbv;
	}
	// 返回索引缓冲区视图
	D3D12_INDEX_BUFFER_VIEW IndexBufferView()const
	{
		D3D12_INDEX_BUFFER_VIEW ibv;
		ibv.BufferLocation = IndexBufferGPU->GetGPUVirtualAddress();
		ibv.Format = IndexFormat;
		ibv.SizeInBytes = IndexBufferByteSize;

		return ibv;
	}
	// 上传到GPU后释放内存
	void DisposeUploaders()
	{
		VertexBufferUploader = nullptr;
		IndexBufferUploader = nullptr;
	}
};

struct Light
{
	// 元素的排列顺序，一定要按照填充对齐的方式，将元素打包为4D向量
	// 下面代码的排列方式刚好能打包成3个4D向量（即一个XMFLOAT3和一个float组成一个4D向量）
	DirectX::XMFLOAT3 Strength = { 0.5f,0.5f,0.5f };//光源颜色（三光通用）
	float FalloffStart = 1.0f;//点光灯和聚光灯的开始衰减距离
	DirectX::XMFLOAT3 Direction = { 0.0f, -1.0f, 0.0f };//方向光和聚光灯的方向向量
	float FalloffEnd = 10.0f;//点光和聚光灯的衰减结束距离
	DirectX::XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };//点光和聚光灯的坐标
	float SpotPower = 64.0f;//聚光灯因子中的参数
};

#define MaxLights 16

// 定义材质常量结构体
struct MaterialConstants
{
	DirectX::XMFLOAT4 DiffuseAlbedo = { 1.0f,1.0f,1.0f,1.0f };
	DirectX::XMFLOAT3 FresnelR0 = { 0.01f,0.01f,0.01f };
	float Roughness = 0.25f;

	DirectX::XMFLOAT4X4 MatTransform = MathHelper::Identity4x4();//纹理动画位移矩阵
};

struct Material
{
	std::string Name;

	int MatCBIndex = -1;//材质常量缓冲区中的索引

	int DiffuseSrvHeapIndex = -1;//漫反射纹理的SRV堆索引

	//标志表示材料已更改，我们需要更新常量缓冲区。
	//因为我们为每个FrameResource都有一个材质常量缓冲区，所以我们必须应用
	//更新到每个FrameResource。因此，当我们修改材料时，我们应该设置
	//NumFramesDirty=gNumFrameResources，以便每个帧资源都能得到更新。
	int NumFramesDirty = gNumFrameResources;

	//用于着色的材质常量缓冲区数据
	DirectX::XMFLOAT4 DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };//材质反照率
	DirectX::XMFLOAT3 FresnelR0 = { 0.01f,0.01f,0.01f };//RF(0)值，即材质的反射属性
	float Roughness = 0.25f;//材质的粗糙度

	DirectX::XMFLOAT4X4 MatTransform = MathHelper::Identity4x4();

};
// 可以看出 纹理资源也是先经过上传堆，再拷贝进入默认堆的
struct Texture
{
	std::string Name;//纹理名
	std::wstring Filename;//纹理所在路径的目录名

	// Resource相当于传递到能让GPU访问的资源
	ComPtr<ID3D12Resource> Resource = nullptr;//返回的纹理资源
	// UploadHeap相当于上传堆
	ComPtr<ID3D12Resource> UploadHeap = nullptr;//返回的上传堆中的纹理资源
};