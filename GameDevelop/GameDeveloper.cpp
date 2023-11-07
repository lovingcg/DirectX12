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

    mWaves = std::make_unique<Waves>(128, 128, 1.0f, 0.03f, 4.0f, 0.2f);

    mBlurFilter = std::make_unique<GaussianBlurFilter>(d3dDevice.Get(), mClientWidth, mClientHeight, DXGI_FORMAT_R8G8B8A8_UNORM);

    LoadTextures();
    BuildRootSignature();
    BuildPostProcessRootSignature();
    BuildDescriptorHeaps();
    BuildShadersAndInputLayout();
    //BuildGeometry();//���Ӻ͵���
    //BuildSkullGeometry();//����ͷ
    BuildLandGeometry();//ɽ��
    BuildWavesGeometryBuffers();//���� 
    BuildBoxGeometry();//����BOX
    BuildTreeSpritesGeometry();//������ľ�����
    BuildGeoSphere();//����20����
    BuildQuadPatchGeometry();//������Ƭ
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

    // Ȼ�������̨����������Ȼ�����������ֵ��
    // �������Ȼ�ö������������������ַ����
    // ��ͨ��ClearRenderTargetView������ClearDepthStencilView����������͸�ֵ��
    // ���ｫRT��Դ����ɫ��ֵΪDarkRed�����죩��
    //D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(rtvHeap->GetCPUDescriptorHandleForHeapStart(), ref_mCurrentBackBuffer, rtvDescriptorSize);
    //cmdList->ClearRenderTargetView(rtvHandle, DirectX::Colors::LightSteelBlue, 0, nullptr);// ���RT����ɫΪ���죬���Ҳ����òü�����
    //D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvHeap->GetCPUDescriptorHandleForHeapStart();
    //cmdList->ClearDepthStencilView(dsvHandle,	// DSV���������
    //    D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,	// FLAG
    //    1.0f,	// Ĭ�����ֵ
    //    0,	// Ĭ��ģ��ֵ
    //    0,	// �ü���������
    //    nullptr);	// �ü�����ָ��

    // �����̨����������Ȼ�����������ֵ
    //const float clear_color_with_alpha[4]={}
    // ������Ч֮��ðѱ���ɫ���ó���������ɫ
    cmdList->ClearRenderTargetView(CurrentBackBufferView(), (float*)&mMainPassCB.FogColor, 0, nullptr);// ���RT����ɫΪ���죬���Ҳ����òü�����
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

    //����CBV��������
    //ID3D12DescriptorHeap* descriptorHeaps[] = { mCbvHeap.Get() };// ע������֮���������飬����Ϊ�����ܰ���SRV��UAV������������ֻ�õ���CBV
    //cmdList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    //����SRV��������
    ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };// ע������֮���������飬����Ϊ�����ܰ���SRV��UAV������������ֻ�õ���SRV
    cmdList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    // ����ͼ�θ���������
    // ��Ϊ��2��������������Ҫ������
    //int passCbvIndex = mPassCbvOffset + mCurrFrameResourceIndex;
    //// passCbvHandle�ĵ�ַƫ����allRitems.size()
    //auto passCbvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mCbvHeap->GetGPUDescriptorHandleForHeapStart());
    //passCbvHandle.Offset(passCbvIndex, cbv_srv_uavDescriptorSize);
    //// 1��ʾ����������ʼ����
    //cmdList->SetGraphicsRootDescriptorTable(1, passCbvHandle);

    // ��passCB��������
    auto passCB = mCurrFrameResource->PassCB->Resource();
    // �Ĵ����ۺţ���ɫ��register��b2����Ҫ�͸�ǩ������ʱ��һһ��Ӧ
    cmdList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());

    //�ֱ�����PSO�����ƶ�Ӧ��Ⱦ��
    if (!fog)
    {
        cmdList->SetPipelineState(mPSOs["unfogopaque"].Get());
        DrawRenderItems(cmdList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);

        cmdList->SetPipelineState(mPSOs["subdivision"].Get());
        DrawRenderItems(cmdList.Get(), mRitemLayer[(int)RenderLayer::Tess]);

        cmdList->SetPipelineState(mPSOs["unfogLOD"].Get());
        DrawRenderItems(cmdList.Get(), mRitemLayer[(int)RenderLayer::geoSphere]);

        cmdList->SetPipelineState(mPSOs["unfogalphaTested"].Get());
        DrawRenderItems(cmdList.Get(), mRitemLayer[(int)RenderLayer::AlphaTested]);
 
        cmdList->SetPipelineState(mPSOs["unfogtreeSprites"].Get());
        DrawRenderItems(cmdList.Get(), mRitemLayer[(int)RenderLayer::AlphaTestedTreeSprites]);

        cmdList->SetPipelineState(mPSOs["unfogtransparent"].Get());
        DrawRenderItems(cmdList.Get(), mRitemLayer[(int)RenderLayer::Transparent]);
    }
    else
    {
        cmdList->SetPipelineState(mPSOs["opaque"].Get());
        DrawRenderItems(cmdList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);

        cmdList->SetPipelineState(mPSOs["fogsubdivision"].Get());
        DrawRenderItems(cmdList.Get(), mRitemLayer[(int)RenderLayer::Tess]);

        cmdList->SetPipelineState(mPSOs["LOD"].Get());
        DrawRenderItems(cmdList.Get(), mRitemLayer[(int)RenderLayer::geoSphere]);

        cmdList->SetPipelineState(mPSOs["alphaTested"].Get());
        DrawRenderItems(cmdList.Get(), mRitemLayer[(int)RenderLayer::AlphaTested]);

        cmdList->SetPipelineState(mPSOs["treeSprites"].Get());
        DrawRenderItems(cmdList.Get(), mRitemLayer[(int)RenderLayer::AlphaTestedTreeSprites]);

        cmdList->SetPipelineState(mPSOs["transparent"].Get());
        DrawRenderItems(cmdList.Get(), mRitemLayer[(int)RenderLayer::Transparent]);
    }
    // ׼����ģ��������Ƶ��󻺳���
    // ִ������ģ�����㣬�õ�ģ�������������blurMap0
    if (is_GaussianBlurFilter)
    {
        mBlurFilter->Execute(cmdList.Get(), mPostProcessRootSignature.Get(),
        mPSOs["horzBlur"].Get(), mPSOs["vertBlur"].Get(), CurrentBackBuffer(), blurCount);

        //����̨������Դת�ɡ�����Ŀ�ꡱ
        cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST));

        //��ģ����������������������̨������
        cmdList->CopyResource(CurrentBackBuffer(), mBlurFilter->Output());
    }
    //int objCbvIndex = 0;//����������ʼ����
    //auto handle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mCbvHeap->GetGPUDescriptorHandleForHeapStart());
    //handle.Offset(objCbvIndex, cbv_srv_uavDescriptorSize);
    //cmdList->SetGraphicsRootDescriptorTable(objCbvIndex, handle);

    //int passCbvIndex = 1;//����������ʼ����
    //handle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mCbvHeap->GetGPUDescriptorHandleForHeapStart());
    //handle.Offset(passCbvIndex, cbv_srv_uavDescriptorSize);
    //cmdList->SetGraphicsRootDescriptorTable(passCbvIndex, handle);
    
    //// ���ݣ�GPU->��Ⱦ��ˮ��
    //// ���ö��㻺����
    //// �������ݰ����Ĵ�����
    //cmdList->IASetVertexBuffers(0, 1, &geo->VertexBufferView());
    //// ��������������
    //cmdList->IASetIndexBuffer(&geo->IndexBufferView());

    //// ���ƶ��㣨ͨ���������������ƣ�
    //// ��Ϊ�˰�����û�������壬���Ե�4������Ϊ0����������������ｫ��ƫ�Ƽ��㡣
    //cmdList->DrawIndexedInstanced(geo->DrawArgs["box"].IndexCount,// ÿ��ʵ��Ҫ���Ƶ�������
    //    1,// ʵ��������
    //    geo->DrawArgs["box"].StartIndexLocation,// ��ʼ����λ��
    //    geo->DrawArgs["box"].BaseVertexLocation,// ��������ʼ������ȫ�������е�λ��
    //    0);// ʵ�����ĸ߼�����
    //cmdList->DrawIndexedInstanced(geo->DrawArgs["grid"].IndexCount, //ÿ��ʵ��Ҫ���Ƶ�������
    //    1,	//ʵ��������
    //    geo->DrawArgs["grid"].StartIndexLocation,	//��ʼ����λ��
    //    geo->DrawArgs["grid"].BaseVertexLocation,	//��������ʼ������ȫ�������е�λ��
    //    0);	//ʵ�����ĸ߼���������ʱ����Ϊ0
    //cmdList->DrawIndexedInstanced(geo->DrawArgs["sphere"].IndexCount, //ÿ��ʵ��Ҫ���Ƶ�������
    //    1,	//ʵ��������
    //    geo->DrawArgs["sphere"].StartIndexLocation,	//��ʼ����λ��
    //    geo->DrawArgs["sphere"].BaseVertexLocation,	//��������ʼ������ȫ�������е�λ��
    //    0);	//ʵ�����ĸ߼���������ʱ����Ϊ0
    //cmdList->DrawIndexedInstanced(geo->DrawArgs["cylinder"].IndexCount, //ÿ��ʵ��Ҫ���Ƶ�������
    //    1,	//ʵ��������
    //    geo->DrawArgs["cylinder"].StartIndexLocation,	//��ʼ����λ��
    //    geo->DrawArgs["cylinder"].BaseVertexLocation,	//��������ʼ������ȫ�������е�λ��
    //    0);	//ʵ�����ĸ߼���������ʱ����Ϊ0
    // �ȵ���Ⱦ��ɣ�����̨��������״̬�ĳɳ���״̬��ʹ��֮���Ƶ�ǰ̨��������ʾ��
    // ���ˣ��ر������б��ȴ�����������С�
    // ��˹ģ�����ٴ�ת��RT��Դ��ת�ɳ���
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
                mRadius = 75.0f;
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

            ImGui::Text("alpha: %.2f degrees", mMainPassCB.alpha);
            ImGui::SliderFloat("##4", &mMainPassCB.alpha, 0.00f, 1.00f, "");

            ImGui::ColorEdit3("Clear Color", clearColor);
        }
        //ImGui::Checkbox("Wiremode", &is_wiremode);
        ImGui::Checkbox("Sunmove", &is_sunmove);
        if (is_sunmove)
        {
            OnKeyboardInput(gt);
        }
        ImGui::Checkbox("CartoonShader", &is_ctshader);
        ImGui::Checkbox("Fog", &fog);
        ImGui::Checkbox("LOD", &lod);
        ImGui::Checkbox("GaussianBlurFilter", &is_GaussianBlurFilter);
        if (is_GaussianBlurFilter)
        {
            ImGui::SliderInt("blurCount", &blurCount, 0, 100);
        }
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
    //�����������Ļ����������������������ʹ�䳤�����Ļ����һ��
    if (mBlurFilter != nullptr)
    {
        mBlurFilter->OnResize(mClientWidth, mClientHeight);
    }
}

