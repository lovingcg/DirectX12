#include "GameDeveloper.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx12.h"
#include <fstream>
#include <memory>

//HINSTANCE hInstance标识存放应用程序的所有资源信息的内存空间的句柄
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
    // reset命令列表离为后面初始化做准备
    ThrowIfFailed(cmdList->Reset(cmdAllocator.Get(), nullptr));

    //获取此堆类型中描述符的增量大小。这是硬件专用的，所以必须查询这些信息
    mCbvSrvDescriptorSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    mCamera.SetPosition(mx, my, mz);

    LoadTextures();
    BuildRootSignature();
    BuildDescriptorHeaps();
    BuildShadersAndInputLayout();
    //BuildShapeGeometry();
    BuildSkullGeometry();
    BuildMaterials();//构建材质
    // BuildRenderItems()函数必须在CreateConstantBufferView()函数之前，不然拿不到allRitems.size()的值
    BuildRenderItems();
    BuildFrameResources();
    BuildPSO();

    // 执行完初始化命令 关闭命令队列并同步CPU和GPU，
    // 没有把BuildGeometry函数放在初始化中，又没有在里面做close和同步，导致GPU数据被提前释放
    ThrowIfFailed(cmdList->Close());
    ID3D12CommandList* cmdsLists[] = { cmdList.Get() };
    cmdQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // 等待直到初始化命令完成
    FlushCmdQueue();

    return true;
}
// Draw函数主要是将我们的各种资源设置到渲染流水线上, 
// 并最终发出绘制命令。首先重置命令分配器cmdAllocator和命令列表cmdList，
// 目的是重置命令和列表，复用相关内存。
//注释的部分其实就是封装起来了
void GameDevelopApp::Draw()
{
    //因为每个帧资源都有自己的命令分配器，所以在Draw函数开始，我们就要Reset当前帧资源中的cmdAllocator。
    auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

    ThrowIfFailed(cmdListAlloc->Reset());// 重复使用记录命令的相关内存
    //ThrowIfFailed(cmdList->Reset(cmdListAlloc.Get(), mPSO.Get()));

    if (is_wiremode)
    {
        ThrowIfFailed(cmdList->Reset(cmdListAlloc.Get(), mPSOs["wireframe"].Get()));// 复用命令列表及其内存
    }
    else
    {
        ThrowIfFailed(cmdList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));// 复用命令列表及其内存
    }

    //设置视口和裁剪矩形
    cmdList->RSSetViewports(1, &viewPort);
    cmdList->RSSetScissorRects(1, &scissorRect);

    // 设置根签名
    cmdList->SetGraphicsRootSignature(mRootSignature.Get());

    // 将后台缓冲资源从呈现状态转换到渲染目标状态（即准备接收图像渲染）
    cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),// 转换资源为后台缓冲区资源
        D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));// 从呈现到渲染目标转换

    // 加入雾效之后得把背景色设置成雾气的颜色
    cmdList->ClearRenderTargetView(CurrentBackBufferView(), (float*)&mMainPassCB.FogColor, 0, nullptr);// 清除RT背景色为暗红，并且不设置裁剪矩形
    cmdList->ClearDepthStencilView(DepthStencilView(),	// DSV描述符句柄
        D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,	// FLAG
        1.0f,	// 默认深度值
        0,	// 默认模板值
        0,	// 裁剪矩形数量
        nullptr);	// 裁剪矩形指针

    // 然后指定将要渲染的缓冲区，即指定RTV和DSV。
    cmdList->OMSetRenderTargets(1,// 待绑定的RTV数量
        &CurrentBackBufferView(),	// 指向RTV数组的指针
        true,	// RTV对象在堆内存中是连续存放的
        &DepthStencilView());	// 指向DSV的指针

    //设置SRV描述符堆
    ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };// 注意这里之所以是数组，是因为还可能包含SRV和UAV，而这里我们只用到了SRV
    cmdList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    // 绑定此场景中使用的所有材质。对于结构化缓冲区，可以绕过堆设置为根描述符。
    auto matBuffer = mCurrFrameResource->MaterialBuffer->Resource();
    cmdList->SetGraphicsRootShaderResourceView(1, matBuffer->GetGPUVirtualAddress());
    // 绑定passCB根描述符
    auto passCB = mCurrFrameResource->PassCB->Resource();
    // 这里的1指的是根参数索引 不是寄存器槽号
    cmdList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());

    //设置描述符表，将纹理资源与流水线绑定(因为只绑定一次，所以不需要做地址偏移)
    cmdList->SetGraphicsRootDescriptorTable(3, mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

    // objCB还是在DrawRenderItems中设置

    //分别设置PSO并绘制对应渲染项
    //绘制不透明物体渲染项
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
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));// 从渲染目标到呈现

    // imgui参与渲染循环
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    //1. 显示大的演示窗口（大部分示例代码在ImGui:：ShowDemoWindow（）中
    bool show_demo_window = true;
    /*if (show_demo_window)
        ImGui::ShowDemoWindow(&show_demo_window);*/

    //2. 展示一个我们自己创造的简单窗口。我们使用Begin/End对来创建一个命名窗口。
    {
        ImGui::Begin("Use ImGui");
        ImGui::Checkbox("Animate Geo", &animateGeo);
        if (animateGeo)// 下面的控件受上面的复选框影响
        {
            ImGui::SameLine(0.0f, 25.0f);// 下一个控件在同一行往右25像素单位
            if (ImGui::Button("Reset Params"))
            {
                mx = 0.0f;
                my = 2.0f;
                mz = -15.0f;
                fov = 0.25f;
                mCamera.SetPosition(mx, my, mz);
            }

            //ImGui::Text("CameraX: %.2f degrees", mx);// 显示文字，用于描述下面的控件 
            //ImGui::SliderFloat("##1", &mx, -15.00f, 15.00f, ""); // 不显示控件标题，但使用##来避免标签重复  空字符串避免显示数字

            //ImGui::Text("CameraY: %.2f degrees", my);// 显示文字，用于描述下面的控件 
            //ImGui::SliderFloat("##2", &my, -10.00f, 10.00f, "");

            //ImGui::Text("CameraZ: %.2f degrees", mz);// 显示文字，用于描述下面的控件 
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
    // 触发ImGui在Direct3D的绘制
    // 因此需要在此之前将后备缓冲区绑定到渲染管线上
    // 需要在绘制的工作结束以后进行
    // 简单说来就是在DrawIndexed之后、RT转PRESENT之前
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdList.Get());
    
    // 完成命令的记录关闭命令列表
    ThrowIfFailed(cmdList->Close());

    // 等CPU将命令都准备好后，需要将待执行的命令列表加入GPU的命令队列。
    // 使用的是ExecuteCommandLists函数。
    ID3D12CommandList* commandLists[] = { cmdList.Get() };// 声明并定义命令列表数组
    cmdQueue->ExecuteCommandLists(_countof(commandLists), commandLists);// 将命令从命令列表传至命令队列

    // 然后交换前后台缓冲区索引（这里的算法是1变0，0变1，为了让后台缓冲区索引永远为0）。
    ThrowIfFailed(swapChain->Present(0, 0));
    mCurrentBackBuffer = (mCurrentBackBuffer + 1) % 2;

    // 最后设置围栏值，刷新命令队列，使CPU和GPU同步，直接封装。
    // FlushCmdQueue();

    //提高围栏值以将命令标记到此围栏点。
    mCurrFrameResource->fenceCPU = ++mCurrentFence;

    // 当GPU处理完命令后，将GPU端Fence++

    //将指令添加到命令队列以设置新的围栏点。
    //因为我们在GPU时间线上，所以新的围栏点不会设置，直到GPU在此Signal（）之前完成所有命令的处理。
    //signal的作用就是，当命令列表执行完之后，fence值设置成mCurrentFence
    cmdQueue->Signal(fence.Get(), mCurrentFence);
}
// 按下鼠标事件
void GameDevelopApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    mLastMousePos.x = x;// 按下的时候记录坐标x分量
    mLastMousePos.y = y;// 按下的时候记录坐标y分量

    SetCapture(mhMainWnd);// 在属于当前线程的指定窗口里，设置鼠标捕获
}
// 松开鼠标事件
void GameDevelopApp::OnMouseUp(WPARAM btnState, int x, int y)
{
    ReleaseCapture();// 按键抬起后释放鼠标捕获
}
// 移动鼠标事件
void GameDevelopApp::OnMouseMove(WPARAM btnState, int x, int y)
{
    if ((btnState & MK_LBUTTON) != 0)// 如果在左键按下状态
    {
        // 将鼠标的移动距离换算成弧度，0.25为调节阈值
        float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
        float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

        // 计算鼠标没有松开前的累计弧度
        mCamera.Pitch(dy);
        mCamera.RotateY(dx);
    }
    else if ((btnState & MK_RBUTTON) != 0)// 如果在右键按下状态
    {
        //将鼠标的移动距离换算成旋转角度，0.25为调节阈值
        float dx = XMConvertToRadians(static_cast<float>(mLastMousePos.x - x) * 0.25f);

        mCamera.Roll(dx);
    }
    // 将当前鼠标坐标赋值给“上一次鼠标坐标”，为下一次鼠标操作提供先前值
    mLastMousePos.x = x;
    mLastMousePos.y = y;
}

