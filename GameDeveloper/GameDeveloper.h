#pragma once

#include "D3DApp.h"
#include "d3dUtil.h"
#include "MathHelper.h"
#include "UploadBuffer.h"
#include <DirectXPackedVector.h>
#include "ProceduralGeometry.h"
#include "FrameResource.h"
#include "Waves.h"
#include "DDSTextureLoader.h"

using namespace DirectX;
using namespace DirectX::PackedVector;

const int gNumFrameResources = 3;//CPU提交3帧资源（命令、常量数据等）

//定义顶点结构体
//struct Vertex
//{
//	XMFLOAT3 Pos;
//	//XMFLOAT4 Color;
//	XMFLOAT4 Color;
//};
//定义常量缓冲区
// 单个物体的常量数据
//struct ObjectConstants
//{
//	XMFLOAT4X4 WorldViewProj= MathHelper::Identity4x4();
//	float gTime = 0.0f;
//};

//将常量缓冲区设置成2个 优化
//struct ObjectConstants
//{
//	XMFLOAT4X4 world = MathHelper::Identity4x4();
//};
//struct PassConstants
//{
//	XMFLOAT4X4 viewProj = MathHelper::Identity4x4();
//};

// 存储单个几何体（包括模型实例）中的渲染数据的一种数据结构，就称作渲染项
struct RenderItem
{
	RenderItem() = default;
	//该几何体的世界矩阵
	XMFLOAT4X4 World = MathHelper::Identity4x4();

	XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

	//NumFramesDirty = gNumFrameResources使得每个帧资源都得到更新
	int NumFramesDirty = gNumFrameResources;

	// 该几何体的常量数据在objConstantBuffer中的索引
	UINT ObjCBIndex = -1;

	MeshGeometry* Geo = nullptr;
	Material* Mat = nullptr;//让不同的渲染项赋予相同的材质了

	//该几何体的图元拓扑类型
	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	//该几何体的绘制三参数
	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	int BaseVertexLocation = 0;
};
// enum class能够有效对枚举作用域进行限定，避免了枚举成员的重定义和枚举成员名称和外部类成员函数名称的重定义
enum class RenderLayer : int
{
	Opaque = 0,
	Mirrors,
	Reflected,
	Transparent,
	Shadow,
	ReflectedShadow,
	Count
};

class GameDevelopApp :public D3DApp
{
public:
	GameDevelopApp();
	~GameDevelopApp();

	virtual bool Init(HINSTANCE hInstance, int nShowCmd) override;

private:
	virtual void OnResize()override;
	virtual void Draw() override;
	virtual void Update() override;
	
	void OnKeyboardInput(const _GameTimer::GameTimer& gt);
	void OnKeyboardInputMove(const _GameTimer::GameTimer& gt);
	void UpdateCamera(const _GameTimer::GameTimer& gt);
	void AnimateMaterials(const _GameTimer::GameTimer& gt);
	void UpdateObjectCBs(const _GameTimer::GameTimer& gt);
	void UpdateMaterialCBs(const _GameTimer::GameTimer& gt);
	void UpdateMainPassCB(const _GameTimer::GameTimer& gt);
	void UpdateReflectedPassCB(const _GameTimer::GameTimer& gt);

	virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
	virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
	virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

	void LoadTextures();
	void BuildBoxGeometry();
	void BuildRoomGeometry();// 构建模板测试场景
	void BuildLandGeometry();//创建山川
	void BuildWavesGeometryBuffers();//构建“湖泊”索引缓冲区
	void BuildSkullGeometry();
	void BuildDescriptorHeaps();// 创建描述符堆（常量描述符堆）
	void BuildRootSignature();// 创建根签名
	void BuildShadersAndInputLayout();//创建输入布局描述和编译着色器字节码
	void BuildPSO();// 构建渲染流水线状态
	void BuildFrameResources();// 构建帧资源
	void BuildMaterials();//创建材质
	void BuildRenderItems();//构建渲染项
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList,const std::vector<RenderItem*>& ritems);

	D3D12_VERTEX_BUFFER_VIEW VertexBufferView() const;
	D3D12_INDEX_BUFFER_VIEW IndexBufferView() const;

	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();

	float GetHillsHeight(float x, float z)	const;
	XMFLOAT3 GetHillsNormal(float x, float z)	const;

