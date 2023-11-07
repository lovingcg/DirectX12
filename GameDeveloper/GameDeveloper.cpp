#include "GameDeveloper.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx12.h"
#include <fstream>
#include <memory>

//HINSTANCE hInstance��ʶ���Ӧ�ó����������Դ��Ϣ���ڴ�ռ�ľ��
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance, PSTR cmdLine, int nShowCmd)
{
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif
    try
    {
        GameDevelopApp theApp;
        if (!theApp.Init(hInstance, nShowCmd))
            return 0;

        return theApp.Run();
    }
    catch (DxException& e)
    {
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
        return 0;
    }
    return 0;
}

GameDevelopApp::GameDevelopApp()
{

}

GameDevelopApp::~GameDevelopApp()
{
    
}

bool GameDevelopApp::Init(HINSTANCE hInstance, int nShowCmd)
{
    if (!D3DApp::Init(hInstance, nShowCmd))
        return false;
    // reset�����б���Ϊ�����ʼ����׼��
    ThrowIfFailed(cmdList->Reset(cmdAllocator.Get(), nullptr));

    //��ȡ�˶���������������������С������Ӳ��ר�õģ����Ա����ѯ��Щ��Ϣ
    mCbvSrvDescriptorSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    LoadTextures();
    BuildRootSignature();
    BuildDescriptorHeaps();
    BuildShadersAndInputLayout();
    BuildRoomGeometry();
    BuildSkullGeometry();
    BuildMaterials();//��������
    // BuildRenderItems()����������CreateConstantBufferView()����֮ǰ����Ȼ�ò���allRitems.size()��ֵ
    BuildRenderItems();
    BuildFrameResources();

    //���˸������������ǾͲ���Ҫ����CBV��Ҳ�Ͳ���Ҫ����CBV���ˣ�������Init�а�����ע�͵���
   /* BuildDescriptorHeaps();
    BuildConstantBuffers();*/
    BuildPSO();

    // ִ�����ʼ������ �ر�������в�ͬ��CPU��GPU��
    // û�а�BuildGeometry�������ڳ�ʼ���У���û����������close��ͬ��������GPU���ݱ���ǰ�ͷ�
    ThrowIfFailed(cmdList->Close());
    ID3D12CommandList* cmdsLists[] = { cmdList.Get() };
    cmdQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // �ȴ�ֱ����ʼ���������
    FlushCmdQueue();

    return true;
}
// Draw������Ҫ�ǽ����ǵĸ�����Դ���õ���Ⱦ��ˮ����, 
// �����շ���������������������������cmdAllocator�������б�cmdList��
// Ŀ��������������б���������ڴ档
//ע�͵Ĳ�����ʵ���Ƿ�װ������
void GameDevelopApp::Draw()
{
    //��Ϊÿ��֡��Դ�����Լ��������������������Draw������ʼ�����Ǿ�ҪReset��ǰ֡��Դ�е�cmdAllocator��
    auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

    ThrowIfFailed(cmdListAlloc->Reset());// �ظ�ʹ�ü�¼���������ڴ�
    //ThrowIfFailed(cmdList->Reset(cmdListAlloc.Get(), mPSO.Get()));

    if (is_wiremode)
    {
        ThrowIfFailed(cmdList->Reset(cmdListAlloc.Get(), mPSOs["wireframe"].Get()));// ���������б����ڴ�
    }
    else
    {
        ThrowIfFailed(cmdList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));// ���������б����ڴ�
    }

    //�����ӿںͲü�����
    cmdList->RSSetViewports(1, &viewPort);
    cmdList->RSSetScissorRects(1, &scissorRect);

    // ���ø�ǩ��
    cmdList->SetGraphicsRootSignature(mRootSignature.Get());

    // ����̨������Դ�ӳ���״̬ת������ȾĿ��״̬����׼������ͼ����Ⱦ��
    cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),// ת����ԴΪ��̨��������Դ
        D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));// �ӳ��ֵ���ȾĿ��ת��

    // ������Ч֮��ðѱ���ɫ���ó���������ɫ
    cmdList->ClearRenderTargetView(CurrentBackBufferView(), clearColor, 0, nullptr);// ���RT����ɫΪ���죬���Ҳ����òü�����
    cmdList->ClearDepthStencilView(DepthStencilView(),	// DSV���������
        D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,	// FLAG
        1.0f,	// Ĭ�����ֵ
        0,	// Ĭ��ģ��ֵ
        0,	// �ü���������
        nullptr);	// �ü�����ָ��

    // Ȼ��ָ����Ҫ��Ⱦ�Ļ���������ָ��RTV��DSV��
    cmdList->OMSetRenderTargets(1,// ���󶨵�RTV����
        &CurrentBackBufferView(),	// ָ��RTV�����ָ��
        true,	// RTV�����ڶ��ڴ�����������ŵ�
        &DepthStencilView());	// ָ��DSV��ָ��

    //����SRV��������
    ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };// ע������֮���������飬����Ϊ�����ܰ���SRV��UAV������������ֻ�õ���SRV
    cmdList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));

    // ��passCB��������
    auto passCB = mCurrFrameResource->PassCB->Resource();
    // �Ĵ����ۺţ���ɫ��register��b2����Ҫ�͸�ǩ������ʱ��һһ��Ӧ
    cmdList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());

    //�ֱ�����PSO�����ƶ�Ӧ��Ⱦ��
    //���Ʋ�͸��������Ⱦ��ذ塢ǽ�����ӡ�ԭ���ã�
    DrawRenderItems(cmdList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);

    // ʹ�ɼ�����������ģ�建��������ֵ1
    cmdList->OMSetStencilRef(1);//����ģ��RefֵΪ1����ΪRef�滻ֵ����Ϊ֮����Ⱦ�����ģ��ֵ��
    cmdList->SetPipelineState(mPSOs["markStencilMirrors"].Get());//���þ���ģ��PSO
    DrawRenderItems(cmdList.Get(), mRitemLayer[(int)RenderLayer::Mirrors]);

    //���ø�������������ƹ��reflectPassCB��
    cmdList->SetGraphicsRootConstantBufferView(2,//����������
        passCB->GetGPUVirtualAddress() + 1 * passCBByteSize);
    //���ƾ���������Ⱦ��������ã�
    cmdList->SetPipelineState(mPSOs["drawStencilReflections"].Get());//���þ�������ģ��PSO
    DrawRenderItems(cmdList.Get(), mRitemLayer[(int)RenderLayer::Reflected]);

    //��ԭpassCB��Refģ��ֵ
    //��ԭ������պ�Refģ��ֵ
    cmdList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());
    // ����Ҳ����ģ����ԣ���ʵ�����ˣ���Ϊ����0��������Զͨ����
    cmdList->OMSetStencilRef(0);

    //���ƾ��ӻ��
    cmdList->SetPipelineState(mPSOs["transparent"].Get());
    DrawRenderItems(cmdList.Get(), mRitemLayer[(int)RenderLayer::Transparent]);

    cmdList->SetPipelineState(mPSOs["shadow"].Get());
    DrawRenderItems(cmdList.Get(), mRitemLayer[(int)RenderLayer::Shadow]);

    cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));// ����ȾĿ�굽����

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
                mTheta = 1.5f * XM_PI;
                mPhi = XM_PIDIV4;
                mRadius = 5.0f;
                mMainPassCB.alpha = 0.3f;
            }
            ImGui::SliderFloat("Scale", &mRadius, 3.0f, 200.0f);// �϶����������С

            ImGui::Text("mTheta: %.2f degrees", mTheta);// ��ʾ���֣�������������Ŀؼ� 
            ImGui::SliderFloat("##1", &mTheta, -XM_PI, XM_PI, ""); // ����ʾ�ؼ����⣬��ʹ��##�������ǩ�ظ�  ���ַ���������ʾ����

            ImGui::Text("mPhi: %.2f degrees", mPhi);
            ImGui::SliderFloat("##2", &mPhi, -XM_PI, XM_PI, "");

            float x = mRadius * sinf(mPhi) * cosf(mTheta);
            float z = mRadius * sinf(mPhi) * sinf(mTheta);
            float y = mRadius * cosf(mPhi);

            ImGui::Text("Camera Position: (%.1f, %.1f, %.1f, 1.0)", x, y, z);

            ImGui::Text("FOV: %.2f degrees", XMConvertToDegrees(fov * MathHelper::Pi));
            ImGui::SliderFloat("##3", &fov, 0.1f, 0.5f, "");

            ImGui::ColorEdit3("Clear Color", clearColor);
        }
        ImGui::Checkbox("Wiremode", &is_wiremode);
        ImGui::Checkbox("Sunmove", &is_sunmove);
        if (is_sunmove)
        {
            OnKeyboardInput(gt);
        }
        ImGui::Checkbox("Skullmove", &is_skullmove);
        if (is_skullmove)
        {
            OnKeyboardInputMove(gt);
        }
        //ImGui::Checkbox("CartoonShader", &is_ctshader);
        //ImGui::Checkbox("Fog", &fog);
        ImGui::End();
    }
    cmdList->SetDescriptorHeaps(1, mSrvHeap.GetAddressOf());
    ImGui::Render();
    // ����ImGui��Direct3D�Ļ���
    // �����Ҫ�ڴ�֮ǰ���󱸻������󶨵���Ⱦ������
    // ��Ҫ�ڻ��ƵĹ��������Ժ����
    // ��˵��������DrawIndexed֮��RTתPRESENT֮ǰ
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdList.Get());
    
    // �������ļ�¼�ر������б�
    ThrowIfFailed(cmdList->Close());

    // ��CPU�����׼���ú���Ҫ����ִ�е������б����GPU��������С�
    // ʹ�õ���ExecuteCommandLists������
    ID3D12CommandList* commandLists[] = { cmdList.Get() };// ���������������б�����
    cmdQueue->ExecuteCommandLists(_countof(commandLists), commandLists);// ������������б����������

    // Ȼ�󽻻�ǰ��̨������������������㷨��1��0��0��1��Ϊ���ú�̨������������ԶΪ0����
    ThrowIfFailed(swapChain->Present(0, 0));
    mCurrentBackBuffer = (mCurrentBackBuffer + 1) % 2;

    // �������Χ��ֵ��ˢ��������У�ʹCPU��GPUͬ����ֱ�ӷ�װ��
    // FlushCmdQueue();

    //���Χ��ֵ�Խ������ǵ���Χ���㡣
    mCurrFrameResource->fenceCPU = ++mCurrentFence;

    // ��GPU����������󣬽�GPU��Fence++

    //��ָ����ӵ���������������µ�Χ���㡣
    //��Ϊ������GPUʱ�����ϣ������µ�Χ���㲻�����ã�ֱ��GPU�ڴ�Signal����֮ǰ�����������Ĵ���
    //signal�����þ��ǣ��������б�ִ����֮��fenceֵ���ó�mCurrentFence
    cmdQueue->Signal(fence.Get(), mCurrentFence);
}
// ��������¼�
void GameDevelopApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    mLastMousePos.x = x;// ���µ�ʱ���¼����x����
    mLastMousePos.y = y;// ���µ�ʱ���¼����y����

    SetCapture(mhMainWnd);// �����ڵ�ǰ�̵߳�ָ�������������겶��
}
// �ɿ�����¼�
void GameDevelopApp::OnMouseUp(WPARAM btnState, int x, int y)
{
    ReleaseCapture();// ����̧����ͷ���겶��
}
// �ƶ�����¼�
void GameDevelopApp::OnMouseMove(WPARAM btnState, int x, int y)
{
    if ((btnState & MK_LBUTTON) != 0)// ������������״̬
    {
        // �������ƶ����뻻��ɻ��ȣ�0.25Ϊ������ֵ
        float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
        float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

        // �������û���ɿ�ǰ���ۼƻ���
        mTheta += dx;
        mPhi += dy;

        // ���ƽǶ�mPhi�ķ�Χ�ڣ�0.1�� Pi-0.1��
        mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
    }
    else if ((btnState & MK_RBUTTON) != 0)// ������Ҽ�����״̬
    {
        // �������ƶ����뻻������Ŵ�С��0.005Ϊ������ֵ
        float dx = 0.2f * static_cast<float>(x - mLastMousePos.x);
        float dy = 0.2f * static_cast<float>(y - mLastMousePos.y);
        // ����������������������ӷ�Χ�뾶
        mRadius += dx - dy;
        //���ƿ��ӷ�Χ�뾶
        mRadius = MathHelper::Clamp(mRadius, 3.0f, 200.0f);
    }
    // ����ǰ������긳ֵ������һ��������ꡱ��Ϊ��һ���������ṩ��ǰֵ
    mLastMousePos.x = x;
    mLastMousePos.y = y;
}