void GameDevelopApp::OnResize()
{
    D3DApp::OnResize();

    mCamera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);

    BoundingFrustum::CreateFromMatrix(mCamFrustum, mCamera.GetProj());
}

// 每一帧的操作
void GameDevelopApp::Update()
{ 
    OnKeyboardInputMove(gt);

    // 每帧遍历一个帧资源（多帧的话就是环形遍历）
    mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
    mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

    //如果GPU端围栏值小于CPU端围栏值，即CPU速度快于GPU，则令CPU等待
    // 一个围栏更新3帧
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

    //左右键改变平行光的Theta角，上下键改变平行光的Phi角
    if (GetAsyncKeyState(VK_LEFT) & 0x8000)
        mSunTheta -= 1.0f * dt;

    if (GetAsyncKeyState(VK_RIGHT) & 0x8000)
        mSunTheta += 1.0f * dt;

    if (GetAsyncKeyState(VK_UP) & 0x8000)
        mSunPhi -= 1.0f * dt;

    if (GetAsyncKeyState(VK_DOWN) & 0x8000)
        mSunPhi += 1.0f * dt;

    //将Phi约束在[0, PI/2]之间
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
    // 只单独改变了湖水材质的matTransform
    auto waterMat = mMaterials["water"].get();

    float& tu = waterMat->MatTransform(3, 0);//返回矩阵第4行第1列的float值（u方向的平移量）
    float& tv = waterMat->MatTransform(3, 1);//返回矩阵第4行第2列的float值（v方向的平移量）

    //因为每帧都会执行此函数，u和v坐标越来越大
    tu += 0.1f * gt.DeltaTime();
    tv += 0.02f * gt.DeltaTime();

    //如果uv超出1，则转回0（相当于循环了）
    //注意UV的初始值为0，因为单位矩阵返回的时候就是0
    if (tu >= 1.0f)
        tu -= 1.0f;

    if (tv >= 1.0f)
        tv -= 1.0f;
    //将tu和tv存入矩阵
    // hlsl中为右乘，这里就不多一步转置了
    waterMat->MatTransform(3, 0) = tu;
    waterMat->MatTransform(3, 1) = tv;

    // 材质已更改，因此需要更新cbuffer
    waterMat->NumFramesDirty = gNumFrameResources;

    // 立方体表面上的火球纹理材质改变
    auto boxMat = mMaterials["wood"].get();
    XMStoreFloat4x4(&boxMat->MatTransform, XMMatrixRotationZ(gt.TotalTime()));
    // 材质已更改，因此需要更新cbuffer
    boxMat->NumFramesDirty = gNumFrameResources;
}

