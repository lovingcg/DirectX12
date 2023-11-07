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

    mCamera.SetPosition(mx, my, mz);

    LoadTextures();
    BuildRootSignature();
    BuildDescriptorHeaps();
    BuildShadersAndInputLayout();
    //BuildShapeGeometry();
    BuildSkullGeometry();
    BuildMaterials();//��������
    // BuildRenderItems()����������CreateConstantBufferView()����֮ǰ����Ȼ�ò���allRitems.size()��ֵ
    BuildRenderItems();
    BuildFrameResources();
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

    //����SRV��������
    ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };// ע������֮���������飬����Ϊ�����ܰ���SRV��UAV������������ֻ�õ���SRV
    cmdList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    // �󶨴˳�����ʹ�õ����в��ʡ����ڽṹ���������������ƹ�������Ϊ����������
    auto matBuffer = mCurrFrameResource->MaterialBuffer->Resource();
    cmdList->SetGraphicsRootShaderResourceView(1, matBuffer->GetGPUVirtualAddress());
    // ��passCB��������
    auto passCB = mCurrFrameResource->PassCB->Resource();
    // �����1ָ���Ǹ��������� ���ǼĴ����ۺ�
    cmdList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());

    //��������������������Դ����ˮ�߰�(��Ϊֻ��һ�Σ����Բ���Ҫ����ַƫ��)
    cmdList->SetGraphicsRootDescriptorTable(3, mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

    // objCB������DrawRenderItems������

    //�ֱ�����PSO�����ƶ�Ӧ��Ⱦ��
    //���Ʋ�͸��������Ⱦ��
    if (!fog)
    {
        cmdList->SetPipelineState(mPSOs["unfogopaque"].Get());
        DrawRenderItems(cmdList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);
    }
    else
    {
        cmdList->SetPipelineState(mPSOs["opaque"].Get());
        DrawRenderItems(cmdList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);
    }

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
                mx = 0.0f;
                my = 2.0f;
                mz = -15.0f;
                fov = 0.25f;
                mCamera.SetPosition(mx, my, mz);
            }

            //ImGui::Text("CameraX: %.2f degrees", mx);// ��ʾ���֣�������������Ŀؼ� 
            //ImGui::SliderFloat("##1", &mx, -15.00f, 15.00f, ""); // ����ʾ�ؼ����⣬��ʹ��##�������ǩ�ظ�  ���ַ���������ʾ����

            //ImGui::Text("CameraY: %.2f degrees", my);// ��ʾ���֣�������������Ŀؼ� 
            //ImGui::SliderFloat("##2", &my, -10.00f, 10.00f, "");

            //ImGui::Text("CameraZ: %.2f degrees", mz);// ��ʾ���֣�������������Ŀؼ� 
            //ImGui::SliderFloat("##3", &mz, -100.00f, 0.00f, "");

            //mCamera.SetPosition(mx, my, mz);

            ImGui::Text("Camera Position: (%.1f, %.1f, %.1f, 1.0)", mCamera.GetPosition3f().x, mCamera.GetPosition3f().y, mCamera.GetPosition3f().z);
            
            ImGui::Text("FOV: %.2f degrees", XMConvertToDegrees(fov * MathHelper::Pi));
            ImGui::SliderFloat("##4", &fov, 0.1f, 0.5f, "");
            mCamera.SetLens(fov * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);

            ImGui::ColorEdit3("Clear Color", clearColor);
        }
        ImGui::Checkbox("Wiremode", &is_wiremode);
        ImGui::Checkbox("Sunmove", &is_sunmove);

        if (is_sunmove)
        {
            OnKeyboardInput(gt);
        }

        ImGui::Checkbox("CartoonShader", &is_ctshader);
        ImGui::Checkbox("FrustumCulling", &mFrustumCullingEnabled);
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
        mCamera.Pitch(dy);
        mCamera.RotateY(dx);
    }
    else if ((btnState & MK_RBUTTON) != 0)// ������Ҽ�����״̬
    {
        //�������ƶ����뻻�����ת�Ƕȣ�0.25Ϊ������ֵ
        float dx = XMConvertToRadians(static_cast<float>(mLastMousePos.x - x) * 0.25f);

        mCamera.Roll(dx);
    }
    // ����ǰ������긳ֵ������һ��������ꡱ��Ϊ��һ���������ṩ��ǰֵ
    mLastMousePos.x = x;
    mLastMousePos.y = y;
}

void GameDevelopApp::OnResize()
{
    D3DApp::OnResize();

    mCamera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);

    BoundingFrustum::CreateFromMatrix(mCamFrustum, mCamera.GetProj());
}