void GameDevelopApp::OnResize()
{
    D3DApp::OnResize();

    //��ͶӰ����Ĺ�����������
    //��֮ǰͶӰ������Update�����й�������������ͶӰ������Ҫÿ֡�ı䣬�����ڴ��ڸı�ʱ�Ż�ı�
    //����Ӧ�÷���OnResize�����У�
    XMMATRIX P = XMMatrixPerspectiveFovLH(fov * MathHelper::Pi, static_cast<float>(mClientWidth) / mClientHeight, 1.0f, 1000.0f);
    XMStoreFloat4x4(&mProj, P);
}

// ÿһ֡�Ĳ���
void GameDevelopApp::Update()
{ 
    //OnKeyboardInputMove(gt);
    UpdateCamera(gt);

    // ÿ֡����һ��֡��Դ����֡�Ļ����ǻ��α�����
    mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
    mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

    //���GPU��Χ��ֵС��CPU��Χ��ֵ����CPU�ٶȿ���GPU������CPU�ȴ�
    // һ��Χ������3֡
    if (mCurrFrameResource->fenceCPU != 0 && fence->GetCompletedValue() < mCurrFrameResource->fenceCPU)
    {
        HANDLE eventHandle = CreateEvent(nullptr, false, false, L"FenceSetDone");
        ThrowIfFailed(fence->SetEventOnCompletion(mCurrFrameResource->fenceCPU, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }

    UpdateObjectCBs(gt);
    UpdateMaterialCBs(gt);
    UpdateMainPassCB(gt);
    UpdateReflectedPassCB(gt);
}

void GameDevelopApp::OnKeyboardInput(const _GameTimer::GameTimer& gt)
{
    const float dt = gt.DeltaTime();

    //���Ҽ��ı�ƽ�й��Theta�ǣ����¼��ı�ƽ�й��Phi��
    if (GetAsyncKeyState(VK_LEFT) & 0x8000)
        mSunTheta -= 1.0f * dt;

    if (GetAsyncKeyState(VK_RIGHT) & 0x8000)
        mSunTheta += 1.0f * dt;

    if (GetAsyncKeyState(VK_UP) & 0x8000)
        mSunPhi -= 1.0f * dt;

    if (GetAsyncKeyState(VK_DOWN) & 0x8000)
        mSunPhi += 1.0f * dt;

    //��PhiԼ����[0, PI/2]֮��
    mSunPhi = MathHelper::Clamp(mSunPhi, 0.1f, XM_PIDIV2);
}

void GameDevelopApp::OnKeyboardInputMove(const _GameTimer::GameTimer& gt)
{
    const float dt = gt.DeltaTime();

    if (GetAsyncKeyState('A') & 0x8000)
        mSkullTranslation.x -= 1.0f * dt;

    if (GetAsyncKeyState('D') & 0x8000)
        mSkullTranslation.x += 1.0f * dt;

    if (GetAsyncKeyState('W') & 0x8000)
        mSkullTranslation.y += 1.0f * dt;

    if (GetAsyncKeyState('S') & 0x8000)
        mSkullTranslation.y -= 1.0f * dt;

    if (GetAsyncKeyState('Z') & 0x8000)
        mSkullTranslation.z += 1.0f * dt;

    if (GetAsyncKeyState('C') & 0x8000)
        mSkullTranslation.z -= 1.0f * dt;

    if (GetAsyncKeyState('E') & 0x8000)
        mSkullrotate -= 0.4f * dt;
    if (GetAsyncKeyState('Q') & 0x8000)
        mSkullrotate += 0.4f * dt;

    // ��ֹ�ƶ���ƽ������
    mSkullTranslation.y = MathHelper::Max(mSkullTranslation.y, 0.0f);

    // �����µ��������
    XMMATRIX skullRotate = XMMatrixRotationY(mSkullrotate * MathHelper::Pi);
    XMMATRIX skullScale = XMMatrixScaling(0.5f, 0.5f, 0.5f);
    XMMATRIX skullOffset = XMMatrixTranslation(mSkullTranslation.x, mSkullTranslation.y, mSkullTranslation.z);

    // �������˳���Ӱ���������ĸ���ת ��������Ҫ�����尴���Լ�������ת
    XMMATRIX skullWorld = skullRotate * skullScale * skullOffset;
    XMStoreFloat4x4(&mSkullRitem->World, skullWorld);

    // ���·�����������
    XMVECTOR mirrorPlane = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f); // xy plane
    XMMATRIX R = XMMatrixReflect(mirrorPlane);
    XMStoreFloat4x4(&mReflectedSkullRitem->World, skullWorld * R);

    // ������Ӱ����
    XMVECTOR shadowPlane = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f); // xz plane
    XMVECTOR toMianLight = -XMLoadFloat3(&mMainPassCB.Lights[0].Direction);
    // DX12�ṩ��XMMatrixShadow������������Ӱ
    XMMATRIX S = XMMatrixShadow(shadowPlane, toMianLight);
    // yOffset��Ϊ�˱�����Ӱ�͵ذ�ģ��������˸��Bug
    XMMATRIX shadowOffsetY = XMMatrixTranslation(0.0f, 0.0001f, 0.0f);
    XMStoreFloat4x4(&mShadowedSkullRitem->World, skullWorld * S * shadowOffsetY);

    // ������Ӱ������������Է������R������
    XMStoreFloat4x4(&mReflectedShadowedSkullRitem->World, skullWorld * R * S * shadowOffsetY);

    mSkullRitem->NumFramesDirty = gNumFrameResources;
    mReflectedSkullRitem->NumFramesDirty = gNumFrameResources;
    mShadowedSkullRitem->NumFramesDirty = gNumFrameResources;
    mReflectedShadowedSkullRitem->NumFramesDirty = gNumFrameResources;
}

void GameDevelopApp::UpdateCamera(const _GameTimer::GameTimer& gt)
{
    // ����������ת��Ϊ�ѿ�������
    mEyePos.x = mRadius * sinf(mPhi) * cosf(mTheta);
    mEyePos.z = mRadius * sinf(mPhi) * sinf(mTheta);
    mEyePos.y = mRadius * cosf(mPhi);
   
    XMVECTOR pos = XMVectorSet(mEyePos.x, mEyePos.y, mEyePos.z, 1.0f);
    XMVECTOR target = XMVectorZero();
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    //�����۲����
    XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
    XMStoreFloat4x4(&mView, view);
}

void GameDevelopApp::AnimateMaterials(const _GameTimer::GameTimer& gt)
{
    // ֻ�����ı��˺�ˮ���ʵ�matTransform
    auto waterMat = mMaterials["water"].get();

    float& tu = waterMat->MatTransform(3, 0);//���ؾ����4�е�1�е�floatֵ��u�����ƽ������
    float& tv = waterMat->MatTransform(3, 1);//���ؾ����4�е�2�е�floatֵ��v�����ƽ������

    //��Ϊÿ֡����ִ�д˺�����u��v����Խ��Խ��
    tu += 0.1f * gt.DeltaTime();
    tv += 0.02f * gt.DeltaTime();

    //���uv����1����ת��0���൱��ѭ���ˣ�
    //ע��UV�ĳ�ʼֵΪ0����Ϊ��λ���󷵻ص�ʱ�����0
    if (tu >= 1.0f)
        tu -= 1.0f;

    if (tv >= 1.0f)
        tv -= 1.0f;
    //��tu��tv�������
    // hlsl��Ϊ�ҳˣ�����Ͳ���һ��ת����
    waterMat->MatTransform(3, 0) = tu;
    waterMat->MatTransform(3, 1) = tv;

    // �����Ѹ��ģ������Ҫ����cbuffer
    waterMat->NumFramesDirty = gNumFrameResources;

    // ����������ϵĻ���������ʸı�
    auto boxMat = mMaterials["wood"].get();
    XMStoreFloat4x4(&boxMat->MatTransform, XMMatrixRotationZ(gt.TotalTime()));
    // �����Ѹ��ģ������Ҫ����cbuffer
    boxMat->NumFramesDirty = gNumFrameResources;
}

void GameDevelopApp::UpdateObjectCBs(const _GameTimer::GameTimer& gt)
{
    auto currObjectCB = mCurrFrameResource->ObjectCB.get();
    for (auto& e : mAllRitems)
    {
        //ֻ���ڳ�����������ʱ�Ÿ���cbuffer���ݡ�
        //����Ҫ��֡��Դ���и��١�
        if (e->NumFramesDirty > 0)
        {
            XMMATRIX world = XMLoadFloat4x4(&e->World);
            // ��texTransform������ObjCB���������ݾʹ����˳������������ˣ������մ�����ɫ��
            XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

            ObjectConstants objConstants;
            XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
            XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));
            //�����ݿ�����GPU����
            //e->ObjCBIndex��֤��������뵥��������һһ��Ӧ
            currObjectCB->CopyData(e->ObjCBIndex, objConstants);

            //��һ��FrameResourceҲ��Ҫ���¡�
            e->NumFramesDirty--;
        }
    }
}

//ʵ��˼·��objConstants���ƣ�����materials�����б���ȡ��Materialָ�룬
//�����丳ֵ�������ṹ���ж�ӦԪ�أ�����ʹ��CopyData���������ݴ���GPU��
//���ǵ�numFramesDirty--����Ϊ������������ÿ��֡��Դ���ܵõ�����
void GameDevelopApp::UpdateMaterialCBs(const _GameTimer::GameTimer& gt)
{
    auto currMaterialCB = mCurrFrameResource->MaterialCB.get();
    for (auto& e : mMaterials)
    {
        Material* mat = e.second.get();//��ü�ֵ�Ե�ֵ����Materialָ�루����ָ��ת��ָͨ�룩
        if (mat->NumFramesDirty > 0)
        {
            XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

            //������Ĳ������Դ��������ṹ���е�Ԫ��
            MaterialConstants matConstants;
            matConstants.DiffuseAlbedo = mat->DiffuseAlbedo;
            matConstants.FresnelR0 = mat->FresnelR0;
            matConstants.Roughness = mat->Roughness;
            XMStoreFloat4x4(&matConstants.MatTransform, XMMatrixTranspose(matTransform));

            //�����ʳ������ݸ��Ƶ�������������Ӧ������ַ��
            currMaterialCB->CopyData(mat->MatCBIndex, matConstants);
            //������һ��֡��Դ
            mat->NumFramesDirty--;
        }
    }
}

