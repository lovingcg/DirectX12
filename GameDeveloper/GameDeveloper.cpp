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

    LoadTextures();
    BuildRootSignature();
    BuildDescriptorHeaps();
    BuildShadersAndInputLayout();
    BuildRoomGeometry();
    BuildSkullGeometry();
    BuildMaterials();//构建材质
    // BuildRenderItems()函数必须在CreateConstantBufferView()函数之前，不然拿不到allRitems.size()的值
    BuildRenderItems();
    BuildFrameResources();

    //有了根描述符，我们就不需要声明CBV，也就不需要创建CBV堆了，我们在Init中把它们注释掉。
   /* BuildDescriptorHeaps();
    BuildConstantBuffers();*/
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
    cmdList->ClearRenderTargetView(CurrentBackBufferView(), clearColor, 0, nullptr);// 清除RT背景色为暗红，并且不设置裁剪矩形
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

    UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));

    // 绑定passCB根描述符
    auto passCB = mCurrFrameResource->PassCB->Resource();
    // 寄存器槽号（着色器register（b2））要和根签名设置时的一一对应
    cmdList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());

    //分别设置PSO并绘制对应渲染项
    //绘制不透明物体渲染项（地板、墙、镜子、原骷髅）
    DrawRenderItems(cmdList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);

    // 使可见镜子像素在模板缓冲上是数值1
    cmdList->OMSetStencilRef(1);//设置模板Ref值为1（作为Ref替换值，作为之后渲染物体的模板值）
    cmdList->SetPipelineState(mPSOs["markStencilMirrors"].Get());//设置镜子模板PSO
    DrawRenderItems(cmdList.Get(), mRitemLayer[(int)RenderLayer::Mirrors]);

    //设置根描述符（镜像灯光的reflectPassCB）
    cmdList->SetGraphicsRootConstantBufferView(2,//根参数索引
        passCB->GetGPUVirtualAddress() + 1 * passCBByteSize);
    //绘制镜像物体渲染项（镜像骷髅）
    cmdList->SetPipelineState(mPSOs["drawStencilReflections"].Get());//设置镜像骷髅模板PSO
    DrawRenderItems(cmdList.Get(), mRitemLayer[(int)RenderLayer::Reflected]);

    //还原passCB和Ref模板值
    //还原镜像光照和Ref模板值
    cmdList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());
    // 并且也不做模板测试（其实是做了，因为都是0，所以永远通过）
    cmdList->OMSetStencilRef(0);

    //绘制镜子混合
    cmdList->SetPipelineState(mPSOs["transparent"].Get());
    DrawRenderItems(cmdList.Get(), mRitemLayer[(int)RenderLayer::Transparent]);

    cmdList->SetPipelineState(mPSOs["shadow"].Get());
    DrawRenderItems(cmdList.Get(), mRitemLayer[(int)RenderLayer::Shadow]);

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
                mTheta = 1.5f * XM_PI;
                mPhi = XM_PIDIV4;
                mRadius = 5.0f;
                mMainPassCB.alpha = 0.3f;
            }
            ImGui::SliderFloat("Scale", &mRadius, 3.0f, 200.0f);// 拖动控制物体大小

            ImGui::Text("mTheta: %.2f degrees", mTheta);// 显示文字，用于描述下面的控件 
            ImGui::SliderFloat("##1", &mTheta, -XM_PI, XM_PI, ""); // 不显示控件标题，但使用##来避免标签重复  空字符串避免显示数字

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
        mTheta += dx;
        mPhi += dy;

        // 限制角度mPhi的范围在（0.1， Pi-0.1）
        mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
    }
    else if ((btnState & MK_RBUTTON) != 0)// 如果在右键按下状态
    {
        // 将鼠标的移动距离换算成缩放大小，0.005为调节阈值
        float dx = 0.2f * static_cast<float>(x - mLastMousePos.x);
        float dy = 0.2f * static_cast<float>(y - mLastMousePos.y);
        // 根据鼠标输入更新摄像机可视范围半径
        mRadius += dx - dy;
        //限制可视范围半径
        mRadius = MathHelper::Clamp(mRadius, 3.0f, 200.0f);
    }
    // 将当前鼠标坐标赋值给“上一次鼠标坐标”，为下一次鼠标操作提供先前值
    mLastMousePos.x = x;
    mLastMousePos.y = y;
}

void GameDevelopApp::OnResize()
{
    D3DApp::OnResize();

    //将投影矩阵的构建放入其中
    //（之前投影矩阵在Update函数中构建，但是由于投影矩阵不需要每帧改变，而是在窗口改变时才会改变
    //所以应该放在OnResize函数中）
    XMMATRIX P = XMMatrixPerspectiveFovLH(fov * MathHelper::Pi, static_cast<float>(mClientWidth) / mClientHeight, 1.0f, 1000.0f);
    XMStoreFloat4x4(&mProj, P);
}