//void GameDevelopApp::UpdateObjectCBs(const _GameTimer::GameTimer& gt)
//{
//    auto currObjectCB = mCurrFrameResource->ObjectCB.get();
//    for (auto& e : mAllRitems)
//    {
//        //只有在常量发生更改时才更新cbuffer数据。
//        //这需要按帧资源进行跟踪。
//        if (e->NumFramesDirty > 0)
//        {
//            XMMATRIX world = XMLoadFloat4x4(&e->World);
//            // 将texTransform更新至ObjCB，这样数据就传入了常量缓冲区中了，并最终传入着色器
//            XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);
//
//            ObjectConstants objConstants;
//            XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
//            XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));
//            objConstants.MaterialIndex = e->Mat->MatCBIndex;
//            //将数据拷贝至GPU缓存
//            //e->ObjCBIndex保证世界矩阵与单个几何体一一对应
//            currObjectCB->CopyData(e->ObjCBIndex, objConstants);
//
//            //下一个FrameResource也需要更新。
//            e->NumFramesDirty--;
//        }
//    }
//}

void GameDevelopApp::UpdateInstanceData(const _GameTimer::GameTimer& gt)
{
    XMMATRIX view = mCamera.GetView();//WorldToView的变换矩阵
    XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);//ViewToWorld的变换矩阵
    // 世界矩阵数据(currInstanceSB)传递至GPU
    auto currInstanceBuffer = mCurrFrameResource->InstanceBuffer.get();
    for (auto& e : mAllRitems)
    {
        //if (e->NumFramesDirty > 0)//帧资源数量判定
        //{
            const auto& instanceData = e->Instances;//每个渲染项的实例数组
            //遍历渲染项中每个实例的数据，将它们传至GPU
            int visibleInstanceCount = 0;

            for (UINT i = 0; i < (UINT)instanceData.size(); ++i)
            {
                XMMATRIX world = XMLoadFloat4x4(&instanceData[i].World);
                XMMATRIX texTransform = XMLoadFloat4x4(&instanceData[i].TexTransform);

                XMMATRIX invWorld = XMMatrixInverse(&XMMatrixDeterminant(world), world);
                //计算每个实例的viewToLocal矩阵
                XMMATRIX viewToLocal = XMMatrixMultiply(invView, invWorld);

                //创建视锥体
                BoundingFrustum localSpaceFrustum;
                localSpaceFrustum.CreateFromMatrix(localSpaceFrustum, mCamera.GetProj());
                // 将视锥体从观察空间变换到局部空间
                mCamFrustum.Transform(localSpaceFrustum, viewToLocal);
                //如果视锥体和实例的AABB不相交，则不上传实例数据
                if ((localSpaceFrustum.Contains(e->Bounds) != DirectX::DISJOINT) || (mFrustumCullingEnabled == false))
                {
                    InstanceData data;//每个实例的数据
                    XMStoreFloat4x4(&data.World, XMMatrixTranspose(world));
                    XMStoreFloat4x4(&data.TexTransform, XMMatrixTranspose(texTransform));
                    // 将材质常量缓冲区中的matCBIndex赋值给新增在instanceData中的materialIndex字段
                    data.MaterialIndex = instanceData[i].MaterialIndex;
                    //将实例数据一个个地拷贝至GPU缓存
                    currInstanceBuffer->CopyData(visibleInstanceCount++, data);
                }
            }
            e->InstanceCount = visibleInstanceCount;//实例个数赋值
            //e->NumFramesDirty--;//下一个帧资源

            // 显示当前屏幕的实例个数
            std::wostringstream outs;
            outs.precision(6);
            outs << L"Instancing and Culling Demo" <<
                L"    " << e->InstanceCount <<
                L" objects visible out of " << e->Instances.size();
            mMainWndCaption = outs.str();
        //}
    }
}

//实现思路和objConstants类似，遍历materials无序列表，提取出Material指针，
//并将其赋值给常量结构体中对应元素，最终使用CopyData函数将数据传至GPU，
//最后记得numFramesDirty--，因为这样才能满足每个帧资源都能得到更新
void GameDevelopApp::UpdateMaterialCBs(const _GameTimer::GameTimer& gt)
{
    auto currMaterialCB = mCurrFrameResource->MaterialBuffer.get();
    for (auto& e : mMaterials)
    {
        Material* mat = e.second.get();//获得键值对的值，即Material指针（智能指针转普通指针）
        if (mat->NumFramesDirty > 0)
        {
            XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

            //将定义的材质属性传给常量结构体中的元素
            MaterialData matConstants;
            matConstants.DiffuseAlbedo = mat->DiffuseAlbedo;
            matConstants.FresnelR0 = mat->FresnelR0;
            matConstants.Roughness = mat->Roughness;
            XMStoreFloat4x4(&matConstants.MatTransform, XMMatrixTranspose(matTransform));
            // 将纹理数组索引也传入GPU
            matConstants.DiffuseMapIndex = mat->DiffuseSrvHeapIndex;

            //将材质常量数据复制到常量缓冲区对应索引地址处
            currMaterialCB->CopyData(mat->MatCBIndex, matConstants);
            //更新下一个帧资源
            mat->NumFramesDirty--;
        }
    }
}