void GameDevelopApp::UpdateMainPassCB(const _GameTimer::GameTimer& gt)
{
    // ��ֵviewProj����
    XMMATRIX view = XMLoadFloat4x4(&mView);
    //XMMATRIX proj = XMLoadFloat4x4(&mProj);
    XMMATRIX proj = XMMatrixPerspectiveFovLH(fov * MathHelper::Pi, static_cast<float>(mClientWidth) / mClientHeight, 1.0f, 1000.0f);
    XMMATRIX viewProj = XMMatrixMultiply(view, proj);

    XMStoreFloat4x4(&mMainPassCB.viewProj, XMMatrixTranspose(viewProj));

    // ��ֵlight����
    mMainPassCB.EyePosW = mEyePos;
    mMainPassCB.AmbientLight = { 0.25f,0.25f,0.35f,1.0f };
    //mMainPassCB.TotalTime = gt.TotalTime();
    mMainPassCB.CartoonShader = is_ctshader;

    // ��������ת���ѿ�������
    // ����
    XMVECTOR lightDir = -MathHelper::SphericalToCartesian(1.0f, mSunTheta, mSunPhi);
    XMStoreFloat3(&mMainPassCB.Lights[0].Direction, lightDir);
    mMainPassCB.Lights[0].Strength = { 0.6f, 0.6f, 0.6f };

    // ������ ������
    mMainPassCB.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
    mMainPassCB.Lights[1].Strength = { 0.3f, 0.3f, 0.3f };
    mMainPassCB.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
    mMainPassCB.Lights[2].Strength = { 0.15f, 0.15f, 0.15f };

    mMainPassCB.FogColor = { 0.7f,0.7f,0.7f,1.0f };
    mMainPassCB.gFogStart = 5.0f;
    mMainPassCB.gFogRange = 150.0f;
    mMainPassCB.pad2 = { 0.0f,0.0f };

    // ��perPass�������ݴ���������
    auto currPassCB = mCurrFrameResource->PassCB.get();
    // 0��ʾ��������干��һ���۲�ͶӰ����
    currPassCB->CopyData(0, mMainPassCB);
}

void GameDevelopApp::UpdateReflectedPassCB(const _GameTimer::GameTimer& gt)
{
    mReflectedPassCB = mMainPassCB;

    XMVECTOR mirrorPlane = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);//������Ƭ�ĶԳ�������
    XMMATRIX R = XMMatrixReflect(mirrorPlane);

    //����ƹ�
    for (int i = 0; i < 3; ++i)
    {
        XMVECTOR lightDir = XMLoadFloat3(&mMainPassCB.Lights[i].Direction);
        XMVECTOR reflectedLightDir = XMVector3TransformNormal(lightDir, R);
        XMStoreFloat3(&mReflectedPassCB.Lights[i].Direction, reflectedLightDir);
    }

    // �洢������1�еķ���
    auto currPassCB = mCurrFrameResource->PassCB.get();
    currPassCB->CopyData(1, mReflectedPassCB);
}

void GameDevelopApp::BuildBoxGeometry()
{
    //std::array<Vertex, 8> vertices =
    //{
    //   /* Vertex({ XMFLOAT3(-1.0f, -1.0f, -1.0f), XMCOLOR(Colors::White) }),
    //    Vertex({ XMFLOAT3(-1.0f, +1.0f, -1.0f), XMCOLOR(Colors::Black) }),
    //    Vertex({ XMFLOAT3(+1.0f, +1.0f, -1.0f), XMCOLOR(Colors::Red) }),
    //    Vertex({ XMFLOAT3(+1.0f, -1.0f, -1.0f), XMCOLOR(Colors::Green) }),
    //    Vertex({ XMFLOAT3(-1.0f, -1.0f, +1.0f), XMCOLOR(Colors::Blue) }),
    //    Vertex({ XMFLOAT3(-1.0f, +1.0f, +1.0f), XMCOLOR(Colors::Yellow) }),
    //    Vertex({ XMFLOAT3(+1.0f, +1.0f, +1.0f), XMCOLOR(Colors::Cyan) }),
    //    Vertex({ XMFLOAT3(+1.0f, -1.0f, +1.0f), XMCOLOR(Colors::Magenta) })*/
    //};

    //std::array<std::uint16_t, 36> indices =
    //{
    //    // ǰ
    //    0, 1, 2,
    //    0, 2, 3,

    //    // ��
    //    4, 6, 5,
    //    4, 7, 6,

    //    // ��
    //    4, 5, 1,
    //    4, 1, 0,

    //    // ��
    //    3, 2, 6,
    //    3, 6, 7,

    //    // ��
    //    1, 5, 6,
    //    1, 6, 2,

    //    // ��
    //    4, 0, 3,
    //    4, 3, 7
    //};

    //mIndexCount = (UINT)indices.size();

    //const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
    //const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

    //// �������㻺����������
    //// ��������->CPU
    //// ��CPU�п���һ�����л��ռ� mVertexBufferCPU�洢�ռ��COM�ӿ�ָ��
    //ThrowIfFailed(D3DCreateBlob(vbByteSize, &mVertexBufferCPU));
    //// �������ݿ�����CPU
    //CopyMemory(mVertexBufferCPU->GetBufferPointer(),// Ŀ��ռ�ָ��
    //    vertices.data(),// ���ݿ�ʼָ�� 
    //    vbByteSize);// ��Сbyte
    //// ��������Ĭ�϶� CPU->GPU
    //mVertexBufferGPU = d3dUtil::CreateDefaultBuffer(d3dDevice.Get(), cmdList.Get(), vertices.data(), vbByteSize, mVertexBufferUploader);

    //// ��������������������
    ////���������ݷŵ���������������GPU���ʣ���Ϊ����һ�㲻��䣬�������ǰ�Ҳ��������Ĭ�϶���
    //ThrowIfFailed(D3DCreateBlob(ibByteSize, &mIndexBufferCPU));
    //CopyMemory(mIndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);
    //mIndexBufferGPU = d3dUtil::CreateDefaultBuffer(d3dDevice.Get(), cmdList.Get(), indices.data(), ibByteSize, mIndexBufferUploader);

    //mVertexByteStride = sizeof(Vertex);
    //mVertexBufferByteSize = vbByteSize;
    //mIndexFormat = DXGI_FORMAT_R16_UINT;
    //mIndexBufferByteSize = ibByteSize;
    
    //����BOX������
    ProceduralGeometry proceGeo;
    ProceduralGeometry::MeshData box = proceGeo.CreateBox(8.0f, 8.0f, 8.0f, 3);

    //���������ݴ���Vertex�ṹ�������Ԫ��
    size_t verticesCount = box.Vertices.size(); // �ܶ�����
    std::vector<Vertex> vertices(verticesCount);//���������б�
    for (size_t i = 0; i < verticesCount; i++)
    {
        vertices[i].Pos = box.Vertices[i].Position;
        vertices[i].Normal = box.Vertices[i].Normal;
        vertices[i].TexC = box.Vertices[i].TexC;
    }

    //���������б�,����ʼ��
    std::vector<std::uint16_t> indices = box.GetIndices16();

    //�����б��С
    const UINT vbByteSize = (UINT)verticesCount * sizeof(Vertex);
    //�����б��С
    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

    //����������
    SubmeshGeometry submesh;
    submesh.BaseVertexLocation = 0;
    submesh.StartIndexLocation = 0;
    submesh.IndexCount = (UINT)indices.size();

    //��ֵMeshGeometry�ṹ�е�����Ԫ��
    auto geo = std::make_unique<MeshGeometry>();	//ָ��MeshGeometry��ָ��
    geo->Name = "boxGeo";
    geo->VertexByteStride = sizeof(Vertex);//��������Ĵ�С
    geo->VertexBufferByteSize = vbByteSize;
    geo->IndexBufferByteSize = ibByteSize;
    geo->IndexFormat = DXGI_FORMAT_R16_UINT;
    geo->DrawArgs["box"] = submesh;

    ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));//��CPU�ϴ������㻺��ռ�
    CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);//���������ݿ�����CPUϵͳ�ڴ�
    geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(d3dDevice.Get(),
        cmdList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);//���������ݴ�CPU������GPU

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));//��CPU�ϴ�����������ռ�
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);//���������ݿ�����CPUϵͳ�ڴ�
    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(d3dDevice.Get(),
        cmdList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);//���������ݴ�CPU������GPU

    //װ���ܵļ�����ӳ���
    mGeometries["boxGeo"] = std::move(geo);
}