// ÿһ֡�Ĳ���
void GameDevelopApp::Update()
{ 
    OnKeyboardInputMove(gt);

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

    //UpdateObjectCBs(gt);
    UpdateInstanceData(gt);
    UpdateMaterialCBs(gt);
    UpdateMainPassCB(gt);
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

    if (GetAsyncKeyState('W') & 0x8000)
        mCamera.Walk(20.0f * dt);

    if (GetAsyncKeyState('S') & 0x8000)
        mCamera.Walk(-20.0f * dt);

    if (GetAsyncKeyState('A') & 0x8000)
        mCamera.Strafe(-20.0f * dt);

    if (GetAsyncKeyState('D') & 0x8000)
        mCamera.Strafe(20.0f * dt);

    if (GetAsyncKeyState('Q') & 0x8000)
        mCamera.UpDown(20.0f * dt);

    if (GetAsyncKeyState('E') & 0x8000)
        mCamera.UpDown(-20.0f * dt);

    mCamera.UpdateViewMatrix();
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

//void GameDevelopApp::UpdateObjectCBs(const _GameTimer::GameTimer& gt)
//{
//    auto currObjectCB = mCurrFrameResource->ObjectCB.get();
//    for (auto& e : mAllRitems)
//    {
//        //ֻ���ڳ�����������ʱ�Ÿ���cbuffer���ݡ�
//        //����Ҫ��֡��Դ���и��١�
//        if (e->NumFramesDirty > 0)
//        {
//            XMMATRIX world = XMLoadFloat4x4(&e->World);
//            // ��texTransform������ObjCB���������ݾʹ����˳������������ˣ������մ�����ɫ��
//            XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);
//
//            ObjectConstants objConstants;
//            XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
//            XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));
//            objConstants.MaterialIndex = e->Mat->MatCBIndex;
//            //�����ݿ�����GPU����
//            //e->ObjCBIndex��֤��������뵥��������һһ��Ӧ
//            currObjectCB->CopyData(e->ObjCBIndex, objConstants);
//
//            //��һ��FrameResourceҲ��Ҫ���¡�
//            e->NumFramesDirty--;
//        }
//    }
//}

void GameDevelopApp::UpdateInstanceData(const _GameTimer::GameTimer& gt)
{
    XMMATRIX view = mCamera.GetView();//WorldToView�ı任����
    XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);//ViewToWorld�ı任����
    // �����������(currInstanceSB)������GPU
    auto currInstanceBuffer = mCurrFrameResource->InstanceBuffer.get();
    for (auto& e : mAllRitems)
    {
        //if (e->NumFramesDirty > 0)//֡��Դ�����ж�
        //{
            const auto& instanceData = e->Instances;//ÿ����Ⱦ���ʵ������
            //������Ⱦ����ÿ��ʵ�������ݣ������Ǵ���GPU
            int visibleInstanceCount = 0;

            for (UINT i = 0; i < (UINT)instanceData.size(); ++i)
            {
                XMMATRIX world = XMLoadFloat4x4(&instanceData[i].World);
                XMMATRIX texTransform = XMLoadFloat4x4(&instanceData[i].TexTransform);

                XMMATRIX invWorld = XMMatrixInverse(&XMMatrixDeterminant(world), world);
                //����ÿ��ʵ����viewToLocal����
                XMMATRIX viewToLocal = XMMatrixMultiply(invView, invWorld);

                //������׶��
                BoundingFrustum localSpaceFrustum;
                localSpaceFrustum.CreateFromMatrix(localSpaceFrustum, mCamera.GetProj());
                // ����׶��ӹ۲�ռ�任���ֲ��ռ�
                mCamFrustum.Transform(localSpaceFrustum, viewToLocal);
                //�����׶���ʵ����AABB���ཻ�����ϴ�ʵ������
                if ((localSpaceFrustum.Contains(e->Bounds) != DirectX::DISJOINT) || (mFrustumCullingEnabled == false))
                {
                    InstanceData data;//ÿ��ʵ��������
                    XMStoreFloat4x4(&data.World, XMMatrixTranspose(world));
                    XMStoreFloat4x4(&data.TexTransform, XMMatrixTranspose(texTransform));
                    // �����ʳ����������е�matCBIndex��ֵ��������instanceData�е�materialIndex�ֶ�
                    data.MaterialIndex = instanceData[i].MaterialIndex;
                    //��ʵ������һ�����ؿ�����GPU����
                    currInstanceBuffer->CopyData(visibleInstanceCount++, data);
                }
            }
            e->InstanceCount = visibleInstanceCount;//ʵ��������ֵ
            //e->NumFramesDirty--;//��һ��֡��Դ

            // ��ʾ��ǰ��Ļ��ʵ������
            std::wostringstream outs;
            outs.precision(6);
            outs << L"Instancing and Culling Demo" <<
                L"    " << e->InstanceCount <<
                L" objects visible out of " << e->Instances.size();
            mMainWndCaption = outs.str();
        //}
    }
}