// 每一帧的操作
void GameDevelopApp::Update()
{ 
    //OnKeyboardInputMove(gt);
    UpdateCamera(gt);

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

    UpdateObjectCBs(gt);
    UpdateMaterialCBs(gt);
    UpdateMainPassCB(gt);
    UpdateReflectedPassCB(gt);
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

    // 防止移动到平面下面
    mSkullTranslation.y = MathHelper::Max(mSkullTranslation.y, 0.0f);

    // 更新新的世界矩阵
    XMMATRIX skullRotate = XMMatrixRotationY(mSkullrotate * MathHelper::Pi);
    XMMATRIX skullScale = XMMatrixScaling(0.5f, 0.5f, 0.5f);
    XMMATRIX skullOffset = XMMatrixTranslation(mSkullTranslation.x, mSkullTranslation.y, mSkullTranslation.z);

    // 矩阵相乘顺序会影响物体绕哪个旋转 这里我们要求物体按照自己本身旋转
    XMMATRIX skullWorld = skullRotate * skullScale * skullOffset;
    XMStoreFloat4x4(&mSkullRitem->World, skullWorld);

    // 更新反射的世界矩阵
    XMVECTOR mirrorPlane = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f); // xy plane
    XMMATRIX R = XMMatrixReflect(mirrorPlane);
    XMStoreFloat4x4(&mReflectedSkullRitem->World, skullWorld * R);

    // 更新阴影矩阵
    XMVECTOR shadowPlane = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f); // xz plane
    XMVECTOR toMianLight = -XMLoadFloat3(&mMainPassCB.Lights[0].Direction);
    // DX12提供了XMMatrixShadow函数来构建阴影
    XMMATRIX S = XMMatrixShadow(shadowPlane, toMianLight);
    // yOffset是为了避免阴影和地板模型重面闪烁的Bug
    XMMATRIX shadowOffsetY = XMMatrixTranslation(0.0f, 0.0001f, 0.0f);
    XMStoreFloat4x4(&mShadowedSkullRitem->World, skullWorld * S * shadowOffsetY);

    // 反射阴影的世界矩阵多乘以反射矩阵R就行了
    XMStoreFloat4x4(&mReflectedShadowedSkullRitem->World, skullWorld * R * S * shadowOffsetY);

    mSkullRitem->NumFramesDirty = gNumFrameResources;
    mReflectedSkullRitem->NumFramesDirty = gNumFrameResources;
    mShadowedSkullRitem->NumFramesDirty = gNumFrameResources;
    mReflectedShadowedSkullRitem->NumFramesDirty = gNumFrameResources;
}

void GameDevelopApp::UpdateCamera(const _GameTimer::GameTimer& gt)
{
    // 将球面坐标转换为笛卡尔坐标
    mEyePos.x = mRadius * sinf(mPhi) * cosf(mTheta);
    mEyePos.z = mRadius * sinf(mPhi) * sinf(mTheta);
    mEyePos.y = mRadius * cosf(mPhi);
   
    XMVECTOR pos = XMVectorSet(mEyePos.x, mEyePos.y, mEyePos.z, 1.0f);
    XMVECTOR target = XMVectorZero();
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    //构建观察矩阵
    XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
    XMStoreFloat4x4(&mView, view);
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

void GameDevelopApp::UpdateObjectCBs(const _GameTimer::GameTimer& gt)
{
    auto currObjectCB = mCurrFrameResource->ObjectCB.get();
    for (auto& e : mAllRitems)
    {
        //只有在常量发生更改时才更新cbuffer数据。
        //这需要按帧资源进行跟踪。
        if (e->NumFramesDirty > 0)
        {
            XMMATRIX world = XMLoadFloat4x4(&e->World);
            // 将texTransform更新至ObjCB，这样数据就传入了常量缓冲区中了，并最终传入着色器
            XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

            ObjectConstants objConstants;
            XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
            XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));
            //将数据拷贝至GPU缓存
            //e->ObjCBIndex保证世界矩阵与单个几何体一一对应
            currObjectCB->CopyData(e->ObjCBIndex, objConstants);

            //下一个FrameResource也需要更新。
            e->NumFramesDirty--;
        }
    }
}

