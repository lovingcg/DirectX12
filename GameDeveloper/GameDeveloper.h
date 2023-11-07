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

const int gNumFrameResources = 3;//CPU�ύ3֡��Դ������������ݵȣ�

//���嶥��ṹ��
//struct Vertex
//{
//	XMFLOAT3 Pos;
//	//XMFLOAT4 Color;
//	XMFLOAT4 Color;
//};
//���峣��������
// ��������ĳ�������
//struct ObjectConstants
//{
//	XMFLOAT4X4 WorldViewProj= MathHelper::Identity4x4();
//	float gTime = 0.0f;
//};

//���������������ó�2�� �Ż�
//struct ObjectConstants
//{
//	XMFLOAT4X4 world = MathHelper::Identity4x4();
//};
//struct PassConstants
//{
//	XMFLOAT4X4 viewProj = MathHelper::Identity4x4();
//};

// �洢���������壨����ģ��ʵ�����е���Ⱦ���ݵ�һ�����ݽṹ���ͳ�����Ⱦ��
struct RenderItem
{
	RenderItem() = default;
	//�ü�������������
	XMFLOAT4X4 World = MathHelper::Identity4x4();

	XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

	//NumFramesDirty = gNumFrameResourcesʹ��ÿ��֡��Դ���õ�����
	int NumFramesDirty = gNumFrameResources;

	// �ü�����ĳ���������objConstantBuffer�е�����
	UINT ObjCBIndex = -1;

	MeshGeometry* Geo = nullptr;
	Material* Mat = nullptr;//�ò�ͬ����Ⱦ�����ͬ�Ĳ�����

	//�ü������ͼԪ��������
	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	//�ü�����Ļ���������
	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	int BaseVertexLocation = 0;
};
// enum class�ܹ���Ч��ö������������޶���������ö�ٳ�Ա���ض����ö�ٳ�Ա���ƺ��ⲿ���Ա�������Ƶ��ض���
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
	void BuildRoomGeometry();// ����ģ����Գ���
	void BuildLandGeometry();//����ɽ��
	void BuildWavesGeometryBuffers();//����������������������
	void BuildSkullGeometry();
	void BuildDescriptorHeaps();// �����������ѣ������������ѣ�
	void BuildRootSignature();// ������ǩ��
	void BuildShadersAndInputLayout();//�������벼�������ͱ�����ɫ���ֽ���
	void BuildPSO();// ������Ⱦ��ˮ��״̬
	void BuildFrameResources();// ����֡��Դ
	void BuildMaterials();//��������
	void BuildRenderItems();//������Ⱦ��
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList,const std::vector<RenderItem*>& ritems);

	D3D12_VERTEX_BUFFER_VIEW VertexBufferView() const;
	D3D12_INDEX_BUFFER_VIEW IndexBufferView() const;

	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();

	float GetHillsHeight(float x, float z)	const;
	XMFLOAT3 GetHillsNormal(float x, float z)	const;

private:

	std::vector<std::unique_ptr<FrameResource>> mFrameResources;//֡��Դ����
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

	//ID3D12Resource����GPU��Դ
	ComPtr<ID3D12Resource> mVertexBufferGPU = nullptr;
	ComPtr<ID3D12Resource> mIndexBufferGPU = nullptr;

	//�ø�ǩ����������Ϳ��Բ�����CBV��
	ComPtr<ID3D12DescriptorHeap> mCbvHeap = nullptr;// ����������������
	ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

	// ���������������ó�2�� �Ż�
	std::unique_ptr<UploadBuffer<ObjectConstants>> mObjectCB = nullptr;// �����ϴ���
	std::unique_ptr<UploadBuffer<PassConstants>> mPassCB = nullptr;

	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;// ��ǩ��

	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;//���벼�������ṹ��

	RenderItem* mSkullRitem = nullptr;
	RenderItem* mShadowedSkullRitem = nullptr;//������Ӱ��Ⱦ��
	RenderItem* mReflectedSkullRitem = nullptr;
	// û�õ���Ⱦ��ǵ�ע�͵� ��Ȼ����������˸
	RenderItem* mFloorMirrorRitem = nullptr;
	RenderItem* mReflectedShadowedSkullRitem = nullptr;

	// ��ɫ���ֶ� ����ɿ���ֲ���ֽ���
	ComPtr<ID3DBlob> mVsByteCode = nullptr;
	ComPtr<ID3DBlob> mPsByteCode = nullptr;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
	std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

	//������Ⱦ��
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;
	//��Ⱦ��PSO���ֵ���Ŀ
	//std::vector<RenderItem*> mOpaqueRitems;
	// ��Ⱦ�PSO
	std::vector<RenderItem*> mRitemLayer[(int)RenderLayer::Count];

	std::unique_ptr<Waves> mWaves;

	PassConstants mMainPassCB;
	PassConstants mReflectedPassCB;

	XMFLOAT3 mSkullTranslation = { 0.0f, 1.0f, -5.0f };
	float mSkullrotate = 0.38f;

	UINT mPassCbvOffset = 0;//��ƫ�������浽passCBV���۲�ͶӰ���󣩵���㡣�������3����������

	ComPtr<ID3D12PipelineState> mPSO = nullptr;// ��Ⱦ��ˮ��״̬����

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