//void GameDevelopApp::BuildGeometry()
//{
//    ProceduralGeometry proceGeo;
//    //float width, float height, float depth, uint32 numSubdivisions
//    ProceduralGeometry::MeshData box = proceGeo.CreateBox(1.5f, 0.5f, 1.5f, 3);
//    //float width, float depth, uint32 m, uint32 n
//    ProceduralGeometry::MeshData grid = proceGeo.CreateGrid(20.0f, 30.0f, 60, 40);
//    //float radius, uint32 sliceCount, uint32 stackCount �뾶����Ƭ�������ѵ�����
//    ProceduralGeometry::MeshData sphere = proceGeo.CreateSphere(0.5f, 20, 20);
//    //float bottomRadius, float topRadius, float height, uint32 sliceCount, uint32 stackCount
//    ProceduralGeometry::MeshData cylinder = proceGeo.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);
//
//    //���㵥�������嶥�����ܶ��������е�ƫ����,˳��Ϊ��box��grid��sphere��cylinder
//    UINT boxVertexOffset = 0;
//    UINT gridVertexOffset = (UINT)box.Vertices.size();
//    UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
//    UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();
//
//    //���㵥�������������������������е�ƫ����,˳��Ϊ��box��grid��sphere��cylinder
//    UINT boxIndexOffset = 0;
//    UINT gridIndexOffset = (UINT)box.Indices32.size();
//    UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
//    UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();
//
//    //cmdList->DrawIndexedInstanced(mIndexCount,// ÿ��ʵ��Ҫ���Ƶ�������
//    //    1,// ʵ��������
//    //    0,// ��ʼ����λ��
//    //    0,// ��������ʼ������ȫ�������е�λ��
//    //    0);// ʵ�����ĸ߼�����
//    // (��DrawIndexedInstanced�����е�1��3��4����)
//    SubmeshGeometry boxSubmesh;
//    boxSubmesh.IndexCount = (UINT)box.Indices32.size();
//    boxSubmesh.BaseVertexLocation = boxVertexOffset;
//    boxSubmesh.StartIndexLocation = boxIndexOffset;
//
//    SubmeshGeometry gridSubmesh;
//    gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
//    gridSubmesh.StartIndexLocation = gridIndexOffset;
//    gridSubmesh.BaseVertexLocation = gridVertexOffset;
//
//    SubmeshGeometry sphereSubmesh;
//    sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
//    sphereSubmesh.StartIndexLocation = sphereIndexOffset;
//    sphereSubmesh.BaseVertexLocation = sphereVertexOffset;
//
//    SubmeshGeometry cylinderSubmesh;
//    cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
//    cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
//    cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;
//
//    //����һ���ܵĶ��㻺��vertices������4��������Ķ������ݴ�������
//    auto totalVertexCount = box.Vertices.size() + grid.Vertices.size() + sphere.Vertices.size() + cylinder.Vertices.size();
//
//    std::vector<Vertex> vertices(totalVertexCount);
//    UINT k = 0;
//    for (size_t i = 0; i < box.Vertices.size(); ++i, ++k)
//    {
//        vertices[k].Pos = box.Vertices[i].Position;
//        vertices[k].Color = XMFLOAT4(DirectX::Colors::DarkGreen);
//    }
//
//    for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
//    {
//        vertices[k].Pos = grid.Vertices[i].Position;
//        vertices[k].Color = XMFLOAT4(DirectX::Colors::ForestGreen);
//    }
//
//    for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
//    {
//        vertices[k].Pos = sphere.Vertices[i].Position;
//        vertices[k].Color = XMFLOAT4(DirectX::Colors::Crimson);
//    }
//
//    for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
//    {
//        vertices[k].Pos = cylinder.Vertices[i].Position;
//        vertices[k].Color = XMFLOAT4(DirectX::Colors::SteelBlue);
//    }
//    // ͬ����һ���ܵ���������indices������4����������������ݴ�������
//    std::vector<std::uint16_t> indices;
//    // ��ָ��λ��locǰ��������[start, end)������Ԫ�� 
//    indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
//    indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
//    indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
//    indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));
//
//    const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
//    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);
//
//    // ����MeshGeometryʵ��
//    geo = std::make_unique<MeshGeometry>();
//    geo->Name = "shapeGeo";
//    // �Ѷ���/���������ϴ���GPU
//    ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
//    CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);
//
//    ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
//    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);
//    // ��������Ĭ�϶� CPU->GPU
//    geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(d3dDevice.Get(), cmdList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);
//    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(d3dDevice.Get(), cmdList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);
//
//    geo->VertexByteStride = sizeof(Vertex);
//    geo->VertexBufferByteSize = vbByteSize;
//    geo->IndexFormat = DXGI_FORMAT_R16_UINT;
//    geo->IndexBufferByteSize = ibByteSize;
//
//    // ������װ�õ�4���������SubmeshGeometry����ֵ������ӳ���
//    geo->DrawArgs["box"] = boxSubmesh;
//    geo->DrawArgs["grid"] = gridSubmesh;
//    geo->DrawArgs["sphere"] = sphereSubmesh;
//    geo->DrawArgs["cylinder"] = cylinderSubmesh;
//    // unique_ptrͨ��moveת����Դ
//    mGeometries[geo->Name] = std::move(geo);
//}

void GameDevelopApp::BuildLandGeometry()
{
    //���������壬��������������б�洢��MeshData��
    ProceduralGeometry proceGeo;
    ProceduralGeometry::MeshData grid = proceGeo.CreateGrid(160.0f, 160.0f, 50, 50);

    // ����ȫ�ֶ���/��������
    std::vector<Vertex> vertices(grid.Vertices.size());
    for (size_t i = 0; i < grid.Vertices.size(); ++i)
    {
        auto& p = grid.Vertices[i].Position;
        vertices[i].Pos = p;
        vertices[i].Pos.y = GetHillsHeight(p.x, p.z);
        vertices[i].Normal = GetHillsNormal(p.x, p.z);
        vertices[i].TexC = grid.Vertices[i].TexC;

        //���ݶ��㲻ͬ��yֵ�����費ͬ�Ķ���ɫ(��ͬ���ζ�Ӧ����ɫ)
        //if (vertices[i].Pos.y < -10.0f)
        //{
        //    //ɳ̲ɫ
        //    vertices[i].Color = XMFLOAT4(1.0f, 0.96f, 0.62f, 1.0f);
        //}
        //else if (vertices[i].Pos.y < 5.0f)
        //{
        //    // Light yellow-green.
        //    vertices[i].Color = XMFLOAT4(0.48f, 0.77f, 0.46f, 1.0f);
        //}
        //else if (vertices[i].Pos.y < 12.0f)
        //{
        //    // Dark yellow-green.
        //    vertices[i].Color = XMFLOAT4(0.1f, 0.48f, 0.19f, 1.0f);
        //}
        //else if (vertices[i].Pos.y < 20.0f)
        //{
        //    // Dark brown.
        //    vertices[i].Color = XMFLOAT4(0.45f, 0.39f, 0.34f, 1.0f);
        //}
        //else
        //{
        //    // White snow.
        //    vertices[i].Color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
        //}
    }

    const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

    std::vector<std::uint16_t> indices = grid.GetIndices16();
    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

    geo = std::make_unique<MeshGeometry>();
    geo->Name = "landGeo";
    // ���ݴ��ڴ濽����CPU
    ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
    CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

    // ��CPU�ϴ���GPU
    geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(d3dDevice.Get(), 
        cmdList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);
    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(d3dDevice.Get(),
        cmdList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

    geo->VertexByteStride = sizeof(Vertex);
    geo->VertexBufferByteSize = vbByteSize;
    geo->IndexFormat = DXGI_FORMAT_R16_UINT;
    geo->IndexBufferByteSize = ibByteSize;

    //��װgrid�Ķ��㡢��������
    SubmeshGeometry submesh;
    submesh.IndexCount = (UINT)indices.size();
    submesh.StartIndexLocation = 0;
    submesh.BaseVertexLocation = 0;

    geo->DrawArgs["grid"] = submesh;

    mGeometries["landGeo"] = std::move(geo);
}

void GameDevelopApp::BuildSkullGeometry()
{
    std::ifstream fin("Models/skull.txt");//��ȡ���������ļ�

    if (!fin)
    {
        MessageBox(0, L"Models/skull.txt not found.", 0, 0);
        return;
    }

    UINT vcount = 0;
    UINT tcount = 0;
    std::string ignore;

    fin >> ignore >> vcount;//��ȡvertexCount����ֵ
    fin >> ignore >> tcount;//��ȡtriangleCount����ֵ
    fin >> ignore >> ignore >> ignore >> ignore;//���в���

    std::vector<Vertex> vertices(vcount);
    //�����б�ֵ
    for (UINT i = 0; i < vcount; ++i)
    {
        fin >> vertices[i].Pos.x >> vertices[i].Pos.y >> vertices[i].Pos.z;
        fin >> vertices[i].Normal.x >> vertices[i].Normal.y >> vertices[i].Normal.z;
        vertices[i].TexC = { 0.0f, 0.0f };
    }

    fin >> ignore;
    fin >> ignore;
    fin >> ignore;

    std::vector<std::int32_t> indices(3 * tcount);
    //�����б�ֵ
    for (UINT i = 0; i < tcount; ++i)
    {
        fin >> indices[i * 3 + 0] >> indices[i * 3 + 1] >> indices[i * 3 + 2];
    }

    fin.close();

    const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::int32_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "skullGeo";

    //��������������ݸ��Ƶ�CPUϵͳ�ڴ���
    ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
    CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);
    //��������������ݴ�CPU�ڴ渴�Ƶ�GPU������
    geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(d3dDevice.Get(),
        cmdList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(d3dDevice.Get(),
        cmdList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

    geo->VertexByteStride = sizeof(Vertex);
    geo->VertexBufferByteSize = vbByteSize;
    geo->IndexFormat = DXGI_FORMAT_R32_UINT;
    geo->IndexBufferByteSize = ibByteSize;

    SubmeshGeometry submesh;
    submesh.IndexCount = (UINT)indices.size();
    submesh.StartIndexLocation = 0;
    submesh.BaseVertexLocation = 0;

    geo->DrawArgs["skull"] = submesh;

    mGeometries[geo->Name] = std::move(geo);
}

void GameDevelopApp::BuildWavesGeometryBuffers()
{
    //��ʼ�������б�ÿ��������3��������
    std::vector<std::uint16_t> indices(3 * mWaves->TriangleCount());
    assert(mWaves->VertexCount() < 0x0000ffff);//��������������65536����ֹ����

    //��������б�
    int m = mWaves->RowCount();
    int n = mWaves->ColumnCount();
    int k = 0;
    for (int i = 0; i < m - 1; ++i)
    {
        for (int j = 0; j < n - 1; ++j)
        {
            indices[k] = i * n + j;
            indices[k + 1] = i * n + j + 1;
            indices[k + 2] = (i + 1) * n + j;

            indices[k + 3] = (i + 1) * n + j;
            indices[k + 4] = i * n + j + 1;
            indices[k + 5] = (i + 1) * n + j + 1;

            k += 6;//��һ���ı���
        }
    }

    UINT vbByteSize = mWaves->VertexCount() * sizeof(Vertex);
    UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "waterGeo";
    //��̬����
    geo->VertexBufferCPU = nullptr;
    geo->VertexBufferGPU = nullptr;

    // ���ڲ���ֻ�Ƕ�������ĸı䣬������ı䶥��������������������滹�Ǿ�̬��
    ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(d3dDevice.Get(),
        cmdList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

    geo->VertexByteStride = sizeof(Vertex);
    geo->VertexBufferByteSize = vbByteSize;
    geo->IndexFormat = DXGI_FORMAT_R16_UINT;
    geo->IndexBufferByteSize = ibByteSize;

    SubmeshGeometry submesh;
    submesh.IndexCount = (UINT)indices.size();
    submesh.StartIndexLocation = 0;
    submesh.BaseVertexLocation = 0;

    geo->DrawArgs["grid"] = submesh;

    mGeometries["waterGeo"] = std::move(geo);
}

//��ͬRTV��DSV������������Ҳ��Ҫ��������ָ�������������ԡ�����������Ҫ������������š�
//���ԣ��ȴ���CBV�ѡ�
void GameDevelopApp::BuildDescriptorHeaps()
{
    //����SRV��
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = 4;//�����µ������ø�
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    srvHeapDesc.NodeMask = 0;
    ThrowIfFailed(d3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

    // ��֮ǰ����õ�������Դת����comptr������
    auto bricksTex = mTextures["bricksTex"]->Resource;
    auto checkboardTex = mTextures["checkboardTex"]->Resource;
    auto iceTex = mTextures["iceTex"]->Resource;
    auto white1x1Tex = mTextures["white1x1Tex"]->Resource;

    //SRV������SRV���ݵص�SRV���ľ��
    CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

    //SRV�����ṹ��
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;//���������˳�򲻸ı�
    srvDesc.Format = bricksTex->GetDesc().Format;//��ͼ��Ĭ�ϸ�ʽ
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;//2D��ͼ
    srvDesc.Texture2D.MostDetailedMip = 0;//ϸ�����꾡��mipmap�㼶Ϊ0
    srvDesc.Texture2D.MipLevels = bricksTex->GetDesc().MipLevels;//mipmap�㼶����
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;//�ɷ��ʵ�mipmap��С�㼶��Ϊ0
    //�������ݵء���SRV
    d3dDevice->CreateShaderResourceView(bricksTex.Get(), &srvDesc, hDescriptor);

    //SRV������SRV����ˮ��SRV���ľ��,����ƫ��һ��SRV
    hDescriptor.Offset(1, mCbvSrvDescriptorSize);
    //SRV�����ṹ���޸�
    srvDesc.Format = checkboardTex->GetDesc().Format;//��ͼ��Ĭ�ϸ�ʽ
    srvDesc.Texture2D.MipLevels = checkboardTex->GetDesc().MipLevels;//mipmap�㼶����
    // ������ˮSRV
    d3dDevice->CreateShaderResourceView(checkboardTex.Get(), &srvDesc, hDescriptor);

    //SRV������SRV���������SRV���ľ��,����ƫ��һ��SRV
    hDescriptor.Offset(1, mCbvSrvDescriptorSize);
    //SRV�����ṹ���޸�
    srvDesc.Format = iceTex->GetDesc().Format;//��ͼ��Ĭ�ϸ�ʽ
    srvDesc.Texture2D.MipLevels = iceTex->GetDesc().MipLevels;//mipmap�㼶����
    //�����������䡱��SRV
    d3dDevice->CreateShaderResourceView(iceTex.Get(), &srvDesc, hDescriptor);

    //SRV������SRV���������SRV���ľ��,����ƫ��һ��SRV
    hDescriptor.Offset(1, mCbvSrvDescriptorSize);
    //SRV�����ṹ���޸�
    srvDesc.Format = white1x1Tex->GetDesc().Format;//��ͼ��Ĭ�ϸ�ʽ
    srvDesc.Texture2D.MipLevels = white1x1Tex->GetDesc().MipLevels;//mipmap�㼶����
    //�����������䡱��SRV
    d3dDevice->CreateShaderResourceView(white1x1Tex.Get(), &srvDesc, hDescriptor);
}

// ��������������
//void GameDevelopApp::BuildConstantBuffers()
//{
//    UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
//
//    UINT objCount = (UINT)mOpaqueRitems.size();
//
//    //����֡��Դ���أ�������2��ѭ�������ѭ������֡��Դ���ڲ�ѭ��������Ⱦ��
//    //��ʱ�Ķ��е�ַҪ���ǵڼ���֡��ԴԪ�أ�����heapIndex��ֵ��objectCount * frameIndex + i��
//    for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex)
//    {
//        auto objectCB = mFrameResources[frameIndex]->ObjectCB->Resource();
//        for (UINT i = 0; i < objCount; ++i)
//        {
//            //��ó����������׵�ַ
//            D3D12_GPU_VIRTUAL_ADDRESS cbAddress = objectCB->GetGPUVirtualAddress();
//
//            // ����������cbv��ַ����ƫ��
//            // �������ڳ����������еĵ�ַ
//            cbAddress += i * objCBByteSize;
//
//            //CBV���е�CBVԪ������
//            int heapIndex = frameIndex * objCount + i;
//            auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCbvHeap->GetCPUDescriptorHandleForHeapStart());//���CBV���׵�ַ
//            handle.Offset(heapIndex, cbv_srv_uavDescriptorSize);//CBV�����CBV���е�CBVԪ�ص�ַ��
//
//            // ����cbv
//            D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
//            cbvDesc.BufferLocation = cbAddress;
//            cbvDesc.SizeInBytes = objCBByteSize;
//
//            // ����cbv
//            d3dDevice->CreateConstantBufferView(&cbvDesc, handle);
//        }
//    }
//
//    // ����perPassCBV
//    UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));
//
//    //��֡��Դ���أ���������Ҫ��һ��ѭ����������3��passCBV
//    for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex)
//    {
//        auto passCB = mFrameResources[frameIndex]->PassCB->Resource();
//        D3D12_GPU_VIRTUAL_ADDRESS cbAddress = passCB->GetGPUVirtualAddress();
//
//        int heapIndex = mPassCbvOffset + frameIndex;
//        auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCbvHeap->GetCPUDescriptorHandleForHeapStart());//���CBV���׵�ַ
//        handle.Offset(heapIndex, cbv_srv_uavDescriptorSize);//CBV�����CBV���е�CBVԪ�ص�ַ��
//
//        D3D12_CONSTANT_BUFFER_VIEW_DESC passCbvDesc;
//        passCbvDesc.BufferLocation = cbAddress;
//        passCbvDesc.SizeInBytes = passCBByteSize;
//
//        d3dDevice->CreateConstantBufferView(&passCbvDesc, handle);
//    }
//
//}

void GameDevelopApp::LoadTextures()
{
    /*שǽ����*/
    auto bricksTex = std::make_unique<Texture>();
    bricksTex->Name = "bricksTex";
    bricksTex->Filename = L"Textures\\bricks3.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(d3dDevice.Get(),
        cmdList.Get(), bricksTex->Filename.c_str(),
        bricksTex->Resource, bricksTex->UploadHeap));
    /*��ש����*/
    auto checkboardTex = std::make_unique<Texture>();
    checkboardTex->Name = "checkboardTex";
    checkboardTex->Filename = L"Textures\\checkboard.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(d3dDevice.Get(),
        cmdList.Get(), checkboardTex->Filename.c_str(),
        checkboardTex->Resource, checkboardTex->UploadHeap));
    /*��������*/
    auto iceTex = std::make_unique<Texture>();
    iceTex->Name = "iceTex";
    iceTex->Filename = L"Textures\\ice.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(d3dDevice.Get(),
        cmdList.Get(), iceTex->Filename.c_str(),
        iceTex->Resource, iceTex->UploadHeap));
    /*��������*/
    auto white1x1Tex = std::make_unique<Texture>();
    white1x1Tex->Name = "white1x1Tex";
    white1x1Tex->Filename = L"Textures\\white1x1.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(d3dDevice.Get(),
        cmdList.Get(), white1x1Tex->Filename.c_str(),
        white1x1Tex->Resource, white1x1Tex->UploadHeap));

    mTextures[bricksTex->Name] = std::move(bricksTex);
    mTextures[checkboardTex->Name] = std::move(checkboardTex);
    mTextures[iceTex->Name] = std::move(iceTex);
    mTextures[white1x1Tex->Name] = std::move(white1x1Tex);
}