void GameDevelopApp::UpdateMainPassCB(const _GameTimer::GameTimer& gt)
{
    // 赋值viewProj参数
    XMMATRIX view = mCamera.GetView();
    //XMMATRIX proj = XMLoadFloat4x4(&mProj);
    XMMATRIX proj = mCamera.GetProj();
    XMMATRIX viewProj = XMMatrixMultiply(view, proj);

    XMStoreFloat4x4(&mMainPassCB.viewProj, XMMatrixTranspose(viewProj));

    // 赋值light参数
    mMainPassCB.EyePosW = mCamera.GetPosition3f();
    mMainPassCB.AmbientLight = { 0.25f,0.25f,0.35f,1.0f };
    mMainPassCB.CartoonShader = is_ctshader;

    // 球面坐标转换笛卡尔坐标
    // 主光
    XMVECTOR lightDir = -MathHelper::SphericalToCartesian(1.0f, mSunTheta, mSunPhi);
    XMStoreFloat3(&mMainPassCB.Lights[0].Direction, lightDir);
    mMainPassCB.Lights[0].Strength = { 0.6f, 0.6f, 0.6f };

    // 辅助光 轮廓光
    mMainPassCB.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
    mMainPassCB.Lights[1].Strength = { 0.3f, 0.3f, 0.3f };
    mMainPassCB.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
    mMainPassCB.Lights[2].Strength = { 0.15f, 0.15f, 0.15f };

    mMainPassCB.FogColor = { 0.7f,0.7f,0.7f,1.0f };
    mMainPassCB.gFogStart = 5.0f;
    mMainPassCB.gFogRange = 150.0f;
    mMainPassCB.pad2 = { 0.0f,0.0f };

    // 将perPass常量数据传给缓冲区
    auto currPassCB = mCurrFrameResource->PassCB.get();
    // 0表示多个几何体共用一个观察投影矩阵
    currPassCB->CopyData(0, mMainPassCB);
}

void GameDevelopApp::BuildShapeGeometry()
{
    ProceduralGeometry proceGeo;
    //float width, float height, float depth, uint32 numSubdivisions
    ProceduralGeometry::MeshData box = proceGeo.CreateBox(1.0f, 1.0f, 1.0f, 3);
    //float width, float depth, uint32 m, uint32 n
    ProceduralGeometry::MeshData grid = proceGeo.CreateGrid(20.0f, 30.0f, 60, 40);
    //float radius, uint32 sliceCount, uint32 stackCount 半径，切片数量，堆叠层数
    ProceduralGeometry::MeshData sphere = proceGeo.CreateSphere(0.5f, 20, 20);
    //float bottomRadius, float topRadius, float height, uint32 sliceCount, uint32 stackCount
    ProceduralGeometry::MeshData cylinder = proceGeo.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);

    //计算单个几何体顶点在总顶点数组中的偏移量,顺序为：box、grid、sphere、cylinder
    UINT boxVertexOffset = 0;
    UINT gridVertexOffset = (UINT)box.Vertices.size();
    UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
    UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();

    //计算单个几何体索引在总索引数组中的偏移量,顺序为：box、grid、sphere、cylinder
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

    //创建一个总的顶点缓存vertices，并将4个子物体的顶点数据存入其中
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
    // 同理创建一个总的索引缓存indices，并将4个子物体的索引数据存入其中
    std::vector<std::uint16_t> indices;
    // 在指定位置loc前插入区间[start, end)的所有元素 
    indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
    indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
    indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
    indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));

    const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

    // 创建MeshGeometry实例
    geo = std::make_unique<MeshGeometry>();
    geo->Name = "shapeGeo";
    // 把顶点/索引数据上传到GPU
    ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
    CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);
    // 创建顶点默认堆 CPU->GPU
    geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(d3dDevice.Get(), cmdList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);
    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(d3dDevice.Get(), cmdList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

    geo->VertexByteStride = sizeof(Vertex);
    geo->VertexBufferByteSize = vbByteSize;
    geo->IndexFormat = DXGI_FORMAT_R16_UINT;
    geo->IndexBufferByteSize = ibByteSize;

    // 并将封装好的4个几何体的SubmeshGeometry对象赋值给无序映射表。
    geo->DrawArgs["box"] = boxSubmesh;
    geo->DrawArgs["grid"] = gridSubmesh;
    geo->DrawArgs["sphere"] = sphereSubmesh;
    geo->DrawArgs["cylinder"] = cylinderSubmesh;
    // unique_ptr通过move转移资源
    mGeometries[geo->Name] = std::move(geo);
}

