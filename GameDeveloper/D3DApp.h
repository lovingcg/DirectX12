#pragma once
#include "DxException.h"
#include <wrl.h>
#include "d3dx12.h"
#include <dxgi1_4.h>
#include "GameTimer.h"
#include <DirectXColors.h>
#include <d3dcompiler.h>

using namespace Microsoft::WRL;

class D3DApp
{
protected:
	D3DApp();
	//ά������
	//delete��ζ�������Ա���������ٱ�����
	D3DApp(const D3DApp& rhs) = delete;
	D3DApp& operator=(const D3DApp& rhs) = delete;
	virtual ~D3DApp();

public:
	//ʹ��һ�����еľ�̬������ȡ��ʵ��
	static D3DApp* GetApp();

	int Run();

	virtual bool Init(HINSTANCE hInstance, int nShowCmd);// ��ʼ�����ں�D3D
	virtual LRESULT CALLBACK MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);// ���ڹ���

protected:
	bool InitWindow(HINSTANCE hInstance, int nShowCmd);// ��ʼ������
	bool InitDirect3D();// ��ʼ��D3D

	virtual void Draw() = 0;
	virtual void Update() = 0;//���ù۲��ͶӰ����

	virtual void OnResize();// ���ڳߴ�ı��¼�

	void CreateDevice();// �����豸
	void CreateFence();// ����Χ��
	void GetDescriptorSize();// �õ���������С
	void SetMSAA();// ���MSAA����֧��
	void CreateCommandObject();// �����������
	void CreateSwapChain();// ����������
	void CreateDescriptorHeap();// ������������
	void CreateRTV();// ������ȾĿ����ͼ
	void CreateDSV();// �������/ģ����ͼ
	void CreateViewPortAndScissorRect();// ������ͼ�Ͳü��ռ����

	ID3D12Resource* CurrentBackBuffer() const;
	D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferView() const;
	D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView() const;

	void FlushCmdQueue();// ʵ��Χ��
	void CalculateFrameState();// ����fps��mspf

	virtual void OnMouseDown(WPARAM btnState,int x,int y) { }
	virtual void OnMouseUp(WPARAM btnState, int x, int y) { }
	virtual void OnMouseMove(WPARAM btnState, int x, int y) { }

protected:
	//mAppָ����Ψһʵ��
	static D3DApp* mApp;

	HWND mhMainWnd = 0;// ���ھ��

	bool mAppPaused = false;  // �Ƿ���ͣ
	bool mMinimized = false;  // �Ƿ���С��
	bool mMaximized = false;  // �Ƿ����
	bool mResizing = false;   // �Ƿ������϶�������С��

	//ָ��ӿںͱ�������
	ComPtr<ID3D12Device> d3dDevice;
	ComPtr<IDXGIFactory4> dxgiFactory;
	
	ComPtr<ID3D12CommandAllocator> cmdAllocator;
	ComPtr<ID3D12CommandQueue> cmdQueue;
	ComPtr<ID3D12GraphicsCommandList> cmdList;
	ComPtr<ID3D12Resource> depthStencilBuffer;
	ComPtr<ID3D12Resource> swapChainBuffer[2];
	ComPtr<IDXGISwapChain> swapChain;

	ComPtr<ID3D12DescriptorHeap> rtvHeap;
	ComPtr<ID3D12DescriptorHeap> dsvHeap;
	ComPtr<ID3D12DescriptorHeap> mSrvHeap;//��ɫ����Դ��ͼ

	D3D12_COMMAND_QUEUE_DESC commandQueueDesc = {};// ��������

	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msaaQualityLevels;// MSAA�ȼ�����

	ComPtr<ID3D12Fence> fence;
	UINT64 mCurrentFence = 0;// ��ǰΧ��ֵ

	D3D12_VIEWPORT viewPort;
	D3D12_RECT scissorRect;

	UINT rtvDescriptorSize = 0;// ��ȾĿ����ͼ��С
	UINT dsvDescriptorSize = 0;// ���/ģ��Ŀ����ͼ��С
	UINT cbv_srv_uavDescriptorSize = 0;// ����������ͼ��С

	_GameTimer::GameTimer gt;// ��Ϸʱ��ʵ��

	UINT mCurrentBackBuffer = 0;

	bool m4xMsaaState = false;// �Ƿ���4X MSAA
	UINT m4xMsaaQuality = 0;// 4xMSAA����

	DXGI_FORMAT mBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	DXGI_FORMAT mDepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	int mClientWidth = 1280;
	int mClientHeight = 720;
};