//实现思路和objConstants类似，遍历materials无序列表，提取出Material指针，
//并将其赋值给常量结构体中对应元素，最终使用CopyData函数将数据传至GPU，
//最后记得numFramesDirty--，因为这样才能满足每个帧资源都能得到更新
void GameDevelopApp::UpdateMaterialCBs(const _GameTimer::GameTimer& gt)
{
    auto currMaterialCB = mCurrFrameResource->MaterialCB.get();
    for (auto& e : mMaterials)
    {
        Material* mat = e.second.get();//获得键值对的值，即Material指针（智能指针转普通指针）
        if (mat->NumFramesDirty > 0)
        {
            XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

            //将定义的材质属性传给常量结构体中的元素
            MaterialConstants matConstants;
            matConstants.DiffuseAlbedo = mat->DiffuseAlbedo;
            matConstants.FresnelR0 = mat->FresnelR0;
            matConstants.Roughness = mat->Roughness;
            XMStoreFloat4x4(&matConstants.MatTransform, XMMatrixTranspose(matTransform));

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
    XMMATRIX view = XMLoadFloat4x4(&mView);
    //XMMATRIX proj = XMLoadFloat4x4(&mProj);
    XMMATRIX proj = XMMatrixPerspectiveFovLH(fov * MathHelper::Pi, static_cast<float>(mClientWidth) / mClientHeight, 1.0f, 1000.0f);
    XMMATRIX viewProj = XMMatrixMultiply(view, proj);

    XMStoreFloat4x4(&mMainPassCB.viewProj, XMMatrixTranspose(viewProj));

    // 赋值light参数
    mMainPassCB.EyePosW = mEyePos;
    mMainPassCB.AmbientLight = { 0.25f,0.25f,0.35f,1.0f };
    //mMainPassCB.TotalTime = gt.TotalTime();
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

void GameDevelopApp::UpdateReflectedPassCB(const _GameTimer::GameTimer& gt)
{
    mReflectedPassCB = mMainPassCB;

    XMVECTOR mirrorPlane = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);//镜像面片的对称轴坐标
    XMMATRIX R = XMMatrixReflect(mirrorPlane);

    //镜像灯光
    for (int i = 0; i < 3; ++i)
    {
        XMVECTOR lightDir = XMLoadFloat3(&mMainPassCB.Lights[i].Direction);
        XMVECTOR reflectedLightDir = XMVector3TransformNormal(lightDir, R);
        XMStoreFloat3(&mReflectedPassCB.Lights[i].Direction, reflectedLightDir);
    }

    // 存储在索引1中的反射
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
    //    // 前
    //    0, 1, 2,
    //    0, 2, 3,

    //    // 后
    //    4, 6, 5,
    //    4, 7, 6,

    //    // 左
    //    4, 5, 1,
    //    4, 1, 0,

    //    // 右
    //    3, 2, 6,
    //    3, 6, 7,

    //    // 上
    //    1, 5, 6,
    //    1, 6, 2,

    //    // 下
    //    4, 0, 3,
    //    4, 3, 7
    //};

    //mIndexCount = (UINT)indices.size();

    //const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
    //const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

    //// 创建顶点缓冲区描述符
    //// 顶点数据->CPU
    //// 在CPU中开辟一段序列化空间 mVertexBufferCPU存储空间的COM接口指针
    //ThrowIfFailed(D3DCreateBlob(vbByteSize, &mVertexBufferCPU));
    //// 顶点数据拷贝到CPU
    //CopyMemory(mVertexBufferCPU->GetBufferPointer(),// 目标空间指针
    //    vertices.data(),// 数据开始指针 
    //    vbByteSize);// 大小byte
    //// 创建顶点默认堆 CPU->GPU
    //mVertexBufferGPU = d3dUtil::CreateDefaultBuffer(d3dDevice.Get(), cmdList.Get(), vertices.data(), vbByteSize, mVertexBufferUploader);

    //// 创建索引缓冲区描述符
    ////把索引数据放到索引缓冲区里让GPU访问，因为索引一般不会变，所以我们把也把它放在默认堆中
    //ThrowIfFailed(D3DCreateBlob(ibByteSize, &mIndexBufferCPU));
    //CopyMemory(mIndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);
    //mIndexBufferGPU = d3dUtil::CreateDefaultBuffer(d3dDevice.Get(), cmdList.Get(), indices.data(), ibByteSize, mIndexBufferUploader);

    //mVertexByteStride = sizeof(Vertex);
    //mVertexBufferByteSize = vbByteSize;
    //mIndexFormat = DXGI_FORMAT_R16_UINT;
    //mIndexBufferByteSize = ibByteSize;
    
    //创建BOX几何体
    ProceduralGeometry proceGeo;
    ProceduralGeometry::MeshData box = proceGeo.CreateBox(8.0f, 8.0f, 8.0f, 3);

    //将顶点数据传入Vertex结构体的数据元素
    size_t verticesCount = box.Vertices.size(); // 总顶点数
    std::vector<Vertex> vertices(verticesCount);//创建顶点列表
    for (size_t i = 0; i < verticesCount; i++)
    {
        vertices[i].Pos = box.Vertices[i].Position;
        vertices[i].Normal = box.Vertices[i].Normal;
        vertices[i].TexC = box.Vertices[i].TexC;
    }

    //创建索引列表,并初始化
    std::vector<std::uint16_t> indices = box.GetIndices16();

    //顶点列表大小
    const UINT vbByteSize = (UINT)verticesCount * sizeof(Vertex);
    //索引列表大小
    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

    //绘制三参数
    SubmeshGeometry submesh;
    submesh.BaseVertexLocation = 0;
    submesh.StartIndexLocation = 0;
    submesh.IndexCount = (UINT)indices.size();

    //赋值MeshGeometry结构中的数据元素
    auto geo = std::make_unique<MeshGeometry>();	//指向MeshGeometry的指针
    geo->Name = "boxGeo";
    geo->VertexByteStride = sizeof(Vertex);//单个顶点的大小
    geo->VertexBufferByteSize = vbByteSize;
    geo->IndexBufferByteSize = ibByteSize;
    geo->IndexFormat = DXGI_FORMAT_R16_UINT;
    geo->DrawArgs["box"] = submesh;

    ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));//在CPU上创建顶点缓存空间
    CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);//将顶点数据拷贝至CPU系统内存
    geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(d3dDevice.Get(),
        cmdList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);//将顶点数据从CPU拷贝至GPU

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));//在CPU上创建索引缓存空间
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);//将索引数据拷贝至CPU系统内存
    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(d3dDevice.Get(),
        cmdList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);//将索引数据从CPU拷贝至GPU

    //装入总的几何体映射表
    mGeometries["boxGeo"] = std::move(geo);
}

//void GameDevelopApp::BuildGeometry()
//{
//    ProceduralGeometry proceGeo;
//    //float width, float height, float depth, uint32 numSubdivisions
//    ProceduralGeometry::MeshData box = proceGeo.CreateBox(1.5f, 0.5f, 1.5f, 3);
//    //float width, float depth, uint32 m, uint32 n
//    ProceduralGeometry::MeshData grid = proceGeo.CreateGrid(20.0f, 30.0f, 60, 40);
//    //float radius, uint32 sliceCount, uint32 stackCount 半径，切片数量，堆叠层数
//    ProceduralGeometry::MeshData sphere = proceGeo.CreateSphere(0.5f, 20, 20);
//    //float bottomRadius, float topRadius, float height, uint32 sliceCount, uint32 stackCount
//    ProceduralGeometry::MeshData cylinder = proceGeo.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);
//
//    //计算单个几何体顶点在总顶点数组中的偏移量,顺序为：box、grid、sphere、cylinder
//    UINT boxVertexOffset = 0;
//    UINT gridVertexOffset = (UINT)box.Vertices.size();
//    UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
//    UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();
//
//    //计算单个几何体索引在总索引数组中的偏移量,顺序为：box、grid、sphere、cylinder
//    UINT boxIndexOffset = 0;
//    UINT gridIndexOffset = (UINT)box.Indices32.size();
//    UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
//    UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();
//
//    //cmdList->DrawIndexedInstanced(mIndexCount,// 每个实例要绘制的索引数
//    //    1,// 实例化个数
//    //    0,// 起始索引位置
//    //    0,// 子物体起始索引在全局索引中的位置
//    //    0);// 实例化的高级技术
//    // (即DrawIndexedInstanced函数中的1、3、4参数)
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
//    //创建一个总的顶点缓存vertices，并将4个子物体的顶点数据存入其中
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
//    // 同理创建一个总的索引缓存indices，并将4个子物体的索引数据存入其中
//    std::vector<std::uint16_t> indices;
//    // 在指定位置loc前插入区间[start, end)的所有元素 
//    indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
//    indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
//    indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
//    indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));
//
//    const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
//    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);
//
//    // 创建MeshGeometry实例
//    geo = std::make_unique<MeshGeometry>();
//    geo->Name = "shapeGeo";
//    // 把顶点/索引数据上传到GPU
//    ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
//    CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);
//
//    ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
//    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);
//    // 创建顶点默认堆 CPU->GPU
//    geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(d3dDevice.Get(), cmdList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);
//    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(d3dDevice.Get(), cmdList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);
//
//    geo->VertexByteStride = sizeof(Vertex);
//    geo->VertexBufferByteSize = vbByteSize;
//    geo->IndexFormat = DXGI_FORMAT_R16_UINT;
//    geo->IndexBufferByteSize = ibByteSize;
//
//    // 并将封装好的4个几何体的SubmeshGeometry对象赋值给无序映射表。
//    geo->DrawArgs["box"] = boxSubmesh;
//    geo->DrawArgs["grid"] = gridSubmesh;
//    geo->DrawArgs["sphere"] = sphereSubmesh;
//    geo->DrawArgs["cylinder"] = cylinderSubmesh;
//    // unique_ptr通过move转移资源
//    mGeometries[geo->Name] = std::move(geo);
//}

