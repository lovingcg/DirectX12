#include "d3dApp.h"
#include "MathHelper.h"
#include "UploadBuffer.h"
#include "GeometryGenerator.h"
#include "Camera.h"
#include "FrameResource.h"
#include "CubeRenderTarget.h"
#include "d3dUtil.h"
#include <DirectXPackedVector.h>
#include "DDSTextureLoader.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx12.h"
#include "ShadowMap.h"
#include "Ssao.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")

const int gNumFrameResources = 3;

const UINT CubeMapSize = 512;

// �洢���������壨����ģ��ʵ�����е���Ⱦ���ݵ�һ�����ݽṹ���ͳ�����Ⱦ��
struct RenderItem
{
	RenderItem() = default;
	RenderItem(const RenderItem& rhs) = delete;

	XMFLOAT4X4 World = MathHelper::Identity4x4();

	XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

	//NumFramesDirty = gNumFrameResourcesʹ��ÿ��֡��Դ���õ�����
	int NumFramesDirty = gNumFrameResources;

	UINT ObjCBIndex = -1;

	Material* Mat = nullptr;
	MeshGeometry* Geo = nullptr;

	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	int BaseVertexLocation = 0;
};

enum class RenderLayer : int
{
	Opaque = 0,
	Opaquess,
	Debug,
	OpaqueDynamicReflectors,
	Sky,
	Count
};

class GameDevelopApp : public D3DApp
{
public:
	GameDevelopApp(HINSTANCE hInstance);
	GameDevelopApp(const GameDevelopApp& rhs) = delete;
	GameDevelopApp& operator=(const GameDevelopApp& rhs) = delete;
	~GameDevelopApp();

	virtual bool Initialize()override;

private:
	virtual void CreateRtvAndDsvDescriptorHeaps()override;
	virtual void OnResize()override;
	virtual void Update(const GameTimer& gt)override;
	virtual void Draw(const GameTimer& gt)override;

	virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
	virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
	virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

	void OnKeyboardInput(const GameTimer& gt);
	void OnKeyboardInputSkyBox(const GameTimer& gt);
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateMaterialBuffer(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);
	void UpdateCubeMapFacePassCBs();
	void UpdateShadowTransform(const GameTimer& gt);
	void UpdateShadowPassCB(const GameTimer& gt);

	void LoadTextures();
	void BuildRootSignature();
	void BuildDescriptorHeaps();
	void BuildCubeDepthStencil();
	void BuildShadersAndInputLayout();
	void BuildSkullGeometry();
	void BuildShapeGeometry();
	void BuildPSOs();
	void BuildFrameResources();
	void BuildMaterials();
	void BuildRenderItems();
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);
	void DrawSceneToCubeMap();
	void DrawSceneToShadowMap();

	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> GetStaticSamplers();
	void BuildCubeFaceCamera(float x, float y, float z);

private:

	std::vector<std::unique_ptr<FrameResource>> mFrameResources;
	FrameResource* mCurrFrameResource = nullptr;
	int mCurrFrameResourceIndex = 0;

	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;

	ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

	ComPtr<ID3D12Resource> mCubeDepthStencilBuffer;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
	std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

	// List of all the render items.
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;

	// Render items divided by PSO.
	std::vector<RenderItem*> mRitemLayer[(int)RenderLayer::Count];

	UINT mSkyTexHeapIndex = 0;
	UINT mSkyTexHeapIndex1 = 0;
	UINT mSkyTexHeapIndex2 = 0;
	UINT mSkyTexHeapIndex3 = 0;
	UINT mDynamicTexHeapIndex = 0;
	UINT mShadowMapHeapIndex = 0;

	RenderItem* mSkullRitem = nullptr;
	RenderItem* mSkyBoxRitem = nullptr;
	RenderItem* mglobeRitem = nullptr;
	RenderItem* mgridRitem = nullptr;

	std::unique_ptr<CubeRenderTarget> mDynamicCubeMap = nullptr;
	CD3DX12_CPU_DESCRIPTOR_HANDLE mCubeDSV;

	std::unique_ptr<ShadowMap> mShadowMap;

	std::unique_ptr<Ssao> mSsao;

	DirectX::BoundingSphere mSceneBounds;

	float mLightNearZ = 0.0f;
	float mLightFarZ = 0.0f;
	XMFLOAT3 mLightPosW;
	XMFLOAT4X4 mLightView = MathHelper::Identity4x4();
	XMFLOAT4X4 mLightProj = MathHelper::Identity4x4();
	XMFLOAT4X4 mShadowTransform = MathHelper::Identity4x4();

	PassConstants mMainPassCB;
	PassConstants mShadowPassCB;// index 7 of pass cbuffer.

	Camera mCamera;
	float mx = 0.0f;
	float my = 2.0f;
	float mz = -15.0f;
	Camera mCubeMapCamera[6];

	POINT mLastMousePos;

	bool animateGeo = true;
	float fov = 0.25f;
	float clearColor[4] = { 0.07f, 0.148f, 0.285f,1.0f };
	bool is_sunmove = true;
	bool is_rorate = true;

	bool is_skybox = false;
	int flag = 1;

	float mX = 0.98f;
	float mY = 0.98f;
	float mZ = 0.98f;
	float mR = 0.01f;

	bool reflect = true;
	bool refract = false;
	int reflectrefract = 0;
	float eta = 0.664332f;

	bool normalmap = true;

	float mLightRotationAngle = 0.0f;
	XMFLOAT3 mBaseLightDirections[3] = {
		XMFLOAT3(0.57735f, -0.57735f, 0.57735f),
		XMFLOAT3(-0.57735f, -0.57735f, 0.57735f),
		XMFLOAT3(0.0f, -0.707f, -0.707f)
	};
	XMFLOAT3 mRotatedLightDirections[3];

	bool is_shadowmap = false;
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
	PSTR cmdLine, int showCmd)
{
	// Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	try
	{
		GameDevelopApp theApp(hInstance);
		if (!theApp.Initialize())
			return 0;

		return theApp.Run();
	}
	catch (DxException& e)
	{
		MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
		return 0;
	}
}

GameDevelopApp::GameDevelopApp(HINSTANCE hInstance)
	: D3DApp(hInstance)
{
	mSceneBounds.Center = XMFLOAT3(0.0f, 0.0f, 0.0f);
	mSceneBounds.Radius = sqrtf(10.0f * 10.0f + 15.0f * 15.0f);
}

GameDevelopApp::~GameDevelopApp()
{
	if (md3dDevice != nullptr)
		FlushCommandQueue();
}

bool GameDevelopApp::Initialize()
{
	if (!D3DApp::Initialize())
		return false;

	// Reset the command list to prep for initialization commands.
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	mCamera.SetPosition(mx, my, mz);

	BuildCubeFaceCamera(0.0f, 2.0f, 0.0f);

	mDynamicCubeMap = std::make_unique<CubeRenderTarget>(md3dDevice.Get(),
		CubeMapSize, CubeMapSize, DXGI_FORMAT_R8G8B8A8_UNORM);

	mShadowMap = std::make_unique<ShadowMap>(
		md3dDevice.Get(), 2048, 2048);

	

	LoadTextures();
	BuildRootSignature();//��ǩ��
	BuildDescriptorHeaps();//��������
	BuildCubeDepthStencil();
	BuildShadersAndInputLayout();
	BuildSkullGeometry();
	BuildShapeGeometry();
	BuildMaterials();
	BuildRenderItems();
	BuildFrameResources();
	BuildPSOs();

	// Execute the initialization commands.
	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Wait until initialization is complete.
	FlushCommandQueue();

	return true;
}

void GameDevelopApp::CreateRtvAndDsvDescriptorHeaps()
{
	// Add +6 RTV for cube render target.
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
	rtvHeapDesc.NumDescriptors = SwapChainBufferCount + 6;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
		&rtvHeapDesc, IID_PPV_ARGS(mRtvHeap.GetAddressOf())));

	// Add +1 DSV for cube render target.
	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
	dsvHeapDesc.NumDescriptors = 2+1;//����1��CubeMap��DSV��1��ShadowMap��DSV
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
		&dsvHeapDesc, IID_PPV_ARGS(mDsvHeap.GetAddressOf())));

	mCubeDSV = CD3DX12_CPU_DESCRIPTOR_HANDLE(
		mDsvHeap->GetCPUDescriptorHandleForHeapStart(),
		1,
		mDsvDescriptorSize);

	// imguiҪ�õ�һ��SRV
	D3D12_DESCRIPTOR_HEAP_DESC SrvHeapDesc;
	SrvHeapDesc.NumDescriptors = 1;
	SrvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	SrvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	SrvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
		&SrvHeapDesc, IID_PPV_ARGS(mSrvHeap.GetAddressOf())));
}