private:

	std::vector<std::unique_ptr<FrameResource>> mFrameResources;//帧资源数组
	FrameResource* mCurrFrameResource = nullptr;
	int mCurrFrameResourceIndex = 0;

	UINT mCbvSrvDescriptorSize = 0;

	UINT mVertexBufferByteSize = 0;
	UINT mVertexByteStride = 0;
	DXGI_FORMAT mIndexFormat = DXGI_FORMAT_R16_UINT;
	UINT mIndexBufferByteSize = 0;

	UINT mIndexCount = 0;

	ComPtr<ID3DBlob> mVertexBufferCPU = nullptr;
	ComPtr<ID3DBlob> mIndexBufferCPU = nullptr;

	ComPtr<ID3D12Resource> mVertexBufferUploader = nullptr;
	ComPtr<ID3D12Resource> mIndexBufferUploader = nullptr;

	//ID3D12Resource代表GPU资源
	ComPtr<ID3D12Resource> mVertexBufferGPU = nullptr;
	ComPtr<ID3D12Resource> mIndexBufferGPU = nullptr;

	//用根签名描述符后就可以不设置CBV了
	ComPtr<ID3D12DescriptorHeap> mCbvHeap = nullptr;// 常量缓冲描述符堆
	ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

	// 将常量缓冲区设置成2个 优化
	std::unique_ptr<UploadBuffer<ObjectConstants>> mObjectCB = nullptr;// 常量上传堆
	std::unique_ptr<UploadBuffer<PassConstants>> mPassCB = nullptr;

	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;// 根签名

	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;//输入布局描述结构体

	RenderItem* mSkullRitem = nullptr;
	RenderItem* mShadowedSkullRitem = nullptr;//骷髅阴影渲染项
	RenderItem* mReflectedSkullRitem = nullptr;
	// 没用的渲染项记得注释掉 不然画面会出现闪烁
	RenderItem* mFloorMirrorRitem = nullptr;
	RenderItem* mReflectedShadowedSkullRitem = nullptr;

	// 着色器字段 编译成可移植的字节码
	ComPtr<ID3DBlob> mVsByteCode = nullptr;
	ComPtr<ID3DBlob> mPsByteCode = nullptr;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
	std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

	//所有渲染项
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;
	//渲染按PSO划分的项目
	//std::vector<RenderItem*> mOpaqueRitems;
	// 渲染项按PSO
	std::vector<RenderItem*> mRitemLayer[(int)RenderLayer::Count];

	std::unique_ptr<Waves> mWaves;

	PassConstants mMainPassCB;
	PassConstants mReflectedPassCB;

	XMFLOAT3 mSkullTranslation = { 0.0f, 1.0f, -5.0f };
	float mSkullrotate = 0.38f;

	UINT mPassCbvOffset = 0;//将偏移量保存到passCBV（观察投影矩阵）的起点。这是最后3个描述符。

	ComPtr<ID3D12PipelineState> mPSO = nullptr;// 渲染流水线状态对象

	XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
	XMFLOAT4X4 mWorld = MathHelper::Identity4x4();
	XMFLOAT4X4 mView = MathHelper::Identity4x4();
	XMFLOAT4X4 mProj = MathHelper::Identity4x4();

	float mTheta = 1.24f * XM_PI;
	float mPhi = 0.42f*XM_PIDIV4;
	float mRadius = 15.0f;

	float mSunTheta = 1.25f * XM_PI;
	float mSunPhi = XM_PIDIV4;

	POINT mLastMousePos;

	float clearColor[4] = { 0.07f, 0.148f, 0.285f,1.0f };

	bool animateGeo = true;
	bool is_wiremode = false;
	bool is_sunmove = true;
	bool is_skullmove = true;
	bool is_ctshader = false;
	bool fog = false;
	float fov = 0.25f;

	std::unique_ptr<MeshGeometry> geo;
};