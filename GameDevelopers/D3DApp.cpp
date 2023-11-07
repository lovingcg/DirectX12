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

// ��ʼ�����ں�D3D
bool D3DApp::Init(HINSTANCE hInstance, int nShowCmd) 
{
	if (!InitWindow(hInstance, nShowCmd))
		return false;
	else if (!InitDirect3D())
		return false;
    OnResize();

    
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    // ����ImGui���
    ImGui::StyleColorsDark();
    //���ھ��
    //��ʼ��win32��dx12 ����ƽ̨/��Ⱦ�����
    ImGui_ImplWin32_Init(mhMainWnd);
    //ImGui DX12��Ҫһ��SRV�Ѳ��ܻ���
    ImGui_ImplDX12_Init(d3dDevice.Get(), 3,
        DXGI_FORMAT_R8G8B8A8_UNORM, mSrvHeap.Get(),
        mSrvHeap.Get()->GetCPUDescriptorHandleForHeapStart(),
        mSrvHeap.Get()->GetGPUDescriptorHandleForHeapStart());

	return true;
}
LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    //MainWndProc�����ǻص����������ò���D3D12App����Protected������ݣ�
    //������D3D12App�����½�һ��MsgProc�����������ش��ڹ�����Ҫ��ֵ��
	return D3DApp::GetApp()->MsgProc(hwnd, msg, wParam, lParam);
}

bool D3DApp::InitWindow(HINSTANCE hInstance, int nShowCmd)
{
    //ע�ᴰ����
    WNDCLASS wc;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainWndProc;// ָ�����ڹ���
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(0, IDC_ARROW);
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpszMenuName = 0;
    wc.lpszClassName = L"ForTheDreamOfGameDevelop";//Ϊ������ָ��һ������

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
    AdjustWindowRect(&R, WS_OVERLAPPEDWINDOW, false);//���ݴ��ڵĿͻ�����С���㴰�ڵĴ�С
    int width = R.right - R.left;
    int hight = R.bottom - R.top;

    mhMainWnd = CreateWindow(L"ForTheDreamOfGameDevelop", L"GameDeveloper", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, width, hight, 0, 0, hInstance, 0);
    if (!mhMainWnd)
    {
        MessageBox(0, L"CreatWindow Failed", 0, 0);
        return 0;
    }

    //��ʾ ���´����õ�mhMainWnd
    ShowWindow(mhMainWnd, nShowCmd);
    UpdateWindow(mhMainWnd);

    return true;
}