// ÿһ֡�Ĳ���
void GameDevelopApp::Update()
{
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

    AnimateMaterials(gt);
    UpdateObjectCBs(gt);
    UpdateMaterialCBs(gt);
    UpdateMainPassCB(gt);
    UpdateWaves(gt);

    //XMMATRIX world;
    //// ��������󴫵ݸ�GPU objConstants
    //for (auto& t : mAllRitems)
    //{
    //    world = XMLoadFloat4x4(&t->World);
    //    XMStoreFloat4x4(&objConstants.world, XMMatrixTranspose(world));
    //    //�����ݿ�����GPU����
    //    //t->ObjCBIndex��֤��������뵥��������һһ��Ӧ
    //    mObjectCB->CopyData(t->ObjCBIndex, objConstants);
    //}
    //XMMATRIX proj = XMLoadFloat4x4(&mProj); 
    //XMMATRIX proj = XMMatrixPerspectiveFovLH(fov * MathHelper::Pi, static_cast<float>(mClientWidth) / mClientHeight, 1.0f, 1000.0f);
    ////XMMATRIX trans = XMMatrixTranslation(0.0f, 0.0f, 0.0f);
    ////�������
    //XMMATRIX viewProj = view * proj;

    ////XMMATRIX��ֵ��XMFLOAT4X4
    ////���worldViewProj���󱣴�ʱת����һ�£�����ΪHLSL�ľ����������ڴ�����������洢�ģ�
    ////���Կ��Կ�����Shader�У��������ҳ˵ġ�����DXMATH���еľ����������ڴ�����������洢�ģ�
    ////����Ϊ����������Ч�ʣ��ڴ���shaderǰҪ���о���ת�ó��о���
    ////XMStoreFloat4x4(&objConstants.world, XMMatrixTranspose(world*trans));
    //////�����ݿ�����GPU����
    ////mObjectCB->CopyData(0, objConstants);

    //XMStoreFloat4x4(&mMainPassCB.viewProj, XMMatrixTranspose(viewProj));
    //auto currPassCB = mCurrFrameResource->PassCB.get();
    ////0��ʾ��������干��һ���۲�ͶӰ����
    //currPassCB->CopyData(0, mMainPassCB);
    ////mPassCB->CopyData(0, passConstants);
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
    mMainPassCB.Lights[0].Strength = { 0.9f, 0.9f, 0.8f };

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
        fin >> ignore >> ignore >> ignore;
        //vertices[i].Color = XMFLOAT4(0.5f, 0.5f, 0.5f, 1.0f);
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

void GameDevelopApp::BuildGeoSphere()
{
    ProceduralGeometry proceGeo;
    //����20����
    ProceduralGeometry::MeshData geoSphere = proceGeo.CreateGeosphere20Face(0.8f);

    std::vector<Vertex> vertices(12);//��ʼ�������б�
    //�����б�ֵ
    for (UINT i = 0; i < 12; i++)
    {
        vertices[i].Pos = geoSphere.Vertices[i].Position;
        vertices[i].Normal = geoSphere.Vertices[i].Normal;
        vertices[i].TexC = geoSphere.Vertices[i].TexC;
    }

    std::vector<std::int16_t> indices(60);//��ʼ�������б�(20����60������)
    //�����б�ֵ
    for (UINT i = 0; i < 60; i++)
    {
        indices[i] = geoSphere.Indices32[i];
    }

    const UINT vbByteSize = vertices.size() * sizeof(Vertex);//���㻺���С
    const UINT ibByteSize = indices.size() * sizeof(std::int16_t);//���������С

    //����������
    SubmeshGeometry geoSphereSubmesh;
    geoSphereSubmesh.BaseVertexLocation = 0;
    geoSphereSubmesh.StartIndexLocation = 0;
    geoSphereSubmesh.IndexCount = (UINT)indices.size();

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "geoSphereGeo";

    //��������������ݸ��Ƶ�CPUϵͳ�ڴ���
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

    geo->DrawArgs["geoSphere"] = geoSphereSubmesh;

    mGeometries["geoSphereGeo"] = std::move(geo);
}

void GameDevelopApp::BuildQuadPatchGeometry()
{
    std::array<XMFLOAT3, 16> vertices =
    {
        // Row 0
        XMFLOAT3(-10.0f, 0.0f, +15.0f),
        XMFLOAT3(-5.0f,  10.0f, +15.0f),
        XMFLOAT3(+5.0f,  10.0f, +15.0f),
        XMFLOAT3(+10.0f, 10.0f, +15.0f),

        // Row 1
        XMFLOAT3(-15.0f, 10.0f, +5.0f),
        XMFLOAT3(-5.0f,  10.0f, +5.0f),
        XMFLOAT3(+5.0f,  30.0f, +5.0f),
        XMFLOAT3(+15.0f, 10.0f, +5.0f),

        // Row 2
        XMFLOAT3(-15.0f, 10.0f, -5.0f),
        XMFLOAT3(-5.0f,  10.0f, -5.0f),
        XMFLOAT3(+5.0f,  10.0f, -5.0f),
        XMFLOAT3(+15.0f, 10.0f, -5.0f),

        // Row 3
        XMFLOAT3(-10.0f, 20.0f, -15.0f),
        XMFLOAT3(-5.0f,  10.0f, -15.0f),
        XMFLOAT3(+5.0f,  10.0f, -15.0f),
        XMFLOAT3(+25.0f, 20.0f, -15.0f)
    };

    std::array<std::int16_t, 16> indices =
    {
        0, 1, 2, 3,
        4, 5, 6, 7,
        8, 9, 10, 11,
        12, 13, 14, 15
    };

    const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "quadpatchGeo";

    ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
    CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

    geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(d3dDevice.Get(),
        cmdList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(d3dDevice.Get(),
        cmdList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

    geo->VertexByteStride = sizeof(XMFLOAT3);
    geo->VertexBufferByteSize = vbByteSize;
    geo->IndexFormat = DXGI_FORMAT_R16_UINT;
    geo->IndexBufferByteSize = ibByteSize;

    SubmeshGeometry quadSubmesh;
    quadSubmesh.IndexCount = (UINT)indices.size();
    quadSubmesh.StartIndexLocation = 0;
    quadSubmesh.BaseVertexLocation = 0;

    geo->DrawArgs["quadpatch"] = quadSubmesh;

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
    //UINT objCount = (UINT)mOpaqueRitems.size();
    ////objCB�д����objectCount����22�����ӻ�������passCB�д����1���ӻ�������һ��23���ӻ�������
    ////������3��֡��Դ������Ҫ��23*3��һ����69���ӻ�����������Ҫ69��CBV��
    //UINT numDescriptors = (objCount + 1) * gNumFrameResources;

    //mPassCbvOffset = objCount * gNumFrameResources;

    //D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
    ////��Ϊ������22�������壬���Ծ���Ҫ22��objCBV����һ��passCBV�������۲�ͶӰ����
    ////�˴�һ�����а���(���������������ʵ����+1)��CBV
    //cbvHeapDesc.NumDescriptors = numDescriptors;// ����perPass CBV
    //cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    //cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;// �ɹ���ɫ������
    //cbvHeapDesc.NodeMask = 0;
    //ThrowIfFailed(d3dDevice->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&mCbvHeap)));

    const int textureDescriptorCount = 6;
    const int blurDescriptorCount = 4;//���������SRV��UAV

    //����SRV��
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = textureDescriptorCount + blurDescriptorCount;//�����µ������ø�
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    srvHeapDesc.NodeMask = 0;
    ThrowIfFailed(d3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

    // ��֮ǰ����õ�������Դת����comptr������
    auto grassTex = mTextures["grassTex"]->Resource;
    auto waterTex = mTextures["waterTex"]->Resource;
    auto fenceTex = mTextures["fenceTex"]->Resource;
    auto fenceTexA = mTextures["fenceTexA"]->Resource;
    auto treeArrayTex = mTextures["treeArrayTex"]->Resource;
    auto white1x1Tex = mTextures["white1x1Tex"]->Resource;

    //SRV������SRV���ݵص�SRV���ľ��
    CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

    //SRV�����ṹ��
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;//���������˳�򲻸ı�
    srvDesc.Format = grassTex->GetDesc().Format;//��ͼ��Ĭ�ϸ�ʽ
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;//2D��ͼ
    srvDesc.Texture2D.MostDetailedMip = 0;//ϸ�����꾡��mipmap�㼶Ϊ0
    srvDesc.Texture2D.MipLevels = grassTex->GetDesc().MipLevels;//mipmap�㼶����
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;//�ɷ��ʵ�mipmap��С�㼶��Ϊ0
    //�������ݵء���SRV
    d3dDevice->CreateShaderResourceView(grassTex.Get(), &srvDesc, hDescriptor);

    //SRV������SRV����ˮ��SRV���ľ��,����ƫ��һ��SRV
    hDescriptor.Offset(1, mCbvSrvDescriptorSize);
    //SRV�����ṹ���޸�
    srvDesc.Format = waterTex->GetDesc().Format;//��ͼ��Ĭ�ϸ�ʽ
    srvDesc.Texture2D.MipLevels = waterTex->GetDesc().MipLevels;//mipmap�㼶����
    // ������ˮSRV
    d3dDevice->CreateShaderResourceView(waterTex.Get(), &srvDesc, hDescriptor);

    //SRV������SRV���������SRV���ľ��,����ƫ��һ��SRV
    hDescriptor.Offset(1, mCbvSrvDescriptorSize);
    //SRV�����ṹ���޸�
    srvDesc.Format = fenceTex->GetDesc().Format;//��ͼ��Ĭ�ϸ�ʽ
    srvDesc.Texture2D.MipLevels = fenceTex->GetDesc().MipLevels;//mipmap�㼶����
    //�����������䡱��SRV
    d3dDevice->CreateShaderResourceView(fenceTex.Get(), &srvDesc, hDescriptor);

    //SRV������SRV���������SRV���ľ��,����ƫ��һ��SRV
    hDescriptor.Offset(1, mCbvSrvDescriptorSize);
    //SRV�����ṹ���޸�
    srvDesc.Format = fenceTexA->GetDesc().Format;//��ͼ��Ĭ�ϸ�ʽ
    srvDesc.Texture2D.MipLevels = fenceTexA->GetDesc().MipLevels;//mipmap�㼶����
    //�����������䡱��SRV
    d3dDevice->CreateShaderResourceView(fenceTexA.Get(), &srvDesc, hDescriptor);

    //SRV������SRV�����õ�SRV���ľ��,����ƫ��һ��SRV
    hDescriptor.Offset(1, mCbvSrvDescriptorSize);
    //SRV�����ṹ���޸�
    srvDesc.Format = white1x1Tex->GetDesc().Format;//��ͼ��Ĭ�ϸ�ʽ
    srvDesc.Texture2D.MipLevels = white1x1Tex->GetDesc().MipLevels;//mipmap�㼶����
    //���������á���SRV
    d3dDevice->CreateShaderResourceView(white1x1Tex.Get(), &srvDesc, hDescriptor);

    // ʹ��BlurFilter��Դ������������
    mBlurFilter->BuildDescriptors(
        CD3DX12_CPU_DESCRIPTOR_HANDLE(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), textureDescriptorCount, mCbvSrvDescriptorSize),
        CD3DX12_GPU_DESCRIPTOR_HANDLE(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), textureDescriptorCount, mCbvSrvDescriptorSize),
        mCbvSrvDescriptorSize);

    // ��������ƫ��Ҫ�ŵ����
    //SRV������SRV����ľ��SRV���ľ��,����ƫ��һ��SRV
    hDescriptor.Offset(1, mCbvSrvDescriptorSize);
    //SRV�����ṹ���޸�
    srvDesc.Format = treeArrayTex->GetDesc().Format;//��ͼ��Ĭ�ϸ�ʽ
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;//2D��������
    srvDesc.Texture2DArray.ArraySize = treeArrayTex->GetDesc().DepthOrArraySize;//�������鳤��
    srvDesc.Texture2DArray.MostDetailedMip = 0;//MipMap�㼶Ϊ0
    srvDesc.Texture2DArray.MipLevels = -1;//û�в㼶
    srvDesc.Texture2DArray.FirstArraySlice = 0;
    //��������ľ����SRV
    d3dDevice->CreateShaderResourceView(treeArrayTex.Get(), &srvDesc, hDescriptor);
}

void GameDevelopApp::BuildTreeSpritesGeometry()
{
    //������ľ�����Ķ���ṹ��
    struct TreeSpriteVertex
    {
        XMFLOAT3 Pos;//���������ĵ�����
        XMFLOAT2 Size;//�����ĳ���
    };

    static const int treeCount = 20;
    std::array<TreeSpriteVertex, 20> vertices;
    for (UINT i = 0; i < treeCount; ++i)
    {
        float x = MathHelper::RandF(-45.0f, 45.0f);//ȡ-45��45��һ�������
        float z = MathHelper::RandF(-45.0f, 45.0f);
        float y = GetHillsHeight(x, z);//���y�������꣨����ɽ����
        
        y += 8.0f;//�ù�������ĵ����ɽ��8.0����λ

        vertices[i].Pos = XMFLOAT3(x, y, z);
        vertices[i].Size = XMFLOAT2(20.0f, 20.0f);
    }

    //��������������
    std::array<std::uint16_t, 20> indices =
    {
        0, 1, 2, 3, 4, 5, 6, 7,
        8, 9, 10, 11, 12, 13, 14, 15,
        16, 17, 18, 19
    };

    const UINT vbByteSize = (UINT)vertices.size() * sizeof(TreeSpriteVertex);
    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "treeSpritesGeo";
    // ����ʹ��CopyMemory�Լ�CreateDefaultBuffer���������������ݴ���GPU.
    ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
    CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

    geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(d3dDevice.Get(),
        cmdList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(d3dDevice.Get(),
        cmdList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

    geo->VertexByteStride = sizeof(TreeSpriteVertex);
    geo->VertexBufferByteSize = vbByteSize;
    geo->IndexFormat = DXGI_FORMAT_R16_UINT;
    geo->IndexBufferByteSize = ibByteSize;

    SubmeshGeometry submesh;
    submesh.IndexCount = (UINT)indices.size();
    submesh.StartIndexLocation = 0;
    submesh.BaseVertexLocation = 0;

    geo->DrawArgs["points"] = submesh;

    mGeometries["treeSpritesGeo"] = std::move(geo);
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

void GameDevelopApp::UpdateWaves(const _GameTimer::GameTimer& gt)
{
    // ÿ�ķ�֮һ�룬����һ�������
    static float t_base = 0.0f;
    if ((gt.TotalTime() - t_base) >= 0.25f)
    {
        t_base += 0.25f;//0.25������һ������

        //������ɺ�����
        int i = MathHelper::Rand(4, mWaves->RowCount() - 5);
        //�������������
        int j = MathHelper::Rand(4, mWaves->ColumnCount() - 5);
        //������ɲ��İ뾶
        float r = MathHelper::RandF(0.2f, 0.5f);//float��RandF����
        //ʹ�ò������̺������ɲ���
        mWaves->Disturb(i, j, r);
    }
    //ÿ֡���²���ģ�⣨�����¶������꣩
    mWaves->Update(gt.DeltaTime());

    //�����µĶ����������GPU�ϴ�����
    auto currWavesVB = mCurrFrameResource->WavesVB.get();
    for (int i = 0; i < mWaves->VertexCount(); ++i)
    {
        Vertex v;

        v.Pos = mWaves->Position(i);
        v.Normal = mWaves->Normal(i);

        //����������ת����UV���꣬��[-w/2, w/2]ӳ�䵽[0, 1]
        v.TexC.x = 0.5f + v.Pos.x / mWaves->Width();
        v.TexC.y = 0.5f - v.Pos.z / mWaves->Depth();
 
        // �ӻ�����һһ��Ӧ �ж��ٸ�������ж��ٸ��ӻ�����
        currWavesVB->CopyData(i, v);
    }
    //��ֵ������GPU�ϵĶ��㻺��
    mWavesRitem->Geo->VertexBufferGPU = currWavesVB->Resource();
}

void GameDevelopApp::LoadTextures()
{
    /*����������*/
    auto fenceTex = std::make_unique<Texture>();
    fenceTex->Name = "fenceTex";
    fenceTex->Filename = L"Textures\\fire.dds";
    // ��ȡDDS�ļ�
    // CreateDDSTextureFromFile12��������������DDS�ļ�����ֵresource��uploadHeap
    // resource�൱�ڷ��ص�Ĭ�϶���Դ�������յ�����������Դ����uploadHeap���Ƿ��ص��ϴ�����Դ
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(d3dDevice.Get(),
        cmdList.Get(), fenceTex->Filename.c_str(),
        fenceTex->Resource, fenceTex->UploadHeap));

    /*�ݵ�����*/
    auto grassTex = std::make_unique<Texture>();
    grassTex->Name = "grassTex";
    grassTex->Filename = L"Textures\\grass.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(d3dDevice.Get(),
        cmdList.Get(), grassTex->Filename.c_str(),
        grassTex->Resource, grassTex->UploadHeap));

    /*��ˮ����*/
    auto waterTex = std::make_unique<Texture>();
    waterTex->Name = "waterTex";
    waterTex->Filename = L"Textures\\water1.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(d3dDevice.Get(),
        cmdList.Get(), waterTex->Filename.c_str(),
        waterTex->Resource, waterTex->UploadHeap));

    /*�������*/
    auto fenceTexA = std::make_unique<Texture>();
    fenceTexA->Name = "fenceTexA";
    fenceTexA->Filename = L"Textures\\WireFence.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(d3dDevice.Get(),
        cmdList.Get(), fenceTexA->Filename.c_str(),
        fenceTexA->Resource, fenceTexA->UploadHeap));

    /*������*/
    auto treeArrayTex = std::make_unique<Texture>();
    treeArrayTex->Name = "treeArrayTex";
    treeArrayTex->Filename = L"Textures\\treeArray2.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(d3dDevice.Get(),
        cmdList.Get(), treeArrayTex->Filename.c_str(),//��wstringת��wChar_t
        treeArrayTex->Resource, treeArrayTex->UploadHeap));

    /*��������*/
    auto white1x1Tex = std::make_unique<Texture>();
    white1x1Tex->Name = "white1x1Tex";
    white1x1Tex->Filename = L"Textures\\white1x1.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(d3dDevice.Get(),
        cmdList.Get(), white1x1Tex->Filename.c_str(),
        white1x1Tex->Resource, white1x1Tex->UploadHeap));

    //װ���ܵ�����ӳ���
    mTextures[grassTex->Name] = std::move(grassTex);
    mTextures[waterTex->Name] = std::move(waterTex);
    mTextures[fenceTex->Name] = std::move(fenceTex);
    mTextures[fenceTexA->Name] = std::move(fenceTexA);
    mTextures[treeArrayTex->Name] = std::move(treeArrayTex);
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
/*��������ĸ�ǩ��*/
// ��ΪCS�ǲ�������Ⱦ��ˮ��ĳһ�׶Σ����Ը�ǩ���Ĵ��������봫ͳ��ǩ��Ҳ��ͬ
void GameDevelopApp::BuildPostProcessRootSignature()
{
    //����SRV����������Ϊ������0
    CD3DX12_DESCRIPTOR_RANGE srvTable;
    srvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,//����������SRV
                  1,//������������
                  0);//���������󶨵ļĴ����ۺ�
    //����UAV����������Ϊ������1
    CD3DX12_DESCRIPTOR_RANGE uavTable;
    uavTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

    //������������������������������������
    CD3DX12_ROOT_PARAMETER slotRootParameter[3];

    slotRootParameter[0].InitAsConstants(12, 0);//12���������Ĵ����ۺ�Ϊ0
    slotRootParameter[1].InitAsDescriptorTable(1, &srvTable);//Range����Ϊ1
    slotRootParameter[2].InitAsDescriptorTable(1, &uavTable);//Range����Ϊ1

    auto staticSamplers = GetStaticSamplers();//��þ�̬����������
    //��ǩ����һ�����������
    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(3,//������������
        slotRootParameter,//������ָ��
        0, //��̬������������0
        nullptr,//��̬������ָ��Ϊ��
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    //�õ����Ĵ�����������һ����ǩ�����ò�λָ��һ�������е�������������������������
    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

    if (errorBlob != nullptr)
    {
        ::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    }
    ThrowIfFailed(hr);

    ThrowIfFailed(d3dDevice->CreateRootSignature(
        0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(mPostProcessRootSignature.GetAddressOf())));
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

    // ��һ��shader��������
    mShaders["treeSpriteVS"] = d3dUtil::CompileShader(L"Shaders\\TreeSprite.hlsl", nullptr, "VS", "vs_5_0");
    mShaders["treeSpriteGS"] = d3dUtil::CompileShader(L"Shaders\\TreeSprite.hlsl", nullptr, "GS", "gs_5_0");
    mShaders["treeSpritePS"] = d3dUtil::CompileShader(L"Shaders\\TreeSprite.hlsl", alphaTestDefines, "PS", "ps_5_0");

    mShaders["unfogtreeSpritePS"] = d3dUtil::CompileShader(L"Shaders\\TreeSprite.hlsl", unfogalphaTestDefines, "PS", "ps_5_0");

    mShaders["GeosphereVS"] = d3dUtil::CompileShader(L"Shaders\\Geosphere.hlsl", nullptr, "VS", "vs_5_0");
    mShaders["GeosphereGS"] = d3dUtil::CompileShader(L"Shaders\\Geosphere.hlsl", nullptr, "GS", "gs_5_0");
    mShaders["GeospherePS"] = d3dUtil::CompileShader(L"Shaders\\Geosphere.hlsl", defines, "PS", "ps_5_0");
    mShaders["unfogGeospherePS"] = d3dUtil::CompileShader(L"Shaders\\Geosphere.hlsl", unfogdefines, "PS", "ps_5_0");

    mShaders["horzBlurCS"] = d3dUtil::CompileShader(L"Shaders\\Blur.hlsl", nullptr, "HorzBlurCS", "cs_5_0");
    mShaders["vertBlurCS"] = d3dUtil::CompileShader(L"Shaders\\Blur.hlsl", nullptr, "VertBlurCS", "cs_5_0");
    //����ɿ���ֲ���ֽ���
    /*mVsByteCode = d3dUtil::CompileShader(L"Shaders\\color.hlsl", nullptr, "VS", "vs_5_0");
    mPsByteCode = d3dUtil::CompileShader(L"Shaders\\color.hlsl", nullptr, "PS", "ps_5_0");*/

    mShaders["tessVS"] = d3dUtil::CompileShader(L"Shaders\\Tessellation.hlsl", nullptr, "VS", "vs_5_0");
    mShaders["tessHS"] = d3dUtil::CompileShader(L"Shaders\\Tessellation.hlsl", nullptr, "HS", "hs_5_0");
    mShaders["tessDS"] = d3dUtil::CompileShader(L"Shaders\\Tessellation.hlsl", nullptr, "DS", "ds_5_0");
    mShaders["tessPS"] = d3dUtil::CompileShader(L"Shaders\\Tessellation.hlsl", nullptr, "PS", "ps_5_0");
    mShaders["fogtessPS"] = d3dUtil::CompileShader(L"Shaders\\Tessellation.hlsl", defines, "PS", "ps_5_0");

    mInputLayout =
    {
        //��һ��0ָ���� ������ͬ����Ĳ�ͬ����0 1 �ڶ���0ָ����ֻ��һ�����뻺���� ���������ֻ��һ�����뻺����
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        // �������������ɫ���ȣ���128λ���ٵ�32λ
        // { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        // ��ΪDXGI_FORMAT�������е�ֵ���ڴ�������С���ֽ�������ʾ�ģ����Դ������Ч�ֽ�д�������Ч�ֽڣ���ʽARGB����ʾΪBGRA��
        //{ "COLOR", 0, DXGI_FORMAT_B8G8R8A8_UNORM, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    mTreeSpriteInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "SIZE", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    mtessInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };
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

    //
    //��ľ������PSO������shader��
    //
    D3D12_GRAPHICS_PIPELINE_STATE_DESC treeSpritePsoDesc = opaquePsoDesc;
    //�滻��ľ������InputLayout
    treeSpritePsoDesc.InputLayout = { mTreeSpriteInputLayout.data(), (UINT)mTreeSpriteInputLayout.size() };// ���벼������  
    //�滻������shader�ֽ���
    treeSpritePsoDesc.VS =
    {
        reinterpret_cast<BYTE*>(mShaders["treeSpriteVS"]->GetBufferPointer()),
        mShaders["treeSpriteVS"]->GetBufferSize()
    };
    treeSpritePsoDesc.GS =
    {
        reinterpret_cast<BYTE*>(mShaders["treeSpriteGS"]->GetBufferPointer()),
        mShaders["treeSpriteGS"]->GetBufferSize()
    };
    treeSpritePsoDesc.PS =
    {
        reinterpret_cast<BYTE*>(mShaders["treeSpritePS"]->GetBufferPointer()),
        mShaders["treeSpritePS"]->GetBufferSize()
    };
    treeSpritePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;//���б�
    treeSpritePsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;//˫����ʾ

    ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&treeSpritePsoDesc, IID_PPV_ARGS(&mPSOs["treeSprites"])));

    // ������Ч
    D3D12_GRAPHICS_PIPELINE_STATE_DESC unfogtreeSpritePsoDesc = treeSpritePsoDesc;
    unfogtreeSpritePsoDesc.PS =
    {
        reinterpret_cast<BYTE*>(mShaders["unfogtreeSpritePS"]->GetBufferPointer()),
        mShaders["unfogtreeSpritePS"]->GetBufferSize()
    };
    ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&unfogtreeSpritePsoDesc, IID_PPV_ARGS(&mPSOs["unfogtreeSprites"])));

    // 20����PSO
    D3D12_GRAPHICS_PIPELINE_STATE_DESC geoSpherePsoDesc = {};
    ZeroMemory(&geoSpherePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
    geoSpherePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
    geoSpherePsoDesc.pRootSignature = mRootSignature.Get();// ���PSO�󶨵ĸ�ǩ��ָ��

    geoSpherePsoDesc.VS =
    { reinterpret_cast<BYTE*>(mShaders["GeosphereVS"]->GetBufferPointer()), mShaders["GeosphereVS"]->GetBufferSize() };
    geoSpherePsoDesc.GS =
    { reinterpret_cast<BYTE*>(mShaders["GeosphereGS"]->GetBufferPointer()),  mShaders["GeosphereGS"]->GetBufferSize() };
    geoSpherePsoDesc.PS =
    { reinterpret_cast<BYTE*>(mShaders["GeospherePS"]->GetBufferPointer()), mShaders["GeospherePS"]->GetBufferSize() };
    geoSpherePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    //geoSpherePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    geoSpherePsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    geoSpherePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    geoSpherePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    geoSpherePsoDesc.SampleMask = UINT_MAX;	//0xffffffff,ȫ��������û������
    geoSpherePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    geoSpherePsoDesc.NumRenderTargets = 1;
    geoSpherePsoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;	//��һ�����޷�������
    geoSpherePsoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    geoSpherePsoDesc.SampleDesc.Count = 1;	//��ʹ��4XMSAA
    geoSpherePsoDesc.SampleDesc.Quality = 0;	//��ʹ��4XMSAA

    ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&geoSpherePsoDesc, IID_PPV_ARGS(&mPSOs["LOD"])));
    // ������Ч
    D3D12_GRAPHICS_PIPELINE_STATE_DESC unfoggeoSpherePsoDesc = geoSpherePsoDesc;
    unfoggeoSpherePsoDesc.PS =
    {
        reinterpret_cast<BYTE*>(mShaders["unfogGeospherePS"]->GetBufferPointer()),
        mShaders["unfogGeospherePS"]->GetBufferSize()
    };
    ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&unfoggeoSpherePsoDesc, IID_PPV_ARGS(&mPSOs["unfogLOD"])));

    //
    //ˮƽ����ģ���ġ�����PSO��
    //
    D3D12_COMPUTE_PIPELINE_STATE_DESC horzBlurPSO = {};
    horzBlurPSO.pRootSignature = mPostProcessRootSignature.Get();// ���PSO�󶨵ĸ�ǩ��ָ��
    horzBlurPSO.CS =
    {
        reinterpret_cast<BYTE*>(mShaders["horzBlurCS"]->GetBufferPointer()),
        mShaders["horzBlurCS"]->GetBufferSize()
    };
    horzBlurPSO.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    ThrowIfFailed(d3dDevice->CreateComputePipelineState(&horzBlurPSO, IID_PPV_ARGS(&mPSOs["horzBlur"])));

    //
    //��ֱ����ģ���ġ�����PSO��
    //
    D3D12_COMPUTE_PIPELINE_STATE_DESC vertBlurPSO = {};
    vertBlurPSO.pRootSignature = mPostProcessRootSignature.Get();
    vertBlurPSO.CS =
    {
        reinterpret_cast<BYTE*>(mShaders["vertBlurCS"]->GetBufferPointer()),
        mShaders["vertBlurCS"]->GetBufferSize()
    };
    vertBlurPSO.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    ThrowIfFailed(d3dDevice->CreateComputePipelineState(&vertBlurPSO, IID_PPV_ARGS(&mPSOs["vertBlur"])));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC quadPatchPsoDesc;
    ZeroMemory(&quadPatchPsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
    quadPatchPsoDesc.InputLayout = { mtessInputLayout.data(), (UINT)mtessInputLayout.size() };
    quadPatchPsoDesc.pRootSignature = mRootSignature.Get();

    quadPatchPsoDesc.VS =
    { reinterpret_cast<BYTE*>(mShaders["tessVS"]->GetBufferPointer()),  mShaders["tessVS"]->GetBufferSize() };
    quadPatchPsoDesc.HS =
    { reinterpret_cast<BYTE*>(mShaders["tessHS"]->GetBufferPointer()), mShaders["tessHS"]->GetBufferSize() };
    quadPatchPsoDesc.DS =
    { reinterpret_cast<BYTE*>(mShaders["tessDS"]->GetBufferPointer()), mShaders["tessDS"]->GetBufferSize() };
    quadPatchPsoDesc.PS =
    { reinterpret_cast<BYTE*>(mShaders["tessPS"]->GetBufferPointer()), mShaders["tessPS"]->GetBufferSize() };

    quadPatchPsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    quadPatchPsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    quadPatchPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    quadPatchPsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    quadPatchPsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    quadPatchPsoDesc.SampleMask = UINT_MAX;	//0xffffffff,ȫ��������û������
    quadPatchPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;//Patch���͵�ͼԪ����
    quadPatchPsoDesc.NumRenderTargets = 1;
    quadPatchPsoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;	//��һ�����޷�������
    quadPatchPsoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    quadPatchPsoDesc.SampleDesc.Count = 1;	//��ʹ��4XMSAA
    quadPatchPsoDesc.SampleDesc.Quality = 0;	//��ʹ��4XMSAA
    ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&quadPatchPsoDesc, IID_PPV_ARGS(&mPSOs["subdivision"])));

    // ������Ч
    D3D12_GRAPHICS_PIPELINE_STATE_DESC fogquadPatchPsoDesc = quadPatchPsoDesc;
    fogquadPatchPsoDesc.PS =
    { reinterpret_cast<BYTE*>(mShaders["fogtessPS"]->GetBufferPointer()), mShaders["fogtessPS"]->GetBufferSize() };
    ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&fogquadPatchPsoDesc, IID_PPV_ARGS(&mPSOs["fogsubdivision"])));
}

