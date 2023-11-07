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
	//维护单例
	//delete意味着这个成员函数不能再被调用
	D3DApp(const D3DApp& rhs) = delete;
	D3DApp& operator=(const D3DApp& rhs) = delete;
	virtual ~D3DApp();

public:
	//使用一个公有的静态方法获取该实例
	static D3DApp* GetApp();

	int Run();

	virtual bool Init(HINSTANCE hInstance, int nShowCmd);// 初始化窗口和D3D
	virtual LRESULT CALLBACK MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);// 窗口过程

protected:
	bool InitWindow(HINSTANCE hInstance, int nShowCmd);// 初始化窗口
	bool InitDirect3D();// 初始化D3D

	virtual void Draw() = 0;
	virtual void Update() = 0;//设置观察和投影矩阵

	virtual void OnResize();// 窗口尺寸改变事件

	void CreateDevice();// 创建设备
	void CreateFence();// 创建围栏
	void GetDescriptorSize();// 得到描述符大小
	void SetMSAA();// 检测MSAA质量支持
	void CreateCommandObject();// 创建命令队列
	void CreateSwapChain();// 创建交换链
	void CreateDescriptorHeap();// 创建描述符堆
	void CreateRTV();// 创建渲染目标视图
	void CreateDSV();// 创建深度/模板视图
	void CreateViewPortAndScissorRect();// 创建视图和裁剪空间矩形

	ID3D12Resource* CurrentBackBuffer() const;
	D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferView() const;
	D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView() const;

	void FlushCmdQueue();// 实现围栏
	void CalculateFrameState();// 计算fps和mspf

	virtual void OnMouseDown(WPARAM btnState,int x,int y) { }
	virtual void OnMouseUp(WPARAM btnState, int x, int y) { }
	virtual void OnMouseMove(WPARAM btnState, int x, int y) { }

protected:
	//mApp指的是唯一实例
	static D3DApp* mApp;

	HWND mhMainWnd = 0;// 窗口句柄

	bool mAppPaused = false;  // 是否暂停
	bool mMinimized = false;  // 是否最小化
	bool mMaximized = false;  // 是否最大化
	bool mResizing = false;   // 是否正在拖动调整大小栏

	//指针接口和变量声明
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
	ComPtr<ID3D12DescriptorHeap> mSrvHeap;//着色器资源视图

	D3D12_COMMAND_QUEUE_DESC commandQueueDesc = {};// 描述队列

	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msaaQualityLevels;// MSAA等级描述

	ComPtr<ID3D12Fence> fence;
	UINT64 mCurrentFence = 0;// 当前围栏值

	D3D12_VIEWPORT viewPort;
	D3D12_RECT scissorRect;

	UINT rtvDescriptorSize = 0;// 渲染目标视图大小
	UINT dsvDescriptorSize = 0;// 深度/模板目标视图大小
	UINT cbv_srv_uavDescriptorSize = 0;// 常量缓冲视图大小

	_GameTimer::GameTimer gt;// 游戏时间实例

	UINT mCurrentBackBuffer = 0;

	bool m4xMsaaState = false;// 是否开启4X MSAA
	UINT m4xMsaaQuality = 0;// 4xMSAA质量

	DXGI_FORMAT mBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	DXGI_FORMAT mDepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	int mClientWidth = 1280;
	int mClientHeight = 720;
};