void GameDevelopApp::BuildSkullGeometry()
{
    std::ifstream fin("Models/skull.txt");//读取骷髅网格文件

    if (!fin)
    {
        MessageBox(0, L"Models/skull.txt not found.", 0, 0);
        return;
    }

    UINT vcount = 0;
    UINT tcount = 0;
    std::string ignore;

    fin >> ignore >> vcount;//读取vertexCount并赋值
    fin >> ignore >> tcount;//读取triangleCount并赋值
    fin >> ignore >> ignore >> ignore >> ignore;//整行不读

    //初始化AABB的vMin和vMax,infinity为32位float中最大的数
    XMFLOAT3 vMinf3(+MathHelper::Infinity, +MathHelper::Infinity, +MathHelper::Infinity);
    XMFLOAT3 vMaxf3(-MathHelper::Infinity, -MathHelper::Infinity, -MathHelper::Infinity);
    //转换到XMVECTOR
    XMVECTOR vMin = XMLoadFloat3(&vMinf3);
    XMVECTOR vMax = XMLoadFloat3(&vMaxf3);

    std::vector<Vertex> vertices(vcount);
    //顶点列表赋值
    for (UINT i = 0; i < vcount; ++i)
    {
        fin >> vertices[i].Pos.x >> vertices[i].Pos.y >> vertices[i].Pos.z;
        fin >> vertices[i].Normal.x >> vertices[i].Normal.y >> vertices[i].Normal.z;
        //vertices[i].TexC = { 0.0f, 0.0f };

        XMVECTOR P = XMLoadFloat3(&vertices[i].Pos);

        vMin = XMVectorMin(vMin, P);
        vMax = XMVectorMax(vMax, P);

        // 将点投影到单位球体上并生成球形纹理坐标
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

    //将AABB转为CE定义（中心center和扩展向量extents）
    DirectX::BoundingBox bounds;
    XMStoreFloat3(&bounds.Center, 0.5f * (vMin + vMax));
    XMStoreFloat3(&bounds.Extents, 0.5f * (vMax - vMin));

    fin >> ignore;
    fin >> ignore;
    fin >> ignore;

    std::vector<std::int32_t> indices(3 * tcount);
    //索引列表赋值
    for (UINT i = 0; i < tcount; ++i)
    {
        fin >> indices[i * 3 + 0] >> indices[i * 3 + 1] >> indices[i * 3 + 2];
    }

    fin.close();

    const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::int32_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "skullGeo";

    //将顶点和索引数据复制到CPU系统内存上
    ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
    CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);
    //将顶点和索引数据从CPU内存复制到GPU缓存上
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

//如同RTV，DSV，常量缓冲区也需要描述符来指定缓冲区的属性。而描述符需要描述符堆来存放。
//所以，先创建CBV堆。
void GameDevelopApp::BuildDescriptorHeaps()
{
    //创建SRV堆
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = 7;//加入新的数量得改
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    srvHeapDesc.NodeMask = 0;
    ThrowIfFailed(d3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

    // 将之前定义好的纹理资源转换成comptr存起来
    auto bricksTex = mTextures["bricksTex"]->Resource;
    auto stoneTex = mTextures["stoneTex"]->Resource;
    auto tileTex = mTextures["tileTex"]->Resource;
    auto crateTex = mTextures["crateTex"]->Resource;
    auto iceTex = mTextures["iceTex"]->Resource;
    auto grassTex = mTextures["grassTex"]->Resource;
    auto defaultTex = mTextures["defaultTex"]->Resource;

    //SRV堆中子SRV的句柄
    CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

    //SRV描述结构体
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;//采样后分量顺序不改变
    srvDesc.Format = bricksTex->GetDesc().Format;//视图的默认格式
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;//2D贴图
    srvDesc.Texture2D.MostDetailedMip = 0;//细节最详尽的mipmap层级为0
    srvDesc.Texture2D.MipLevels = bricksTex->GetDesc().MipLevels;//mipmap层级数量
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;//可访问的mipmap最小层级数为0
    d3dDevice->CreateShaderResourceView(bricksTex.Get(), &srvDesc, hDescriptor);

    //SRV堆中子SRV的句柄,继续偏移一个SRV
    hDescriptor.Offset(1, mCbvSrvDescriptorSize);
    //SRV描述结构体修改
    srvDesc.Format = stoneTex->GetDesc().Format;//视图的默认格式
    srvDesc.Texture2D.MipLevels = stoneTex->GetDesc().MipLevels;//mipmap层级数量
    d3dDevice->CreateShaderResourceView(stoneTex.Get(), &srvDesc, hDescriptor);

    //SRV堆中子SRV的句柄,继续偏移一个SRV
    hDescriptor.Offset(1, mCbvSrvDescriptorSize);
    //SRV描述结构体修改
    srvDesc.Format = tileTex->GetDesc().Format;//视图的默认格式
    srvDesc.Texture2D.MipLevels = tileTex->GetDesc().MipLevels;//mipmap层级数量
    d3dDevice->CreateShaderResourceView(tileTex.Get(), &srvDesc, hDescriptor);

    //SRV堆中子SRV的句柄,继续偏移一个SRV
    hDescriptor.Offset(1, mCbvSrvDescriptorSize);
    //SRV描述结构体修改
    srvDesc.Format = crateTex->GetDesc().Format;//视图的默认格式
    srvDesc.Texture2D.MipLevels = crateTex->GetDesc().MipLevels;//mipmap层级数量
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
    /*砖墙纹理*/
    auto bricksTex = std::make_unique<Texture>();
    bricksTex->Name = "bricksTex";
    bricksTex->Filename = L"Textures\\bricks.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(d3dDevice.Get(),
        cmdList.Get(), bricksTex->Filename.c_str(),
        bricksTex->Resource, bricksTex->UploadHeap));
    /*石头纹理*/
    auto stoneTex = std::make_unique<Texture>();
    stoneTex->Name = "stoneTex";
    stoneTex->Filename = L"Textures\\stone.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(d3dDevice.Get(),
        cmdList.Get(), stoneTex->Filename.c_str(),
        stoneTex->Resource, stoneTex->UploadHeap));
    /*地板纹理*/
    auto tileTex = std::make_unique<Texture>();
    tileTex->Name = "tileTex";
    tileTex->Filename = L"Textures\\tile.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(d3dDevice.Get(),
        cmdList.Get(), tileTex->Filename.c_str(),
        tileTex->Resource, tileTex->UploadHeap));
    /*箱子纹理*/
    auto crateTex = std::make_unique<Texture>();
    crateTex->Name = "crateTex";
    crateTex->Filename = L"Textures\\WoodCrate02.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(d3dDevice.Get(),
        cmdList.Get(), crateTex->Filename.c_str(),
        crateTex->Resource, crateTex->UploadHeap));
    /*镜子纹理*/
    auto iceTex = std::make_unique<Texture>();
    iceTex->Name = "iceTex";
    iceTex->Filename = L"Textures\\ice.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(d3dDevice.Get(),
        cmdList.Get(), iceTex->Filename.c_str(),
        iceTex->Resource, iceTex->UploadHeap));
    /*草地纹理*/
    auto grassTex = std::make_unique<Texture>();
    grassTex->Name = "grassTex";
    grassTex->Filename = L"Textures\\grass.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(d3dDevice.Get(),
        cmdList.Get(), grassTex->Filename.c_str(),
        grassTex->Resource, grassTex->UploadHeap));
    /*骷髅纹理*/
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