void GameDevelopApp::OnResize()
{
	D3DApp::OnResize();

	mCamera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
}

void GameDevelopApp::Update(const GameTimer& gt)
{
	OnKeyboardInput(gt);

	//
	// Animate the skull around the center sphere.
	//
	XMMATRIX skullScale = XMMatrixScaling(0.2f, 0.2f, 0.2f);
	XMMATRIX skullOffset = XMMatrixTranslation(3.0f, 2.0f, 0.0f);
	XMMATRIX skullLocalRotate = XMMatrixRotationY(2.0f * gt.TotalTime());
	XMMATRIX skullGlobalRotate = XMMatrixRotationY(0.5f * gt.TotalTime());
	if (is_rorate)
	{
		XMStoreFloat4x4(&mSkullRitem->World, skullScale * skullLocalRotate * skullOffset * skullGlobalRotate);
	}
	else
	{
		XMStoreFloat4x4(&mSkullRitem->World, skullScale * skullOffset * skullGlobalRotate);
	}
	mSkullRitem->NumFramesDirty = gNumFrameResources;

	// ������������Ĳ���
	auto mirror0 = std::make_unique<Material>();
	mirror0->Name = "mirror1";
	mirror0->MatCBIndex = 9;
	mirror0->DiffuseSrvHeapIndex = 2;
	// ʵʱ���¼ǵø�
	mirror0->NormalSrvHeapIndex = 6;
	mirror0->DiffuseAlbedo = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
	mirror0->FresnelR0 = XMFLOAT3(mX, mY, mZ);
	mirror0->Roughness = mR;
	mMaterials["mirror1"] = std::move(mirror0);
	
	mglobeRitem->Mat = mMaterials["mirror1"].get();
	// ���²��ʵ�����ʱ�����������ü���
	mglobeRitem->NumFramesDirty = gNumFrameResources;

	if (normalmap)
	{
		auto tile0 = std::make_unique<Material>();
		tile0->Name = "tile1";
		tile0->MatCBIndex = 1;
		tile0->DiffuseSrvHeapIndex = 1;
		tile0->NormalSrvHeapIndex = 5;
		tile0->DiffuseAlbedo = XMFLOAT4(0.9f, 0.9f, 0.9f, 1.0f);
		tile0->FresnelR0 = XMFLOAT3(0.2f, 0.2f, 0.2f);
		tile0->Roughness = 0.1f;
		mMaterials["tile1"] = std::move(tile0);

		mgridRitem->Mat = mMaterials["tile1"].get();
		// ���²��ʵ�����ʱ�����������ü���
		mgridRitem->NumFramesDirty = gNumFrameResources;
	}
	if (!normalmap)
	{
		auto tile0 = std::make_unique<Material>();
		tile0->Name = "tile1";
		tile0->MatCBIndex = 1;
		tile0->DiffuseSrvHeapIndex = 1;
		tile0->NormalSrvHeapIndex = 6;
		tile0->DiffuseAlbedo = XMFLOAT4(0.9f, 0.9f, 0.9f, 1.0f);
		tile0->FresnelR0 = XMFLOAT3(0.2f, 0.2f, 0.2f);
		tile0->Roughness = 0.1f;
		mMaterials["tile1"] = std::move(tile0);

		mgridRitem->Mat = mMaterials["tile1"].get();
		// ���²��ʵ�����ʱ�����������ü���
		mgridRitem->NumFramesDirty = gNumFrameResources;
	}

	// Cycle through the circular frame resource array.
	mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
	mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();


	if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}

	//
	// Animate the lights (and hence shadows).
	//
	mLightRotationAngle += 0.1f * gt.DeltaTime();

	XMMATRIX R = XMMatrixRotationY(mLightRotationAngle);
	for (int i = 0; i < 3; ++i)
	{
		XMVECTOR lightDir = XMLoadFloat3(&mBaseLightDirections[i]);
		lightDir = XMVector3TransformNormal(lightDir, R);
		XMStoreFloat3(&mRotatedLightDirections[i], lightDir);
	}

	UpdateObjectCBs(gt);
	UpdateMaterialBuffer(gt);
	UpdateShadowTransform(gt);
	UpdateMainPassCB(gt);
	UpdateCubeMapFacePassCBs();
	UpdateShadowPassCB(gt);
}

void GameDevelopApp::Draw(const GameTimer& gt)
{
	auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

	ThrowIfFailed(cmdListAlloc->Reset());

	ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));

	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	auto matBuffer = mCurrFrameResource->MaterialBuffer->Resource();
	mCommandList->SetGraphicsRootShaderResourceView(2, matBuffer->GetGPUVirtualAddress());

	CD3DX12_GPU_DESCRIPTOR_HANDLE skyTexDescriptor(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	if(flag==1)
		skyTexDescriptor.Offset(mSkyTexHeapIndex, mCbvSrvUavDescriptorSize);
	if(flag==2)
		skyTexDescriptor.Offset(mSkyTexHeapIndex1, mCbvSrvUavDescriptorSize);
	if (flag == 3)
		skyTexDescriptor.Offset(mSkyTexHeapIndex2, mCbvSrvUavDescriptorSize);
	if (flag == 4)
		skyTexDescriptor.Offset(mSkyTexHeapIndex3, mCbvSrvUavDescriptorSize);
	mCommandList->SetGraphicsRootDescriptorTable(3, skyTexDescriptor);

	mCommandList->SetGraphicsRootDescriptorTable(4, mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

	DrawSceneToShadowMap();

	DrawSceneToCubeMap();

	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress());


	CD3DX12_GPU_DESCRIPTOR_HANDLE dynamicTexDescriptor(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	dynamicTexDescriptor.Offset(mDynamicTexHeapIndex, mCbvSrvUavDescriptorSize);
	mCommandList->SetGraphicsRootDescriptorTable(3, dynamicTexDescriptor);

	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::OpaqueDynamicReflectors]);

	//�����һ������ �����ڶ�̬����֮��
	if (is_shadowmap)
	{
		mCommandList->SetPipelineState(mPSOs["debug"].Get());
		DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Debug]);// �����ƺõ�shadowMap��ʾ����Ļ��
	}
	
	mCommandList->SetPipelineState(mPSOs["opaque"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);

	mCommandList->SetGraphicsRootDescriptorTable(3, skyTexDescriptor);

	mCommandList->SetPipelineState(mPSOs["opaque"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaquess]);

	mCommandList->SetPipelineState(mPSOs["sky"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Sky]);

	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	// imgui������Ⱦѭ��
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	//1. ��ʾ�����ʾ���ڣ��󲿷�ʾ��������ImGui:��ShowDemoWindow������
	bool show_demo_window = true;
	/*if (show_demo_window)
		ImGui::ShowDemoWindow(&show_demo_window);*/

		//2. չʾһ�������Լ�����ļ򵥴��ڡ�����ʹ��Begin/End��������һ���������ڡ�
	{
		ImGui::Begin("Use ImGui");
		ImGui::Checkbox("Animate Geo", &animateGeo);
		if (animateGeo)// ����Ŀؼ�������ĸ�ѡ��Ӱ��
		{
			ImGui::SameLine(0.0f, 25.0f);// ��һ���ؼ���ͬһ������25���ص�λ
			if (ImGui::Button("Reset Params"))
			{
				mx = 0.0f;
				my = 2.0f;
				mz = -15.0f;
				fov = 0.25f;
				mCamera.SetPosition(mx, my, mz);
			}

			ImGui::Text("Camera Position: (%.1f, %.1f, %.1f, 1.0)", mCamera.GetPosition3f().x, mCamera.GetPosition3f().y, mCamera.GetPosition3f().z);

			ImGui::Text("FOV: %.2f degrees", XMConvertToDegrees(fov * MathHelper::Pi));
			ImGui::SliderFloat("##1", &fov, 0.1f, 0.5f, "");
			mCamera.SetLens(fov * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);

			ImGui::Text("Center sphere FresnelR0: %.2f %.2f %.2f", mX, mY, mZ);
			ImGui::SliderFloat("##2", &mX, 0.01f, 1.00f, "");
			ImGui::SliderFloat("##3", &mY, 0.01f, 1.00f, "");
			ImGui::SliderFloat("##4", &mZ, 0.01f, 1.00f, "");

			ImGui::Text("Center sphere Roughness: %.2f", mR);
			ImGui::SliderFloat("##5", &mR, 0.01f, 0.99f, "");

			ImGui::ColorEdit3("Clear Color", clearColor);
		}
		ImGui::Checkbox("Sunmove", &is_sunmove);

		ImGui::Checkbox("Rorate", &is_rorate);
		ImGui::Checkbox("SkyBox", &is_skybox);
		if (is_skybox)
		{
			OnKeyboardInputSkyBox(gt);
			ImGui::Text("Switch Sky Box: %d-snowfield %d-grassland", 1, 2);
			ImGui::Text("Switch Sky Box: %d-desert %d-sunset", 3, 4);
		}
		ImGui::Checkbox("Reflect", &reflect);
		if (reflect)
		{
			refract = false;
			reflectrefract = 0;
		}
		ImGui::Checkbox("Refract", &refract);
		if (refract)
		{
			reflect = false;
			reflectrefract = 1;
			ImGui::Text("Center sphere eta: %.6f", eta);
			ImGui::SliderFloat("##6", &eta, 0.000001f, 1.000000f, "");
		}
		ImGui::Checkbox("Grid NormalMap", &normalmap);
		ImGui::Checkbox("ShadowMap", &is_shadowmap);
		ImGui::End();
	}
	mCommandList->SetDescriptorHeaps(1, mSrvHeap.GetAddressOf());
	ImGui::Render();
	// ����ImGui��Direct3D�Ļ���
	// �����Ҫ�ڴ�֮ǰ���󱸻������󶨵���Ⱦ������
	// ��Ҫ�ڻ��ƵĹ��������Ժ����
	// ��˵��������DrawIndexed֮��RTתPRESENT֮ǰ
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), mCommandList.Get());

	ThrowIfFailed(mCommandList->Close());

	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Swap the back and front buffers
	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

	// Advance the fence value to mark commands up to this fence point.
	mCurrFrameResource->Fence = ++mCurrentFence;

	mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void GameDevelopApp::OnMouseDown(WPARAM btnState, int x, int y)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;

	SetCapture(mhMainWnd);
}

void GameDevelopApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

void GameDevelopApp::OnMouseMove(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		// Make each pixel correspond to a quarter of a degree.
		float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
		float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

		mCamera.Pitch(dy);
		mCamera.RotateY(dx);
	}

	mLastMousePos.x = x;
	mLastMousePos.y = y;
}

void GameDevelopApp::OnKeyboardInput(const GameTimer& gt)
{
	const float dt = gt.DeltaTime();

	if (GetAsyncKeyState('W') & 0x8000)
		mCamera.Walk(10.0f * dt);

	if (GetAsyncKeyState('S') & 0x8000)
		mCamera.Walk(-10.0f * dt);

	if (GetAsyncKeyState('A') & 0x8000)
		mCamera.Strafe(-10.0f * dt);

	if (GetAsyncKeyState('D') & 0x8000)
		mCamera.Strafe(10.0f * dt);

	if (GetAsyncKeyState('Q') & 0x8000)
		mCamera.UpDown(-10.0f * dt);

	if (GetAsyncKeyState('E') & 0x8000)
		mCamera.UpDown(10.0f * dt);

	mCamera.UpdateViewMatrix();
}

void GameDevelopApp::OnKeyboardInputSkyBox(const GameTimer& gt)
{
	if (GetAsyncKeyState('1') & 0x8000)
	{
		mSkyBoxRitem->Mat= mMaterials["sky"].get();
		flag = 1;
	}
	if (GetAsyncKeyState('2') & 0x8000)
	{
		mSkyBoxRitem->Mat = mMaterials["sky1"].get();
		flag = 2;
	}
	if (GetAsyncKeyState('3') & 0x8000)
	{
		mSkyBoxRitem->Mat = mMaterials["sky2"].get();
		flag = 3;
	}
	if (GetAsyncKeyState('4') & 0x8000)
	{
		mSkyBoxRitem->Mat = mMaterials["sky3"].get();
		flag = 4;
	}
}

void GameDevelopApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto currObjectCB = mCurrFrameResource->ObjectCB.get();
	for (auto& e : mAllRitems)
	{
		// Only update the cbuffer data if the constants have changed.  
		// This needs to be tracked per frame resource.
		if (e->NumFramesDirty > 0)
		{
			XMMATRIX world = XMLoadFloat4x4(&e->World);
			XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
			XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));
			objConstants.MaterialIndex = e->Mat->MatCBIndex;

			currObjectCB->CopyData(e->ObjCBIndex, objConstants);

			// Next FrameResource need to be updated too.
			e->NumFramesDirty--;
		}
	}
}

void GameDevelopApp::UpdateMaterialBuffer(const GameTimer& gt)
{
	auto currMaterialBuffer = mCurrFrameResource->MaterialBuffer.get();
	for (auto& e : mMaterials)
	{
		// Only update the cbuffer data if the constants have changed.  If the cbuffer
		// data changes, it needs to be updated for each FrameResource.
		Material* mat = e.second.get();
		if (mat->NumFramesDirty > 0)
		{
			XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

			MaterialData matData;
			matData.DiffuseAlbedo = mat->DiffuseAlbedo;
			matData.FresnelR0 = mat->FresnelR0;
			matData.Roughness = mat->Roughness;
			XMStoreFloat4x4(&matData.MatTransform, XMMatrixTranspose(matTransform));
			matData.DiffuseMapIndex = mat->DiffuseSrvHeapIndex;
			matData.geta = eta;
			matData.NormalMapIndex = mat->NormalSrvHeapIndex;

			currMaterialBuffer->CopyData(mat->MatCBIndex, matData);

			// Next FrameResource need to be updated too.
			mat->NumFramesDirty--;
		}
	}
}

void GameDevelopApp::UpdateMainPassCB(const GameTimer& gt)
{
	XMMATRIX view = mCamera.GetView();
	XMMATRIX proj = mCamera.GetProj();

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
	XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
	XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

	XMMATRIX shadowTransform = XMLoadFloat4x4(&mShadowTransform);

	XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	XMStoreFloat4x4(&mMainPassCB.ShadowTransform, XMMatrixTranspose(shadowTransform));
	mMainPassCB.EyePosW = mCamera.GetPosition3f();
	mMainPassCB.rerf = reflectrefract;
	mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
	mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.FarZ = 1000.0f;
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.DeltaTime = gt.DeltaTime();
	mMainPassCB.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };
	mMainPassCB.Lights[0].Direction = mRotatedLightDirections[0];
	mMainPassCB.Lights[0].Strength = { 0.9f, 0.8f, 0.7f };
	mMainPassCB.Lights[1].Direction = mRotatedLightDirections[1];
	mMainPassCB.Lights[1].Strength = { 0.4f, 0.4f, 0.4f };
	mMainPassCB.Lights[2].Direction = mRotatedLightDirections[2];
	mMainPassCB.Lights[2].Strength = { 0.2f, 0.2f, 0.2f };

	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void GameDevelopApp::UpdateCubeMapFacePassCBs()
{
	for (int i = 0; i < 6; ++i)
	{
		PassConstants cubeFacePassCB = mMainPassCB;

		XMMATRIX view = mCubeMapCamera[i].GetView();
		XMMATRIX proj = mCubeMapCamera[i].GetProj();

		XMMATRIX viewProj = XMMatrixMultiply(view, proj);
		XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
		XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
		XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

		XMStoreFloat4x4(&cubeFacePassCB.View, XMMatrixTranspose(view));
		XMStoreFloat4x4(&cubeFacePassCB.InvView, XMMatrixTranspose(invView));
		XMStoreFloat4x4(&cubeFacePassCB.Proj, XMMatrixTranspose(proj));
		XMStoreFloat4x4(&cubeFacePassCB.InvProj, XMMatrixTranspose(invProj));
		XMStoreFloat4x4(&cubeFacePassCB.ViewProj, XMMatrixTranspose(viewProj));
		XMStoreFloat4x4(&cubeFacePassCB.InvViewProj, XMMatrixTranspose(invViewProj));
		cubeFacePassCB.EyePosW = mCubeMapCamera[i].GetPosition3f();

		auto currPassCB = mCurrFrameResource->PassCB.get();

		// Cube map pass cbuffers are stored in elements 1-6.
		currPassCB->CopyData(1 + i, cubeFacePassCB);
	}
}
// �����ƹ�ת����ռ�����ľ�������
void GameDevelopApp::UpdateShadowTransform(const GameTimer& gt)
{
	// �����Ͷ��������Ӱ
	XMVECTOR lightDir = XMLoadFloat3(&mRotatedLightDirections[0]);
	XMVECTOR lightPos = -2.0f * mSceneBounds.Radius * lightDir;
	XMVECTOR targetPos = XMLoadFloat3(&mSceneBounds.Center);
	XMVECTOR lightUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	// WorldToLight���� ������ռ�ת�ƹ�ռ䣩
	XMMATRIX lightView = XMMatrixLookAtLH(lightPos, targetPos, lightUp);

	XMStoreFloat3(&mLightPosW, lightPos);//�ƹ�����

	// ����Χ��任����Դ�ռ�
	XMFLOAT3 sphereCenterLS;
	XMStoreFloat3(&sphereCenterLS, XMVector3TransformCoord(targetPos, lightView));

	// λ�ڹ�Դ�ռ��а�Χ����������ͶӰ�Ӿ���
	float l = sphereCenterLS.x - mSceneBounds.Radius;
	float b = sphereCenterLS.y - mSceneBounds.Radius;
	float n = sphereCenterLS.z - mSceneBounds.Radius;
	float r = sphereCenterLS.x + mSceneBounds.Radius;
	float t = sphereCenterLS.y + mSceneBounds.Radius;
	float f = sphereCenterLS.z + mSceneBounds.Radius;

	mLightNearZ = n;//���ü������
	mLightFarZ = f;//Զ�ü������
	//����LightToProject���󣨵ƹ�ռ�תNDC�ռ䣩
	XMMATRIX lightProj = XMMatrixOrthographicOffCenterLH(l, r, b, t, n, f);

	// ����NDCToTexture����NDC�ռ�ת����ռ䣩
	// ��[-1, 1]ת��[0, 1]
	XMMATRIX T(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);

	// ����LightToTexture���ƹ�ռ�ת����ռ䣩
	XMMATRIX S = lightView * lightProj * T;
	XMStoreFloat4x4(&mLightView, lightView);
	XMStoreFloat4x4(&mLightProj, lightProj);
	XMStoreFloat4x4(&mShadowTransform, S);
}
// ����Ӱͼ�����PassCB���ݴ���GPU��ˮ��
void GameDevelopApp::UpdateShadowPassCB(const GameTimer& gt)
{
	mShadowPassCB = mMainPassCB;
	XMMATRIX view = XMLoadFloat4x4(&mLightView);
	XMMATRIX proj = XMLoadFloat4x4(&mLightProj);

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
	XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
	XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

	UINT w = mShadowMap->Width();
	UINT h = mShadowMap->Height();

	XMStoreFloat4x4(&mShadowPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mShadowPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mShadowPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mShadowPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mShadowPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mShadowPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	mShadowPassCB.EyePosW = mLightPosW;
	mShadowPassCB.RenderTargetSize = XMFLOAT2((float)w, (float)h);
	mShadowPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / w, 1.0f / h);
	mShadowPassCB.NearZ = mLightNearZ;
	mShadowPassCB.FarZ = mLightFarZ;
	//��ShadowMap��PassCB����7��������Main��CubeMap֮��
	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(7, mShadowPassCB);
}

