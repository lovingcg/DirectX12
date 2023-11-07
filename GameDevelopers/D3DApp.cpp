#include "D3DApp.h"
#include <windowsx.h>
#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx12.h"

D3DApp::D3DApp()
{
	assert(mApp == nullptr);
	mApp = this;
}

D3DApp* D3DApp::mApp = nullptr;
D3DApp* D3DApp::GetApp()
{
	return mApp;
}

D3DApp::~D3DApp()
{
	if (d3dDevice != nullptr)
		FlushCmdQueue();
}

float D3DApp::AspectRatio()const
{
    return static_cast<float>(mClientWidth) / mClientHeight;
}

// 初始化窗口和D3D
bool D3DApp::Init(HINSTANCE hInstance, int nShowCmd) 
{
	if (!InitWindow(hInstance, nShowCmd))
		return false;
	else if (!InitDirect3D())
		return false;
    OnResize();

    
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    // 设置ImGui风格
    ImGui::StyleColorsDark();
    //窗口句柄
    //初始化win32及dx12 设置平台/渲染器后端
    ImGui_ImplWin32_Init(mhMainWnd);
    //ImGui DX12需要一个SRV堆才能绘制
    ImGui_ImplDX12_Init(d3dDevice.Get(), 3,
        DXGI_FORMAT_R8G8B8A8_UNORM, mSrvHeap.Get(),
        mSrvHeap.Get()->GetCPUDescriptorHandleForHeapStart(),
        mSrvHeap.Get()->GetGPUDescriptorHandleForHeapStart());

	return true;
}
LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    //MainWndProc函数是回调函数，调用不了D3D12App类中Protected里的数据，
    //所以在D3D12App类中新建一个MsgProc宿主函数返回窗口过程需要的值。
	return D3DApp::GetApp()->MsgProc(hwnd, msg, wParam, lParam);
}

bool D3DApp::InitWindow(HINSTANCE hInstance, int nShowCmd)
{
    //注册窗口类
    WNDCLASS wc;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainWndProc;// 指定窗口过程
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(0, IDC_ARROW);
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpszMenuName = 0;
    wc.lpszClassName = L"ForTheDreamOfGameDevelop";//为窗口类指定一个类名

    if (!RegisterClass(&wc))
    {
        MessageBox(0, L"RegisterClass Failed", 0, 0);
        return 0;
    }

    RECT R;
    R.left = 0;
    R.top = 0;
    R.right = mClientWidth;
    R.bottom = mClientHeight;
    AdjustWindowRect(&R, WS_OVERLAPPEDWINDOW, false);//根据窗口的客户区大小计算窗口的大小
    int width = R.right - R.left;
    int hight = R.bottom - R.top;

    mhMainWnd = CreateWindow(L"ForTheDreamOfGameDevelop", L"GameDeveloper", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, width, hight, 0, 0, hInstance, 0);
    if (!mhMainWnd)
    {
        MessageBox(0, L"CreatWindow Failed", 0, 0);
        return 0;
    }

    //显示 更新窗口用到mhMainWnd
    ShowWindow(mhMainWnd, nShowCmd);
    UpdateWindow(mhMainWnd);

    return true;
}

bool D3DApp::InitDirect3D()
{
    //开启D3D12调试层
#if defined(DEBUG) || defined(_DEBUG)
    {
        ComPtr<ID3D12Debug> debugController;
        ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)));
        debugController->EnableDebugLayer();
    }