//ʵ��˼·��objConstants���ƣ�����materials�����б���ȡ��Materialָ�룬
//�����丳ֵ�������ṹ���ж�ӦԪ�أ�����ʹ��CopyData���������ݴ���GPU��
//���ǵ�numFramesDirty--����Ϊ������������ÿ��֡��Դ���ܵõ�����
void GameDevelopApp::UpdateMaterialCBs(const _GameTimer::GameTimer& gt)
{
    auto currMaterialCB = mCurrFrameResource->MaterialBuffer.get();
    for (auto& e : mMaterials)
    {
        Material* mat = e.second.get();//��ü�ֵ�Ե�ֵ����Materialָ�루����ָ��ת��ָͨ�룩
        if (mat->NumFramesDirty > 0)
        {
            XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

            //������Ĳ������Դ��������ṹ���е�Ԫ��
            MaterialData matConstants;
            matConstants.DiffuseAlbedo = mat->DiffuseAlbedo;
            matConstants.FresnelR0 = mat->FresnelR0;
            matConstants.Roughness = mat->Roughness;
            XMStoreFloat4x4(&matConstants.MatTransform, XMMatrixTranspose(matTransform));
            // ��������������Ҳ����GPU
            matConstants.DiffuseMapIndex = mat->DiffuseSrvHeapIndex;

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
    XMMATRIX view = mCamera.GetView();
    //XMMATRIX proj = XMLoadFloat4x4(&mProj);
    XMMATRIX proj = mCamera.GetProj();
    XMMATRIX viewProj = XMMatrixMultiply(view, proj);

    XMStoreFloat4x4(&mMainPassCB.viewProj, XMMatrixTranspose(viewProj));

    // ��ֵlight����
    mMainPassCB.EyePosW = mCamera.GetPosition3f();
    mMainPassCB.AmbientLight = { 0.25f,0.25f,0.35f,1.0f };
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

void GameDevelopApp::BuildShapeGeometry()
{
    ProceduralGeometry proceGeo;
    //float width, float height, float depth, uint32 numSubdivisions
    ProceduralGeometry::MeshData box = proceGeo.CreateBox(1.0f, 1.0f, 1.0f, 3);
    //float width, float depth, uint32 m, uint32 n
    ProceduralGeometry::MeshData grid = proceGeo.CreateGrid(20.0f, 30.0f, 60, 40);
    //float radius, uint32 sliceCount, uint32 stackCount �뾶����Ƭ�������ѵ�����
    ProceduralGeometry::MeshData sphere = proceGeo.CreateSphere(0.5f, 20, 20);
    //float bottomRadius, float topRadius, float height, uint32 sliceCount, uint32 stackCount
    ProceduralGeometry::MeshData cylinder = proceGeo.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);

    //���㵥�������嶥�����ܶ��������е�ƫ����,˳��Ϊ��box��grid��sphere��cylinder
    UINT boxVertexOffset = 0;
    UINT gridVertexOffset = (UINT)box.Vertices.size();
    UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
    UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();

    //���㵥�������������������������е�ƫ����,˳��Ϊ��box��grid��sphere��cylinder
    UINT boxIndexOffset = 0;
    UINT gridIndexOffset = (UINT)box.Indices32.size();
    UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
    UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();

    SubmeshGeometry boxSubmesh;
    boxSubmesh.IndexCount = (UINT)box.Indices32.size();
    boxSubmesh.BaseVertexLocation = boxVertexOffset;
    boxSubmesh.StartIndexLocation = boxIndexOffset;

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

    //����һ���ܵĶ��㻺��vertices������4��������Ķ������ݴ�������
    auto totalVertexCount = box.Vertices.size() + grid.Vertices.size() + sphere.Vertices.size() + cylinder.Vertices.size();

    std::vector<Vertex> vertices(totalVertexCount);
    UINT k = 0;
    for (size_t i = 0; i < box.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = box.Vertices[i].Position;
        vertices[k].Normal = box.Vertices[i].Normal;
        vertices[k].TexC = box.Vertices[i].TexC;
    }

    for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = grid.Vertices[i].Position;
        vertices[k].Normal = grid.Vertices[i].Normal;
        vertices[k].TexC = grid.Vertices[i].TexC;
    }

    for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = sphere.Vertices[i].Position;
        vertices[k].Normal = sphere.Vertices[i].Normal;
        vertices[k].TexC = sphere.Vertices[i].TexC;
    }

    for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = cylinder.Vertices[i].Position;
        vertices[k].Normal = cylinder.Vertices[i].Normal;
        vertices[k].TexC = cylinder.Vertices[i].TexC;
    }
    // ͬ����һ���ܵ���������indices������4����������������ݴ�������
    std::vector<std::uint16_t> indices;
    // ��ָ��λ��locǰ��������[start, end)������Ԫ�� 
    indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
    indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
    indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
    indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));

    const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

    // ����MeshGeometryʵ��
    geo = std::make_unique<MeshGeometry>();
    geo->Name = "shapeGeo";
    // �Ѷ���/���������ϴ���GPU
    ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
    CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);
    // ��������Ĭ�϶� CPU->GPU
    geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(d3dDevice.Get(), cmdList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);
    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(d3dDevice.Get(), cmdList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

    geo->VertexByteStride = sizeof(Vertex);
    geo->VertexBufferByteSize = vbByteSize;
    geo->IndexFormat = DXGI_FORMAT_R16_UINT;
    geo->IndexBufferByteSize = ibByteSize;

    // ������װ�õ�4���������SubmeshGeometry����ֵ������ӳ���
    geo->DrawArgs["box"] = boxSubmesh;
    geo->DrawArgs["grid"] = gridSubmesh;
    geo->DrawArgs["sphere"] = sphereSubmesh;
    geo->DrawArgs["cylinder"] = cylinderSubmesh;
    // unique_ptrͨ��moveת����Դ
    mGeometries[geo->Name] = std::move(geo);
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

    //��ʼ��AABB��vMin��vMax,infinityΪ32λfloat��������
    XMFLOAT3 vMinf3(+MathHelper::Infinity, +MathHelper::Infinity, +MathHelper::Infinity);
    XMFLOAT3 vMaxf3(-MathHelper::Infinity, -MathHelper::Infinity, -MathHelper::Infinity);
    //ת����XMVECTOR
    XMVECTOR vMin = XMLoadFloat3(&vMinf3);
    XMVECTOR vMax = XMLoadFloat3(&vMaxf3);

    std::vector<Vertex> vertices(vcount);
    //�����б�ֵ
    for (UINT i = 0; i < vcount; ++i)
    {
        fin >> vertices[i].Pos.x >> vertices[i].Pos.y >> vertices[i].Pos.z;
        fin >> vertices[i].Normal.x >> vertices[i].Normal.y >> vertices[i].Normal.z;
        //vertices[i].TexC = { 0.0f, 0.0f };

        XMVECTOR P = XMLoadFloat3(&vertices[i].Pos);

        vMin = XMVectorMin(vMin, P);
        vMax = XMVectorMax(vMax, P);

        // ����ͶӰ����λ�����ϲ�����������������
        XMFLOAT3 spherePos;
        XMStoreFloat3(&spherePos, XMVector3Normalize(P));

        float theta = atan2f(spherePos.z, spherePos.x);

        // Put in [0, 2pi].
        if (theta < 0.0f)
            theta += XM_2PI;

        float phi = acosf(spherePos.y);

        float u = theta / (2.0f * XM_PI);
        float v = phi / XM_PI;

        vertices[i].TexC = { u, v };
    }

    //��AABBתΪCE���壨����center����չ����extents��
    DirectX::BoundingBox bounds;
    XMStoreFloat3(&bounds.Center, 0.5f * (vMin + vMax));
    XMStoreFloat3(&bounds.Extents, 0.5f * (vMax - vMin));

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
    submesh.Bounds = bounds;

    geo->DrawArgs["skull"] = submesh;

    mGeometries[geo->Name] = std::move(geo);
}