void GameDevelopApp::LoadTextures()
{
	std::vector<std::string> texNames =
	{
		"bricksDiffuseMap",
		"tileDiffuseMap",
		"defaultDiffuseMap",
		"skyCubeMap",
		"crateMap",
		"skyCubeMap1",
		"skyCubeMap2",
		"skyCubeMap3",
		"bricksNormalMap",
		"tileNormalMap",
		"defaultNormalMap",
	};

	std::vector<std::wstring> texFilenames =
	{
		L"Textures\\bricks2.dds",
		L"Textures\\tile.dds",
		L"Textures\\white1x1.dds",
		L"Textures\\snowcube1024.dds",
		L"Textures\\WoodCrate02.dds",
		L"Textures\\grasscube1024.dds",
		L"Textures\\desertcube1024.dds",
		L"Textures\\sunsetcube1024.dds",
		L"Textures\\bricks2_nmap.dds",
		L"Textures\\tile_nmap.dds",
		L"Textures\\default_nmap.dds",
	};

	for (int i = 0; i < (int)texNames.size(); ++i)
	{
		auto texMap = std::make_unique<Texture>();
		texMap->Name = texNames[i];
		texMap->Filename = texFilenames[i];
		ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
			mCommandList.Get(), texMap->Filename.c_str(),
			texMap->Resource, texMap->UploadHeap));
		
		mTextures[texMap->Name] = std::move(texMap);
	}
}

void GameDevelopApp::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable0;
	// ���ִ���PSO���� һ���Ǹ�ǩ������������
	texTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0);

	//texTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);

	CD3DX12_DESCRIPTOR_RANGE texTable1;
	texTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 10, 2, 0);//������Ӱ��ͼ�����ŵ��Ĵ���2��

	CD3DX12_ROOT_PARAMETER slotRootParameter[5];

	// Perfomance TIP: Order from most frequent to least frequent.
	slotRootParameter[0].InitAsConstantBufferView(0);
	slotRootParameter[1].InitAsConstantBufferView(1);
	slotRootParameter[2].InitAsShaderResourceView(0, 1);
	slotRootParameter[3].InitAsDescriptorTable(1, &texTable0, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[4].InitAsDescriptorTable(1, &texTable1, D3D12_SHADER_VISIBILITY_PIXEL);

	auto staticSamplers = GetStaticSamplers();

	// A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(5, slotRootParameter,
		(UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void GameDevelopApp::BuildDescriptorHeaps()
{
	//
	// Create the SRV heap.
	//
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 14;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

	//
	// Fill out the heap with actual descriptors.
	//
	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	auto bricksTex = mTextures["bricksDiffuseMap"]->Resource;
	auto tileTex = mTextures["tileDiffuseMap"]->Resource;
	auto whiteTex = mTextures["defaultDiffuseMap"]->Resource;
	auto skyTex = mTextures["skyCubeMap"]->Resource;
	auto skyTex1 = mTextures["skyCubeMap1"]->Resource;
	auto skyTex2 = mTextures["skyCubeMap2"]->Resource;
	auto skyTex3 = mTextures["skyCubeMap3"]->Resource;
	auto crateTex = mTextures["crateMap"]->Resource;
	auto bricksNormalTex = mTextures["bricksNormalMap"]->Resource;
	auto tileNormalTex = mTextures["tileNormalMap"]->Resource;
	auto defaultNormalTex = mTextures["defaultNormalMap"]->Resource;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = bricksTex->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = bricksTex->GetDesc().MipLevels;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	md3dDevice->CreateShaderResourceView(bricksTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);

	srvDesc.Format = tileTex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = tileTex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(tileTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);

	srvDesc.Format = whiteTex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = whiteTex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(whiteTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);

	srvDesc.Format = crateTex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = crateTex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(crateTex.Get(), &srvDesc, hDescriptor);

	// 3�ŷ�����ͼ
	hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);

	srvDesc.Format = bricksNormalTex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = bricksNormalTex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(bricksNormalTex.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);

	srvDesc.Format = tileNormalTex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = tileNormalTex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(tileNormalTex.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);

	srvDesc.Format = defaultNormalTex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = defaultNormalTex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(defaultNormalTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);

	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
	srvDesc.TextureCube.MostDetailedMip = 0;
	srvDesc.TextureCube.MipLevels = skyTex->GetDesc().MipLevels;
	srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
	srvDesc.Format = skyTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(skyTex.Get(), &srvDesc, hDescriptor);
	mSkyTexHeapIndex = 7;

	hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
	srvDesc.TextureCube.MostDetailedMip = 0;
	srvDesc.TextureCube.MipLevels = skyTex1->GetDesc().MipLevels;
	srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
	srvDesc.Format = skyTex1->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(skyTex1.Get(), &srvDesc, hDescriptor);
	mSkyTexHeapIndex1 = 8;

	hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
	srvDesc.TextureCube.MostDetailedMip = 0;
	srvDesc.TextureCube.MipLevels = skyTex2->GetDesc().MipLevels;
	srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
	srvDesc.Format = skyTex2->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(skyTex2.Get(), &srvDesc, hDescriptor);
	mSkyTexHeapIndex2 = 9;

	hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
	srvDesc.TextureCube.MostDetailedMip = 0;
	srvDesc.TextureCube.MipLevels = skyTex3->GetDesc().MipLevels;
	srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
	srvDesc.Format = skyTex3->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(skyTex3.Get(), &srvDesc, hDescriptor);
	mSkyTexHeapIndex3 = 10;
	
	mDynamicTexHeapIndex = mSkyTexHeapIndex3 + 1;

	auto srvCpuStart = mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	auto srvGpuStart = mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
	auto rtvCpuStart = mRtvHeap->GetCPUDescriptorHandleForHeapStart();
	auto dsvCpuStart = mDsvHeap->GetCPUDescriptorHandleForHeapStart();

	// Cubemap RTV goes after the swap chain descriptors.
	int rtvOffset = SwapChainBufferCount;

	CD3DX12_CPU_DESCRIPTOR_HANDLE cubeRtvHandles[6];
	for (int i = 0; i < 6; ++i)
		cubeRtvHandles[i] = CD3DX12_CPU_DESCRIPTOR_HANDLE(rtvCpuStart, rtvOffset + i, mRtvDescriptorSize);

	// Dynamic cubemap SRV is after the sky SRV.
	mDynamicCubeMap->BuildDescriptors(
		CD3DX12_CPU_DESCRIPTOR_HANDLE(srvCpuStart, mDynamicTexHeapIndex, mCbvSrvUavDescriptorSize),
		CD3DX12_GPU_DESCRIPTOR_HANDLE(srvGpuStart, mDynamicTexHeapIndex, mCbvSrvUavDescriptorSize),
		cubeRtvHandles);

	// ������ܵ�SRV�Ѻ�DSV���в������ͼ��SRV��DSV��ע���ַƫ�Ƶ�����
	mShadowMapHeapIndex = mDynamicTexHeapIndex + 1;
	//hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);//
	mShadowMap->BuildDescriptors(
		CD3DX12_CPU_DESCRIPTOR_HANDLE(srvCpuStart, mShadowMapHeapIndex, mCbvSrvUavDescriptorSize),//SRV�ڶ��е�ַ��CPU�ϱ��ݣ�
		CD3DX12_GPU_DESCRIPTOR_HANDLE(srvGpuStart, mShadowMapHeapIndex, mCbvSrvUavDescriptorSize),//SRV�ڶ��е�ַ��GPU�ϣ�
		CD3DX12_CPU_DESCRIPTOR_HANDLE(dsvCpuStart, 2, mDsvDescriptorSize));//�ܹ�3�� ƫ������ ��Ϊ֮ǰ����1��CubeMap��DSV
}

void GameDevelopApp::BuildCubeDepthStencil()
{
	// Create the depth/stencil buffer and view.
	D3D12_RESOURCE_DESC depthStencilDesc;
	depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthStencilDesc.Alignment = 0;
	depthStencilDesc.Width = CubeMapSize;
	depthStencilDesc.Height = CubeMapSize;
	depthStencilDesc.DepthOrArraySize = 1;
	depthStencilDesc.MipLevels = 1;
	depthStencilDesc.Format = mDepthStencilFormat;
	depthStencilDesc.SampleDesc.Count = 1;
	depthStencilDesc.SampleDesc.Quality = 0;
	depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE optClear;
	optClear.Format = mDepthStencilFormat;
	optClear.DepthStencil.Depth = 1.0f;
	optClear.DepthStencil.Stencil = 0;
	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&depthStencilDesc,
		D3D12_RESOURCE_STATE_COMMON,
		&optClear,
		IID_PPV_ARGS(mCubeDepthStencilBuffer.GetAddressOf())));

	// Create descriptor to mip level 0 of entire resource using the format of the resource.
	md3dDevice->CreateDepthStencilView(mCubeDepthStencilBuffer.Get(), nullptr, mCubeDSV);

	// Transition the resource from its initial state to be used as a depth buffer.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mCubeDepthStencilBuffer.Get(),
		D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE));
}