bool D3DApp::InitDirect3D()
{
    //����D3D12���Բ�
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
// ���ڳߴ�ı��¼�
void D3DApp::OnResize()
{
    assert(d3dDevice);
    assert(swapChain);
    assert(cmdAllocator);

    // �ı���Դǰ��ͬ��
    FlushCmdQueue();
    ThrowIfFailed(cmdList->Reset(cmdAllocator.Get(), nullptr));

    //�ͷ�֮ǰ����Դ��Ϊ�������´�������׼��
    for (int i = 0; i < 2; i++)
    {
        swapChainBuffer[i].Reset();
    }
    depthStencilBuffer.Reset();

    //���µ�����̨��������Դ�Ĵ�С
    ThrowIfFailed(swapChain->ResizeBuffers(2,
        mClientWidth,
        mClientHeight,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));

    // ��̨��������������
    mCurrentBackBuffer = 0;
    //����������ԭ����InitDirect3D()��ִ�У���OnResize()�����滻������������InitDirect3D()��ִ��
    CreateRTV();
    CreateDSV();
    // ִ��Resize���� ֻҪ��Դ������ �ͱ���ִ�������б�ر�����
    ThrowIfFailed(cmdList->Close());
    ID3D12CommandList* cmdsLists[] = { cmdList.Get() };
    cmdQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // �ȴ�Resize�������
    FlushCmdQueue();

    CreateViewPortAndScissorRect();
}
// �ⲿ�ļ�����
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ���ڹ���
// ����hwnd�������ھ����msg�Ǵ���������Ϣ��wParam���������������룬lParam��ϵͳ��������Ϣ(�������˹���������Ϣ)
LRESULT CALLBACK D3DApp::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) 
{
    // ImGuiҪ��Ӧ��Ϣ������Դ��ڹ��̺�����һ���޸ġ�
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
        return true;
    //����������֮�����봰�ڹ��̺���һ�Σ�Ȼ������const ImGuiIO imio = ImGui::GetIO()��һ�����ʱ����ʾ�����Ļ�û�б�������
    // ���԰�IMGUI_CHECKVERSION();ImGui::CreateContext();�ŵ����Ｔ��
    //��ʼ�������Ĳ�����һЩ����
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    const ImGuiIO imio = ImGui::GetIO();
    // ��Ϣ����
    switch (msg)
    {
        // �����ڳߴ緢���任ʱ
        case WM_SIZE:
            mClientWidth = LOWORD(lParam);
            mClientHeight = HIWORD(lParam);
            if (d3dDevice)
            {
                if (wParam == SIZE_MINIMIZED)//�����С��,����ͣ��Ϸ��������С�������״̬
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
                else if (wParam == SIZE_RESTORED)//���ڱ���ԭ��������Ϣ
                {
                    //����С����ԭ
                    if (mMinimized)
                    {
                        mAppPaused = false;
                        mMinimized = false;
                        OnResize();
                    }
                    //����󻯻�ԭ
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
        //��갴������ʱ�Ĵ����������ң�
        case WM_LBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_RBUTTONDOWN:
            // wParamΪ�������������룬lParamΪϵͳ�����Ĺ����Ϣ
            OnMouseDown(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        //��갴��̧��ʱ�Ĵ����������ң�
        case WM_LBUTTONUP:
        case WM_MBUTTONUP:
        case WM_RBUTTONUP:
            OnMouseUp(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        //����ƶ��Ĵ���
        case WM_MOUSEMOVE:
            //�������ImguiҲ�벶׽�����Ϣ�Ļ�������ô�͡���ImGuiȥ�����������ǵ���Ϸ�߼���
            if (imio.WantCaptureMouse)
            {
                break;
            }
            OnMouseMove(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
     
        // �����ڱ�����ʱ����ֹ��Ϣѭ��
        case WM_DESTROY:
            PostQuitMessage(0);	// ��ֹ��Ϣѭ����������WM_QUIT��Ϣ
            return 0;
        default:
            break;
    }
    // ������û�д������Ϣת����Ĭ�ϵĴ��ڹ���
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ��Ϣѭ������Ϸѭ�����
//��Draw����ÿ����һ�Σ���ʵ����һ֡�����Ժ��ڻ�����ǰ�����֡ʱ��ļ��㣩��
int D3DApp::Run()
{
    // ������Ϣ�ṹ��
    MSG msg = { 0 };
    // ÿ����Ϣѭ������һ�μ�����
    gt.Reset();
    // ���PeekMessage����������0��˵��û�н��ܵ�WM_QUIT
    while (msg.message != WM_QUIT)
    {
        // ����д�����Ϣ�ͽ��д���
        if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))// PeekMessage�������Զ����msg�ṹ��Ԫ��
        {
            TranslateMessage(&msg);//�յ�����Ϣ����Ԥ������Ϣ����ӹ�һ�¡��������Ϣ�����˼ӹ�����ô��Ҫ����Ϣ����������ϵͳ���д�����Ϣ��
            DispatchMessage(&msg);//����ϵͳ���ô��ڴ������������¼����Ϣ�Ĵ���ʽ�������������һ������Ϣ�Ĵ���
        }
        // �����ִ�ж�������Ϸ�߼�
        else
        {
            gt.Tick();// ����ÿ��֡���ʱ��
            if (!gt.IsStopped())//���������ͣ״̬�����ǲ�������Ϸ
            {
                CalculateFrameState();
                Update();//ÿһ֡�����ܳ���mvp�任
                Draw();
            }
            //�������ͣ״̬��������100��
            else
            {
                Sleep(100);
            }
        }
    }
    // destruct imgui
    // �������shutdown�Լ�destroyContext��������ڴ�й©
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    return (int)msg.wParam;
}

void D3DApp::CreateDevice()
{
    //ComPtr < A > a;	//����һ��ComPtr����ָ��
    // &a  //A** ���ͣ����������ã�д����(�������)
    ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory)));
    ThrowIfFailed(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&d3dDevice)));
}
//ͬ��CPU��GPU
void D3DApp::CreateFence()
{
    ThrowIfFailed(d3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
}

// ��ȡ��������С�������С����֪������������ÿ��Ԫ�صĴ�С���������ڲ�ͬ��GPUƽ̨�ϵĴ�С���죩��
// �����ڵ�ַ����ƫ�����ҵ����е�������Ԫ�ء�
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
    commandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;// ָ��GPU����ִ�е����������ֱ�������б�δ�̳��κ�GPU״̬
    commandQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;// Ĭ���������
    ThrowIfFailed(d3dDevice->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&cmdQueue)));// �����������
    ThrowIfFailed(d3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAllocator)));// �������������
    // ���������б�
    ThrowIfFailed(d3dDevice->CreateCommandList(0, // ����ֵΪ0����GPU
        D3D12_COMMAND_LIST_TYPE_DIRECT, // �����б�����
        cmdAllocator.Get(),	// ����������ӿ�ָ��
        nullptr,	// ��ˮ��״̬����PSO�����ﲻ���ƣ����Կ�ָ��
        IID_PPV_ARGS(&cmdList)));	// ���ش����������б�
    cmdList->Close();// ���������б�ǰ���뽫��ر�
}
//���������� �������д�����ȾĿ����Դ������̨��������Դ
void D3DApp::CreateSwapChain()
{
    swapChain.Reset();
    // ����������
    DXGI_SWAP_CHAIN_DESC swapChainDesc;// �����������ṹ��
    swapChainDesc.BufferDesc.Width = mClientWidth;	// �������ֱ��ʵĿ��
    swapChainDesc.BufferDesc.Height = mClientHeight;	// �������ֱ��ʵĸ߶�
    swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;	// ����������ʾ��ʽ
    swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;	// ˢ���ʵķ���
    swapChainDesc.BufferDesc.RefreshRate.Numerator = 60;	// ˢ���ʵķ�ĸ
    swapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;	// ����ɨ��VS����ɨ��(δָ����)
    swapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;	// ͼ�������Ļ�����죨δָ���ģ�
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;	// ��������Ⱦ����̨������������Ϊ��ȾĿ�꣩
    swapChainDesc.OutputWindow = mhMainWnd;// ��Ⱦ���ھ��
    swapChainDesc.SampleDesc.Count = 1;	// ���ز�������
    swapChainDesc.SampleDesc.Quality = 0;	// ���ز�������
    swapChainDesc.Windowed = true;	// �Ƿ񴰿ڻ�
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;	// �̶�д��
    swapChainDesc.BufferCount = 2;	// ��̨������������˫���壩
    swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;	// ����Ӧ����ģʽ���Զ�ѡ�������ڵ�ǰ���ڳߴ����ʾģʽ��
    // ����DXGI�ӿ��µĹ����ഴ��������
    //��һ��������ʵ��������нӿ�ָ��
    //a.GetAddressOf();  //�õ�A**��ֻ������������
    ThrowIfFailed(dxgiFactory->CreateSwapChain(cmdQueue.Get(), &swapChainDesc, swapChain.GetAddressOf()));
}
//���������Ǵ����������һ�������ڴ�ռ䡣
//��Ϊ��˫��̨���壬����Ҫ�������2��RTV��RTV�ѣ������ģ�建��ֻ��һ�������Դ���1��DSV��DSV�ѡ�
//����������������Խṹ�壬Ȼ��ͨ���豸������������
void D3DApp::CreateDescriptorHeap()
{
    D3D12_DESCRIPTOR_HEAP_DESC rtvDescriptorHeapDesc;
    D3D12_DESCRIPTOR_HEAP_DESC dsvDescriptorHeapDesc;
    D3D12_DESCRIPTOR_HEAP_DESC srvDescriptorHeapDesc;
    D3D12_DESCRIPTOR_HEAP_DESC cbHeapDesc;
    // ���ȴ���RTV��
    rtvDescriptorHeapDesc.NumDescriptors = 2;
    rtvDescriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    rtvDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDescriptorHeapDesc.NodeMask = 0;
    ThrowIfFailed(d3dDevice->CreateDescriptorHeap(&rtvDescriptorHeapDesc, IID_PPV_ARGS(&rtvHeap)));
    // Ȼ�󴴽�DSV��
    dsvDescriptorHeapDesc.NumDescriptors = 1;
    dsvDescriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    dsvDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvDescriptorHeapDesc.NodeMask = 0;
    ThrowIfFailed(d3dDevice->CreateDescriptorHeap(&dsvDescriptorHeapDesc, IID_PPV_ARGS(&dsvHeap)));
    // ����RSV��
    srvDescriptorHeapDesc.NumDescriptors = 1;
    srvDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDescriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    srvDescriptorHeapDesc.NodeMask = 0;
    ThrowIfFailed(d3dDevice->CreateDescriptorHeap(&srvDescriptorHeapDesc, IID_PPV_ARGS(mSrvHeap.GetAddressOf())));
}
void D3DApp::CreateRTV()
{
    //�õ���CD3DX12_CPU_DESCRIPTOR_HANDL�����������d3dx12.hͷ�ļ��ж���
    //DX�Ⲣû�м��ɣ���Ҫ�������ء�
    //�ȴ�RTV�����õ��׸�RTV���
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(rtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (int i = 0; i < 2; ++i)
    {
        // ��ô��ڽ������еĺ�̨��������Դ
        swapChain->GetBuffer(i, IID_PPV_ARGS(swapChainBuffer[i].GetAddressOf()));
        // ����RTV
        d3dDevice->CreateRenderTargetView(swapChainBuffer[i].Get(),
            nullptr,	// �ڽ������������Ѿ������˸���Դ�����ݸ�ʽ����������ָ��Ϊ��ָ��
            rtvHeapHandle);	// ����������ṹ�壨�����Ǳ��壬�̳���CD3DX12_CPU_DESCRIPTOR_HANDLE��
        // ƫ�Ƶ����������е���һ��������
        rtvHeapHandle.Offset(1, rtvDescriptorSize);
    }
}
// �������/ģ�建����������ͼ
// ��������ǣ�
//   ����CPU�д�����DS��Դ��
//   Ȼ��ͨ��CreateCommittedResource������DS��Դ�ύ��GPU�Դ��У�
//   ��󴴽�DSV���Դ��е�DS��Դ��DSV�����ϵ������
void D3DApp::CreateDSV()
{
    D3D12_RESOURCE_DESC dsvResourceDesc;// ����ģ������
    // ��CPU�д��������ģ��������Դ
    // ����������Դ
    dsvResourceDesc.Alignment = 0;// ָ������
    dsvResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;// ָ����Դά�ȣ����ͣ�ΪTEXTURE2D
    dsvResourceDesc.DepthOrArraySize = 1;// �������Ϊ1
    dsvResourceDesc.Width = mClientWidth;// ��Դ��
    dsvResourceDesc.Height = mClientHeight;// ��Դ��
    dsvResourceDesc.MipLevels = 1;// MIPMAP�㼶����
    dsvResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    dsvResourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;// ���ģ����Դ��Flag
    dsvResourceDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;// 24λ��ȣ�8λģ��,���и������͵ĸ�ʽDXGI_FORMAT_R24G8_TYPELESSҲ����ʹ��
    dsvResourceDesc.SampleDesc.Count = 4;// ���ز�������
    dsvResourceDesc.SampleDesc.Quality = msaaQualityLevels.NumQualityLevels - 1;	// ���ز�������
    // ���������Դ���Ż�ֵ
    CD3DX12_CLEAR_VALUE optClear;// �����Դ���Ż�ֵ��������������ִ���ٶȣ�CreateCommittedResource�����д��룩
    optClear.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;// 24λ��ȣ�8λģ��,���и������͵ĸ�ʽDXGI_FORMAT_R24G8_TYPELESSҲ����ʹ��
    optClear.DepthStencil.Depth = 1;// ��ʼ���ֵΪ1
    optClear.DepthStencil.Stencil = 0;// ��ʼģ��ֵΪ0
    // ����һ����Դ��һ���ѣ�������Դ�ύ�����У������ģ�������ύ��GPU�Դ��У�
    CD3DX12_HEAP_PROPERTIES hpp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);// �������е����ԣ�����ΪĬ�϶ѡ�
    ThrowIfFailed(d3dDevice->CreateCommittedResource(&hpp,	// ������ΪĬ�϶ѣ�����д�룩
        D3D12_HEAP_FLAG_NONE,	// Flag
        &dsvResourceDesc,	// ���涨���DSV��Դָ��
        D3D12_RESOURCE_STATE_COMMON,	// ��Դ��״̬Ϊ��ʼ״̬
        &optClear,	// ���涨����Ż�ֵָ��
        IID_PPV_ARGS(&depthStencilBuffer)));	// �������ģ����Դ
    // ����DSV
    // DSV���
    d3dDevice->CreateDepthStencilView(depthStencilBuffer.Get(), nullptr, dsvHeap->GetCPUDescriptorHandleForHeapStart());
}
//��Դ�ڲ�ͬ��ʱ������Ų�ͬ�����ã����磬��ʱ������ֻ������ʱ�����ǿ�д��ġ�
//��ResourceBarrier�µ�Transition������ת����Դ״̬��
//void resourceBarrierBuild()
//{
//    cmdList->ResourceBarrier(1,	// Barrier���ϸ���
//        &CD3DX12_RESOURCE_BARRIER::Transition(depthStencilBuffer.Get(),
//            D3D12_RESOURCE_STATE_COMMON,	// ת��ǰ״̬������ʱ��״̬����CreateCommittedResource�����ж����״̬��
//            D3D12_RESOURCE_STATE_DEPTH_WRITE));	// ת����״̬Ϊ��д������ͼ������һ��D3D12_RESOURCE_STATE_DEPTH_READ��ֻ�ɶ������ͼ
//    // �������������cmdList����ExecuteCommandLists����������������б���������У�Ҳ���Ǵ�CPU����GPU�Ĺ��̡�
//    // ע�⣺�ڴ����������ǰ����ر������б�
//    ThrowIfFailed(cmdList->Close());// ������������ر�
//    //a.Get();  //�õ�A*
//    ID3D12CommandList* cmdLists[] = { cmdList.Get() };// ���������������б�����
//    cmdQueue->ExecuteCommandLists(_countof(cmdLists), cmdLists);
//}
// ʵ��Χ��
void D3DApp::FlushCmdQueue()
{
    mCurrentFence++;// CPU��������رպ󣬽���ǰΧ��ֵ+1
    cmdQueue->Signal(fence.Get(), mCurrentFence);// ��GPU������CPU���������󣬽�fence�ӿ��е�Χ��ֵ+1����fence->GetCompletedValue()+1
    if (fence->GetCompletedValue() < mCurrentFence)// ���С�ڣ�˵��GPUû�д�������������
    {
        HANDLE eventHandle = CreateEvent(nullptr, false, false, L"FenceSetDone");//�����¼�
        fence->SetEventOnCompletion(mCurrentFence, eventHandle);// ��Χ���ﵽmCurrentFenceֵ����ִ�е�Signal����ָ���޸���Χ��ֵ��ʱ������eventHandle�¼�
        WaitForSingleObject(eventHandle, INFINITE);// �ȴ�GPU����Χ���������¼���������ǰ�߳�ֱ���¼�������ע���Enent���������ٵȴ���
        // ���û��Set��Wait���������ˣ�Set��Զ������ã�����Ҳ��û�߳̿��Ի�������̣߳�
        CloseHandle(eventHandle);
    }
}