void GameDevelopApp::BuildLandGeometry()
{
    //创建几何体，并将顶点和索引列表存储在MeshData中
    ProceduralGeometry proceGeo;
    ProceduralGeometry::MeshData grid = proceGeo.CreateGrid(160.0f, 160.0f, 50, 50);

    // 创建全局顶点/索引数据
    std::vector<Vertex> vertices(grid.Vertices.size());
    for (size_t i = 0; i < grid.Vertices.size(); ++i)
    {
        auto& p = grid.Vertices[i].Position;
        vertices[i].Pos = p;
        vertices[i].Pos.y = GetHillsHeight(p.x, p.z);
        vertices[i].Normal = GetHillsNormal(p.x, p.z);
        vertices[i].TexC = grid.Vertices[i].TexC;

        //根据顶点不同的y值，给予不同的顶点色(不同海拔对应的颜色)
        //if (vertices[i].Pos.y < -10.0f)
        //{
        //    //沙滩色
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
    // 数据从内存拷贝到CPU
    ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
    CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

    // 从CPU上传到GPU
    geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(d3dDevice.Get(), 
        cmdList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);
    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(d3dDevice.Get(),
        cmdList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

    geo->VertexByteStride = sizeof(Vertex);
    geo->VertexBufferByteSize = vbByteSize;
    geo->IndexFormat = DXGI_FORMAT_R16_UINT;
    geo->IndexBufferByteSize = ibByteSize;

    //封装grid的顶点、索引数据
    SubmeshGeometry submesh;
    submesh.IndexCount = (UINT)indices.size();
    submesh.StartIndexLocation = 0;
    submesh.BaseVertexLocation = 0;

    geo->DrawArgs["grid"] = submesh;

    mGeometries["landGeo"] = std::move(geo);
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

    std::vector<Vertex> vertices(vcount);
    //顶点列表赋值
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

    geo->DrawArgs["skull"] = submesh;

    mGeometries[geo->Name] = std::move(geo);
}

void GameDevelopApp::BuildWavesGeometryBuffers()
{
    //初始化索引列表（每个三角形3个索引）
    std::vector<std::uint16_t> indices(3 * mWaves->TriangleCount());
    assert(mWaves->VertexCount() < 0x0000ffff);//顶点索引数大于65536则中止程序

    //填充索引列表
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

            k += 6;//下一个四边形
        }
    }

    UINT vbByteSize = mWaves->VertexCount() * sizeof(Vertex);
    UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "waterGeo";
    //动态设置
    geo->VertexBufferCPU = nullptr;
    geo->VertexBufferGPU = nullptr;

    // 由于波动只是顶点坐标的改变，并不会改变顶点的索引，所以索引缓存还是静态的
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

//如同RTV，DSV，常量缓冲区也需要描述符来指定缓冲区的属性。而描述符需要描述符堆来存放。
//所以，先创建CBV堆。
void GameDevelopApp::BuildDescriptorHeaps()
{
    //创建SRV堆
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = 4;//加入新的数量得改
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    srvHeapDesc.NodeMask = 0;
    ThrowIfFailed(d3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

    // 将之前定义好的纹理资源转换成comptr存起来
    auto bricksTex = mTextures["bricksTex"]->Resource;
    auto checkboardTex = mTextures["checkboardTex"]->Resource;
    auto iceTex = mTextures["iceTex"]->Resource;
    auto white1x1Tex = mTextures["white1x1Tex"]->Resource;

    //SRV堆中子SRV（草地的SRV）的句柄
    CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

    //SRV描述结构体
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;//采样后分量顺序不改变
    srvDesc.Format = bricksTex->GetDesc().Format;//视图的默认格式
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;//2D贴图
    srvDesc.Texture2D.MostDetailedMip = 0;//细节最详尽的mipmap层级为0
    srvDesc.Texture2D.MipLevels = bricksTex->GetDesc().MipLevels;//mipmap层级数量
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;//可访问的mipmap最小层级数为0
    //创建“草地”的SRV
    d3dDevice->CreateShaderResourceView(bricksTex.Get(), &srvDesc, hDescriptor);

    //SRV堆中子SRV（湖水的SRV）的句柄,继续偏移一个SRV
    hDescriptor.Offset(1, mCbvSrvDescriptorSize);
    //SRV描述结构体修改
    srvDesc.Format = checkboardTex->GetDesc().Format;//视图的默认格式
    srvDesc.Texture2D.MipLevels = checkboardTex->GetDesc().MipLevels;//mipmap层级数量
    // 创建湖水SRV
    d3dDevice->CreateShaderResourceView(checkboardTex.Get(), &srvDesc, hDescriptor);

    //SRV堆中子SRV（板条箱的SRV）的句柄,继续偏移一个SRV
    hDescriptor.Offset(1, mCbvSrvDescriptorSize);
    //SRV描述结构体修改
    srvDesc.Format = iceTex->GetDesc().Format;//视图的默认格式
    srvDesc.Texture2D.MipLevels = iceTex->GetDesc().MipLevels;//mipmap层级数量
    //创建“板条箱”的SRV
    d3dDevice->CreateShaderResourceView(iceTex.Get(), &srvDesc, hDescriptor);

    //SRV堆中子SRV（板条箱的SRV）的句柄,继续偏移一个SRV
    hDescriptor.Offset(1, mCbvSrvDescriptorSize);
    //SRV描述结构体修改
    srvDesc.Format = white1x1Tex->GetDesc().Format;//视图的默认格式
    srvDesc.Texture2D.MipLevels = white1x1Tex->GetDesc().MipLevels;//mipmap层级数量
    //创建“板条箱”的SRV
    d3dDevice->CreateShaderResourceView(white1x1Tex.Get(), &srvDesc, hDescriptor);
}

// 创建常量描述符
//void GameDevelopApp::BuildConstantBuffers()
//{
//    UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
//
//    UINT objCount = (UINT)mOpaqueRitems.size();
//
//    //考虑帧资源因素，所以套2层循环，外层循环控制帧资源，内层循环控制渲染项
//    //此时的堆中地址要考虑第几个帧资源元素，所以heapIndex的值是objectCount * frameIndex + i。
//    for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex)
//    {
//        auto objectCB = mFrameResources[frameIndex]->ObjectCB->Resource();
//        for (UINT i = 0; i < objCount; ++i)
//        {
//            //获得常量缓冲区首地址
//            D3D12_GPU_VIRTUAL_ADDRESS cbAddress = objectCB->GetGPUVirtualAddress();
//
//            // 根据索引对cbv地址进行偏移
//            // 子物体在常量缓冲区中的地址
//            cbAddress += i * objCBByteSize;
//
//            //CBV堆中的CBV元素索引
//            int heapIndex = frameIndex * objCount + i;
//            auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCbvHeap->GetCPUDescriptorHandleForHeapStart());//获得CBV堆首地址
//            handle.Offset(heapIndex, cbv_srv_uavDescriptorSize);//CBV句柄（CBV堆中的CBV元素地址）
//
//            // 描述cbv
//            D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
//            cbvDesc.BufferLocation = cbAddress;
//            cbvDesc.SizeInBytes = objCBByteSize;
//
//            // 创建cbv
//            d3dDevice->CreateConstantBufferView(&cbvDesc, handle);
//        }
//    }
//
//    // 创建perPassCBV
//    UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));
//
//    //有帧资源因素，所以我们要加一层循环，来生成3个passCBV
//    for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex)
//    {
//        auto passCB = mFrameResources[frameIndex]->PassCB->Resource();
//        D3D12_GPU_VIRTUAL_ADDRESS cbAddress = passCB->GetGPUVirtualAddress();
//
//        int heapIndex = mPassCbvOffset + frameIndex;
//        auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCbvHeap->GetCPUDescriptorHandleForHeapStart());//获得CBV堆首地址
//        handle.Offset(heapIndex, cbv_srv_uavDescriptorSize);//CBV句柄（CBV堆中的CBV元素地址）
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
    /*砖墙纹理*/
    auto bricksTex = std::make_unique<Texture>();
    bricksTex->Name = "bricksTex";
    bricksTex->Filename = L"Textures\\bricks3.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(d3dDevice.Get(),
        cmdList.Get(), bricksTex->Filename.c_str(),
        bricksTex->Resource, bricksTex->UploadHeap));
    /*地砖纹理*/
    auto checkboardTex = std::make_unique<Texture>();
    checkboardTex->Name = "checkboardTex";
    checkboardTex->Filename = L"Textures\\checkboard.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(d3dDevice.Get(),
        cmdList.Get(), checkboardTex->Filename.c_str(),
        checkboardTex->Resource, checkboardTex->UploadHeap));
    /*镜面纹理*/
    auto iceTex = std::make_unique<Texture>();
    iceTex->Name = "iceTex";
    iceTex->Filename = L"Textures\\ice.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(d3dDevice.Get(),
        cmdList.Get(), iceTex->Filename.c_str(),
        iceTex->Resource, iceTex->UploadHeap));
    /*骨骼纹理*/
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