//��ǩ�������������Ⱦ���߻���˵Shader������ִ�д�����Ҫ�ĸ�����Դ��ʲô���ķ�ʽ�����Լ�������ڴ桢�Դ��в���
//��ǩ����ʵ���ǽ���ɫ����Ҫ�õ������ݰ󶨵���Ӧ�ļĴ������ϣ�����ɫ�����ʡ�
// ��Ҫָ������GPU�϶�Ӧ�ļĴ���
//��ǩ����Ϊ������������������������������ʹ��������������ʼ����ǩ����

//��ǩ�������ǽ��������ݰ����Ĵ����ۣ�����ɫ��������ʡ�
//��Ϊ����������2���������ݽṹ�壬����Ҫ����2��Ԫ�صĸ���������2��CBV������2���Ĵ����ۡ�
void GameDevelopApp::BuildRootSignature()
{
    //�����ɵ���CBV����ɵ���������
    CD3DX12_DESCRIPTOR_RANGE texTable;
    texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,// ����������
        1,// ����������
        0);// ���������󶨵ļĴ����ۺ�

    // ������������������������������������
    CD3DX12_ROOT_PARAMETER slotRootParameter[4];
   
    slotRootParameter[0].InitAsDescriptorTable(1, // Range����
        &texTable,// Rangeָ��
        D3D12_SHADER_VISIBILITY_PIXEL);// ����Դֻ��������ɫ���ɶ�
    // ������������ �ô�������Ҫ����CBV�ѣ�����ֱ�����ø�����������ָʾҪ�󶨵���Դ
    // 0 1 2��ʾ�Ĵ����ۺ�
    // ������ʾ������Ƶ�����Ƶ������
    slotRootParameter[1].InitAsConstantBufferView(0);// ObjectConstants
    slotRootParameter[2].InitAsConstantBufferView(1);// PassConstants
    // ���˲��ʳ�������
    slotRootParameter[3].InitAsConstantBufferView(2);// MaterialConstants

    //�ڸ�ǩ�������У����뾲̬�����������������ǩ���󶨣�Ϊ���մ�����ɫ����׼��
    auto staticSamplers = GetStaticSamplers();

    ////�����ɵ���CBV����ɵ���������
    //CD3DX12_DESCRIPTOR_RANGE objCbvTable;
    ////������opengl�еĲ��ֲַ�
    //objCbvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV,// ����������
    //    1,// ����������
    //    0);// ���������󶨵ļĴ����ۺ�
    //slotRootParameter[0].InitAsDescriptorTable(1, &objCbvTable);

    //CD3DX12_DESCRIPTOR_RANGE passCbvTable;
    //passCbvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1);
    //slotRootParameter[1].InitAsDescriptorTable(1, &passCbvTable);

    // ��ǩ����һϵ�и��������
    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4,// ������������
        slotRootParameter,// ������ָ��
        (UINT)staticSamplers.size(),// ��̬��������
        staticSamplers.data(),// ��̬������
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);// ѡ���������� ��Ҫһ�鶥�㻺�����󶨵����벼��
    //�õ����Ĵ�����������һ����ǩ�����ò�λָ��һ�������е�������������������������
    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc,// ����������ָ��
        D3D_ROOT_SIGNATURE_VERSION_1,// ������version
        serializedRootSig.GetAddressOf(),// ���л��ڴ��
        errorBlob.GetAddressOf());// ���л�������Ϣ
    if (errorBlob != nullptr) {
        OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    }
    ThrowIfFailed(hr);
    // ������ǩ�������л��ڴ��
    ThrowIfFailed(d3dDevice->CreateRootSignature(
        0,// ����������
        serializedRootSig->GetBufferPointer(),// ��ǩ���󶨵����л��ڴ�ָ��
        serializedRootSig->GetBufferSize(),// ��ǩ���󶨵����л��ڴ�byte
        IID_PPV_ARGS(&mRootSignature)));// ��ǩ��COM ID
}