void GameDevelopApp::BuildShadersAndInputLayout()
{
	const D3D_SHADER_MACRO alphaTestDefines[] =
	{
		"ALPHA_TEST", "1",
		NULL, NULL
	};

	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "PS", "ps_5_1");

	mShaders["skyVS"] = d3dUtil::CompileShader(L"Shaders\\Sky.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["skyPS"] = d3dUtil::CompileShader(L"Shaders\\Sky.hlsl", nullptr, "PS", "ps_5_1");

	mShaders["shadowVS"] = d3dUtil::CompileShader(L"Shaders\\Shadows.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["shadowOpaquePS"] = d3dUtil::CompileShader(L"Shaders\\Shadows.hlsl", nullptr, "PS", "ps_5_1");
	mShaders["shadowAlphaTestedPS"] = d3dUtil::CompileShader(L"Shaders\\Shadows.hlsl", alphaTestDefines, "PS", "ps_5_1");

	mShaders["debugVS"] = d3dUtil::CompileShader(L"Shaders\\ShadowDebug.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["debugPS"] = d3dUtil::CompileShader(L"Shaders\\ShadowDebug.hlsl", nullptr, "PS", "ps_5_1");

	mInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
}

void GameDevelopApp::BuildSkullGeometry()
{
	std::ifstream fin("Models\\skull.txt");

	if (!fin)
	{
		MessageBox(0, L"Models/skull.txt not found.", 0, 0);
		return;
	}

	UINT vcount = 0;
	UINT tcount = 0;
	std::string ignore;

	fin >> ignore >> vcount;
	fin >> ignore >> tcount;
	fin >> ignore >> ignore >> ignore >> ignore;

	XMFLOAT3 vMinf3(+MathHelper::Infinity, +MathHelper::Infinity, +MathHelper::Infinity);
	XMFLOAT3 vMaxf3(-MathHelper::Infinity, -MathHelper::Infinity, -MathHelper::Infinity);

	XMVECTOR vMin = XMLoadFloat3(&vMinf3);
	XMVECTOR vMax = XMLoadFloat3(&vMaxf3);

	std::vector<Vertex> vertices(vcount);
	for (UINT i = 0; i < vcount; ++i)
	{
		fin >> vertices[i].Pos.x >> vertices[i].Pos.y >> vertices[i].Pos.z;
		fin >> vertices[i].Normal.x >> vertices[i].Normal.y >> vertices[i].Normal.z;

		vertices[i].TexC = { 0.0f, 0.0f };

		//// ��֪��ķ���
		//DirectX::XMFLOAT3 normal(vertices[i].Normal.x, vertices[i].Normal.y, vertices[i].Normal.z);

		////��������
		//DirectX::XMFLOAT3 tangent;
		//DirectX::XMVECTOR normalVec = DirectX::XMLoadFloat3(&normal);
		//DirectX::XMVECTOR referenceVec = DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
		//DirectX::XMVECTOR tangentVec = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(normalVec, referenceVec));
		//DirectX::XMStoreFloat3(&tangent, tangentVec);

		//vertices[i].TangentU.x = tangent.x;
		//vertices[i].TangentU.y = tangent.y;
		//vertices[i].TangentU.z = tangent.z;
		//XMVECTOR P = XMLoadFloat3(&vertices[i].Pos);

		XMVECTOR P = XMLoadFloat3(&vertices[i].Pos);

		XMVECTOR N = XMLoadFloat3(&vertices[i].Normal);

		// Generate a tangent vector so normal mapping works.  We aren't applying
		// a texture map to the skull, so we just need any tangent vector so that
		// the math works out to give us the original interpolated vertex normal.
		XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
		if (fabsf(XMVectorGetX(XMVector3Dot(N, up))) < 1.0f - 0.001f)
		{
			XMVECTOR T = XMVector3Normalize(XMVector3Cross(up, N));
			XMStoreFloat3(&vertices[i].TangentU, T);
		}
		else
		{
			up = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
			XMVECTOR T = XMVector3Normalize(XMVector3Cross(N, up));
			XMStoreFloat3(&vertices[i].TangentU, T);
		}

		vMin = XMVectorMin(vMin, P);
		vMax = XMVectorMax(vMax, P);
	}

	BoundingBox bounds;
	XMStoreFloat3(&bounds.Center, 0.5f * (vMin + vMax));
	XMStoreFloat3(&bounds.Extents, 0.5f * (vMax - vMin));

	fin >> ignore;
	fin >> ignore;
	fin >> ignore;

	std::vector<std::int32_t> indices(3 * tcount);
	for (UINT i = 0; i < tcount; ++i)
	{
		fin >> indices[i * 3 + 0] >> indices[i * 3 + 1] >> indices[i * 3 + 2];
	}

	fin.close();

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::int32_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "skullGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R32_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;
	submesh.Bounds = bounds;

	geo->DrawArgs["skull"] = submesh;

	mGeometries[geo->Name] = std::move(geo);
}

void GameDevelopApp::BuildShapeGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 3);
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(20.0f, 30.0f, 60, 40);
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);
	// ����һ����Ƭ��Ⱦ�����ͼ
	GeometryGenerator::MeshData quad = geoGen.CreateQuad(-1.0f, 1.0f, 0.75f, 0.75f, 0.0f);

	// Cache the vertex offsets to each object in the concatenated vertex buffer.
	UINT boxVertexOffset = 0;
	UINT gridVertexOffset = (UINT)box.Vertices.size();
	UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
	UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();
	UINT quadVertexOffset = cylinderVertexOffset + (UINT)cylinder.Vertices.size();

	// Cache the starting index for each object in the concatenated index buffer.
	UINT boxIndexOffset = 0;
	UINT gridIndexOffset = (UINT)box.Indices32.size();
	UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
	UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();
	UINT quadIndexOffset = cylinderIndexOffset + (UINT)cylinder.Indices32.size();

	SubmeshGeometry boxSubmesh;
	boxSubmesh.IndexCount = (UINT)box.Indices32.size();
	boxSubmesh.StartIndexLocation = boxIndexOffset;
	boxSubmesh.BaseVertexLocation = boxVertexOffset;

	SubmeshGeometry gridSubmesh;
	gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
	gridSubmesh.StartIndexLocation = gridIndexOffset;
	gridSubmesh.BaseVertexLocation = gridVertexOffset;

	SubmeshGeometry sphereSubmesh;
	sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
	sphereSubmesh.StartIndexLocation = sphereIndexOffset;
	sphereSubmesh.BaseVertexLocation = sphereVertexOffset;

	SubmeshGeometry cylinderSubmesh;
	cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
	cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
	cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;

	SubmeshGeometry quadSubmesh;
	quadSubmesh.IndexCount = (UINT)quad.Indices32.size();
	quadSubmesh.StartIndexLocation = quadIndexOffset;
	quadSubmesh.BaseVertexLocation = quadVertexOffset;

	auto totalVertexCount =
		box.Vertices.size() +
		grid.Vertices.size() +
		sphere.Vertices.size() +
		cylinder.Vertices.size()+
		quad.Vertices.size();

	std::vector<Vertex> vertices(totalVertexCount);

	UINT k = 0;
	for (size_t i = 0; i < box.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = box.Vertices[i].Position;
		vertices[k].Normal = box.Vertices[i].Normal;
		vertices[k].TexC = box.Vertices[i].TexC;
		vertices[k].TangentU = box.Vertices[i].TangentU;
	}

	for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = grid.Vertices[i].Position;
		vertices[k].Normal = grid.Vertices[i].Normal;
		vertices[k].TexC = grid.Vertices[i].TexC;
		vertices[k].TangentU = grid.Vertices[i].TangentU;
	}

	for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = sphere.Vertices[i].Position;
		vertices[k].Normal = sphere.Vertices[i].Normal;
		vertices[k].TexC = sphere.Vertices[i].TexC;
		vertices[k].TangentU = sphere.Vertices[i].TangentU;
	}

	for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cylinder.Vertices[i].Position;
		vertices[k].Normal = cylinder.Vertices[i].Normal;
		vertices[k].TexC = cylinder.Vertices[i].TexC;
		vertices[k].TangentU = cylinder.Vertices[i].TangentU;
	}

	for (int i = 0; i < quad.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = quad.Vertices[i].Position;
		vertices[k].Normal = quad.Vertices[i].Normal;
		vertices[k].TexC = quad.Vertices[i].TexC;
		vertices[k].TangentU = quad.Vertices[i].TangentU;
	}

	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
	indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
	indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
	indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));
	indices.insert(indices.end(), std::begin(quad.GetIndices16()), std::end(quad.GetIndices16()));

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "shapeGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	geo->DrawArgs["box"] = boxSubmesh;
	geo->DrawArgs["grid"] = gridSubmesh;
	geo->DrawArgs["sphere"] = sphereSubmesh;
	geo->DrawArgs["cylinder"] = cylinderSubmesh;
	geo->DrawArgs["quad"] = quadSubmesh;

	mGeometries[geo->Name] = std::move(geo);
}