#endif

    CreateDevice();
    CreateFence();
    GetDescriptorSize();
    SetMSAA();
    CreateCommandObject();
    CreateSwapChain();
    CreateDescriptorHeap();
   /* CreateRTV();
    CreateDSV();
    CreateViewPortAndScissorRect();*/

    return true;
}
// 窗口尺寸改变事件
void D3DApp::OnResize()
{
    assert(d3dDevice);
    assert(swapChain);
    assert(cmdAllocator);

    // 改变资源前先同步
    FlushCmdQueue();
    ThrowIfFailed(cmdList->Reset(cmdAllocator.Get(), nullptr));

    //释放之前的资源，为我们重新创建做好准备
    for (int i = 0; i < 2; i++)
    {
        swapChainBuffer[i].Reset();
    }
    depthStencilBuffer.Reset();

    //重新调整后台缓冲区资源的大小
    ThrowIfFailed(swapChain->ResizeBuffers(2,
        mClientWidth,
        mClientHeight,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));

    // 后台缓冲区索引置零
    mCurrentBackBuffer = 0;
    //这三个函数原本在InitDirect3D()中执行，用OnResize()函数替换掉三函数并在InitDirect3D()后执行
    CreateRTV();
    CreateDSV();
    // 执行Resize命令 只要资源重置了 就必须执行命令列表关闭重置
    ThrowIfFailed(cmdList->Close());
    ID3D12CommandList* cmdsLists[] = { cmdList.Get() };
    cmdQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // 等待Resize命令完成
    FlushCmdQueue();

    CreateViewPortAndScissorRect();
}
// 外部文件函数
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// 窗口过程
// 参数hwnd是主窗口句柄，msg是传进来的消息，wParam是输入的虚拟键代码，lParam是系统反馈的信息(这里获得了光标的坐标信息)
LRESULT CALLBACK D3DApp::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) 
{
    // ImGui要响应消息，还需对窗口过程函数做一番修改。
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
        return true;
    //当创建窗口之后会进入窗口过程函数一次，然后遇到const ImGuiIO imio = ImGui::GetIO()这一句代码时，提示上下文还没有被创建。
    // 所以把IMGUI_CHECKVERSION();ImGui::CreateContext();放到这里即可
    //初始化上下文并设置一些参数
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    const ImGuiIO imio = ImGui::GetIO();
    // 消息处理
    switch (msg)
    {
        // 当窗口尺寸发生变换时
        case WM_SIZE:
            mClientWidth = LOWORD(lParam);
            mClientHeight = HIWORD(lParam);
            if (d3dDevice)
            {
                if (wParam == SIZE_MINIMIZED)//如果最小化,则暂停游戏，调整最小化和最大化状态
                {
                    mAppPaused = true;
                    mMinimized = true;
                    mMaximized = false;
                }
                else if (wParam == SIZE_MAXIMIZED)
                {
                    mAppPaused = false;
                    mMinimized = false;
                    mMaximized = true;
                    OnResize();
                }
                else if (wParam == SIZE_RESTORED)//窗口被还原产生的信息
                {
                    //从最小化还原
                    if (mMinimized)
                    {
                        mAppPaused = false;
                        mMinimized = false;
                        OnResize();
                    }
                    //从最大化还原
                    else if (mMaximized)
                    {
                        mAppPaused = false;
                        mMaximized = false;
                        OnResize();
                    }
                    else if (mResizing)
                    {

                    }
                    else
                    {
                        OnResize();
                    }
                }
            }
            return 0;
        //鼠标按键按下时的触发（左中右）
        case WM_LBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_RBUTTONDOWN:
            // wParam为输入的虚拟键代码，lParam为系统反馈的光标信息
            OnMouseDown(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        //鼠标按键抬起时的触发（左中右）
        case WM_LBUTTONUP:
        case WM_MBUTTONUP:
        case WM_RBUTTONUP:
            OnMouseUp(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        //鼠标移动的触发
        case WM_MOUSEMOVE:
            //即如果“Imgui也想捕捉这个消息的话”，那么就“让ImGui去处理，跳过我们的游戏逻辑”
            if (imio.WantCaptureMouse)
            {
                break;
            }
            OnMouseMove(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
     
        // 当窗口被销毁时，终止消息循环
        case WM_DESTROY:
            PostQuitMessage(0);	// 终止消息循环，并发出WM_QUIT消息
            return 0;
        default:
            break;
    }
    // 将上面没有处理的消息转发给默认的窗口过程
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// 消息循环，游戏循环框架
//（Draw函数每运行一次，其实就是一帧，所以后期会在它前面加上帧时间的计算）。
int D3DApp::Run()
{
    // 定义消息结构体
    MSG msg = { 0 };
    // 每次消息循环重置一次计数器
    gt.Reset();
    // 如果PeekMessage函数不等于0，说明没有接受到WM_QUIT
    while (msg.message != WM_QUIT)
    {
        // 如果有窗口消息就进行处理
        if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))// PeekMessage函数会自动填充msg结构体元素
        {
            TranslateMessage(&msg);//收到的消息进行预处理，信息翻译加工一下。如果对信息进行了加工，那么就要把信息交还给操作系统进行处理信息。
            DispatchMessage(&msg);//操作系统调用窗口处理函数，里面记录了信息的处理方式。这样就完成了一条条信息的处理。
        }
        // 否则就执行动画和游戏逻辑
        else
        {
            gt.Tick();// 计算每两帧间隔时间
            if (!gt.IsStopped())//如果不是暂停状态，我们才运行游戏
            {
                CalculateFrameState();
                Update();//每一帧都可能出现mvp变换
                Draw();
            }
            //如果是暂停状态，则休眠100秒
            else
            {
                Sleep(100);
            }
        }
    }
    // destruct imgui
    // 如果不加shutdown以及destroyContext，会造成内存泄漏
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    return (int)msg.wParam;
}

void D3DApp::CreateDevice()
{
    //ComPtr < A > a;	//定义一个ComPtr智能指针
    // &a  //A** 类型，并增加引用，写入用(传入参数)
    ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory)));
    ThrowIfFailed(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&d3dDevice)));
}
//同步CPU和GPU
void D3DApp::CreateFence()
{
    ThrowIfFailed(d3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
}

// 获取描述符大小，这个大小可以知道描述符堆中每个元素的大小（描述符在不同的GPU平台上的大小各异），
// 方便在地址中做偏移来找到堆中的描述符元素。
void D3DApp::GetDescriptorSize()
{
    rtvDescriptorSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    dsvDescriptorSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    cbv_srv_uavDescriptorSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void D3DApp::SetMSAA()
{
    msaaQualityLevels.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    msaaQualityLevels.SampleCount = 1;
    msaaQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
    msaaQualityLevels.NumQualityLevels = 0;
    ThrowIfFailed(d3dDevice->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &msaaQualityLevels, sizeof(msaaQualityLevels)));
    assert(msaaQualityLevels.NumQualityLevels > 0);
}

void D3DApp::CreateCommandObject()
{
    commandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;// 指定GPU可以执行的命令缓冲区，直接命令列表未继承任何GPU状态
    commandQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;// 默认命令队列
    ThrowIfFailed(d3dDevice->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&cmdQueue)));// 创建命令队列
    ThrowIfFailed(d3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAllocator)));// 创建命令分配器
    // 创建命令列表
    ThrowIfFailed(d3dDevice->CreateCommandList(0, // 掩码值为0，单GPU
        D3D12_COMMAND_LIST_TYPE_DIRECT, // 命令列表类型
        cmdAllocator.Get(),	// 命令分配器接口指针
        nullptr,	// 流水线状态对象PSO，这里不绘制，所以空指针
        IID_PPV_ARGS(&cmdList)));	// 返回创建的命令列表
    cmdList->Close();// 重置命令列表前必须将其关闭
}
//创建交换链 交换链中存着渲染目标资源，即后台缓冲区资源
void D3DApp::CreateSwapChain()
{
    swapChain.Reset();
    // 描述交换链
    DXGI_SWAP_CHAIN_DESC swapChainDesc;// 描述交换链结构体
    swapChainDesc.BufferDesc.Width = mClientWidth;	// 缓冲区分辨率的宽度
    swapChainDesc.BufferDesc.Height = mClientHeight;	// 缓冲区分辨率的高度
    swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;	// 缓冲区的显示格式
    swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;	// 刷新率的分子
    swapChainDesc.BufferDesc.RefreshRate.Numerator = 60;	// 刷新率的分母
    swapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;	// 逐行扫描VS隔行扫描(未指定的)
    swapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;	// 图像相对屏幕的拉伸（未指定的）
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;	// 将数据渲染至后台缓冲区（即作为渲染目标）
    swapChainDesc.OutputWindow = mhMainWnd;// 渲染窗口句柄
    swapChainDesc.SampleDesc.Count = 1;	// 多重采样数量
    swapChainDesc.SampleDesc.Quality = 0;	// 多重采样质量
    swapChainDesc.Windowed = true;	// 是否窗口化
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;	// 固定写法
    swapChainDesc.BufferCount = 2;	// 后台缓冲区数量（双缓冲）
    swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;	// 自适应窗口模式（自动选择最适于当前窗口尺寸的显示模式）
    // 利用DXGI接口下的工厂类创建交换链
    //第一个参数其实是命令队列接口指针
    //a.GetAddressOf();  //得到A**，只读，不改引用
    ThrowIfFailed(dxgiFactory->CreateSwapChain(cmdQueue.Get(), &swapChainDesc, swapChain.GetAddressOf()));
}
//描述符堆是存放描述符的一段连续内存空间。
//因为是双后台缓冲，所以要创建存放2个RTV的RTV堆，而深度模板缓存只有一个，所以创建1个DSV的DSV堆。
//先填充描述符堆属性结构体，然后通过设备创建描述符堆
void D3DApp::CreateDescriptorHeap()
{
    D3D12_DESCRIPTOR_HEAP_DESC rtvDescriptorHeapDesc;
    D3D12_DESCRIPTOR_HEAP_DESC dsvDescriptorHeapDesc;
    D3D12_DESCRIPTOR_HEAP_DESC srvDescriptorHeapDesc;
    D3D12_DESCRIPTOR_HEAP_DESC cbHeapDesc;
    // 首先创建RTV堆
    rtvDescriptorHeapDesc.NumDescriptors = 2;
    rtvDescriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    rtvDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDescriptorHeapDesc.NodeMask = 0;
    ThrowIfFailed(d3dDevice->CreateDescriptorHeap(&rtvDescriptorHeapDesc, IID_PPV_ARGS(&rtvHeap)));
    // 然后创建DSV堆
    dsvDescriptorHeapDesc.NumDescriptors = 1;
    dsvDescriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    dsvDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvDescriptorHeapDesc.NodeMask = 0;
    ThrowIfFailed(d3dDevice->CreateDescriptorHeap(&dsvDescriptorHeapDesc, IID_PPV_ARGS(&dsvHeap)));
    // 创建RSV堆
    srvDescriptorHeapDesc.NumDescriptors = 1;
    srvDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDescriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    srvDescriptorHeapDesc.NodeMask = 0;
    ThrowIfFailed(d3dDevice->CreateDescriptorHeap(&srvDescriptorHeapDesc, IID_PPV_ARGS(mSrvHeap.GetAddressOf())));
}
void D3DApp::CreateRTV()
{
    //用到了CD3DX12_CPU_DESCRIPTOR_HANDL，这个变体在d3dx12.h头文件中定义
    //DX库并没有集成，需要自行下载。
    //先从RTV堆中拿到首个RTV句柄
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(rtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (int i = 0; i < 2; ++i)
    {
        // 获得存于交换链中的后台缓冲区资源
        swapChain->GetBuffer(i, IID_PPV_ARGS(swapChainBuffer[i].GetAddressOf()));
        // 创建RTV
        d3dDevice->CreateRenderTargetView(swapChainBuffer[i].Get(),
            nullptr,	// 在交换链创建中已经定义了该资源的数据格式，所以这里指定为空指针
            rtvHeapHandle);	// 描述符句柄结构体（这里是变体，继承自CD3DX12_CPU_DESCRIPTOR_HANDLE）
        // 偏移到描述符堆中的下一个缓冲区
        rtvHeapHandle.Offset(1, rtvDescriptorSize);
    }
}
// 创建深度/模板缓冲区及其视图
// 具体过程是，
//   先在CPU中创建好DS资源，
//   然后通过CreateCommittedResource函数将DS资源提交至GPU显存中，
//   最后创建DSV将显存中的DS资源和DSV句柄联系起来。
void D3DApp::CreateDSV()
{
    D3D12_RESOURCE_DESC dsvResourceDesc;// 描述模板类型
    // 在CPU中创建好深度模板数据资源
    // 描述纹理资源
    dsvResourceDesc.Alignment = 0;// 指定对齐
    dsvResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;// 指定资源维度（类型）为TEXTURE2D
    dsvResourceDesc.DepthOrArraySize = 1;// 纹理深度为1
    dsvResourceDesc.Width = mClientWidth;// 资源宽
    dsvResourceDesc.Height = mClientHeight;// 资源高
    dsvResourceDesc.MipLevels = 1;// MIPMAP层级数量
    dsvResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    dsvResourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;// 深度模板资源的Flag
    dsvResourceDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;// 24位深度，8位模板,还有个无类型的格式DXGI_FORMAT_R24G8_TYPELESS也可以使用
    dsvResourceDesc.SampleDesc.Count = 4;// 多重采样数量
    dsvResourceDesc.SampleDesc.Quality = msaaQualityLevels.NumQualityLevels - 1;	// 多重采样质量
    // 描述清除资源的优化值
    CD3DX12_CLEAR_VALUE optClear;// 清除资源的优化值，提高清除操作的执行速度（CreateCommittedResource函数中传入）
    optClear.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;// 24位深度，8位模板,还有个无类型的格式DXGI_FORMAT_R24G8_TYPELESS也可以使用
    optClear.DepthStencil.Depth = 1;// 初始深度值为1
    optClear.DepthStencil.Stencil = 0;// 初始模板值为0
    // 创建一个资源和一个堆，并将资源提交至堆中（将深度模板数据提交至GPU显存中）
    CD3DX12_HEAP_PROPERTIES hpp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);// 堆所具有的属性，设置为默认堆。
    ThrowIfFailed(d3dDevice->CreateCommittedResource(&hpp,	// 堆类型为默认堆（不能写入）
        D3D12_HEAP_FLAG_NONE,	// Flag
        &dsvResourceDesc,	// 上面定义的DSV资源指针
        D3D12_RESOURCE_STATE_COMMON,	// 资源的状态为初始状态
        &optClear,	// 上面定义的优化值指针
        IID_PPV_ARGS(&depthStencilBuffer)));	// 返回深度模板资源
    // 创建DSV
    // DSV句柄
    d3dDevice->CreateDepthStencilView(depthStencilBuffer.Get(), nullptr, dsvHeap->GetCPUDescriptorHandleForHeapStart());
}
//资源在不同的时间段有着不同的作用，比如，有时候它是只读，有时候又是可写入的。
//用ResourceBarrier下的Transition函数来转换资源状态。
//void resourceBarrierBuild()
//{
//    cmdList->ResourceBarrier(1,	// Barrier屏障个数
//        &CD3DX12_RESOURCE_BARRIER::Transition(depthStencilBuffer.Get(),
//            D3D12_RESOURCE_STATE_COMMON,	// 转换前状态（创建时的状态，即CreateCommittedResource函数中定义的状态）
//            D3D12_RESOURCE_STATE_DEPTH_WRITE));	// 转换后状态为可写入的深度图，还有一个D3D12_RESOURCE_STATE_DEPTH_READ是只可读的深度图
//    // 等所有命令都进入cmdList后，用ExecuteCommandLists函数将命令从命令列表传入命令队列，也就是从CPU传入GPU的过程。
//    // 注意：在传入命令队列前必须关闭命令列表。
//    ThrowIfFailed(cmdList->Close());// 命令添加完后将其关闭
//    //a.Get();  //得到A*
//    ID3D12CommandList* cmdLists[] = { cmdList.Get() };// 声明并定义命令列表数组
//    cmdQueue->ExecuteCommandLists(_countof(cmdLists), cmdLists);
//}
// 实现围栏
void D3DApp::FlushCmdQueue()
{
    mCurrentFence++;// CPU传完命令并关闭后，将当前围栏值+1
    cmdQueue->Signal(fence.Get(), mCurrentFence);// 当GPU处理完CPU传入的命令后，将fence接口中的围栏值+1，即fence->GetCompletedValue()+1
    if (fence->GetCompletedValue() < mCurrentFence)// 如果小于，说明GPU没有处理完所有命令
    {
        HANDLE eventHandle = CreateEvent(nullptr, false, false, L"FenceSetDone");//创建事件
        fence->SetEventOnCompletion(mCurrentFence, eventHandle);// 当围栏达到mCurrentFence值（即执行到Signal（）指令修改了围栏值）时触发的eventHandle事件
        WaitForSingleObject(eventHandle, INFINITE);// 等待GPU命中围栏，激发事件（阻塞当前线程直到事件触发，注意此Enent需先设置再等待，
        // 如果没有Set就Wait，就死锁了，Set永远不会调用，所以也就没线程可以唤醒这个线程）
        CloseHandle(eventHandle);
    }
}