void D3DApp::CreateViewPortAndScissorRect()
{
    // �ӿ�����
    // TopLeftX��TopLeftY�Ǻ�̨���������ϵ�����꣬��ʼ�ǣ�0��0����
    // Width��Height�ǻ������ĳ���Ϊ�˲�����ͼ���������ţ�������ֵ�ʹ��ڵ�Width��Height����ȵ�
    viewPort.TopLeftX = 0;
    viewPort.TopLeftY = 0;
    viewPort.Width = mClientWidth;
    viewPort.Height = mClientHeight;
    viewPort.MaxDepth = 1.0f;
    viewPort.MinDepth = 0.0f;
    // �ü��������ã�����������ض������޳���
    // ǰ����Ϊ���ϵ����꣬������Ϊ���µ�����
    scissorRect.left = 0;
    scissorRect.top = 0;
    scissorRect.right = mClientWidth;
    scissorRect.bottom = mClientHeight;
}

void D3DApp::CalculateFrameState()
{
    static int frameCnt = 0;// ��֡��
    static float timeElapsed = 0.0f;//��ʱ��
    frameCnt++;
    if (gt.TotalTime() - timeElapsed >= 1.0f)
    {
        float fps = frameCnt;// ÿ�����֡
        float mspf = 1000.0f / fps;// ÿ֡���ٺ���

        std::wstring fpsStr = std::to_wstring(fps);
        std::wstring mspfStr = std::to_wstring(mspf);
        // ��֡����ʾ�ڴ�����
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