void GameDevelopApp::BuildShadersAndInputLayout()
{
    // ���������г��������õ�alphaTest������Ҫ������Ϊ��ѡ���Ҫʹ�ã������ڱ�����ɫ��ʱ����ALPHA_TEST��
    // LPCSTR Name;
    // LPCSTR Definition;
    // ��ɫ����
    const D3D_SHADER_MACRO defines[] =
    {
        "FOG", "1",
        NULL, NULL
    };
    // D3D_SHADER_MACRO�ṹ�������ֺͶ���
    const D3D_SHADER_MACRO alphaTestDefines[] =
    {
        "ALPHA_TEST", "1",
        "FOG", "1",
        NULL, NULL
    };

    const D3D_SHADER_MACRO unfogdefines[] =
    {
        NULL, NULL
    };
    // D3D_SHADER_MACRO�ṹ�������ֺͶ���
    const D3D_SHADER_MACRO unfogalphaTestDefines[] =
    {
        "ALPHA_TEST", "1",
        NULL, NULL
    };

    // ��shader�����ֳ���3�࣬һ���Ƕ��㺯����standardVS����һ���ǲ�͸����ƬԪ������opaquePS����
    // ����һ����alphaTest��ƬԪ������alphaTestPS��
    mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\color.hlsl", nullptr, "VS", "vs_5_0");
    mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\color.hlsl", defines, "PS", "ps_5_0");
    mShaders["alphaTestedPS"] = d3dUtil::CompileShader(L"Shaders\\color.hlsl", alphaTestDefines, "PS", "ps_5_0");

    mShaders["unfogopaquePS"] = d3dUtil::CompileShader(L"Shaders\\color.hlsl", unfogdefines, "PS", "ps_5_0");
    mShaders["unfogalphaTestedPS"] = d3dUtil::CompileShader(L"Shaders\\color.hlsl", unfogalphaTestDefines, "PS", "ps_5_0");

    //����ɿ���ֲ���ֽ���
    /*mVsByteCode = d3dUtil::CompileShader(L"Shaders\\color.hlsl", nullptr, "VS", "vs_5_0");
    mPsByteCode = d3dUtil::CompileShader(L"Shaders\\color.hlsl", nullptr, "PS", "ps_5_0");*/

    mInputLayout =
    {
        //0��ʾ�Ĵ����ۺ�Ϊ0
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        // �������������ɫ���ȣ���128λ���ٵ�32λ
        // { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        // ��ΪDXGI_FORMAT�������е�ֵ���ڴ�������С���ֽ�������ʾ�ģ����Դ������Ч�ֽ�д�������Ч�ֽڣ���ʽARGB����ʾΪBGRA��
        //{ "COLOR", 0, DXGI_FORMAT_B8G8R8A8_UNORM, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };
}

void GameDevelopApp::BuildRoomGeometry()
{
    //���㻺��
    std::array<Vertex, 40> vertices =
    {
        //�ذ�ģ�͵Ķ���Pos��Normal��TexCoord
        Vertex(-3.5f, 0.0f, -10.0f, 0.0f, 1.0f, 0.0f, 0.0f, 4.0f), // 0 
        Vertex(-3.5f, 0.0f,   0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f),
        Vertex(7.5f, 0.0f,   0.0f, 0.0f, 1.0f, 0.0f, 4.0f, 0.0f),
        Vertex(7.5f, 0.0f, -10.0f, 0.0f, 1.0f, 0.0f, 4.0f, 4.0f),

        //ǽ��ģ�͵Ķ���Pos��Normal��TexCoord
        Vertex(-3.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 2.0f), // 4
        Vertex(-3.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
        Vertex(-2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.5f, 0.0f),
        Vertex(-2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.5f, 2.0f),

        Vertex(2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 2.0f), // 8 
        Vertex(2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
        Vertex(7.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 2.0f, 0.0f),
        Vertex(7.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 2.0f, 2.0f),

        Vertex(-3.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f), // 12
        Vertex(-3.5f, 6.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
        Vertex(7.5f, 6.0f, 0.0f, 0.0f, 0.0f, -1.0f, 6.0f, 0.0f),
        Vertex(7.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 6.0f, 1.0f),

        Vertex(-3.5f, 6.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f), // 16
        Vertex(-3.5f, 6.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f),
        Vertex(7.5f, 6.0f, 1.0f, 0.0f, 1.0f, 0.0f, 4.0f, 0.0f),
        Vertex(7.5f, 6.0f, 0.0f, 0.0f, 1.0f, 0.0f, 4.0f, 1.0f),

        Vertex(-3.5f, 6.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 4.0f), // 20
        Vertex(7.5f, 6.0f, 1.0f, 0.0f, 0.0f, 1.0f, 4.0f, 4.0f),
        Vertex(7.5f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 4.0f, 0.0f),
        Vertex(-3.5f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f),

        Vertex(-3.5f, 6.0f, 0.0f, -1.0f, 0.0f, 0.0f, 4.0f, 1.0f), // 24
        Vertex(-3.5f, 6.0f, 1.0f, -1.0f, 0.0f, 0.0f, 4.0f, 0.0f),
        Vertex(-3.5f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f),
        Vertex(-3.5f, 0.0f, 1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f),

        Vertex(7.5f, 6.0f, 0.0f, 1.0f, 0.0f, 0.0f, 4.0f, 1.0f), // 28
        Vertex(7.5f, 6.0f, 1.0f, 1.0f, 0.0f, 0.0f, 4.0f, 0.0f),
        Vertex(7.5f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f),
        Vertex(7.5f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f),

        Vertex(-3.5f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f), // 32
        Vertex(-3.5f, 0.0f, 1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f),
        Vertex(7.5f, 0.0f, 1.0f, 0.0f, -1.0f, 0.0f, 4.0f, 0.0f),
        Vertex(7.5f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 4.0f, 1.0f),

        //����ģ�͵Ķ���Pos��Normal��TexCoord
        Vertex(-2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f), // 36
        Vertex(-2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
        Vertex(2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f),
        Vertex(2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f)
    };

    //��������
    std::array<std::int16_t, 60> indices =
    {
        // Floor
        0, 1, 2,
        0, 2, 3,

        // Walls
        4, 5, 6,
        4, 6, 7,

        8, 9, 10,
        8, 10, 11,

        12, 13, 14,
        12, 14, 15,

        16, 17, 18,
        16, 18, 19,

        20, 23, 22,
        20, 22, 21,

        25, 24, 26,
        25, 26, 27,

        28, 29, 30,
        29, 31, 30,

        32, 34, 33,
        32, 35, 34,

        // Mirror
        36, 37, 38,
        36, 38, 39
    };

    //��������Ļ������������ϲ������������������
    SubmeshGeometry floorSubmesh;
    floorSubmesh.IndexCount = 6;
    floorSubmesh.StartIndexLocation = 0;
    floorSubmesh.BaseVertexLocation = 0;

    SubmeshGeometry wallSubmesh;
    wallSubmesh.IndexCount = 48;
    wallSubmesh.StartIndexLocation = 6;
    wallSubmesh.BaseVertexLocation = 0;

    SubmeshGeometry mirrorSubmesh;
    mirrorSubmesh.IndexCount = 6;
    mirrorSubmesh.StartIndexLocation = 54;
    mirrorSubmesh.BaseVertexLocation = 0;

    const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "roomGeo";

    ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
    CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

    geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(d3dDevice.Get(),
        cmdList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(d3dDevice.Get(),
        cmdList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

    geo->VertexByteStride = sizeof(Vertex);
    geo->VertexBufferByteSize = vbByteSize;
    geo->IndexFormat = DXGI_FORMAT_R16_UINT;
    geo->IndexBufferByteSize = ibByteSize;

    geo->DrawArgs["floor"] = floorSubmesh;
    geo->DrawArgs["wall"] = wallSubmesh;
    geo->DrawArgs["mirror"] = mirrorSubmesh;

    mGeometries[geo->Name] = std::move(geo);
}

//����ɫ�����õ�������Դ��״̬���󶨵���ˮ����
void GameDevelopApp::BuildPSO()
{
    //��͸�������PSO������Ҫ��ϣ�
    D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc = {};
    //����ڴ�
    ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
    opaquePsoDesc.InputLayout = { mInputLayout.data(),(UINT)mInputLayout.size() };// ���벼������  
    opaquePsoDesc.pRootSignature = mRootSignature.Get();// ���PSO�󶨵ĸ�ǩ��ָ��

    opaquePsoDesc.VS =
    {
        reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()),
        mShaders["standardVS"]->GetBufferSize()
    };// ���󶨵Ķ�����ɫ��
    opaquePsoDesc.PS =
    {
        reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
        mShaders["opaquePS"]->GetBufferSize()
    };// ���󶨵�������ɫ��

    opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);// ��դ��״̬
    opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);// ���״̬
    opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);// ���/ģ�����״̬
    opaquePsoDesc.SampleMask = UINT_MAX;// ÿ��������Ĳ������
    opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;// ͼԪ��������
    opaquePsoDesc.NumRenderTargets = 1;// ��ȾĿ������
    opaquePsoDesc.RTVFormats[0] = mBackBufferFormat;// ��ȾĿ��ĸ�ʽ
    opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;// ���ز�������
    opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;// ���ز�������
    opaquePsoDesc.DSVFormat = mDepthStencilFormat;// ���/ģ�建������ʽ
    ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));

    // ������Ч
    D3D12_GRAPHICS_PIPELINE_STATE_DESC unfogopaquePsoDesc = opaquePsoDesc;
    unfogopaquePsoDesc.PS =
    {
        reinterpret_cast<BYTE*>(mShaders["unfogopaquePS"]->GetBufferPointer()),
        mShaders["unfogopaquePS"]->GetBufferSize()
    };// ���󶨵�������ɫ��
    ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&unfogopaquePsoDesc, IID_PPV_ARGS(&mPSOs["unfogopaque"])));

    //��������PSO����Ҫ��ϣ�
    D3D12_GRAPHICS_PIPELINE_STATE_DESC transparentPsoDesc = opaquePsoDesc;

    D3D12_RENDER_TARGET_BLEND_DESC transparencyBlendDesc;
    transparencyBlendDesc.BlendEnable = true;//�Ƿ��������ϣ�Ĭ��ֵΪfalse��
    transparencyBlendDesc.LogicOpEnable = false;//�Ƿ����߼����(Ĭ��ֵΪfalse)
    transparencyBlendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;//RGB����е�Դ�������Fsrc������ȡԴ��ɫ��alphaͨ��ֵ��
    transparencyBlendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;//RGB����е�Ŀ��������Fdest������ȡ1-alpha��
    transparencyBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;//RGB��������(����ѡ��ӷ�)
    transparencyBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;//alpha����е�Դ�������Fsrc��ȡ1��
    transparencyBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;//alpha����е�Ŀ��������Fsrc��ȡ0��
    transparencyBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;//alpha��������(����ѡ��ӷ�)
    transparencyBlendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;//�߼���������(�ղ���������ʹ��)
    transparencyBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;//��̨������д�����֣�û�����֣���ȫ��д�룩

    transparentPsoDesc.BlendState.RenderTarget[0] = transparencyBlendDesc;//��ֵRenderTarget��һ��Ԫ�أ�����ÿһ����ȾĿ��ִ����ͬ����
    ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&transparentPsoDesc, IID_PPV_ARGS(&mPSOs["transparent"])));

    // ������Ч
    D3D12_GRAPHICS_PIPELINE_STATE_DESC unfogtransparentPsoDesc = transparentPsoDesc;
    unfogtransparentPsoDesc.PS =
    {
        reinterpret_cast<BYTE*>(mShaders["unfogopaquePS"]->GetBufferPointer()),
        mShaders["unfogopaquePS"]->GetBufferSize()
    };// ���󶨵�������ɫ��

    unfogtransparentPsoDesc.BlendState.RenderTarget[0] = transparencyBlendDesc;
    ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&unfogtransparentPsoDesc, IID_PPV_ARGS(&mPSOs["unfogtransparent"])));

    //AlphaTest�����PSO������Ҫ��ϣ�
    D3D12_GRAPHICS_PIPELINE_STATE_DESC alphaTestedPsoDesc = opaquePsoDesc;//ʹ�ò�͸�������PSO��ʼ��
    alphaTestedPsoDesc.PS =
    {
        reinterpret_cast<BYTE*>(mShaders["alphaTestedPS"]->GetBufferPointer()),
        mShaders["alphaTestedPS"]->GetBufferSize()
    };

    alphaTestedPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;//˫����ʾ
    ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&alphaTestedPsoDesc, IID_PPV_ARGS(&mPSOs["alphaTested"])));

    // ������Ч
    D3D12_GRAPHICS_PIPELINE_STATE_DESC unfogalphaTestedPsoDesc = alphaTestedPsoDesc;
    unfogalphaTestedPsoDesc.PS =
    {
        reinterpret_cast<BYTE*>(mShaders["unfogalphaTestedPS"]->GetBufferPointer()),
        mShaders["unfogalphaTestedPS"]->GetBufferSize()
    };

    unfogalphaTestedPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;//˫����ʾ
    ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&unfogalphaTestedPsoDesc, IID_PPV_ARGS(&mPSOs["unfogalphaTested"])));

    ///PSO for opaque wireframe objects.
    D3D12_GRAPHICS_PIPELINE_STATE_DESC WireframePsoDesc = opaquePsoDesc;
    WireframePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&WireframePsoDesc, IID_PPV_ARGS(&mPSOs["wireframe"])));

    // PSO���ڱ��ģ�巴�侵��
    CD3DX12_BLEND_DESC  mirrorBlendState(D3D12_DEFAULT);
    mirrorBlendState.RenderTarget[0].RenderTargetWriteMask = 0;//��ֹ��ɫ����д��

    D3D12_DEPTH_STENCIL_DESC mirrorDSS;//���ģ�����
    mirrorDSS.DepthEnable = true;//������Ȳ���
    mirrorDSS.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;//��ֹ���д��
    mirrorDSS.DepthFunc = D3D12_COMPARISON_FUNC_LESS;//�ȽϺ�����С�ڡ�
    mirrorDSS.StencilEnable = true;//����ģ�����
    mirrorDSS.StencilReadMask = 0xff;//Ĭ��255��������ģ��ֵ
    mirrorDSS.StencilWriteMask = 0xff;//Ĭ��255��������ģ��ֵ

    mirrorDSS.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;//ģ�����ʧ�ܣ�����ԭģ��ֵ
    mirrorDSS.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;//��Ȳ���ʧ�ܣ�����ԭģ��ֵ
    mirrorDSS.FrontFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;//���ģ�����ͨ�����滻Refģ��ֵ
    mirrorDSS.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;//�ȽϺ�������Զͨ�����ԡ�

    // ���治��Ⱦ�����д
    mirrorDSS.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    mirrorDSS.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    mirrorDSS.BackFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
    mirrorDSS.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC markMirrorsPsoDesc = opaquePsoDesc;
    markMirrorsPsoDesc.BlendState = mirrorBlendState;
    markMirrorsPsoDesc.DepthStencilState = mirrorDSS;
    ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&markMirrorsPsoDesc, IID_PPV_ARGS(&mPSOs["markStencilMirrors"])));

    // ���þ������õ�PSO
    D3D12_DEPTH_STENCIL_DESC reflectionsDSS;
    reflectionsDSS.DepthEnable = true;
    reflectionsDSS.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    reflectionsDSS.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    reflectionsDSS.StencilEnable = true;
    reflectionsDSS.StencilReadMask = 0xff;
    reflectionsDSS.StencilWriteMask = 0xff;

    reflectionsDSS.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    reflectionsDSS.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    reflectionsDSS.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;//���ģ�����ͨ��������ԭģ��ֵģ��ֵ
    reflectionsDSS.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;//�ȽϺ��������ڡ�
    // ���治��Ⱦ�����д
    reflectionsDSS.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    reflectionsDSS.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    reflectionsDSS.BackFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    reflectionsDSS.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC drawReflectionsPsoDesc = opaquePsoDesc;
    drawReflectionsPsoDesc.DepthStencilState = reflectionsDSS;
    drawReflectionsPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;// �����޳�
    // ��֪Direct3D����ʱ������������ο��������� ���򣬶���˳ʱ������������ο������泯��
    // ��ʵ���� �ǶԷ��ߵķ���Ҳ������ �����䡱���Դ�ʹ�����Ϊ������
    drawReflectionsPsoDesc.RasterizerState.FrontCounterClockwise = true;//����ת����
    ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&drawReflectionsPsoDesc, IID_PPV_ARGS(&mPSOs["drawStencilReflections"])));

    // ����������Ӱ��PSO
    D3D12_DEPTH_STENCIL_DESC shadowDSS;
    shadowDSS.DepthEnable = true;
    shadowDSS.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    shadowDSS.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    shadowDSS.StencilEnable = true;
    shadowDSS.StencilReadMask = 0xff;
    shadowDSS.StencilWriteMask = 0xff;

    shadowDSS.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;//ģ�����ʧ�ܣ�����ԭģ��ֵ
    shadowDSS.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;//��Ȳ���ʧ�ܣ�����ԭģ��ֵ
    // ������ƬԪ���ػ�ϵ�bug
    shadowDSS.FrontFace.StencilPassOp = D3D12_STENCIL_OP_INCR;//���ģ�����ͨ����ģ��ֵ����
    shadowDSS.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;//�ȽϺ��������ڡ�
    // ���治��Ⱦ�����д
    shadowDSS.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    shadowDSS.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    shadowDSS.BackFace.StencilPassOp = D3D12_STENCIL_OP_INCR;
    shadowDSS.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC shadowPsoDesc = transparentPsoDesc;
    shadowPsoDesc.DepthStencilState = shadowDSS;
    ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&shadowPsoDesc, IID_PPV_ARGS(&mPSOs["shadow"])));
}