//根签名描述清楚了渲染管线或者说Shader编译后的执行代码需要的各种资源以什么样的方式传入以及如何在内存、显存中布局
//根签名其实就是将着色器需要用到的数据绑定到对应的寄存器槽上，供着色器访问。
// 主要指定的是GPU上对应的寄存器
//根签名分为描述符表、根描述符、根常量，这里使用描述符表来初始化根签名。

//根签名作用是将常量数据绑定至寄存器槽，供着色器程序访问。
//因为现在我们有2个常量数据结构体，所以要创建2个元素的根参数，即2个CBV表，并绑定2个寄存器槽。
// 将实例缓冲区绑定到SRV上（StructuredBuffer需使用SRV）
void GameDevelopApp::BuildRootSignature()
{
    //创建由单个CBV所组成的描述符表
    CD3DX12_DESCRIPTOR_RANGE texTable;
    texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,// 描述符类型
        7,// 描述符数量（纹理数量）
        0);// 描述符所绑定的寄存器槽号

    // 根参数可以是描述符表、根描述符、根常量
    CD3DX12_ROOT_PARAMETER slotRootParameter[4];
    
    // 创建根描述符 好处：不需要创建CBV堆，而是直接设置根描述符即可指示要绑定的资源
    // 0 1 2表示寄存器槽号
    // 性能提示：从最频繁到最不频繁排序
    // 实例结构化缓存：(t0, space1)
    slotRootParameter[0].InitAsShaderResourceView(0, 1);
    // 材质结构化缓存：(t1, space1)
    slotRootParameter[1].InitAsShaderResourceView(1, 1);
    // 主Pass缓存：(b0)
    slotRootParameter[2].InitAsConstantBufferView(0);
    // 纹理数组：(t0, space0)
    slotRootParameter[3].InitAsDescriptorTable(1, // Range数量
        &texTable,// Range指针
        D3D12_SHADER_VISIBILITY_PIXEL);// 该资源只在像素着色器可读
    
    // 加了材质常量数据
    //slotRootParameter[3].InitAsConstantBufferView(2);// MaterialConstants

    //在根签名函数中，传入静态采样器，并将其与根签名绑定，为最终传入着色器做准备
    auto staticSamplers = GetStaticSamplers();

    // 根签名由一系列根参数组成
    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4,// 根参数的数量
        slotRootParameter,// 根参数指针
        (UINT)staticSamplers.size(),// 静态采样数量
        staticSamplers.data(),// 静态采样器
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);// 选择输入汇编器 需要一组顶点缓冲区绑定的输入布局
    //用单个寄存器槽来创建一个根签名，该槽位指向一个仅含有单个常量缓冲区的描述符区域
    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc,// 根参数描述指针
        D3D_ROOT_SIGNATURE_VERSION_1,// 根参数version
        serializedRootSig.GetAddressOf(),// 序列化内存块
        errorBlob.GetAddressOf());// 序列化错误信息
    if (errorBlob != nullptr) {
        OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    }
    ThrowIfFailed(hr);
    // 创建根签名与序列化内存绑定
    ThrowIfFailed(d3dDevice->CreateRootSignature(
        0,// 适配器数量
        serializedRootSig->GetBufferPointer(),// 根签名绑定的序列化内存指针
        serializedRootSig->GetBufferSize(),// 根签名绑定的序列化内存byte
        IID_PPV_ARGS(&mRootSignature)));// 根签名COM ID
}

void GameDevelopApp::BuildShadersAndInputLayout()
{
    // 并不是所有场景都会用到alphaTest，所以要将其设为可选项，若要使用，则需在编译着色器时定义ALPHA_TEST宏
    // LPCSTR Name;
    // LPCSTR Definition;
    // 着色器宏
    const D3D_SHADER_MACRO defines[] =
    {
        "FOG", "1",
        NULL, NULL
    };
    // D3D_SHADER_MACRO结构体是名字和定义
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
    // D3D_SHADER_MACRO结构体是名字和定义
    /*const D3D_SHADER_MACRO unfogalphaTestDefines[] =
    {
        "ALPHA_TEST", "1",
        NULL, NULL
    };*/

    // 将shader函数分成了3类，一类是顶点函数（standardVS），一类是不透明的片元函数（opaquePS），
    // 还有一类是alphaTest的片元函数（alphaTestPS）
    // 寄存器Space的用法只在ShaderModel5.1及以上才能使用，所以将ShaderModel改成5.1
    mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\color.hlsl", nullptr, "VS", "vs_5_1");
    mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\color.hlsl", defines, "PS", "ps_5_1");

    //mShaders["alphaTestedPS"] = d3dUtil::CompileShader(L"Shaders\\color.hlsl", alphaTestDefines, "PS", "ps_5_0");

    mShaders["unfogopaquePS"] = d3dUtil::CompileShader(L"Shaders\\color.hlsl", unfogdefines, "PS", "ps_5_1");
    //mShaders["unfogalphaTestedPS"] = d3dUtil::CompileShader(L"Shaders\\color.hlsl", unfogalphaTestDefines, "PS", "ps_5_0");

    mInputLayout =
    {
        //0表示寄存器槽号为0
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };
}