void D3DApp::CreateViewPortAndScissorRect()
{
    // 视口设置
    // TopLeftX和TopLeftY是后台缓冲区左上点的坐标，初始是（0，0），
    // Width和Height是缓冲区的长宽，为了不产生图像拉伸缩放，这两个值和窗口的Width、Height是相等的
    viewPort.TopLeftX = 0;
    viewPort.TopLeftY = 0;
    viewPort.Width = mClientWidth;
    viewPort.Height = mClientHeight;
    viewPort.MaxDepth = 1.0f;
    viewPort.MinDepth = 0.0f;
    // 裁剪矩形设置（矩形外的像素都将被剔除）
    // 前两个为左上点坐标，后两个为右下点坐标
    scissorRect.left = 0;
    scissorRect.top = 0;
    scissorRect.right = mClientWidth;
    scissorRect.bottom = mClientHeight;
}

void D3DApp::CalculateFrameState()
{
    static int frameCnt = 0;// 总帧数
    static float timeElapsed = 0.0f;//总时间
    frameCnt++;
    if (gt.TotalTime() - timeElapsed >= 1.0f)
    {
        float fps = frameCnt;// 每秒多少帧
        float mspf = 1000.0f / fps;// 每帧多少毫秒

        std::wstring fpsStr = std::to_wstring(fps);
        std::wstring mspfStr = std::to_wstring(mspf);
        // 将帧数显示在窗口上
        std::wstring windowText = mMainWndCaption + L"    fps: " + fpsStr + L"    " + L"mspf: " + mspfStr;
        SetWindowText(mhMainWnd, windowText.c_str());

        frameCnt = 0;
        timeElapsed += 1.0f;
    }
}

ID3D12Resource* D3DApp::CurrentBackBuffer() const
{
    return swapChainBuffer[mCurrentBackBuffer].Get();
}

D3D12_CPU_DESCRIPTOR_HANDLE D3DApp::CurrentBackBufferView() const 
{
    return CD3DX12_CPU_DESCRIPTOR_HANDLE(
        rtvHeap->GetCPUDescriptorHandleForHeapStart(),
        mCurrentBackBuffer,
        rtvDescriptorSize);
}

D3D12_CPU_DESCRIPTOR_HANDLE D3DApp::DepthStencilView() const 
{
    return dsvHeap->GetCPUDescriptorHandleForHeapStart();
}