//根签名描述清楚了渲染管线或者说Shader编译后的执行代码需要的各种资源以什么样的方式传入以及如何在内存、显存中布局
//根签名其实就是将着色器需要用到的数据绑定到对应的寄存器槽上，供着色器访问。
// 主要指定的是GPU上对应的寄存器
//根签名分为描述符表、根描述符、根常量，这里使用描述符表来初始化根签名。

//根签名作用是将常量数据绑定至寄存器槽，供着色器程序访问。
//因为现在我们有2个常量数据结构体，所以要创建2个元素的根参数，即2个CBV表，并绑定2个寄存器槽。
void GameDevelopApp::BuildRootSignature()
{
    //创建由单个CBV所组成的描述符表
    CD3DX12_DESCRIPTOR_RANGE texTable;
    texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,// 描述符类型
        1,// 描述符数量
        0);// 描述符所绑定的寄存器槽号

    // 根参数可以是描述符表、根描述符、根常量
    CD3DX12_ROOT_PARAMETER slotRootParameter[4];
   
    slotRootParameter[0].InitAsDescriptorTable(1, // Range数量
        &texTable,// Range指针
        D3D12_SHADER_VISIBILITY_PIXEL);// 该资源只在像素着色器可读
    // 创建根描述符 好处：不需要创建CBV堆，而是直接设置根描述符即可指示要绑定的资源
    // 0 1 2表示寄存器槽号
    // 性能提示：从最频繁到最不频繁排序
    slotRootParameter[1].InitAsConstantBufferView(0);// ObjectConstants
    slotRootParameter[2].InitAsConstantBufferView(1);// PassConstants
    // 加了材质常量数据
    slotRootParameter[3].InitAsConstantBufferView(2);// MaterialConstants

    //在根签名函数中，传入静态采样器，并将其与根签名绑定，为最终传入着色器做准备
    auto staticSamplers = GetStaticSamplers();

    ////创建由单个CBV所组成的描述符表
    //CD3DX12_DESCRIPTOR_RANGE objCbvTable;
    ////类似于opengl中的布局分布
    //objCbvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV,// 描述符类型
    //    1,// 描述符数量
    //    0);// 描述符所绑定的寄存器槽号
    //slotRootParameter[0].InitAsDescriptorTable(1, &objCbvTable);

    //CD3DX12_DESCRIPTOR_RANGE passCbvTable;
    //passCbvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1);
    //slotRootParameter[1].InitAsDescriptorTable(1, &passCbvTable);

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
    // D3D_SHADER_MACRO结构体是名字和定义
    const D3D_SHADER_MACRO unfogalphaTestDefines[] =
    {
        "ALPHA_TEST", "1",
        NULL, NULL
    };

    // 将shader函数分成了3类，一类是顶点函数（standardVS），一类是不透明的片元函数（opaquePS），
    // 还有一类是alphaTest的片元函数（alphaTestPS）
    mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\color.hlsl", nullptr, "VS", "vs_5_0");
    mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\color.hlsl", defines, "PS", "ps_5_0");
    mShaders["alphaTestedPS"] = d3dUtil::CompileShader(L"Shaders\\color.hlsl", alphaTestDefines, "PS", "ps_5_0");

    mShaders["unfogopaquePS"] = d3dUtil::CompileShader(L"Shaders\\color.hlsl", unfogdefines, "PS", "ps_5_0");
    mShaders["unfogalphaTestedPS"] = d3dUtil::CompileShader(L"Shaders\\color.hlsl", unfogalphaTestDefines, "PS", "ps_5_0");

    //编译成可移植的字节码
    /*mVsByteCode = d3dUtil::CompileShader(L"Shaders\\color.hlsl", nullptr, "VS", "vs_5_0");
    mPsByteCode = d3dUtil::CompileShader(L"Shaders\\color.hlsl", nullptr, "PS", "ps_5_0");*/

    mInputLayout =
    {
        //0表示寄存器槽号为0
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        // 如何缩减顶点颜色精度，从128位减少到32位
        // { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        // 因为DXGI_FORMAT符号所列的值在内存中是用小端字节序来表示的，所以从最低有效字节写至最高有效字节，格式ARGB被表示为BGRA。
        //{ "COLOR", 0, DXGI_FORMAT_B8G8R8A8_UNORM, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };
}