//��ͬRTV��DSV������������Ҳ��Ҫ��������ָ�������������ԡ�����������Ҫ������������š�
//���ԣ��ȴ���CBV�ѡ�
void GameDevelopApp::BuildDescriptorHeaps()
{
    //����SRV��
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = 7;//�����µ������ø�
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    srvHeapDesc.NodeMask = 0;
    ThrowIfFailed(d3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

    // ��֮ǰ����õ�������Դת����comptr������
    auto bricksTex = mTextures["bricksTex"]->Resource;
    auto stoneTex = mTextures["stoneTex"]->Resource;
    auto tileTex = mTextures["tileTex"]->Resource;
    auto crateTex = mTextures["crateTex"]->Resource;
    auto iceTex = mTextures["iceTex"]->Resource;
    auto grassTex = mTextures["grassTex"]->Resource;
    auto defaultTex = mTextures["defaultTex"]->Resource;

    //SRV������SRV�ľ��
    CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

    //SRV�����ṹ��
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;//���������˳�򲻸ı�
    srvDesc.Format = bricksTex->GetDesc().Format;//��ͼ��Ĭ�ϸ�ʽ
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;//2D��ͼ
    srvDesc.Texture2D.MostDetailedMip = 0;//ϸ�����꾡��mipmap�㼶Ϊ0
    srvDesc.Texture2D.MipLevels = bricksTex->GetDesc().MipLevels;//mipmap�㼶����
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;//�ɷ��ʵ�mipmap��С�㼶��Ϊ0
    d3dDevice->CreateShaderResourceView(bricksTex.Get(), &srvDesc, hDescriptor);

    //SRV������SRV�ľ��,����ƫ��һ��SRV
    hDescriptor.Offset(1, mCbvSrvDescriptorSize);
    //SRV�����ṹ���޸�
    srvDesc.Format = stoneTex->GetDesc().Format;//��ͼ��Ĭ�ϸ�ʽ
    srvDesc.Texture2D.MipLevels = stoneTex->GetDesc().MipLevels;//mipmap�㼶����
    d3dDevice->CreateShaderResourceView(stoneTex.Get(), &srvDesc, hDescriptor);

    //SRV������SRV�ľ��,����ƫ��һ��SRV
    hDescriptor.Offset(1, mCbvSrvDescriptorSize);
    //SRV�����ṹ���޸�
    srvDesc.Format = tileTex->GetDesc().Format;//��ͼ��Ĭ�ϸ�ʽ
    srvDesc.Texture2D.MipLevels = tileTex->GetDesc().MipLevels;//mipmap�㼶����
    d3dDevice->CreateShaderResourceView(tileTex.Get(), &srvDesc, hDescriptor);

    //SRV������SRV�ľ��,����ƫ��һ��SRV
    hDescriptor.Offset(1, mCbvSrvDescriptorSize);
    //SRV�����ṹ���޸�
    srvDesc.Format = crateTex->GetDesc().Format;//��ͼ��Ĭ�ϸ�ʽ
    srvDesc.Texture2D.MipLevels = crateTex->GetDesc().MipLevels;//mipmap�㼶����
    d3dDevice->CreateShaderResourceView(crateTex.Get(), &srvDesc, hDescriptor);

    hDescriptor.Offset(1, mCbvSrvDescriptorSize);
    srvDesc.Format = iceTex->GetDesc().Format;
    srvDesc.Texture2D.MipLevels = iceTex->GetDesc().MipLevels;
    d3dDevice->CreateShaderResourceView(iceTex.Get(), &srvDesc, hDescriptor);

    hDescriptor.Offset(1, mCbvSrvDescriptorSize);
    srvDesc.Format = grassTex->GetDesc().Format;
    srvDesc.Texture2D.MipLevels = grassTex->GetDesc().MipLevels;
    d3dDevice->CreateShaderResourceView(grassTex.Get(), &srvDesc, hDescriptor);

    hDescriptor.Offset(1, mCbvSrvDescriptorSize);
    srvDesc.Format = defaultTex->GetDesc().Format;
    srvDesc.Texture2D.MipLevels = defaultTex->GetDesc().MipLevels;
    d3dDevice->CreateShaderResourceView(defaultTex.Get(), &srvDesc, hDescriptor);
}

void GameDevelopApp::LoadTextures()
{
    /*שǽ����*/
    auto bricksTex = std::make_unique<Texture>();
    bricksTex->Name = "bricksTex";
    bricksTex->Filename = L"Textures\\bricks.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(d3dDevice.Get(),
        cmdList.Get(), bricksTex->Filename.c_str(),
        bricksTex->Resource, bricksTex->UploadHeap));
    /*ʯͷ����*/
    auto stoneTex = std::make_unique<Texture>();
    stoneTex->Name = "stoneTex";
    stoneTex->Filename = L"Textures\\stone.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(d3dDevice.Get(),
        cmdList.Get(), stoneTex->Filename.c_str(),
        stoneTex->Resource, stoneTex->UploadHeap));
    /*�ذ�����*/
    auto tileTex = std::make_unique<Texture>();
    tileTex->Name = "tileTex";
    tileTex->Filename = L"Textures\\tile.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(d3dDevice.Get(),
        cmdList.Get(), tileTex->Filename.c_str(),
        tileTex->Resource, tileTex->UploadHeap));
    /*��������*/
    auto crateTex = std::make_unique<Texture>();
    crateTex->Name = "crateTex";
    crateTex->Filename = L"Textures\\WoodCrate02.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(d3dDevice.Get(),
        cmdList.Get(), crateTex->Filename.c_str(),
        crateTex->Resource, crateTex->UploadHeap));
    /*��������*/
    auto iceTex = std::make_unique<Texture>();
    iceTex->Name = "iceTex";
    iceTex->Filename = L"Textures\\ice.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(d3dDevice.Get(),
        cmdList.Get(), iceTex->Filename.c_str(),
        iceTex->Resource, iceTex->UploadHeap));
    /*�ݵ�����*/
    auto grassTex = std::make_unique<Texture>();
    grassTex->Name = "grassTex";
    grassTex->Filename = L"Textures\\grass.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(d3dDevice.Get(),
        cmdList.Get(), grassTex->Filename.c_str(),
        grassTex->Resource, grassTex->UploadHeap));
    /*��������*/
    auto defaultTex = std::make_unique<Texture>();
    defaultTex->Name = "defaultTex";
    defaultTex->Filename = L"Textures\\white1x1.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(d3dDevice.Get(),
        cmdList.Get(), defaultTex->Filename.c_str(),
        defaultTex->Resource, defaultTex->UploadHeap));

    mTextures[bricksTex->Name] = std::move(bricksTex);
    mTextures[stoneTex->Name] = std::move(stoneTex);
    mTextures[tileTex->Name] = std::move(tileTex);
    mTextures[crateTex->Name] = std::move(crateTex);
    mTextures[iceTex->Name] = std::move(iceTex);
    mTextures[grassTex->Name] = std::move(grassTex);
    mTextures[defaultTex->Name] = std::move(defaultTex);
}