void GameDevelopApp::BuildPSOs()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

	//
	// PSO for opaque objects.
	//
	ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	opaquePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
	opaquePsoDesc.pRootSignature = mRootSignature.Get();
	opaquePsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()),
		mShaders["standardVS"]->GetBufferSize()
	};
	opaquePsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
		mShaders["opaquePS"]->GetBufferSize()
	};
	opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	opaquePsoDesc.SampleMask = UINT_MAX;
	opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	opaquePsoDesc.NumRenderTargets = 1;
	opaquePsoDesc.RTVFormats[0] = mBackBufferFormat;
	opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	opaquePsoDesc.DSVFormat = mDepthStencilFormat;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));

	//
	// PSO for sky.
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC skyPsoDesc = opaquePsoDesc;

	// The camera is inside the sky sphere, so just turn off culling.
	skyPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

	// Make sure the depth function is LESS_EQUAL and not just LESS.  
	// Otherwise, the normalized depth values at z = 1 (NDC) will 
	// fail the depth test if the depth buffer was cleared to 1.
	skyPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	skyPsoDesc.pRootSignature = mRootSignature.Get();
	skyPsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["skyVS"]->GetBufferPointer()),
		mShaders["skyVS"]->GetBufferSize()
	};
	skyPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["skyPS"]->GetBufferPointer()),
		mShaders["skyPS"]->GetBufferSize()
	};
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&skyPsoDesc, IID_PPV_ARGS(&mPSOs["sky"])));

	//��Ϊ�������ɫ������������Ҫ��������һ��shader����Ⱦ���
	D3D12_GRAPHICS_PIPELINE_STATE_DESC smapPsoDesc = opaquePsoDesc;
	smapPsoDesc.RasterizerState.DepthBias = 100000;//�̶���ƫ����
	smapPsoDesc.RasterizerState.DepthBiasClamp = 0.0f;// �����������ƫ����
	smapPsoDesc.RasterizerState.SlopeScaledDepthBias = 1.0f;//���ݶ����б��������ƫ�Ƴɶ�����������
	smapPsoDesc.pRootSignature = mRootSignature.Get();
	smapPsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["shadowVS"]->GetBufferPointer()),
		mShaders["shadowVS"]->GetBufferSize()
	};
	smapPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["shadowOpaquePS"]->GetBufferPointer()),
		mShaders["shadowOpaquePS"]->GetBufferSize()
	};

	// ��Ӱͼ����Ⱦ���������漰��ȾĿ��
	smapPsoDesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
	smapPsoDesc.NumRenderTargets = 0;//û����ȾĿ�꣬��ֹ��ɫ���
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&smapPsoDesc, IID_PPV_ARGS(&mPSOs["shadow_opaque"])));

	//
	// һ��shader����������Ӱͼ��������Ⱦ����Ƭ��
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC debugPsoDesc = opaquePsoDesc;
	debugPsoDesc.pRootSignature = mRootSignature.Get();
	debugPsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["debugVS"]->GetBufferPointer()),
		mShaders["debugVS"]->GetBufferSize()
	};
	debugPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["debugPS"]->GetBufferPointer()),
		mShaders["debugPS"]->GetBufferSize()
	};
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&debugPsoDesc, IID_PPV_ARGS(&mPSOs["debug"])));
}

void GameDevelopApp::BuildFrameResources()
{
	for (int i = 0; i < gNumFrameResources; ++i)
	{
		mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
			8, (UINT)mAllRitems.size(), (UINT)mMaterials.size()));
	}
}