void GameDevelopApp::BuildFrameResources()
{
    for (int i = 0; i < gNumFrameResources; ++i)
    {
        // FrameResource���캯��������wave�Ķ�����
        mFrameResources.push_back(std::make_unique<FrameResource>(d3dDevice.Get(), 1, (UINT)mAllRitems.size(),(UINT)mMaterials.size(), mWaves->VertexCount()));
    }
}

void GameDevelopApp::BuildMaterials()
{
    //����½�صĲ���
    auto grass = std::make_unique<Material>();
    grass->Name = "grass";
    grass->MatCBIndex = 0;
    // �������õ�SRV��ƫ�ƹ�ϵ��0Ϊ�ݵأ�1Ϊ��ˮ��2ΪBOX 
    grass->DiffuseSrvHeapIndex = 0;
    // ������ͼ����������ͼ�������ɫ��Ϊ�����䷴���ʼ��ɣ���˽������еķ������޸�Ϊ1������Ӱ�����巴����
    grass->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);//½�صķ����ʣ���ɫ��
    grass->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);//½�ص�R0
    grass->Roughness = 0.125f;//½�صĴֲڶȣ���һ����ģ�

    //�����ˮ�Ĳ���
    auto water = std::make_unique<Material>();
    water->Name = "water";
    water->MatCBIndex = 1;
    water->DiffuseSrvHeapIndex = 1;
    water->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    water->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);;//��ˮ��R0����Ϊû��͸���Ⱥ������ʣ����������0.1��
    water->Roughness = 0.0f;

    //����BOX�Ĳ���
    auto wood = std::make_unique<Material>();
    wood->Name = "wood";
    wood->MatCBIndex = 2;
    wood->DiffuseSrvHeapIndex = 2;
    wood->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);//ľͷ�ķ����ʣ���ɫ��
    wood->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);//ľͷ��R0
    wood->Roughness = 0.25f;//ľͷ�Ĵֲڶȣ���һ����ģ�

    auto wirefence = std::make_unique<Material>();
    wirefence->Name = "wirefence";
    wirefence->MatCBIndex = 3;
    wirefence->DiffuseSrvHeapIndex = 3;
    wirefence->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    wirefence->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
    wirefence->Roughness = 0.25f;

    //����treeBillboard�Ĳ���
    auto treeSprites = std::make_unique<Material>();
    treeSprites->Name = "treeSprites";
    treeSprites->MatCBIndex = 5;
    treeSprites->DiffuseSrvHeapIndex = 5;
    treeSprites->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    treeSprites->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
    treeSprites->Roughness = 0.125f;

    //�����ͷ�Ĳ���
    auto skullMat = std::make_unique<Material>();
    skullMat->Name = "skullMat";
    skullMat->MatCBIndex = 4;
    skullMat->DiffuseSrvHeapIndex = 4;
    skullMat->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    skullMat->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
    skullMat->Roughness = 0.3f;

    mMaterials["grass"] = std::move(grass);
    mMaterials["water"] = std::move(water);
    mMaterials["wood"] = std::move(wood);
    mMaterials["wirefence"] = std::move(wirefence);
    mMaterials["treeSprites"] = std::move(treeSprites);
    mMaterials["skullMat"] = std::move(skullMat);
}