//��ǩ�������������Ⱦ���߻���˵Shader������ִ�д�����Ҫ�ĸ�����Դ��ʲô���ķ�ʽ�����Լ�������ڴ桢�Դ��в���
//��ǩ����ʵ���ǽ���ɫ����Ҫ�õ������ݰ󶨵���Ӧ�ļĴ������ϣ�����ɫ�����ʡ�
// ��Ҫָ������GPU�϶�Ӧ�ļĴ���
//��ǩ����Ϊ������������������������������ʹ��������������ʼ����ǩ����

//��ǩ�������ǽ��������ݰ����Ĵ����ۣ�����ɫ��������ʡ�
//��Ϊ����������2���������ݽṹ�壬����Ҫ����2��Ԫ�صĸ���������2��CBV������2���Ĵ����ۡ�
// ��ʵ���������󶨵�SRV�ϣ�StructuredBuffer��ʹ��SRV��
void GameDevelopApp::BuildRootSignature()
{
    //�����ɵ���CBV����ɵ���������
    CD3DX12_DESCRIPTOR_RANGE texTable;
    texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,// ����������
        7,// ����������������������
        0);// ���������󶨵ļĴ����ۺ�

    // ������������������������������������
    CD3DX12_ROOT_PARAMETER slotRootParameter[4];
    
    // ������������ �ô�������Ҫ����CBV�ѣ�����ֱ�����ø�����������ָʾҪ�󶨵���Դ
    // 0 1 2��ʾ�Ĵ����ۺ�
    // ������ʾ������Ƶ�����Ƶ������
    // ʵ���ṹ�����棺(t0, space1)
    slotRootParameter[0].InitAsShaderResourceView(0, 1);
    // ���ʽṹ�����棺(t1, space1)
    slotRootParameter[1].InitAsShaderResourceView(1, 1);
    // ��Pass���棺(b0)
    slotRootParameter[2].InitAsConstantBufferView(0);
    // �������飺(t0, space0)
    slotRootParameter[3].InitAsDescriptorTable(1, // Range����
        &texTable,// Rangeָ��
        D3D12_SHADER_VISIBILITY_PIXEL);// ����Դֻ��������ɫ���ɶ�
    
    // ���˲��ʳ�������
    //slotRootParameter[3].InitAsConstantBufferView(2);// MaterialConstants

    //�ڸ�ǩ�������У����뾲̬�����������������ǩ���󶨣�Ϊ���մ�����ɫ����׼��
    auto staticSamplers = GetStaticSamplers();

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
    //const D3D_SHADER_MACRO alphaTestDefines[] =
    //{
    //    "ALPHA_TEST", "1",
    //    "FOG", "1",
    //    NULL, NULL
    //};

    const D3D_SHADER_MACRO unfogdefines[] =
    {
        NULL, NULL
    };
    // D3D_SHADER_MACRO�ṹ�������ֺͶ���
    /*const D3D_SHADER_MACRO unfogalphaTestDefines[] =
    {
        "ALPHA_TEST", "1",
        NULL, NULL
    };*/

    // ��shader�����ֳ���3�࣬һ���Ƕ��㺯����standardVS����һ���ǲ�͸����ƬԪ������opaquePS����
    // ����һ����alphaTest��ƬԪ������alphaTestPS��
    // �Ĵ���Space���÷�ֻ��ShaderModel5.1�����ϲ���ʹ�ã����Խ�ShaderModel�ĳ�5.1
    mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\color.hlsl", nullptr, "VS", "vs_5_1");
    mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\color.hlsl", defines, "PS", "ps_5_1");

    //mShaders["alphaTestedPS"] = d3dUtil::CompileShader(L"Shaders\\color.hlsl", alphaTestDefines, "PS", "ps_5_0");

    mShaders["unfogopaquePS"] = d3dUtil::CompileShader(L"Shaders\\color.hlsl", unfogdefines, "PS", "ps_5_1");
    //mShaders["unfogalphaTestedPS"] = d3dUtil::CompileShader(L"Shaders\\color.hlsl", unfogalphaTestDefines, "PS", "ps_5_0");

    mInputLayout =
    {
        //0��ʾ�Ĵ����ۺ�Ϊ0
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
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

    ///PSO for opaque wireframe objects.
    D3D12_GRAPHICS_PIPELINE_STATE_DESC WireframePsoDesc = opaquePsoDesc;
    WireframePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&WireframePsoDesc, IID_PPV_ARGS(&mPSOs["wireframe"])));
}