void GameDevelopApp::BuildMaterials()
{
	auto bricks0 = std::make_unique<Material>();
	bricks0->Name = "bricks0";
	bricks0->MatCBIndex = 0;
	bricks0->DiffuseSrvHeapIndex = 0;
	bricks0->NormalSrvHeapIndex = 4;
	bricks0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	bricks0->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	bricks0->Roughness = 0.3f;

	auto tile0 = std::make_unique<Material>();
	tile0->Name = "tile0";
	tile0->MatCBIndex = 1;
	tile0->DiffuseSrvHeapIndex = 1;
	tile0->NormalSrvHeapIndex = 5;
	tile0->DiffuseAlbedo = XMFLOAT4(0.9f, 0.9f, 0.9f, 1.0f);
	tile0->FresnelR0 = XMFLOAT3(0.2f, 0.2f, 0.2f);
	tile0->Roughness = 0.1f;

	auto mirror0 = std::make_unique<Material>();
	mirror0->Name = "mirror0";
	mirror0->MatCBIndex = 2;
	mirror0->DiffuseSrvHeapIndex = 2;
	mirror0->NormalSrvHeapIndex = 6;//ͨ�õķ�����ͼ RGBA��Ϊ1�ķ���ͼ����û�з���Ч��
	mirror0->DiffuseAlbedo = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
	mirror0->FresnelR0 = XMFLOAT3(0.98f, 0.97f, 0.95f);
	mirror0->Roughness = 0.1f;

	auto sky = std::make_unique<Material>();
	sky->Name = "sky";
	sky->MatCBIndex = 3;
	sky->DiffuseSrvHeapIndex = 4;
	sky->NormalSrvHeapIndex = 6;
	sky->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	sky->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	sky->Roughness = 1.0f;

	auto skullMat = std::make_unique<Material>();
	skullMat->Name = "skullMat";
	skullMat->MatCBIndex = 4;
	skullMat->DiffuseSrvHeapIndex = 2;
	skullMat->NormalSrvHeapIndex = 6;
	skullMat->DiffuseAlbedo = XMFLOAT4(0.3f, 0.3f, 0.3f, 1.0f);
	skullMat->FresnelR0 = XMFLOAT3(0.6f, 0.6f, 0.6f);
	skullMat->Roughness = 0.2f;

	//�������ӵĲ���
	auto crate0 = std::make_unique<Material>();
	crate0->Name = "crate0";
	crate0->MatCBIndex = 5;
	crate0->DiffuseSrvHeapIndex = 3;
	crate0->NormalSrvHeapIndex = 6;
	crate0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	crate0->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	crate0->Roughness = 0.2f;

	auto sky1 = std::make_unique<Material>();
	sky1->Name = "sky1";
	sky1->MatCBIndex = 6;
	sky1->DiffuseSrvHeapIndex = 5;
	sky1->NormalSrvHeapIndex = 6;
	sky1->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	sky1->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	sky1->Roughness = 1.0f;

	auto sky2 = std::make_unique<Material>();
	sky2->Name = "sky2";
	sky2->MatCBIndex = 7;
	sky2->DiffuseSrvHeapIndex = 6;
	sky2->NormalSrvHeapIndex = 6;
	sky2->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	sky2->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	sky2->Roughness = 1.0f;

	auto sky3 = std::make_unique<Material>();
	sky3->Name = "sky1";
	sky3->MatCBIndex = 8;
	sky3->DiffuseSrvHeapIndex = 7;
	sky3->NormalSrvHeapIndex = 6;
	sky3->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	sky3->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	sky3->Roughness = 1.0f;

	mMaterials["bricks0"] = std::move(bricks0);
	mMaterials["tile0"] = std::move(tile0);
	mMaterials["mirror0"] = std::move(mirror0);
	mMaterials["sky"] = std::move(sky);
	mMaterials["sky1"] = std::move(sky1);
	mMaterials["sky2"] = std::move(sky2);
	mMaterials["sky3"] = std::move(sky3);
	mMaterials["skullMat"] = std::move(skullMat);
	mMaterials["crate0"] = std::move(crate0);
}

void GameDevelopApp::BuildRenderItems()
{
	auto skyRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&skyRitem->World, XMMatrixScaling(5000.0f, 5000.0f, 5000.0f));
	skyRitem->TexTransform = MathHelper::Identity4x4();
	skyRitem->ObjCBIndex = 0;
	skyRitem->Mat = mMaterials["sky"].get();
	skyRitem->Geo = mGeometries["shapeGeo"].get();
	skyRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	skyRitem->IndexCount = skyRitem->Geo->DrawArgs["sphere"].IndexCount;
	skyRitem->StartIndexLocation = skyRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
	skyRitem->BaseVertexLocation = skyRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

	mSkyBoxRitem = skyRitem.get();

	mRitemLayer[(int)RenderLayer::Sky].push_back(skyRitem.get());
	mAllRitems.push_back(std::move(skyRitem));

	auto skullRitem = std::make_unique<RenderItem>();
	skullRitem->World = MathHelper::Identity4x4();
	skullRitem->TexTransform = MathHelper::Identity4x4();
	skullRitem->ObjCBIndex = 1;
	skullRitem->Mat = mMaterials["skullMat"].get();
	skullRitem->Geo = mGeometries["skullGeo"].get();
	skullRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	skullRitem->IndexCount = skullRitem->Geo->DrawArgs["skull"].IndexCount;
	skullRitem->StartIndexLocation = skullRitem->Geo->DrawArgs["skull"].StartIndexLocation;
	skullRitem->BaseVertexLocation = skullRitem->Geo->DrawArgs["skull"].BaseVertexLocation;

	mSkullRitem = skullRitem.get();

	mRitemLayer[(int)RenderLayer::Opaque].push_back(skullRitem.get());
	mAllRitems.push_back(std::move(skullRitem));

	auto boxRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&boxRitem->World, XMMatrixScaling(2.0f, 1.0f, 2.0f) * XMMatrixTranslation(0.0f, 0.5f, 0.0f));
	XMStoreFloat4x4(&boxRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	boxRitem->ObjCBIndex = 2;
	boxRitem->Mat = mMaterials["crate0"].get();
	boxRitem->Geo = mGeometries["shapeGeo"].get();
	boxRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxRitem->IndexCount = boxRitem->Geo->DrawArgs["box"].IndexCount;
	boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;
	boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::Opaque].push_back(boxRitem.get());
	mAllRitems.push_back(std::move(boxRitem));

	auto globeRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&globeRitem->World, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(0.0f, 2.0f, 0.0f));
	XMStoreFloat4x4(&globeRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	globeRitem->ObjCBIndex = 3;
	globeRitem->Mat = mMaterials["mirror0"].get();
	globeRitem->Geo = mGeometries["shapeGeo"].get();
	globeRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	globeRitem->IndexCount = globeRitem->Geo->DrawArgs["sphere"].IndexCount;
	globeRitem->StartIndexLocation = globeRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
	globeRitem->BaseVertexLocation = globeRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

	mglobeRitem = globeRitem.get();

	mRitemLayer[(int)RenderLayer::OpaqueDynamicReflectors].push_back(globeRitem.get());
	mAllRitems.push_back(std::move(globeRitem));

	auto gridRitem = std::make_unique<RenderItem>();
	gridRitem->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&gridRitem->TexTransform, XMMatrixScaling(8.0f, 8.0f, 1.0f));
	gridRitem->ObjCBIndex = 4;
	gridRitem->Mat = mMaterials["tile0"].get();
	gridRitem->Geo = mGeometries["shapeGeo"].get();
	gridRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
	gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;

	mgridRitem = gridRitem.get();

	mRitemLayer[(int)RenderLayer::Opaque].push_back(gridRitem.get());
	mAllRitems.push_back(std::move(gridRitem));

	auto quadRitem = std::make_unique<RenderItem>();
	quadRitem->World = MathHelper::Identity4x4();
	quadRitem->TexTransform = MathHelper::Identity4x4();
	quadRitem->ObjCBIndex = 5;
	quadRitem->Mat = mMaterials["bricks0"].get();
	quadRitem->Geo = mGeometries["shapeGeo"].get();
	quadRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	quadRitem->IndexCount = quadRitem->Geo->DrawArgs["quad"].IndexCount;
	quadRitem->StartIndexLocation = quadRitem->Geo->DrawArgs["quad"].StartIndexLocation;
	quadRitem->BaseVertexLocation = quadRitem->Geo->DrawArgs["quad"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::Debug].push_back(quadRitem.get());
	mAllRitems.push_back(std::move(quadRitem));

	XMMATRIX brickTexTransform = XMMatrixScaling(1.5f, 2.0f, 1.0f);
	UINT objCBIndex = 6;
	for (int i = 0; i < 5; ++i)
	{
		auto leftCylRitem = std::make_unique<RenderItem>();
		auto rightCylRitem = std::make_unique<RenderItem>();
		auto leftSphereRitem = std::make_unique<RenderItem>();
		auto rightSphereRitem = std::make_unique<RenderItem>();

		XMMATRIX leftCylWorld = XMMatrixTranslation(-5.0f, 1.5f, -10.0f + i * 5.0f);
		XMMATRIX rightCylWorld = XMMatrixTranslation(+5.0f, 1.5f, -10.0f + i * 5.0f);

		XMMATRIX leftSphereWorld = XMMatrixTranslation(-5.0f, 3.5f, -10.0f + i * 5.0f);
		XMMATRIX rightSphereWorld = XMMatrixTranslation(+5.0f, 3.5f, -10.0f + i * 5.0f);

		XMStoreFloat4x4(&leftCylRitem->World, rightCylWorld);
		XMStoreFloat4x4(&leftCylRitem->TexTransform, brickTexTransform);
		leftCylRitem->ObjCBIndex = objCBIndex++;
		leftCylRitem->Mat = mMaterials["bricks0"].get();
		leftCylRitem->Geo = mGeometries["shapeGeo"].get();
		leftCylRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftCylRitem->IndexCount = leftCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
		leftCylRitem->StartIndexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
		leftCylRitem->BaseVertexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

		XMStoreFloat4x4(&rightCylRitem->World, leftCylWorld);
		XMStoreFloat4x4(&rightCylRitem->TexTransform, brickTexTransform);
		rightCylRitem->ObjCBIndex = objCBIndex++;
		rightCylRitem->Mat = mMaterials["bricks0"].get();
		rightCylRitem->Geo = mGeometries["shapeGeo"].get();
		rightCylRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightCylRitem->IndexCount = rightCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
		rightCylRitem->StartIndexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
		rightCylRitem->BaseVertexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

		XMStoreFloat4x4(&leftSphereRitem->World, leftSphereWorld);
		leftSphereRitem->TexTransform = MathHelper::Identity4x4();
		leftSphereRitem->ObjCBIndex = objCBIndex++;
		leftSphereRitem->Mat = mMaterials["mirror0"].get();
		leftSphereRitem->Geo = mGeometries["shapeGeo"].get();
		leftSphereRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftSphereRitem->IndexCount = leftSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
		leftSphereRitem->StartIndexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
		leftSphereRitem->BaseVertexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

		XMStoreFloat4x4(&rightSphereRitem->World, rightSphereWorld);
		rightSphereRitem->TexTransform = MathHelper::Identity4x4();
		rightSphereRitem->ObjCBIndex = objCBIndex++;
		rightSphereRitem->Mat = mMaterials["mirror0"].get();
		rightSphereRitem->Geo = mGeometries["shapeGeo"].get();
		rightSphereRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightSphereRitem->IndexCount = rightSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
		rightSphereRitem->StartIndexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
		rightSphereRitem->BaseVertexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

		mRitemLayer[(int)RenderLayer::Opaque].push_back(leftCylRitem.get());
		mRitemLayer[(int)RenderLayer::Opaque].push_back(rightCylRitem.get());
		mRitemLayer[(int)RenderLayer::Opaquess].push_back(leftSphereRitem.get());
		mRitemLayer[(int)RenderLayer::Opaquess].push_back(rightSphereRitem.get());

		mAllRitems.push_back(std::move(leftCylRitem));
		mAllRitems.push_back(std::move(rightCylRitem));
		mAllRitems.push_back(std::move(leftSphereRitem));
		mAllRitems.push_back(std::move(rightSphereRitem));
	}
}

void GameDevelopApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

	auto objectCB = mCurrFrameResource->ObjectCB->Resource();

	// For each render item...
	for (size_t i = 0; i < ritems.size(); ++i)
	{
		auto ri = ritems[i];

		cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
		cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;

		cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress);

		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}

void GameDevelopApp::DrawSceneToCubeMap()
{
	mCommandList->RSSetViewports(1, &mDynamicCubeMap->Viewport());
	mCommandList->RSSetScissorRects(1, &mDynamicCubeMap->ScissorRect());

	// Change to RENDER_TARGET.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mDynamicCubeMap->Resource(),
		D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_RENDER_TARGET));

	UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));

	// For each cube map face.
	for (int i = 0; i < 6; ++i)
	{
		// Clear the back buffer and depth buffer.
		mCommandList->ClearRenderTargetView(mDynamicCubeMap->Rtv(i), Colors::LightSteelBlue, 0, nullptr);
		mCommandList->ClearDepthStencilView(mCubeDSV, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

		// Specify the buffers we are going to render to.
		mCommandList->OMSetRenderTargets(1, &mDynamicCubeMap->Rtv(i), true, &mCubeDSV);

		// Bind the pass constant buffer for this cube map face so we use 
		// the right view/proj matrix for this cube face.
		auto passCB = mCurrFrameResource->PassCB->Resource();
		D3D12_GPU_VIRTUAL_ADDRESS passCBAddress = passCB->GetGPUVirtualAddress() + (1 + i) * passCBByteSize;
		mCommandList->SetGraphicsRootConstantBufferView(1, passCBAddress);

		mCommandList->SetPipelineState(mPSOs["opaque"].Get());
		DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);
		DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaquess]);

		mCommandList->SetPipelineState(mPSOs["sky"].Get());
		DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Sky]);

		mCommandList->SetPipelineState(mPSOs["opaque"].Get());
	}

	// Change back to GENERIC_READ so we can read the texture in a shader.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mDynamicCubeMap->Resource(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_GENERIC_READ));
}

void GameDevelopApp::DrawSceneToShadowMap()
{
	//�����ӿںͲü�����
	mCommandList->RSSetViewports(1, &mShadowMap->Viewport());
	mCommandList->RSSetScissorRects(1, &mShadowMap->ScissorRect());

	// �����ͼ��Դת��д��״̬����Ⱦ�����Ϣ��
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mShadowMap->Resource(),
		D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_DEPTH_WRITE));

	UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));

	// Clear the back buffer and depth buffer.
	mCommandList->ClearDepthStencilView(mShadowMap->Dsv(),
		D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	//����ȾĿ������Ϊ�գ���ֹ���̨������д����ɫ
	mCommandList->OMSetRenderTargets(0, //RenderTarget����Ϊ0
		nullptr, //RTVָ��Ϊ��
		false, //�ڴ治�������
		&mShadowMap->Dsv());//DSV���

	// Ϊ��Ӱͼ��Ⱦ������ĳ���������
	auto passCB = mCurrFrameResource->PassCB->Resource();
	D3D12_GPU_VIRTUAL_ADDRESS passCBAddress = passCB->GetGPUVirtualAddress() + 7 * passCBByteSize;//��6����̬����
	mCommandList->SetGraphicsRootConstantBufferView(1, passCBAddress);

	// ����PSO������
	mCommandList->SetPipelineState(mPSOs["shadow_opaque"].Get());

	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaquess]);
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::OpaqueDynamicReflectors]);

	// ����Ӱͼ��Դ���óɿɶ�������������shader�в�����Ӱͼ
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mShadowMap->Resource(),
		D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_GENERIC_READ));
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> GameDevelopApp::GetStaticSamplers()
{
	const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
		0, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		1, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		2, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		3, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
		4, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressW
		0.0f,                             // mipLODBias
		8);                               // maxAnisotropy

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
		5, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
		0.0f,                              // mipLODBias
		8);                                // maxAnisotropy

	const CD3DX12_STATIC_SAMPLER_DESC shadow(
		6, // ��ɫ���Ĵ���
		D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressW
		0.0f,                               // mipLODBias
		16,                                 // maxAnisotropy
		D3D12_COMPARISON_FUNC_LESS_EQUAL,	//ִ����Ӱͼ�ıȽϲ���
		D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK);

	return {
		pointWrap, pointClamp,
		linearWrap, linearClamp,
		anisotropicWrap, anisotropicClamp ,
		shadow 
	};
}

void GameDevelopApp::BuildCubeFaceCamera(float x, float y, float z)
{
	// Generate the cube map about the given position.
	XMFLOAT3 center(x, y, z);
	XMFLOAT3 worldUp(0.0f, 1.0f, 0.0f);

	// Look along each coordinate axis.
	XMFLOAT3 targets[6] =
	{
		XMFLOAT3(x + 1.0f, y, z), // +X
		XMFLOAT3(x - 1.0f, y, z), // -X
		XMFLOAT3(x, y + 1.0f, z), // +Y
		XMFLOAT3(x, y - 1.0f, z), // -Y
		XMFLOAT3(x, y, z + 1.0f), // +Z
		XMFLOAT3(x, y, z - 1.0f)  // -Z
	};

	XMFLOAT3 ups[6] =
	{
		XMFLOAT3(0.0f, 1.0f, 0.0f),  // +X
		XMFLOAT3(0.0f, 1.0f, 0.0f),  // -X
		XMFLOAT3(0.0f, 0.0f, -1.0f), // +Y
		XMFLOAT3(0.0f, 0.0f, +1.0f), // -Y
		XMFLOAT3(0.0f, 1.0f, 0.0f),	 // +Z
		XMFLOAT3(0.0f, 1.0f, 0.0f)	 // -Z
	};

	for (int i = 0; i < 6; ++i)
	{
		mCubeMapCamera[i].LookAt(center, targets[i], ups[i]);
		mCubeMapCamera[i].SetLens(0.5f * XM_PI, 1.0f, 0.1f, 1000.0f);
		mCubeMapCamera[i].UpdateViewMatrix();
	}
}