void GameDevelopApp::BuildRoomGeometry()
{
    //顶点缓存
    std::array<Vertex, 40> vertices =
    {
        //地板模型的顶点Pos、Normal、TexCoord
        Vertex(-3.5f, 0.0f, -10.0f, 0.0f, 1.0f, 0.0f, 0.0f, 4.0f), // 0 
        Vertex(-3.5f, 0.0f,   0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f),
        Vertex(7.5f, 0.0f,   0.0f, 0.0f, 1.0f, 0.0f, 4.0f, 0.0f),
        Vertex(7.5f, 0.0f, -10.0f, 0.0f, 1.0f, 0.0f, 4.0f, 4.0f),

        //墙壁模型的顶点Pos、Normal、TexCoord
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

        //镜子模型的顶点Pos、Normal、TexCoord
        Vertex(-2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f), // 36
        Vertex(-2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
        Vertex(2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f),
        Vertex(2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f)
    };

    //索引缓存
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

    //三个物体的绘制三参数（合并顶点和索引缓冲区）
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

    //混合物体的PSO（需要混合）
    D3D12_GRAPHICS_PIPELINE_STATE_DESC transparentPsoDesc = opaquePsoDesc;

    D3D12_RENDER_TARGET_BLEND_DESC transparencyBlendDesc;
    transparencyBlendDesc.BlendEnable = true;//是否开启常规混合（默认值为false）
    transparencyBlendDesc.LogicOpEnable = false;//是否开启逻辑混合(默认值为false)
    transparencyBlendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;//RGB混合中的源混合因子Fsrc（这里取源颜色的alpha通道值）
    transparencyBlendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;//RGB混合中的目标混合因子Fdest（这里取1-alpha）
    transparencyBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;//RGB混合运算符(这里选择加法)
    transparencyBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;//alpha混合中的源混合因子Fsrc（取1）
    transparencyBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;//alpha混合中的目标混合因子Fsrc（取0）
    transparencyBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;//alpha混合运算符(这里选择加法)
    transparencyBlendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;//逻辑混合运算符(空操作，即不使用)
    transparencyBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;//后台缓冲区写入遮罩（没有遮罩，即全部写入）

    transparentPsoDesc.BlendState.RenderTarget[0] = transparencyBlendDesc;//赋值RenderTarget第一个元素，即对每一个渲染目标执行相同操作
    ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&transparentPsoDesc, IID_PPV_ARGS(&mPSOs["transparent"])));

    // 不加雾效
    D3D12_GRAPHICS_PIPELINE_STATE_DESC unfogtransparentPsoDesc = transparentPsoDesc;
    unfogtransparentPsoDesc.PS =
    {
        reinterpret_cast<BYTE*>(mShaders["unfogopaquePS"]->GetBufferPointer()),
        mShaders["unfogopaquePS"]->GetBufferSize()
    };// 待绑定的像素着色器

    unfogtransparentPsoDesc.BlendState.RenderTarget[0] = transparencyBlendDesc;
    ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&unfogtransparentPsoDesc, IID_PPV_ARGS(&mPSOs["unfogtransparent"])));

    //AlphaTest物体的PSO（不需要混合）
    D3D12_GRAPHICS_PIPELINE_STATE_DESC alphaTestedPsoDesc = opaquePsoDesc;//使用不透明物体的PSO初始化
    alphaTestedPsoDesc.PS =
    {
        reinterpret_cast<BYTE*>(mShaders["alphaTestedPS"]->GetBufferPointer()),
        mShaders["alphaTestedPS"]->GetBufferSize()
    };

    alphaTestedPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;//双面显示
    ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&alphaTestedPsoDesc, IID_PPV_ARGS(&mPSOs["alphaTested"])));

    // 不加雾效
    D3D12_GRAPHICS_PIPELINE_STATE_DESC unfogalphaTestedPsoDesc = alphaTestedPsoDesc;
    unfogalphaTestedPsoDesc.PS =
    {
        reinterpret_cast<BYTE*>(mShaders["unfogalphaTestedPS"]->GetBufferPointer()),
        mShaders["unfogalphaTestedPS"]->GetBufferSize()
    };

    unfogalphaTestedPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;//双面显示
    ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&unfogalphaTestedPsoDesc, IID_PPV_ARGS(&mPSOs["unfogalphaTested"])));

    ///PSO for opaque wireframe objects.
    D3D12_GRAPHICS_PIPELINE_STATE_DESC WireframePsoDesc = opaquePsoDesc;
    WireframePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&WireframePsoDesc, IID_PPV_ARGS(&mPSOs["wireframe"])));

    // PSO用于标记模板反射镜面
    CD3DX12_BLEND_DESC  mirrorBlendState(D3D12_DEFAULT);
    mirrorBlendState.RenderTarget[0].RenderTargetWriteMask = 0;//禁止颜色数据写入

    D3D12_DEPTH_STENCIL_DESC mirrorDSS;//深度模板测试
    mirrorDSS.DepthEnable = true;//开启深度测试
    mirrorDSS.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;//禁止深度写入
    mirrorDSS.DepthFunc = D3D12_COMPARISON_FUNC_LESS;//比较函数“小于”
    mirrorDSS.StencilEnable = true;//开启模板测试
    mirrorDSS.StencilReadMask = 0xff;//默认255，不屏蔽模板值
    mirrorDSS.StencilWriteMask = 0xff;//默认255，不屏蔽模板值

    mirrorDSS.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;//模板测试失败，保持原模板值
    mirrorDSS.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;//深度测试失败，保持原模板值
    mirrorDSS.FrontFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;//深度模板测试通过，替换Ref模板值
    mirrorDSS.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;//比较函数“永远通过测试”

    // 反面不渲染，随便写
    mirrorDSS.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    mirrorDSS.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    mirrorDSS.BackFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
    mirrorDSS.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC markMirrorsPsoDesc = opaquePsoDesc;
    markMirrorsPsoDesc.BlendState = mirrorBlendState;
    markMirrorsPsoDesc.DepthStencilState = mirrorDSS;
    ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&markMirrorsPsoDesc, IID_PPV_ARGS(&mPSOs["markStencilMirrors"])));

    // 设置镜像骷髅的PSO
    D3D12_DEPTH_STENCIL_DESC reflectionsDSS;
    reflectionsDSS.DepthEnable = true;
    reflectionsDSS.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    reflectionsDSS.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    reflectionsDSS.StencilEnable = true;
    reflectionsDSS.StencilReadMask = 0xff;
    reflectionsDSS.StencilWriteMask = 0xff;

    reflectionsDSS.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    reflectionsDSS.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    reflectionsDSS.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;//深度模板测试通过，保持原模板值模板值
    reflectionsDSS.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;//比较函数“等于”
    // 反面不渲染，随便写
    reflectionsDSS.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    reflectionsDSS.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    reflectionsDSS.BackFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    reflectionsDSS.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC drawReflectionsPsoDesc = opaquePsoDesc;
    drawReflectionsPsoDesc.DepthStencilState = reflectionsDSS;
    drawReflectionsPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;// 背面剔除
    // 告知Direct3D将逆时针绕序的三角形看作是正面 朝向，而将顺时针绕序的三角形看作背面朝向
    // 这实际上 是对法线的方向也进行了 “反射”，以此使镜像成为外向朝向
    drawReflectionsPsoDesc.RasterizerState.FrontCounterClockwise = true;//镜像翻转绕序
    ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&drawReflectionsPsoDesc, IID_PPV_ARGS(&mPSOs["drawStencilReflections"])));

    // 设置骷髅阴影的PSO
    D3D12_DEPTH_STENCIL_DESC shadowDSS;
    shadowDSS.DepthEnable = true;
    shadowDSS.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    shadowDSS.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    shadowDSS.StencilEnable = true;
    shadowDSS.StencilReadMask = 0xff;
    shadowDSS.StencilWriteMask = 0xff;

    shadowDSS.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;//模板测试失败，保持原模板值
    shadowDSS.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;//深度测试失败，保持原模板值
    // 解决多个片元多重混合的bug
    shadowDSS.FrontFace.StencilPassOp = D3D12_STENCIL_OP_INCR;//深度模板测试通过，模板值递增
    shadowDSS.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;//比较函数“等于”
    // 反面不渲染，随便写
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
        // 必须在BuildFrameResources函数中，正确填写子passCB的数量为2
        mFrameResources.push_back(std::make_unique<FrameResource>(d3dDevice.Get(), 2, (UINT)mAllRitems.size(),(UINT)mMaterials.size()));
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
    bricks->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
    bricks->Roughness = 0.25f;

    //定义砖墙的材质
    auto checkertile = std::make_unique<Material>();
    checkertile->Name = "checkertile";
    checkertile->MatCBIndex = 1;
    checkertile->DiffuseSrvHeapIndex = 1;
    checkertile->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    checkertile->FresnelR0 = XMFLOAT3(0.07f, 0.07f, 0.07f);
    checkertile->Roughness = 0.3f;

    //定义镜面的材质
    auto icemirror = std::make_unique<Material>();
    icemirror->Name = "icemirror";
    icemirror->MatCBIndex = 2;
    icemirror->DiffuseSrvHeapIndex = 2;
    icemirror->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.5f);// 镜面a通道0.5
    icemirror->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
    icemirror->Roughness = 0.5f;

    //定义骨头的材质
    auto skullMat = std::make_unique<Material>();
    skullMat->Name = "skullMat";
    skullMat->MatCBIndex = 3;
    skullMat->DiffuseSrvHeapIndex = 3;
    skullMat->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    skullMat->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
    skullMat->Roughness = 0.3f;
    
    //定义阴影的材质
    auto shadowMat = std::make_unique<Material>();
    shadowMat->Name = "shadowMat";
    shadowMat->MatCBIndex = 4;
    shadowMat->DiffuseSrvHeapIndex = 3;//贴图还是用骷髅的白色贴图
    shadowMat->DiffuseAlbedo = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.5f);// 反照率给黑色 最终颜色为黑色
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
    // 墙壁渲染项
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

    // 地板渲染项
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

    // 镜面渲染项
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
    mRitemLayer[(int)RenderLayer::Transparent].push_back(mirrorRitem.get());//最后绘制镜子用Transparent

    // 骨骼渲染项
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

    // 骨骼反射渲染项
    auto reflectedSkullRitem = std::make_unique<RenderItem>();
    *reflectedSkullRitem = *skullRitem;// 复制渲染项
    XMStoreFloat4x4(&reflectedSkullRitem->World, XMMatrixScaling(0.5f, 0.5f, 0.5f) * XMMatrixTranslation(-10.0f, 1.0f, 0.0f) * XMMatrixRotationY(1.57f));
    reflectedSkullRitem->TexTransform = MathHelper::Identity4x4();
    reflectedSkullRitem->ObjCBIndex = 4;
    mReflectedSkullRitem = reflectedSkullRitem.get();
    mRitemLayer[(int)RenderLayer::Reflected].push_back(reflectedSkullRitem.get());

    // 阴影骷髅渲染项
    auto shadowedSkullRitem = std::make_unique<RenderItem>();
    *shadowedSkullRitem = *skullRitem;
    shadowedSkullRitem->ObjCBIndex = 5;
    shadowedSkullRitem->Mat = mMaterials["shadowMat"].get();
    // 单独提取了mSkullShadowRitem指针，因为move会销毁内存，而此时的阴影矩阵还没有设置，所以先存一份出来，之后设置矩阵用
    mShadowedSkullRitem = shadowedSkullRitem.get();
    mRitemLayer[(int)RenderLayer::Shadow].push_back(shadowedSkullRitem.get());

    // 地板反射渲染项
    auto floorMirrorRitem = std::make_unique<RenderItem>();
    *floorMirrorRitem = *floorRitem;
    XMVECTOR mirrorPlane = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f); // xy plane
    XMMATRIX R = XMMatrixReflect(mirrorPlane);
    XMStoreFloat4x4(&floorMirrorRitem->World, R);
    floorMirrorRitem->ObjCBIndex = 6;
    mRitemLayer[(int)RenderLayer::Reflected].push_back(floorMirrorRitem.get());

    // 阴影反射渲染项
    auto reflectedShadowedSkullRitem = std::make_unique<RenderItem>();;
    *reflectedShadowedSkullRitem = *shadowedSkullRitem;
    reflectedShadowedSkullRitem->ObjCBIndex = 7;
    mReflectedShadowedSkullRitem = reflectedShadowedSkullRitem.get();
    mRitemLayer[(int)RenderLayer::Reflected].push_back(reflectedShadowedSkullRitem.get());
    
    //move会释放Ritem内存，所以必须在mRitemLayer之后执行
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

        // 设置顶点缓冲区
        cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
        // 设置索引缓冲区
        cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
        // 设置图元拓扑
        cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

        //将帧资源因素在DrawRenderItems函数中考虑进去
        // 在绘制渲染项中，我们以前在这里设置根参数表传递常量数据，这里我们绑定根描述符，直接上传GPU里的常量数据。
        /*UINT cbvIndex = mCurrFrameResourceIndex * (UINT)mOpaqueRitems.size() + ri->ObjCBIndex;
        auto handle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mCbvHeap->GetGPUDescriptorHandleForHeapStart());
        handle.Offset(cbvIndex, cbv_srv_uavDescriptorSize);*/
        // 设置根签名表
        //cmdList->SetGraphicsRootDescriptorTable(0, handle);

        //设置描述符表，将纹理资源与流水线绑定
        CD3DX12_GPU_DESCRIPTOR_HANDLE tex(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
        tex.Offset(ri->Mat->DiffuseSrvHeapIndex, mCbvSrvDescriptorSize);
        cmdList->SetGraphicsRootDescriptorTable(0, tex);

        //设置其他的根参数
        // 设置根描述符，将根描述符与资源绑定 这里我们绑定根描述符，直接上传GPU里的常量数据。
        D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress();
        objCBAddress += ri->ObjCBIndex * objCBByteSize;
        D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex * matCBByteSize;
        // objCBAddress子资源的地址
        // 世界矩阵不同
        cmdList->SetGraphicsRootConstantBufferView(1, objCBAddress);
        // 3指的是根签名数组下标
        cmdList->SetGraphicsRootConstantBufferView(3, matCBAddress);

        // 绘制顶点实例
        //绘制顶点（通过索引缓冲区绘制）
        cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
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