void GameDevelopApp::BuildFrameResources()
{
    for (int i = 0; i < gNumFrameResources; ++i)
    {
        // ������BuildFrameResources�����У���ȷ��д��passCB������Ϊ2
        mFrameResources.push_back(std::make_unique<FrameResource>(d3dDevice.Get(), 1, mInstanceCount,(UINT)mMaterials.size()));
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
    bricks->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
    bricks->Roughness = 0.1f;

    //����ʯͷ�Ĳ���
    auto stone0 = std::make_unique<Material>();
    stone0->Name = "stone0";
    stone0->MatCBIndex = 1;
    stone0->DiffuseSrvHeapIndex = 1;
    stone0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    stone0->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
    stone0->Roughness = 0.3f;

    //����ذ�Ĳ���
    auto tile0 = std::make_unique<Material>();
    tile0->Name = "tile0";
    tile0->MatCBIndex = 2;
    tile0->DiffuseSrvHeapIndex = 2;
    tile0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    tile0->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
    tile0->Roughness = 0.3f;
    
    //�������ӵĲ���
    auto crate0 = std::make_unique<Material>();
    crate0->Name = "crate0";
    crate0->MatCBIndex = 3;
    crate0->DiffuseSrvHeapIndex = 3;
    crate0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    crate0->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
    crate0->Roughness = 0.2f;

    auto ice0 = std::make_unique<Material>();
    ice0->Name = "ice0";
    ice0->MatCBIndex = 4;
    ice0->DiffuseSrvHeapIndex = 4;
    ice0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    ice0->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
    ice0->Roughness = 0.0f;

    auto grass0 = std::make_unique<Material>();
    grass0->Name = "grass0";
    grass0->MatCBIndex = 5;
    grass0->DiffuseSrvHeapIndex = 5;
    grass0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    grass0->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
    grass0->Roughness = 0.2f;

    auto skullMat = std::make_unique<Material>();
    skullMat->Name = "skullMat";
    skullMat->MatCBIndex = 6;
    skullMat->DiffuseSrvHeapIndex = 6;
    skullMat->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    skullMat->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
    skullMat->Roughness = 0.5f;

    mMaterials["bricks"] = std::move(bricks);
    mMaterials["stone0"] = std::move(stone0);
    mMaterials["tile0"] = std::move(tile0);
    mMaterials["crate0"] = std::move(crate0);
    mMaterials["ice0"] = std::move(ice0);
    mMaterials["grass0"] = std::move(grass0);
    mMaterials["skullMat"] = std::move(skullMat);
}

void GameDevelopApp::BuildRenderItems()
{
    //// ������Ⱦ��
    //auto boxRitem = std::make_unique<RenderItem>();
    ///*XMStoreFloat4x4(&boxRitem->World, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(0.0f, 1.0f, 0.0f));
    //XMStoreFloat4x4(&boxRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));*/
    //boxRitem->ObjCBIndex = 0;
    //boxRitem->Mat = mMaterials["crate0"].get();
    //boxRitem->Geo = mGeometries["shapeGeo"].get();
    //boxRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    //boxRitem->InstanceCount = 1;//ʵ������
    //boxRitem->IndexCount = boxRitem->Geo->DrawArgs["box"].IndexCount;
    //boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;
    //boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;
    //boxRitem->Instances.resize(1);
    //++mInstanceCount;
    //boxRitem->Instances[0].World = XMFLOAT4X4(
    //    2.0f, 0.0f, 0.0f, 0.0f,
    //    0.0f, 2.0f, 0.0f, 0.0f,
    //    0.0f, 0.0f, 2.0f, 0.0f,
    //    0.0f, 1.0f, 0.0f, 1.0f
    //);
    //XMStoreFloat4x4(&boxRitem->Instances[0].TexTransform, XMMatrixScaling(2.0f, 2.0f, 1.0f));
    //boxRitem->Instances[0].MaterialIndex = 3;
    //mRitemLayer[(int)RenderLayer::Opaque].push_back(boxRitem.get());
    ////mAllRitems.push_back(std::move(boxRitem));

    //// �ذ���Ⱦ��
    //auto gridRitem = std::make_unique<RenderItem>();
    //gridRitem->ObjCBIndex = 1;
    //gridRitem->Mat = mMaterials["tile0"].get();
    //gridRitem->Geo = mGeometries["shapeGeo"].get();
    //gridRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    //gridRitem->InstanceCount = 1;//ʵ������
    //gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
    //gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
    //gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
    //gridRitem->Instances.resize(1);
    //++mInstanceCount;
    //gridRitem->Instances[0].World = XMFLOAT4X4(
    //    1.0f, 0.0f, 0.0f, 0.0f,
    //    0.0f, 1.0f, 0.0f, 0.0f,
    //    0.0f, 0.0f, 1.0f, 0.0f,
    //    0.0f, 0.0f, 0.0f, 1.0f
    //);
    //XMStoreFloat4x4(&gridRitem->Instances[0].TexTransform, XMMatrixScaling(8.0f, 8.0f, 1.0f));
    //gridRitem->Instances[0].MaterialIndex = 2;
    //mRitemLayer[(int)RenderLayer::Opaque].push_back(gridRitem.get());

    //
    ////mAllRitems.push_back(std::move(gridRitem));

    //// ���������Ⱦ��
    //auto CylRitem = std::make_unique<RenderItem>();
    //CylRitem->World = MathHelper::Identity4x4();
    //CylRitem->TexTransform = MathHelper::Identity4x4();
    //CylRitem->ObjCBIndex = 2;
    //CylRitem->Mat = mMaterials["bricks"].get();
    //CylRitem->Geo = mGeometries["shapeGeo"].get();
    //CylRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    //CylRitem->InstanceCount = 10;//ʵ������
    //CylRitem->IndexCount = CylRitem->Geo->DrawArgs["cylinder"].IndexCount;
    //CylRitem->StartIndexLocation = CylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
    //CylRitem->BaseVertexLocation = CylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

    //CylRitem->Instances.resize(10);
    //mInstanceCount += 10;
    //for (int i = 0; i < 5; ++i)
    //{
    //    CylRitem->Instances[i].World = XMFLOAT4X4(
    //        1.0f, 0.0f, 0.0f, 0.0f,
    //        0.0f, 1.0f, 0.0f, 0.0f,
    //        0.0f, 0.0f, 1.0f, 0.0f,
    //        -5.0f, 1.5f, -10.0f + i * 5.0f, 1.0f
    //    );
    //    XMStoreFloat4x4(&CylRitem->Instances[i].TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
    //    CylRitem->Instances[i].MaterialIndex = 0;

    //    CylRitem->Instances[i+5].World = XMFLOAT4X4(
    //        1.0f, 0.0f, 0.0f, 0.0f,
    //        0.0f, 1.0f, 0.0f, 0.0f,
    //        0.0f, 0.0f, 1.0f, 0.0f,
    //        +5.0f, 1.5f, -10.0f + i * 5.0f, 1.0f
    //    );
    //    XMStoreFloat4x4(&CylRitem->Instances[i+5].TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
    //    CylRitem->Instances[i+5].MaterialIndex = 0;

    //}
    //
    //mRitemLayer[(int)RenderLayer::Opaque].push_back(CylRitem.get());
    //mAllRitems.push_back(std::move(CylRitem));

    //auto SphereRitem = std::make_unique<RenderItem>();
    //SphereRitem->World = MathHelper::Identity4x4();
    //SphereRitem->TexTransform = MathHelper::Identity4x4();
    //SphereRitem->ObjCBIndex = 3;
    //SphereRitem->Mat = mMaterials["stone0"].get();
    //SphereRitem->Geo = mGeometries["shapeGeo"].get();
    //SphereRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    //SphereRitem->InstanceCount = 10;//ʵ������
    //SphereRitem->IndexCount = SphereRitem->Geo->DrawArgs["sphere"].IndexCount;
    //SphereRitem->StartIndexLocation = SphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
    //SphereRitem->BaseVertexLocation = SphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

    //SphereRitem->Instances.resize(10);
    //mInstanceCount += 10;
    //for (int i = 0; i < 5; ++i)
    //{
    //    SphereRitem->Instances[i].World = XMFLOAT4X4(
    //        1.0f, 0.0f, 0.0f, 0.0f,
    //        0.0f, 1.0f, 0.0f, 0.0f,
    //        0.0f, 0.0f, 1.0f, 0.0f,
    //        -5.0f, 3.5f, -10.0f + i * 5.0f, 1.0f
    //    );
    //    XMStoreFloat4x4(&SphereRitem->Instances[i].TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
    //    SphereRitem->Instances[i].MaterialIndex = 1;

    //    SphereRitem->Instances[i + 5].World = XMFLOAT4X4(
    //        1.0f, 0.0f, 0.0f, 0.0f,
    //        0.0f, 1.0f, 0.0f, 0.0f,
    //        0.0f, 0.0f, 1.0f, 0.0f,
    //        +5.0f, 3.5f, -10.0f + i * 5.0f, 1.0f
    //    );
    //    XMStoreFloat4x4(&SphereRitem->Instances[i+5].TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
    //    SphereRitem->Instances[i+5].MaterialIndex = 1;

    //}

    //mRitemLayer[(int)RenderLayer::Opaque].push_back(SphereRitem.get());
    //mAllRitems.push_back(std::move(SphereRitem));
    ////mAllRitems.push_back(std::move(CylRitem));

    //mAllRitems.push_back(std::move(boxRitem));
    //mAllRitems.push_back(std::move(gridRitem));

    //UINT objCBIndex = 2;
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

    //    XMStoreFloat4x4(&leftCylRitem->World, rightCylWorld);
    //    XMStoreFloat4x4(&leftCylRitem->TexTransform, brickTexTransform);
    //    leftCylRitem->ObjCBIndex = objCBIndex++;
    //    leftCylRitem->Mat = mMaterials["bricks"].get();
    //    leftCylRitem->Geo = mGeometries["shapeGeo"].get();
    //    leftCylRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    //    leftCylRitem->InstanceCount = 0;//ʵ������
    //    leftCylRitem->IndexCount = leftCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
    //    leftCylRitem->StartIndexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
    //    leftCylRitem->BaseVertexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
    //    leftCylRitem->Instances.resize(1);
    //    ++mInstanceCount;
    //    mRitemLayer[(int)RenderLayer::Opaque].push_back(leftCylRitem.get());

    //    XMStoreFloat4x4(&rightCylRitem->World, leftCylWorld);
    //    XMStoreFloat4x4(&rightCylRitem->TexTransform, brickTexTransform);
    //    rightCylRitem->ObjCBIndex = objCBIndex++;
    //    rightCylRitem->Mat = mMaterials["bricks"].get();
    //    rightCylRitem->Geo = mGeometries["shapeGeo"].get();
    //    rightCylRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    //    rightCylRitem->InstanceCount = 0;//ʵ������
    //    rightCylRitem->IndexCount = rightCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
    //    rightCylRitem->StartIndexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
    //    rightCylRitem->BaseVertexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
    //    rightCylRitem->Instances.resize(1);
    //    ++mInstanceCount;
    //    mRitemLayer[(int)RenderLayer::Opaque].push_back(rightCylRitem.get());

    //    XMStoreFloat4x4(&leftSphereRitem->World, leftSphereWorld);
    //    leftSphereRitem->TexTransform = MathHelper::Identity4x4();
    //    leftSphereRitem->ObjCBIndex = objCBIndex++;
    //    leftSphereRitem->Mat = mMaterials["stone0"].get();
    //    leftSphereRitem->Geo = mGeometries["shapeGeo"].get();
    //    leftSphereRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    //    leftSphereRitem->InstanceCount = 0;//ʵ������
    //    leftSphereRitem->IndexCount = leftSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
    //    leftSphereRitem->StartIndexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
    //    leftSphereRitem->BaseVertexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;
    //    leftSphereRitem->Instances.resize(1);
    //    ++mInstanceCount;
    //    mRitemLayer[(int)RenderLayer::Opaque].push_back(leftSphereRitem.get());

    //    XMStoreFloat4x4(&rightSphereRitem->World, rightSphereWorld);
    //    rightSphereRitem->TexTransform = MathHelper::Identity4x4();
    //    rightSphereRitem->ObjCBIndex = objCBIndex++;
    //    rightSphereRitem->Mat = mMaterials["stone0"].get();
    //    rightSphereRitem->Geo = mGeometries["shapeGeo"].get();
    //    rightSphereRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    //    rightSphereRitem->InstanceCount = 0;//ʵ������
    //    rightSphereRitem->IndexCount = rightSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
    //    rightSphereRitem->StartIndexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
    //    rightSphereRitem->BaseVertexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;
    //    rightSphereRitem->Instances.resize(1);
    //    ++mInstanceCount;
    //    mRitemLayer[(int)RenderLayer::Opaque].push_back(rightSphereRitem.get());

    //    mAllRitems.push_back(std::move(leftCylRitem));
    //    mAllRitems.push_back(std::move(rightCylRitem));
    //    mAllRitems.push_back(std::move(leftSphereRitem));
    //    mAllRitems.push_back(std::move(rightSphereRitem));
    //}
    //UINT objCBIndex = 0;
    // ������Ⱦ��
    auto skullRitem = std::make_unique<RenderItem>();
    skullRitem->World = MathHelper::Identity4x4();
    skullRitem->TexTransform = MathHelper::Identity4x4();
    //skullRitem->ObjCBIndex = 0;
    skullRitem->Mat = mMaterials["skullMat"].get();
    skullRitem->Geo = mGeometries["skullGeo"].get();
    skullRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    skullRitem->InstanceCount = 0;//ʵ������
    skullRitem->IndexCount = skullRitem->Geo->DrawArgs["skull"].IndexCount;
    skullRitem->StartIndexLocation = skullRitem->Geo->DrawArgs["skull"].StartIndexLocation;
    skullRitem->BaseVertexLocation = skullRitem->Geo->DrawArgs["skull"].BaseVertexLocation;
    skullRitem->Bounds = skullRitem->Geo->DrawArgs["skull"].Bounds;

    // ����ʵ������
    const int n = 5;
    mInstanceCount = n * n * n;
    skullRitem->Instances.resize(mInstanceCount);

    float width = 200.0f;
    float height = 200.0f;
    float depth = 200.0f;

    float x = -0.5f * width;
    float y = -0.5f * height;
    float z = -0.5f * depth;
    float dx = width / (n - 1);
    float dy = height / (n - 1);
    float dz = depth / (n - 1);

    for (int k = 0; k < n; ++k)
    {
        for (int i = 0; i < n; ++i)
        {
            for (int j = 0; j < n; ++j)
            {
                int index = k * n * n + i * n + j;
                // ����άդ��ʵ������λ��
                skullRitem->Instances[index].World = XMFLOAT4X4(
                    1.0f, 0.0f, 0.0f, 0.0f,
                    0.0f, 1.0f, 0.0f, 0.0f,
                    0.0f, 0.0f, 1.0f, 0.0f,
                    x + j * dx, y + i * dy, z + k * dz, 1.0f
                );
                XMStoreFloat4x4(&skullRitem->Instances[index].TexTransform, XMMatrixScaling(2.0f, 2.0f, 1.0f));
                skullRitem->Instances[index].MaterialIndex = index % mMaterials.size();
            }
        }
    }

    mRitemLayer[(int)RenderLayer::Opaque].push_back(skullRitem.get());

    //move���ͷ�Ritem�ڴ棬���Ա�����mRitemLayer֮��ִ��
    mAllRitems.push_back(std::move(skullRitem));
}

void GameDevelopApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
    /*UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

    auto objectCB = mCurrFrameResource->ObjectCB->Resource();*/

    for (size_t i = 0; i < ritems.size(); ++i)
    {
        auto ri = ritems[i];

        // ���ö��㻺����
        cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
        // ��������������
        cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
        // ����ͼԪ����
        cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

        //���������ĸ�����
        // ���ø�����������������������Դ�� �������ǰ󶨸���������ֱ���ϴ�GPU��ĳ������ݡ�
        // ֻ��Ҫ��objConstants
        /*D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress();
        objCBAddress += ri->ObjCBIndex * objCBByteSize;*/
        //D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex * matCBByteSize;
        // objCBAddress����Դ�ĵ�ַ
        // �������ͬ
        // cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress);
        //// 3ָ���Ǹ�ǩ�������±�
        //cmdList->SetGraphicsRootConstantBufferView(3, matCBAddress);
        
        //����ʵ�����������ĸ�ǩ�������ڽṹ����������˵�������ƹ��������Ѷ�ֱ�ӽ������ó�������
        auto instanceBuffer = mCurrFrameResource->InstanceBuffer->Resource();
        cmdList->SetGraphicsRootShaderResourceView(0, //����������
            instanceBuffer->GetGPUVirtualAddress());
        // ���ƶ���ʵ��
        //���ƶ��㣨ͨ���������������ƣ�
        cmdList->DrawIndexedInstanced(ri->IndexCount, ri->InstanceCount, ri->StartIndexLocation, ri->BaseVertexLocation, 0);//ʵ�����ĸ߼���������ʱ����Ϊ0
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