void GameDevelopApp::BuildFrameResources()
{
    for (int i = 0; i < gNumFrameResources; ++i)
    {
        // ������BuildFrameResources�����У���ȷ��д��passCB������Ϊ2
        mFrameResources.push_back(std::make_unique<FrameResource>(d3dDevice.Get(), 2, (UINT)mAllRitems.size(),(UINT)mMaterials.size()));
    }
}

void GameDevelopApp::BuildMaterials()
{
    //����ذ�Ĳ���
    auto bricks = std::make_unique<Material>();
    bricks->Name = "bricks";
    bricks->MatCBIndex = 0;
    bricks->DiffuseSrvHeapIndex = 0;
    bricks->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    bricks->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
    bricks->Roughness = 0.25f;

    //����שǽ�Ĳ���
    auto checkertile = std::make_unique<Material>();
    checkertile->Name = "checkertile";
    checkertile->MatCBIndex = 1;
    checkertile->DiffuseSrvHeapIndex = 1;
    checkertile->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    checkertile->FresnelR0 = XMFLOAT3(0.07f, 0.07f, 0.07f);
    checkertile->Roughness = 0.3f;

    //���徵��Ĳ���
    auto icemirror = std::make_unique<Material>();
    icemirror->Name = "icemirror";
    icemirror->MatCBIndex = 2;
    icemirror->DiffuseSrvHeapIndex = 2;
    icemirror->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.5f);// ����aͨ��0.5
    icemirror->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
    icemirror->Roughness = 0.5f;

    //�����ͷ�Ĳ���
    auto skullMat = std::make_unique<Material>();
    skullMat->Name = "skullMat";
    skullMat->MatCBIndex = 3;
    skullMat->DiffuseSrvHeapIndex = 3;
    skullMat->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    skullMat->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
    skullMat->Roughness = 0.3f;
    
    //������Ӱ�Ĳ���
    auto shadowMat = std::make_unique<Material>();
    shadowMat->Name = "shadowMat";
    shadowMat->MatCBIndex = 4;
    shadowMat->DiffuseSrvHeapIndex = 3;//��ͼ���������õİ�ɫ��ͼ
    shadowMat->DiffuseAlbedo = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.5f);// �����ʸ���ɫ ������ɫΪ��ɫ
    shadowMat->FresnelR0 = XMFLOAT3(0.001f, 0.001f, 0.001f);
    shadowMat->Roughness = 0.0f;

    mMaterials["bricks"] = std::move(bricks);
    mMaterials["checkertile"] = std::move(checkertile);
    mMaterials["icemirror"] = std::move(icemirror);
    mMaterials["skullMat"] = std::move(skullMat);
    mMaterials["shadowMat"] = std::move(shadowMat);
}

