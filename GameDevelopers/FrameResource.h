#pragma once

#include "d3dUtil.h"
#include "UploadBuffer.h"
#include "MathHelper.h"
#include <DirectXPackedVector.h>

using namespace DirectX::PackedVector;
using namespace DirectX;


//类中包括每帧绘制所需要的数据：每帧独立的命令分配器、每帧独立的常量缓冲区、每帧的围栏值。
//顺带的，把每帧需要的顶点索引以及常量数据结构体也一并移过来。
//同时CommandList，CommandAllocator这些本质也属于资源，它们的声明周期管理方法和其他资源并无二致。

//定义顶点结构体
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
//单个物体的物体常量数据（不变的）
//struct ObjectConstants
//{
//	XMFLOAT4X4 World = MathHelper::Identity4x4();
//	// 设置每个物体的UV缩放
//	// 因为贴图平铺密度是不同的，所以必须针对不同物体设置不同的UV缩放矩阵
//	XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();
//
//	//不同物体去索引对应材质
//	UINT     MaterialIndex;
//	UINT     ObjPad0;
//	UINT     ObjPad1;
//	UINT     ObjPad2;
//};

struct InstanceData
{
	XMFLOAT4X4 World = MathHelper::Identity4x4();
	XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

	// 不同物体去索引对应材质
	UINT MaterialIndex;
	UINT InstancePad0;
	UINT InstancePad1;
	UINT InstancePad2;
};

//单个物体的过程常量数据(每帧变化)
struct PassConstants
{
	XMFLOAT4X4 viewProj = MathHelper::Identity4x4();

	XMFLOAT3 EyePosW = { 0.0f,0.0f,0.0f };
	//为了4维向量的打包传输，必须对齐4D向量，也就是说3D的eyePosW后面必须有一个1D的数据，所以暂时用float类型的totalTime来占位
	//float TotalTime = 0.0f;
	bool CartoonShader = false;

	XMFLOAT4 FogColor = { 0.7f, 0.7f, 0.7f, 1.0f };
	float gFogStart = 5.0f;// 雾的起始范围
	float gFogRange = 150.0f;// 雾的作用范围
	XMFLOAT2 pad2 = { 0.0f, 0.0f };//占位	

	float alpha = 0.3f;
	XMFLOAT3 cbPerObjectPad3 = { 0.0f,0.0f,0.0f };//补位数 必须四维向量对齐

	// 灯光数据
	XMFLOAT4 AmbientLight = { 0.0f,0.0f,0.0f,1.0f };
	Light Lights[MaxLights];
};
// 对于主Pass常量，每帧只需要传递一次给Shader。而对于纹理、物体常量和材质，需要每一帧对每一个渲染项进行传递Shader。
// 而动态索引就是让纹理和材质像主Pass常量传递一样，每帧只需要传递一次，把材质和纹理的选择交给Shader进行处理。
struct MaterialData
{
	DirectX::XMFLOAT4 DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };//材质反照率
	DirectX::XMFLOAT3 FresnelR0 = { 0.01f, 0.01f, 0.01f };//RF(0)值，即材质的反射属性
	float Roughness = 64.0f;//材质的粗糙度

	DirectX::XMFLOAT4X4 MatTransform = MathHelper::Identity4x4();//纹理动画位移矩阵

	UINT DiffuseMapIndex = 0;//纹理数组索引
	//占位，向量数据打包时必须占满4位
	UINT MaterialPad0;
	UINT MaterialPad1;
	UINT MaterialPad2;
};

struct FrameResource
{
public:
	FrameResource(ID3D12Device* device, UINT passCount, UINT maxInstanceCount, UINT materialCount);
	FrameResource(const FrameResource& rhs) = delete;
	FrameResource& operator=(const FrameResource& rhs) = delete;
	~FrameResource();

	//在GPU处理完命令之前，我们无法重置分配器。
	//因此，每个帧都需要自己的分配器。
	//每帧资源都需要独立的命令分配器
	ComPtr<ID3D12CommandAllocator> CmdListAlloc;


	//在GPU处理完命令之前，我们无法更新cbuffer
	//所以每个帧都需要自己的cbuffer。
	//每帧都需要单独的资源缓冲区（此案例仅为2个常量缓冲区）
	std::unique_ptr<UploadBuffer<PassConstants>> PassCB = nullptr;
	//std::unique_ptr<UploadBuffer<ObjectConstants>> ObjectCB = nullptr;
	// 创建材质缓冲区（类似objCB和passCB）
	//std::unique_ptr<UploadBuffer<MaterialConstants>> MaterialCB = nullptr;

	std::unique_ptr<UploadBuffer<MaterialData>> MaterialBuffer = nullptr;

	//指向结构化缓冲区InstanceData的智能指针
	std::unique_ptr<UploadBuffer<InstanceData>> InstanceBuffer = nullptr;

	//用于标记到此围栏点的命令的围栏值。这让我们
	//检查GPU是否仍在使用这些帧资源。
	//CPU端的围栏值
	UINT64 fenceCPU = 0;
};