void GameDevelopApp::BuildRenderItems()
{
    //// ����box��Ⱦ��
    //auto boxRitem = std::make_unique<RenderItem>();
    //// �����������
    //XMStoreFloat4x4(&boxRitem->World, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(0.0f, 0.5f, 0.0f));
    //// ������������ڳ��������е�����
    //boxRitem->ObjCBIndex = 0;//BOX�������ݣ�world������objConstantBuffer����0��
    //boxRitem->Geo = mGeometries["shapeGeo"].get();
    //boxRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    //boxRitem->IndexCount = boxRitem->Geo->DrawArgs["box"].IndexCount;
    //boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;
    //boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;
    //mAllRitems.push_back(std::move(boxRitem));

    //// ����grid��Ⱦ��
    //auto gridRitem = std::make_unique<RenderItem>();
    //gridRitem->World = MathHelper::Identity4x4();
    //gridRitem->ObjCBIndex = 1;
    //gridRitem->Geo = mGeometries["shapeGeo"].get();
    //gridRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    //gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
    //gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
    //gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
    //mAllRitems.push_back(std::move(gridRitem));

    //auto skullRitem = std::make_unique<RenderItem>();
    //XMStoreFloat4x4(&skullRitem->World, XMMatrixScaling(0.5f, 0.5f, 0.5f) * XMMatrixTranslation(0.0f, 1.0f, 0.0f));
    //skullRitem->ObjCBIndex = 2;
    //skullRitem->Geo = mGeometries["skullGeo"].get();
    //skullRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    //skullRitem->IndexCount = skullRitem->Geo->DrawArgs["skull"].IndexCount;
    //skullRitem->StartIndexLocation = skullRitem->Geo->DrawArgs["skull"].StartIndexLocation;
    //skullRitem->BaseVertexLocation = skullRitem->Geo->DrawArgs["skull"].BaseVertexLocation;
    //mAllRitems.push_back(std::move(skullRitem));

    //UINT fllowObjCBIndex = 3;//����ȥ�ļ����峣��������CB�е�������2��ʼ
    ////��Բ����Բ��ʵ��ģ�ʹ�����Ⱦ����
    //for (int i = 0; i < 5; ++i)
    //{
    //    auto leftCylRitem = std::make_unique<RenderItem>();
    //    auto rightCylRitem = std::make_unique<RenderItem>();
    //    auto leftSphereRitem = std::make_unique<RenderItem>();
    //    auto rightSphereRitem = std::make_unique<RenderItem>();

    //    XMMATRIX leftCylWorld = XMMatrixTranslation(-5.0f, 1.5f, -10.0f + i * 5.0f);
    //    XMMATRIX rightCylWorld = XMMatrixTranslation(+5.0f, 1.5f, -10.0f + i * 5.0f);

    //    XMMATRIX leftSphereWorld = XMMatrixTranslation(-5.0f, 3.5f, -10.0f + i * 5.0f);
    //    XMMATRIX rightSphereWorld = XMMatrixTranslation(+5.0f, 3.5f, -10.0f + i * 5.0f);

    //    //���5��Բ��
    //    XMStoreFloat4x4(&leftCylRitem->World, leftCylWorld);
    //    leftCylRitem->ObjCBIndex = fllowObjCBIndex++;
    //    leftCylRitem->Geo = mGeometries["shapeGeo"].get();
    //    leftCylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    //    leftCylRitem->IndexCount = leftCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
    //    leftCylRitem->StartIndexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
    //    leftCylRitem->BaseVertexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
    //    //�ұ�5��Բ��
    //    XMStoreFloat4x4(&rightCylRitem->World, rightCylWorld);
    //    rightCylRitem->ObjCBIndex = fllowObjCBIndex++;
    //    rightCylRitem->Geo = mGeometries["shapeGeo"].get();
    //    rightCylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    //    rightCylRitem->IndexCount = rightCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
    //    rightCylRitem->StartIndexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
    //    rightCylRitem->BaseVertexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
    //    //���5����
    //    XMStoreFloat4x4(&leftSphereRitem->World, leftSphereWorld);
    //    leftSphereRitem->ObjCBIndex = fllowObjCBIndex++;
    //    leftSphereRitem->Geo = mGeometries["shapeGeo"].get();
    //    leftSphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    //    leftSphereRitem->IndexCount = leftSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
    //    leftSphereRitem->StartIndexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
    //    leftSphereRitem->BaseVertexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;
    //    //�ұ�5����
    //    XMStoreFloat4x4(&rightSphereRitem->World, rightSphereWorld);
    //    rightSphereRitem->ObjCBIndex = fllowObjCBIndex++;
    //    rightSphereRitem->Geo = mGeometries["shapeGeo"].get();
    //    rightSphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    //    rightSphereRitem->IndexCount = rightSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
    //    rightSphereRitem->StartIndexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
    //    rightSphereRitem->BaseVertexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

    //    mAllRitems.push_back(std::move(leftCylRitem));
    //    mAllRitems.push_back(std::move(rightCylRitem));
    //    mAllRitems.push_back(std::move(leftSphereRitem));
    //    mAllRitems.push_back(std::move(rightSphereRitem));
    //}
    //// ��䴫�ݸ�pso������
    //for (auto& e : mAllRitems)
    //    mOpaqueRitems.push_back(e.get());

    //ɽ����Ⱦ��
    auto gridRitem = std::make_unique<RenderItem>();
    gridRitem->World = MathHelper::Identity4x4();
    XMStoreFloat4x4(&gridRitem->TexTransform, XMMatrixScaling(5.0f, 5.0f, 1.0f));
    gridRitem->ObjCBIndex = 1;//�������ݣ�world������objConstantBuffer����0��
    gridRitem->Mat = mMaterials["grass"].get();
    gridRitem->Geo = mGeometries["landGeo"].get();
    gridRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
    gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
    gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;

    mRitemLayer[(int)RenderLayer::Opaque].push_back(gridRitem.get());


    //������Ⱦ��
    auto wavesRitem = std::make_unique<RenderItem>();
    wavesRitem->World = MathHelper::Identity4x4();
    XMStoreFloat4x4(&wavesRitem->TexTransform, XMMatrixScaling(5.0f, 5.0f, 1.0f));
    wavesRitem->ObjCBIndex = 0;
    wavesRitem->Mat = mMaterials["water"].get();
    wavesRitem->Geo = mGeometries["waterGeo"].get();
    wavesRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    wavesRitem->IndexCount = wavesRitem->Geo->DrawArgs["grid"].IndexCount;
    wavesRitem->StartIndexLocation = wavesRitem->Geo->DrawArgs["grid"].StartIndexLocation;
    wavesRitem->BaseVertexLocation = wavesRitem->Geo->DrawArgs["grid"].BaseVertexLocation;

    mWavesRitem = wavesRitem.get();

    mRitemLayer[(int)RenderLayer::Transparent].push_back(wavesRitem.get());

    //BOX��Ⱦ��
    auto boxRitem = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&boxRitem->World, XMMatrixTranslation(-15.0f, 2.0f, 15.0f));
    boxRitem->TexTransform = MathHelper::Identity4x4();
    boxRitem->ObjCBIndex = 2;//BOX�ĳ������ݣ�world������objConstantBuffer����2��
    boxRitem->Mat = mMaterials["wood"].get();
    boxRitem->Geo = mGeometries["boxGeo"].get();	//��ֵ��ǰ�ġ�BOX��MeshGeoetry
    boxRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    boxRitem->IndexCount = boxRitem->Geo->DrawArgs["box"].IndexCount;
    boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;
    boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;

    mRitemLayer[(int)RenderLayer::Opaque].push_back(boxRitem.get());

    //�����Ⱦ��
    auto fenceRitem = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&fenceRitem->World, XMMatrixTranslation(3.0f, 2.0f, -9.0f));
    fenceRitem->TexTransform = MathHelper::Identity4x4();
    fenceRitem->ObjCBIndex = 3;
    fenceRitem->Mat = mMaterials["wirefence"].get();
    fenceRitem->Geo = mGeometries["boxGeo"].get();
    fenceRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    fenceRitem->IndexCount = fenceRitem->Geo->DrawArgs["box"].IndexCount;
    fenceRitem->StartIndexLocation = fenceRitem->Geo->DrawArgs["box"].StartIndexLocation;
    fenceRitem->BaseVertexLocation = fenceRitem->Geo->DrawArgs["box"].BaseVertexLocation;

    mRitemLayer[(int)RenderLayer::AlphaTested].push_back(fenceRitem.get());

    //�������Ⱦ��
    auto treeSpritesRitem = std::make_unique<RenderItem>();
    treeSpritesRitem->World = MathHelper::Identity4x4();
    treeSpritesRitem->ObjCBIndex = 4;
    treeSpritesRitem->Mat = mMaterials["treeSprites"].get();
    treeSpritesRitem->Geo = mGeometries["treeSpritesGeo"].get();//��ֵ��ǰ�ġ�treeSpritesGeo��MeshGeoetry
    treeSpritesRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;//������ɫ��ֻ���ܵ��б�����б�
    treeSpritesRitem->IndexCount = treeSpritesRitem->Geo->DrawArgs["points"].IndexCount;
    treeSpritesRitem->StartIndexLocation = treeSpritesRitem->Geo->DrawArgs["points"].StartIndexLocation;
    treeSpritesRitem->BaseVertexLocation = treeSpritesRitem->Geo->DrawArgs["points"].BaseVertexLocation;

    mRitemLayer[(int)RenderLayer::AlphaTestedTreeSprites].push_back(treeSpritesRitem.get());

    //20������Ⱦ��
    auto geoSphereRitem = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&geoSphereRitem->World, XMMatrixTranslation(0.0f, 5.0f, 0.0f)* XMMatrixScaling(5.0f, 5.0f, 5.0f));
    geoSphereRitem->ObjCBIndex = 5;
    geoSphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    geoSphereRitem->Geo = mGeometries["geoSphereGeo"].get();
    geoSphereRitem->Mat = mMaterials["skullMat"].get();//�����ͷ���ʸ�����ͷ
    geoSphereRitem->IndexCount = geoSphereRitem->Geo->DrawArgs["geoSphere"].IndexCount;
    geoSphereRitem->BaseVertexLocation = geoSphereRitem->Geo->DrawArgs["geoSphere"].BaseVertexLocation;
    geoSphereRitem->StartIndexLocation = geoSphereRitem->Geo->DrawArgs["geoSphere"].StartIndexLocation;
    mRitemLayer[(int)RenderLayer::geoSphere].push_back(geoSphereRitem.get());
    // ��Ƭ��Ⱦ��
    auto quadPatchRitem = std::make_unique<RenderItem>();
    quadPatchRitem->World = MathHelper::Identity4x4();
    quadPatchRitem->TexTransform = MathHelper::Identity4x4();
    quadPatchRitem->ObjCBIndex = 6;
    quadPatchRitem->Mat = mMaterials["skullMat"].get();
    quadPatchRitem->Geo = mGeometries["quadpatchGeo"].get();
    quadPatchRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_16_CONTROL_POINT_PATCHLIST;//16�����Ƶ��patch�б�
    quadPatchRitem->IndexCount = quadPatchRitem->Geo->DrawArgs["quadpatch"].IndexCount;
    quadPatchRitem->StartIndexLocation = quadPatchRitem->Geo->DrawArgs["quadpatch"].StartIndexLocation;
    quadPatchRitem->BaseVertexLocation = quadPatchRitem->Geo->DrawArgs["quadpatch"].BaseVertexLocation;
    mRitemLayer[(int)RenderLayer::Tess].push_back(quadPatchRitem.get());

    mAllRitems.push_back(std::move(wavesRitem));
    mAllRitems.push_back(std::move(gridRitem));
    mAllRitems.push_back(std::move(boxRitem));
    mAllRitems.push_back(std::move(fenceRitem));
    mAllRitems.push_back(std::move(treeSpritesRitem));
    mAllRitems.push_back(std::move(geoSphereRitem));
    mAllRitems.push_back(std::move(quadPatchRitem));
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