void GameDevelopApp::BuildRenderItems()
{
    // ǽ����Ⱦ��
    auto wallsRitem = std::make_unique<RenderItem>();
    wallsRitem->World = MathHelper::Identity4x4();
    wallsRitem->TexTransform = MathHelper::Identity4x4();
    wallsRitem->ObjCBIndex = 0;
    wallsRitem->Mat = mMaterials["bricks"].get();
    wallsRitem->Geo = mGeometries["roomGeo"].get();
    wallsRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    wallsRitem->IndexCount = wallsRitem->Geo->DrawArgs["wall"].IndexCount;
    wallsRitem->StartIndexLocation = wallsRitem->Geo->DrawArgs["wall"].StartIndexLocation;
    wallsRitem->BaseVertexLocation = wallsRitem->Geo->DrawArgs["wall"].BaseVertexLocation;
    mRitemLayer[(int)RenderLayer::Opaque].push_back(wallsRitem.get());

    // �ذ���Ⱦ��
    auto floorRitem = std::make_unique<RenderItem>();
    floorRitem->World = MathHelper::Identity4x4();
    XMStoreFloat4x4(&floorRitem->TexTransform, DirectX::XMMatrixScaling(1.6f, 1.6f, 1.0f));
    floorRitem->ObjCBIndex = 1;
    floorRitem->Mat = mMaterials["checkertile"].get();
    floorRitem->Geo = mGeometries["roomGeo"].get();
    floorRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    floorRitem->IndexCount = floorRitem->Geo->DrawArgs["floor"].IndexCount;
    floorRitem->StartIndexLocation = floorRitem->Geo->DrawArgs["floor"].StartIndexLocation;
    floorRitem->BaseVertexLocation = floorRitem->Geo->DrawArgs["floor"].BaseVertexLocation;
    mRitemLayer[(int)RenderLayer::Opaque].push_back(floorRitem.get());

    // ������Ⱦ��
    auto mirrorRitem = std::make_unique<RenderItem>();
    mirrorRitem->World = MathHelper::Identity4x4();
    mirrorRitem->TexTransform = MathHelper::Identity4x4();
    mirrorRitem->ObjCBIndex = 2;
    mirrorRitem->Mat = mMaterials["icemirror"].get();
    mirrorRitem->Geo = mGeometries["roomGeo"].get();
    mirrorRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    mirrorRitem->IndexCount = mirrorRitem->Geo->DrawArgs["mirror"].IndexCount;
    mirrorRitem->StartIndexLocation = mirrorRitem->Geo->DrawArgs["mirror"].StartIndexLocation;
    mirrorRitem->BaseVertexLocation = mirrorRitem->Geo->DrawArgs["mirror"].BaseVertexLocation;
    mRitemLayer[(int)RenderLayer::Mirrors].push_back(mirrorRitem.get());
    mRitemLayer[(int)RenderLayer::Transparent].push_back(mirrorRitem.get());//�����ƾ�����Transparent

    // ������Ⱦ��
    auto skullRitem = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&skullRitem->World, XMMatrixTranslation(10.0f, 1.0f, 0.0f) * XMMatrixRotationY(1.57f) * XMMatrixScaling(0.5f, 0.5f, 0.5f));
    skullRitem->TexTransform = MathHelper::Identity4x4();
    skullRitem->ObjCBIndex = 3;
    skullRitem->Mat = mMaterials["skullMat"].get();
    skullRitem->Geo = mGeometries["skullGeo"].get();
    skullRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    skullRitem->IndexCount = skullRitem->Geo->DrawArgs["skull"].IndexCount;
    skullRitem->StartIndexLocation = skullRitem->Geo->DrawArgs["skull"].StartIndexLocation;
    skullRitem->BaseVertexLocation = skullRitem->Geo->DrawArgs["skull"].BaseVertexLocation;
    mSkullRitem = skullRitem.get();
    mRitemLayer[(int)RenderLayer::Opaque].push_back(skullRitem.get());

    // ����������Ⱦ��
    auto reflectedSkullRitem = std::make_unique<RenderItem>();
    *reflectedSkullRitem = *skullRitem;// ������Ⱦ��
    XMStoreFloat4x4(&reflectedSkullRitem->World, XMMatrixScaling(0.5f, 0.5f, 0.5f) * XMMatrixTranslation(-10.0f, 1.0f, 0.0f) * XMMatrixRotationY(1.57f));
    reflectedSkullRitem->TexTransform = MathHelper::Identity4x4();
    reflectedSkullRitem->ObjCBIndex = 4;
    mReflectedSkullRitem = reflectedSkullRitem.get();
    mRitemLayer[(int)RenderLayer::Reflected].push_back(reflectedSkullRitem.get());

    // ��Ӱ������Ⱦ��
    auto shadowedSkullRitem = std::make_unique<RenderItem>();
    *shadowedSkullRitem = *skullRitem;
    shadowedSkullRitem->ObjCBIndex = 5;
    shadowedSkullRitem->Mat = mMaterials["shadowMat"].get();
    // ������ȡ��mSkullShadowRitemָ�룬��Ϊmove�������ڴ棬����ʱ����Ӱ����û�����ã������ȴ�һ�ݳ�����֮�����þ�����
    mShadowedSkullRitem = shadowedSkullRitem.get();
    mRitemLayer[(int)RenderLayer::Shadow].push_back(shadowedSkullRitem.get());

    // �ذ巴����Ⱦ��
    auto floorMirrorRitem = std::make_unique<RenderItem>();
    *floorMirrorRitem = *floorRitem;
    XMVECTOR mirrorPlane = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f); // xy plane
    XMMATRIX R = XMMatrixReflect(mirrorPlane);
    XMStoreFloat4x4(&floorMirrorRitem->World, R);
    floorMirrorRitem->ObjCBIndex = 6;
    mRitemLayer[(int)RenderLayer::Reflected].push_back(floorMirrorRitem.get());

    // ��Ӱ������Ⱦ��
    auto reflectedShadowedSkullRitem = std::make_unique<RenderItem>();;
    *reflectedShadowedSkullRitem = *shadowedSkullRitem;
    reflectedShadowedSkullRitem->ObjCBIndex = 7;
    mReflectedShadowedSkullRitem = reflectedShadowedSkullRitem.get();
    mRitemLayer[(int)RenderLayer::Reflected].push_back(reflectedShadowedSkullRitem.get());
    
    //move���ͷ�Ritem�ڴ棬���Ա�����mRitemLayer֮��ִ��
    mAllRitems.push_back(std::move(wallsRitem));
    mAllRitems.push_back(std::move(floorRitem));
    mAllRitems.push_back(std::move(mirrorRitem));
    mAllRitems.push_back(std::move(skullRitem));
    mAllRitems.push_back(std::move(reflectedSkullRitem));
    mAllRitems.push_back(std::move(shadowedSkullRitem));
    mAllRitems.push_back(std::move(floorMirrorRitem));
    mAllRitems.push_back(std::move(reflectedShadowedSkullRitem));
}

void GameDevelopApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
    UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
    UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

    auto objectCB = mCurrFrameResource->ObjectCB->Resource();
    auto matCB = mCurrFrameResource->MaterialCB->Resource();

    for (size_t i = 0; i < ritems.size(); ++i)
    {
        auto ri = ritems[i];

        // ���ö��㻺����
        cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
        // ��������������
        cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
        // ����ͼԪ����
        cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

        //��֡��Դ������DrawRenderItems�����п��ǽ�ȥ
        // �ڻ�����Ⱦ���У�������ǰ���������ø��������ݳ������ݣ��������ǰ󶨸���������ֱ���ϴ�GPU��ĳ������ݡ�
        /*UINT cbvIndex = mCurrFrameResourceIndex * (UINT)mOpaqueRitems.size() + ri->ObjCBIndex;
        auto handle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mCbvHeap->GetGPUDescriptorHandleForHeapStart());
        handle.Offset(cbvIndex, cbv_srv_uavDescriptorSize);*/
        // ���ø�ǩ����
        //cmdList->SetGraphicsRootDescriptorTable(0, handle);

        //��������������������Դ����ˮ�߰�
        CD3DX12_GPU_DESCRIPTOR_HANDLE tex(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
        tex.Offset(ri->Mat->DiffuseSrvHeapIndex, mCbvSrvDescriptorSize);
        cmdList->SetGraphicsRootDescriptorTable(0, tex);

        //���������ĸ�����
        // ���ø�����������������������Դ�� �������ǰ󶨸���������ֱ���ϴ�GPU��ĳ������ݡ�
        D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress();
        objCBAddress += ri->ObjCBIndex * objCBByteSize;
        D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex * matCBByteSize;
        // objCBAddress����Դ�ĵ�ַ
        // �������ͬ
        cmdList->SetGraphicsRootConstantBufferView(1, objCBAddress);
        // 3ָ���Ǹ�ǩ�������±�
        cmdList->SetGraphicsRootConstantBufferView(3, matCBAddress);

        // ���ƶ���ʵ��
        //���ƶ��㣨ͨ���������������ƣ�
        cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
    }
}

// ���ض��㻺������ͼ
D3D12_VERTEX_BUFFER_VIEW GameDevelopApp::VertexBufferView() const
{
    D3D12_VERTEX_BUFFER_VIEW vbv;
    vbv.BufferLocation = mVertexBufferGPU->GetGPUVirtualAddress();//���㻺������Դ�����ַ
    vbv.StrideInBytes = mVertexByteStride;//ÿ������Ԫ����ռ�õ��ֽ���
    vbv.SizeInBytes = mVertexBufferByteSize;//���㻺������С�����ж������ݴ�С��

    return vbv;
}
// ����������������ͼ
D3D12_INDEX_BUFFER_VIEW GameDevelopApp::IndexBufferView() const
{
    D3D12_INDEX_BUFFER_VIEW ibv;
    ibv.BufferLocation = mIndexBufferGPU->GetGPUVirtualAddress();
    ibv.Format = mIndexFormat;
    ibv.SizeInBytes = mIndexBufferByteSize;

    return ibv;
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GameDevelopApp::GetStaticSamplers()
{
    //������POINT,ѰַģʽWRAP�ľ�̬������
    CD3DX12_STATIC_SAMPLER_DESC pointWarp(0,	//��ɫ���Ĵ���
        D3D12_FILTER_MIN_MAG_MIP_POINT,		//����������ΪPOINT(������ֵ)
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,	//U�����ϵ�ѰַģʽΪWRAP���ظ�Ѱַģʽ��
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,	//V�����ϵ�ѰַģʽΪWRAP���ظ�Ѱַģʽ��
        D3D12_TEXTURE_ADDRESS_MODE_WRAP);	//W�����ϵ�ѰַģʽΪWRAP���ظ�Ѱַģʽ��

    //������POINT,ѰַģʽCLAMP�ľ�̬������
    CD3DX12_STATIC_SAMPLER_DESC pointClamp(1,	//��ɫ���Ĵ���
        D3D12_FILTER_MIN_MAG_MIP_POINT,		//����������ΪPOINT(������ֵ)
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,	//U�����ϵ�ѰַģʽΪCLAMP��ǯλѰַģʽ��
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,	//V�����ϵ�ѰַģʽΪCLAMP��ǯλѰַģʽ��
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP);	//W�����ϵ�ѰַģʽΪCLAMP��ǯλѰַģʽ��

    //������LINEAR,ѰַģʽWRAP�ľ�̬������
    CD3DX12_STATIC_SAMPLER_DESC linearWarp(2,	//��ɫ���Ĵ���
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,		//����������ΪLINEAR(���Բ�ֵ)
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,	//U�����ϵ�ѰַģʽΪWRAP���ظ�Ѱַģʽ��
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,	//V�����ϵ�ѰַģʽΪWRAP���ظ�Ѱַģʽ��
        D3D12_TEXTURE_ADDRESS_MODE_WRAP);	//W�����ϵ�ѰַģʽΪWRAP���ظ�Ѱַģʽ��

    //������LINEAR,ѰַģʽCLAMP�ľ�̬������
    CD3DX12_STATIC_SAMPLER_DESC linearClamp(3,	//��ɫ���Ĵ���
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,		//����������ΪLINEAR(���Բ�ֵ)
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,	//U�����ϵ�ѰַģʽΪCLAMP��ǯλѰַģʽ��
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,	//V�����ϵ�ѰַģʽΪCLAMP��ǯλѰַģʽ��
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP);	//W�����ϵ�ѰַģʽΪCLAMP��ǯλѰַģʽ��

    //������ANISOTROPIC,ѰַģʽWRAP�ľ�̬������
    CD3DX12_STATIC_SAMPLER_DESC anisotropicWarp(4,	//��ɫ���Ĵ���
        D3D12_FILTER_ANISOTROPIC,			//����������ΪANISOTROPIC(��������)
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,	//U�����ϵ�ѰַģʽΪWRAP���ظ�Ѱַģʽ��
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,	//V�����ϵ�ѰַģʽΪWRAP���ظ�Ѱַģʽ��
        D3D12_TEXTURE_ADDRESS_MODE_WRAP);	//W�����ϵ�ѰַģʽΪWRAP���ظ�Ѱַģʽ��

    //������LINEAR,ѰַģʽCLAMP�ľ�̬������
    CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(5,	//��ɫ���Ĵ���
        D3D12_FILTER_ANISOTROPIC,			//����������ΪANISOTROPIC(��������)
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,	//U�����ϵ�ѰַģʽΪCLAMP��ǯλѰַģʽ��
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,	//V�����ϵ�ѰַģʽΪCLAMP��ǯλѰַģʽ��
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP);	//W�����ϵ�ѰַģʽΪCLAMP��ǯλѰַģʽ��

    return{ pointWarp, pointClamp, linearWarp, linearClamp, anisotropicWarp, anisotropicClamp };
}

//�������x��zֵ�������Һ�����ʹ����һ��ѭ���𵴵Ĳ��岨�ȡ�
float GameDevelopApp::GetHillsHeight(float x, float z)   const
{
    return 0.3f * (z * sinf(0.1f * x) + x * cosf(0.1f * z));
}

XMFLOAT3 GameDevelopApp::GetHillsNormal(float x, float z)   const
{
    // n = (-df/dx, 1, -df/dz)
    XMFLOAT3 n(
        -0.03f * z * cosf(0.1f * x) - 0.3f * cosf(0.1f * z),
        1.0f,
        -0.3f * sinf(0.1f * x) + 0.03f * x * sinf(0.1f * z));

    XMVECTOR unitNormal = XMVector3Normalize(XMLoadFloat3(&n));
    XMStoreFloat3(&n, unitNormal);

    return n;
}