//把着色器所用的所有资源和状态都绑定到流水线上
void GameDevelopApp::BuildPSO()
{
    //不透明物体的PSO（不需要混合）
    D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc = {};
    //清空内存
    ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
    opaquePsoDesc.InputLayout = { mInputLayout.data(),(UINT)mInputLayout.size() };// 输入布局描述  
    opaquePsoDesc.pRootSignature = mRootSignature.Get();// 与此PSO绑定的根签名指针

    opaquePsoDesc.VS =
    {
        reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()),
        mShaders["standardVS"]->GetBufferSize()
    };// 待绑定的顶点着色器
    opaquePsoDesc.PS =
    {
        reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
        mShaders["opaquePS"]->GetBufferSize()
    };// 待绑定的像素着色器

    opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);// 光栅化状态
    opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);// 混合状态
    opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);// 深度/模板测试状态
    opaquePsoDesc.SampleMask = UINT_MAX;// 每个采样点的采样情况
    opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;// 图元拓扑类型
    opaquePsoDesc.NumRenderTargets = 1;// 渲染目标数量
    opaquePsoDesc.RTVFormats[0] = mBackBufferFormat;// 渲染目标的格式
    opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;// 多重采样数量
    opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;// 多重采样级别
    opaquePsoDesc.DSVFormat = mDepthStencilFormat;// 深度/模板缓冲区格式
    ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));

    // 不加雾效
    D3D12_GRAPHICS_PIPELINE_STATE_DESC unfogopaquePsoDesc = opaquePsoDesc;
    unfogopaquePsoDesc.PS =
    {
        reinterpret_cast<BYTE*>(mShaders["unfogopaquePS"]->GetBufferPointer()),
        mShaders["unfogopaquePS"]->GetBufferSize()
    };// 待绑定的像素着色器
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
        // 必须在BuildFrameResources函数中，正确填写子passCB的数量为2
        mFrameResources.push_back(std::make_unique<FrameResource>(d3dDevice.Get(), 1, mInstanceCount,(UINT)mMaterials.size()));
    }
}

void GameDevelopApp::BuildMaterials()
{
    //定义地板的材质
    auto bricks = std::make_unique<Material>();
    bricks->Name = "bricks";
    bricks->MatCBIndex = 0;
    bricks->DiffuseSrvHeapIndex = 0;
    bricks->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    bricks->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
    bricks->Roughness = 0.1f;

    //定义石头的材质
    auto stone0 = std::make_unique<Material>();
    stone0->Name = "stone0";
    stone0->MatCBIndex = 1;
    stone0->DiffuseSrvHeapIndex = 1;
    stone0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    stone0->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
    stone0->Roughness = 0.3f;

    //定义地板的材质
    auto tile0 = std::make_unique<Material>();
    tile0->Name = "tile0";
    tile0->MatCBIndex = 2;
    tile0->DiffuseSrvHeapIndex = 2;
    tile0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    tile0->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
    tile0->Roughness = 0.3f;
    
    //定义箱子的材质
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
    //// 箱子渲染项
    //auto boxRitem = std::make_unique<RenderItem>();
    ///*XMStoreFloat4x4(&boxRitem->World, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(0.0f, 1.0f, 0.0f));
    //XMStoreFloat4x4(&boxRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));*/
    //boxRitem->ObjCBIndex = 0;
    //boxRitem->Mat = mMaterials["crate0"].get();
    //boxRitem->Geo = mGeometries["shapeGeo"].get();
    //boxRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    //boxRitem->InstanceCount = 1;//实例个数
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

    //// 地板渲染项
    //auto gridRitem = std::make_unique<RenderItem>();
    //gridRitem->ObjCBIndex = 1;
    //gridRitem->Mat = mMaterials["tile0"].get();
    //gridRitem->Geo = mGeometries["shapeGeo"].get();
    //gridRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    //gridRitem->InstanceCount = 1;//实例个数
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

    //// 球和柱子渲染项
    //auto CylRitem = std::make_unique<RenderItem>();
    //CylRitem->World = MathHelper::Identity4x4();
    //CylRitem->TexTransform = MathHelper::Identity4x4();
    //CylRitem->ObjCBIndex = 2;
    //CylRitem->Mat = mMaterials["bricks"].get();
    //CylRitem->Geo = mGeometries["shapeGeo"].get();
    //CylRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    //CylRitem->InstanceCount = 10;//实例个数
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
    //SphereRitem->InstanceCount = 10;//实例个数
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
    //    leftCylRitem->InstanceCount = 0;//实例个数
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
    //    rightCylRitem->InstanceCount = 0;//实例个数
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
    //    leftSphereRitem->InstanceCount = 0;//实例个数
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
    //    rightSphereRitem->InstanceCount = 0;//实例个数
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
    // 骷髅渲染项
    auto skullRitem = std::make_unique<RenderItem>();
    skullRitem->World = MathHelper::Identity4x4();
    skullRitem->TexTransform = MathHelper::Identity4x4();
    //skullRitem->ObjCBIndex = 0;
    skullRitem->Mat = mMaterials["skullMat"].get();
    skullRitem->Geo = mGeometries["skullGeo"].get();
    skullRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    skullRitem->InstanceCount = 0;//实例个数
    skullRitem->IndexCount = skullRitem->Geo->DrawArgs["skull"].IndexCount;
    skullRitem->StartIndexLocation = skullRitem->Geo->DrawArgs["skull"].StartIndexLocation;
    skullRitem->BaseVertexLocation = skullRitem->Geo->DrawArgs["skull"].BaseVertexLocation;
    skullRitem->Bounds = skullRitem->Geo->DrawArgs["skull"].Bounds;

    // 生成实例数据
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
                // 沿三维栅格实例化的位置
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

    //move会释放Ritem内存，所以必须在mRitemLayer之后执行
    mAllRitems.push_back(std::move(skullRitem));
}

void GameDevelopApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
    /*UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

    auto objectCB = mCurrFrameResource->ObjectCB->Resource();*/

    for (size_t i = 0; i < ritems.size(); ++i)
    {
        auto ri = ritems[i];

        // 设置顶点缓冲区
        cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
        // 设置索引缓冲区
        cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
        // 设置图元拓扑
        cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

        //设置其他的根参数
        // 设置根描述符，将根描述符与资源绑定 这里我们绑定根描述符，直接上传GPU里的常量数据。
        // 只需要绑定objConstants
        /*D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress();
        objCBAddress += ri->ObjCBIndex * objCBByteSize;*/
        //D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex * matCBByteSize;
        // objCBAddress子资源的地址
        // 世界矩阵不同
        // cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress);
        //// 3指的是根签名数组下标
        //cmdList->SetGraphicsRootConstantBufferView(3, matCBAddress);
        
        //设置实例化缓冲区的根签名，对于结构化缓冲区来说，可以绕过描述符堆而直接将其设置成描述符
        auto instanceBuffer = mCurrFrameResource->InstanceBuffer->Resource();
        cmdList->SetGraphicsRootShaderResourceView(0, //根参数索引
            instanceBuffer->GetGPUVirtualAddress());
        // 绘制顶点实例
        //绘制顶点（通过索引缓冲区绘制）
        cmdList->DrawIndexedInstanced(ri->IndexCount, ri->InstanceCount, ri->StartIndexLocation, ri->BaseVertexLocation, 0);//实例化的高级技术，暂时设置为0
    }
}

// 返回顶点缓冲区视图
D3D12_VERTEX_BUFFER_VIEW GameDevelopApp::VertexBufferView() const
{
    D3D12_VERTEX_BUFFER_VIEW vbv;
    vbv.BufferLocation = mVertexBufferGPU->GetGPUVirtualAddress();//顶点缓冲区资源虚拟地址
    vbv.StrideInBytes = mVertexByteStride;//每个顶点元素所占用的字节数
    vbv.SizeInBytes = mVertexBufferByteSize;//顶点缓冲区大小（所有顶点数据大小）

    return vbv;
}
// 返回索引缓冲区视图
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
    //过滤器POINT,寻址模式WRAP的静态采样器
    CD3DX12_STATIC_SAMPLER_DESC pointWarp(0,	//着色器寄存器
        D3D12_FILTER_MIN_MAG_MIP_POINT,		//过滤器类型为POINT(常量插值)
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,	//U方向上的寻址模式为WRAP（重复寻址模式）
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,	//V方向上的寻址模式为WRAP（重复寻址模式）
        D3D12_TEXTURE_ADDRESS_MODE_WRAP);	//W方向上的寻址模式为WRAP（重复寻址模式）

    //过滤器POINT,寻址模式CLAMP的静态采样器
    CD3DX12_STATIC_SAMPLER_DESC pointClamp(1,	//着色器寄存器
        D3D12_FILTER_MIN_MAG_MIP_POINT,		//过滤器类型为POINT(常量插值)
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,	//U方向上的寻址模式为CLAMP（钳位寻址模式）
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,	//V方向上的寻址模式为CLAMP（钳位寻址模式）
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP);	//W方向上的寻址模式为CLAMP（钳位寻址模式）

    //过滤器LINEAR,寻址模式WRAP的静态采样器
    CD3DX12_STATIC_SAMPLER_DESC linearWarp(2,	//着色器寄存器
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,		//过滤器类型为LINEAR(线性插值)
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,	//U方向上的寻址模式为WRAP（重复寻址模式）
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,	//V方向上的寻址模式为WRAP（重复寻址模式）
        D3D12_TEXTURE_ADDRESS_MODE_WRAP);	//W方向上的寻址模式为WRAP（重复寻址模式）

    //过滤器LINEAR,寻址模式CLAMP的静态采样器
    CD3DX12_STATIC_SAMPLER_DESC linearClamp(3,	//着色器寄存器
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,		//过滤器类型为LINEAR(线性插值)
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,	//U方向上的寻址模式为CLAMP（钳位寻址模式）
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,	//V方向上的寻址模式为CLAMP（钳位寻址模式）
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP);	//W方向上的寻址模式为CLAMP（钳位寻址模式）

    //过滤器ANISOTROPIC,寻址模式WRAP的静态采样器
    CD3DX12_STATIC_SAMPLER_DESC anisotropicWarp(4,	//着色器寄存器
        D3D12_FILTER_ANISOTROPIC,			//过滤器类型为ANISOTROPIC(各向异性)
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,	//U方向上的寻址模式为WRAP（重复寻址模式）
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,	//V方向上的寻址模式为WRAP（重复寻址模式）
        D3D12_TEXTURE_ADDRESS_MODE_WRAP);	//W方向上的寻址模式为WRAP（重复寻址模式）

    //过滤器LINEAR,寻址模式CLAMP的静态采样器
    CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(5,	//着色器寄存器
        D3D12_FILTER_ANISOTROPIC,			//过滤器类型为ANISOTROPIC(各向异性)
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,	//U方向上的寻址模式为CLAMP（钳位寻址模式）
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,	//V方向上的寻址模式为CLAMP（钳位寻址模式）
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP);	//W方向上的寻址模式为CLAMP（钳位寻址模式）

    return{ pointWarp, pointClamp, linearWarp, linearClamp, anisotropicWarp, anisotropicClamp };
}

//对坐标的x和z值做正余弦函数，使生成一个循环震荡的波峰波谷。
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