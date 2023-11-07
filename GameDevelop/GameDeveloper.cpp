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

    mWaves = std::make_unique<Waves>(128, 128, 1.0f, 0.03f, 4.0f, 0.2f);

    mBlurFilter = std::make_unique<GaussianBlurFilter>(d3dDevice.Get(), mClientWidth, mClientHeight, DXGI_FORMAT_R8G8B8A8_UNORM);

    LoadTextures();
    BuildRootSignature();
    BuildPostProcessRootSignature();
    BuildDescriptorHeaps();
    BuildShadersAndInputLayout();
    //BuildGeometry();//柱子和地面
    //BuildSkullGeometry();//骷髅头
    BuildLandGeometry();//山川
    BuildWavesGeometryBuffers();//湖泊 
    BuildBoxGeometry();//创建BOX
    BuildTreeSpritesGeometry();//创建树木公告板
    BuildGeoSphere();//创建20面体
    BuildQuadPatchGeometry();//创建面片
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

    // 然后清除后台缓冲区和深度缓冲区，并赋值。
    // 步骤是先获得堆中描述符句柄（即地址），
    // 再通过ClearRenderTargetView函数和ClearDepthStencilView函数做清除和赋值。
    // 这里将RT资源背景色赋值为DarkRed（暗红）。
    //D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(rtvHeap->GetCPUDescriptorHandleForHeapStart(), ref_mCurrentBackBuffer, rtvDescriptorSize);
    //cmdList->ClearRenderTargetView(rtvHandle, DirectX::Colors::LightSteelBlue, 0, nullptr);// 清除RT背景色为暗红，并且不设置裁剪矩形
    //D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvHeap->GetCPUDescriptorHandleForHeapStart();
    //cmdList->ClearDepthStencilView(dsvHandle,	// DSV描述符句柄
    //    D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,	// FLAG
    //    1.0f,	// 默认深度值
    //    0,	// 默认模板值
    //    0,	// 裁剪矩形数量
    //    nullptr);	// 裁剪矩形指针

    // 清除后台缓冲区和深度缓冲区，并赋值
    //const float clear_color_with_alpha[4]={}
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

    //设置CBV描述符堆
    //ID3D12DescriptorHeap* descriptorHeaps[] = { mCbvHeap.Get() };// 注意这里之所以是数组，是因为还可能包含SRV和UAV，而这里我们只用到了CBV
    //cmdList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    //设置SRV描述符堆
    ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };// 注意这里之所以是数组，是因为还可能包含SRV和UAV，而这里我们只用到了SRV
    cmdList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    // 设置图形根描述符表
    // 因为有2个描述符表，所以要绑定两次
    //int passCbvIndex = mPassCbvOffset + mCurrFrameResourceIndex;
    //// passCbvHandle的地址偏移是allRitems.size()
    //auto passCbvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mCbvHeap->GetGPUDescriptorHandleForHeapStart());
    //passCbvHandle.Offset(passCbvIndex, cbv_srv_uavDescriptorSize);
    //// 1表示根参数的起始索引
    //cmdList->SetGraphicsRootDescriptorTable(1, passCbvHandle);

    // 绑定passCB根描述符
    auto passCB = mCurrFrameResource->PassCB->Resource();
    // 寄存器槽号（着色器register（b2））要和根签名设置时的一一对应
    cmdList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());

    //分别设置PSO并绘制对应渲染项
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
    // 准备将模糊输出复制到后缓冲区
    // 执行离屏模糊计算，得到模糊后的离屏纹理blurMap0
    if (is_GaussianBlurFilter)
    {
        mBlurFilter->Execute(cmdList.Get(), mPostProcessRootSignature.Get(),
        mPSOs["horzBlur"].Get(), mPSOs["vertBlur"].Get(), CurrentBackBuffer(), blurCount);

        //将后台缓冲资源转成“复制目标”
        cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST));

        //将模糊处理后的离屏纹理拷贝给后台缓冲区
        cmdList->CopyResource(CurrentBackBuffer(), mBlurFilter->Output());
    }
    //int objCbvIndex = 0;//根参数的起始索引
    //auto handle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mCbvHeap->GetGPUDescriptorHandleForHeapStart());
    //handle.Offset(objCbvIndex, cbv_srv_uavDescriptorSize);
    //cmdList->SetGraphicsRootDescriptorTable(objCbvIndex, handle);

    //int passCbvIndex = 1;//根参数的起始索引
    //handle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mCbvHeap->GetGPUDescriptorHandleForHeapStart());
    //handle.Offset(passCbvIndex, cbv_srv_uavDescriptorSize);
    //cmdList->SetGraphicsRootDescriptorTable(passCbvIndex, handle);
    
    //// 数据：GPU->渲染流水线
    //// 设置顶点缓冲区
    //// 顶点数据绑定至寄存器槽
    //cmdList->IASetVertexBuffers(0, 1, &geo->VertexBufferView());
    //// 设置索引缓冲区
    //cmdList->IASetIndexBuffer(&geo->IndexBufferView());

    //// 绘制顶点（通过索引缓冲区绘制）
    //// 因为此案例中没有子物体，所以第4个参数为0，如果有子物体这里将做偏移计算。
    //cmdList->DrawIndexedInstanced(geo->DrawArgs["box"].IndexCount,// 每个实例要绘制的索引数
    //    1,// 实例化个数
    //    geo->DrawArgs["box"].StartIndexLocation,// 起始索引位置
    //    geo->DrawArgs["box"].BaseVertexLocation,// 子物体起始索引在全局索引中的位置
    //    0);// 实例化的高级技术
    //cmdList->DrawIndexedInstanced(geo->DrawArgs["grid"].IndexCount, //每个实例要绘制的索引数
    //    1,	//实例化个数
    //    geo->DrawArgs["grid"].StartIndexLocation,	//起始索引位置
    //    geo->DrawArgs["grid"].BaseVertexLocation,	//子物体起始索引在全局索引中的位置
    //    0);	//实例化的高级技术，暂时设置为0
    //cmdList->DrawIndexedInstanced(geo->DrawArgs["sphere"].IndexCount, //每个实例要绘制的索引数
    //    1,	//实例化个数
    //    geo->DrawArgs["sphere"].StartIndexLocation,	//起始索引位置
    //    geo->DrawArgs["sphere"].BaseVertexLocation,	//子物体起始索引在全局索引中的位置
    //    0);	//实例化的高级技术，暂时设置为0
    //cmdList->DrawIndexedInstanced(geo->DrawArgs["cylinder"].IndexCount, //每个实例要绘制的索引数
    //    1,	//实例化个数
    //    geo->DrawArgs["cylinder"].StartIndexLocation,	//起始索引位置
    //    geo->DrawArgs["cylinder"].BaseVertexLocation,	//子物体起始索引在全局索引中的位置
    //    0);	//实例化的高级技术，暂时设置为0
    // 等到渲染完成，将后台缓冲区的状态改成呈现状态，使其之后推到前台缓冲区显示。
    // 完了，关闭命令列表，等待传入命令队列。
    // 高斯模糊后再次转换RT资源，转成呈现
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
                mRadius = 75.0f;
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
    //如果更改了屏幕长宽，则重新生成离屏纹理使其长宽和屏幕长宽一致
    if (mBlurFilter != nullptr)
    {
        mBlurFilter->OnResize(mClientWidth, mClientHeight);
    }
}

// 每一帧的操作
void GameDevelopApp::Update()
{
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

    AnimateMaterials(gt);
    UpdateObjectCBs(gt);
    UpdateMaterialCBs(gt);
    UpdateMainPassCB(gt);
    UpdateWaves(gt);

    //XMMATRIX world;
    //// 将世界矩阵传递给GPU objConstants
    //for (auto& t : mAllRitems)
    //{
    //    world = XMLoadFloat4x4(&t->World);
    //    XMStoreFloat4x4(&objConstants.world, XMMatrixTranspose(world));
    //    //将数据拷贝至GPU缓存
    //    //t->ObjCBIndex保证世界矩阵与单个几何体一一对应
    //    mObjectCB->CopyData(t->ObjCBIndex, objConstants);
    //}
    //XMMATRIX proj = XMLoadFloat4x4(&mProj); 
    //XMMATRIX proj = XMMatrixPerspectiveFovLH(fov * MathHelper::Pi, static_cast<float>(mClientWidth) / mClientHeight, 1.0f, 1000.0f);
    ////XMMATRIX trans = XMMatrixTranslation(0.0f, 0.0f, 0.0f);
    ////矩阵计算
    //XMMATRIX viewProj = view * proj;

    ////XMMATRIX赋值给XMFLOAT4X4
    ////最后将worldViewProj矩阵保存时转置了一下，是因为HLSL的矩阵数据在内存中是列主序存储的，
    ////所以可以看到在Shader中，矩阵是右乘的。而在DXMATH库中的矩阵数据在内存中是行主序存储的，
    ////所以为了提升计算效率，在传入shader前要将行矩阵转置成列矩阵。
    ////XMStoreFloat4x4(&objConstants.world, XMMatrixTranspose(world*trans));
    //////将数据拷贝至GPU缓存
    ////mObjectCB->CopyData(0, objConstants);

    //XMStoreFloat4x4(&mMainPassCB.viewProj, XMMatrixTranspose(viewProj));
    //auto currPassCB = mCurrFrameResource->PassCB.get();
    ////0表示多个几何体共用一个观察投影矩阵
    //currPassCB->CopyData(0, mMainPassCB);
    ////mPassCB->CopyData(0, passConstants);
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
    mMainPassCB.Lights[0].Strength = { 0.9f, 0.9f, 0.8f };

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
        fin >> ignore >> ignore >> ignore;
        //vertices[i].Color = XMFLOAT4(0.5f, 0.5f, 0.5f, 1.0f);
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

void GameDevelopApp::BuildGeoSphere()
{
    ProceduralGeometry proceGeo;
    //创建20面体
    ProceduralGeometry::MeshData geoSphere = proceGeo.CreateGeosphere20Face(0.8f);

    std::vector<Vertex> vertices(12);//初始化顶点列表
    //顶点列表赋值
    for (UINT i = 0; i < 12; i++)
    {
        vertices[i].Pos = geoSphere.Vertices[i].Position;
        vertices[i].Normal = geoSphere.Vertices[i].Normal;
        vertices[i].TexC = geoSphere.Vertices[i].TexC;
    }

    std::vector<std::int16_t> indices(60);//初始化索引列表(20面体60个索引)
    //索引列表赋值
    for (UINT i = 0; i < 60; i++)
    {
        indices[i] = geoSphere.Indices32[i];
    }

    const UINT vbByteSize = vertices.size() * sizeof(Vertex);//顶点缓存大小
    const UINT ibByteSize = indices.size() * sizeof(std::int16_t);//索引缓存大小

    //绘制三参数
    SubmeshGeometry geoSphereSubmesh;
    geoSphereSubmesh.BaseVertexLocation = 0;
    geoSphereSubmesh.StartIndexLocation = 0;
    geoSphereSubmesh.IndexCount = (UINT)indices.size();

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "geoSphereGeo";

    //将顶点和索引数据复制到CPU系统内存上
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
    //UINT objCount = (UINT)mOpaqueRitems.size();
    ////objCB中存放着objectCount个（22个）子缓冲区，passCB中存放着1个子缓冲区，一共23个子缓冲区。
    ////现在有3个帧资源，所以要将23*3，一共是69个子缓冲区，即需要69个CBV。
    //UINT numDescriptors = (objCount + 1) * gNumFrameResources;

    //mPassCbvOffset = objCount * gNumFrameResources;

    //D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
    ////因为现在有22个几何体，所以就需要22个objCBV，和一个passCBV（包含观察投影矩阵）
    ////此处一个堆中包含(几何体个数（包含实例）+1)个CBV
    //cbvHeapDesc.NumDescriptors = numDescriptors;// 传递perPass CBV
    //cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    //cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;// 可供着色器访问
    //cbvHeapDesc.NodeMask = 0;
    //ThrowIfFailed(d3dDevice->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&mCbvHeap)));

    const int textureDescriptorCount = 6;
    const int blurDescriptorCount = 4;//横向纵向各SRV和UAV

    //创建SRV堆
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = textureDescriptorCount + blurDescriptorCount;//加入新的数量得改
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    srvHeapDesc.NodeMask = 0;
    ThrowIfFailed(d3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

    // 将之前定义好的纹理资源转换成comptr存起来
    auto grassTex = mTextures["grassTex"]->Resource;
    auto waterTex = mTextures["waterTex"]->Resource;
    auto fenceTex = mTextures["fenceTex"]->Resource;
    auto fenceTexA = mTextures["fenceTexA"]->Resource;
    auto treeArrayTex = mTextures["treeArrayTex"]->Resource;
    auto white1x1Tex = mTextures["white1x1Tex"]->Resource;

    //SRV堆中子SRV（草地的SRV）的句柄
    CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

    //SRV描述结构体
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;//采样后分量顺序不改变
    srvDesc.Format = grassTex->GetDesc().Format;//视图的默认格式
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;//2D贴图
    srvDesc.Texture2D.MostDetailedMip = 0;//细节最详尽的mipmap层级为0
    srvDesc.Texture2D.MipLevels = grassTex->GetDesc().MipLevels;//mipmap层级数量
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;//可访问的mipmap最小层级数为0
    //创建“草地”的SRV
    d3dDevice->CreateShaderResourceView(grassTex.Get(), &srvDesc, hDescriptor);

    //SRV堆中子SRV（湖水的SRV）的句柄,继续偏移一个SRV
    hDescriptor.Offset(1, mCbvSrvDescriptorSize);
    //SRV描述结构体修改
    srvDesc.Format = waterTex->GetDesc().Format;//视图的默认格式
    srvDesc.Texture2D.MipLevels = waterTex->GetDesc().MipLevels;//mipmap层级数量
    // 创建湖水SRV
    d3dDevice->CreateShaderResourceView(waterTex.Get(), &srvDesc, hDescriptor);

    //SRV堆中子SRV（板条箱的SRV）的句柄,继续偏移一个SRV
    hDescriptor.Offset(1, mCbvSrvDescriptorSize);
    //SRV描述结构体修改
    srvDesc.Format = fenceTex->GetDesc().Format;//视图的默认格式
    srvDesc.Texture2D.MipLevels = fenceTex->GetDesc().MipLevels;//mipmap层级数量
    //创建“板条箱”的SRV
    d3dDevice->CreateShaderResourceView(fenceTex.Get(), &srvDesc, hDescriptor);

    //SRV堆中子SRV（板条箱的SRV）的句柄,继续偏移一个SRV
    hDescriptor.Offset(1, mCbvSrvDescriptorSize);
    //SRV描述结构体修改
    srvDesc.Format = fenceTexA->GetDesc().Format;//视图的默认格式
    srvDesc.Texture2D.MipLevels = fenceTexA->GetDesc().MipLevels;//mipmap层级数量
    //创建“板条箱”的SRV
    d3dDevice->CreateShaderResourceView(fenceTexA.Get(), &srvDesc, hDescriptor);

    //SRV堆中子SRV（骷髅的SRV）的句柄,继续偏移一个SRV
    hDescriptor.Offset(1, mCbvSrvDescriptorSize);
    //SRV描述结构体修改
    srvDesc.Format = white1x1Tex->GetDesc().Format;//视图的默认格式
    srvDesc.Texture2D.MipLevels = white1x1Tex->GetDesc().MipLevels;//mipmap层级数量
    //创建“骷髅”的SRV
    d3dDevice->CreateShaderResourceView(white1x1Tex.Get(), &srvDesc, hDescriptor);

    // 使用BlurFilter资源的描述符填充堆
    mBlurFilter->BuildDescriptors(
        CD3DX12_CPU_DESCRIPTOR_HANDLE(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), textureDescriptorCount, mCbvSrvDescriptorSize),
        CD3DX12_GPU_DESCRIPTOR_HANDLE(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), textureDescriptorCount, mCbvSrvDescriptorSize),
        mCbvSrvDescriptorSize);

    // 纹理数组偏移要放到最后
    //SRV堆中子SRV（树木的SRV）的句柄,继续偏移一个SRV
    hDescriptor.Offset(1, mCbvSrvDescriptorSize);
    //SRV描述结构体修改
    srvDesc.Format = treeArrayTex->GetDesc().Format;//视图的默认格式
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;//2D纹理数组
    srvDesc.Texture2DArray.ArraySize = treeArrayTex->GetDesc().DepthOrArraySize;//纹理数组长度
    srvDesc.Texture2DArray.MostDetailedMip = 0;//MipMap层级为0
    srvDesc.Texture2DArray.MipLevels = -1;//没有层级
    srvDesc.Texture2DArray.FirstArraySlice = 0;
    //创建“树木”的SRV
    d3dDevice->CreateShaderResourceView(treeArrayTex.Get(), &srvDesc, hDescriptor);
}

void GameDevelopApp::BuildTreeSpritesGeometry()
{
    //定义树木公告板的顶点结构体
    struct TreeSpriteVertex
    {
        XMFLOAT3 Pos;//公告板的中心点坐标
        XMFLOAT2 Size;//公告板的长宽
    };

    static const int treeCount = 20;
    std::array<TreeSpriteVertex, 20> vertices;
    for (UINT i = 0; i < treeCount; ++i)
    {
        float x = MathHelper::RandF(-45.0f, 45.0f);//取-45到45的一个随机数
        float z = MathHelper::RandF(-45.0f, 45.0f);
        float y = GetHillsHeight(x, z);//获得y方向坐标（贴着山川）
        
        y += 8.0f;//让公告板中心点高于山川8.0个单位

        vertices[i].Pos = XMFLOAT3(x, y, z);
        vertices[i].Size = XMFLOAT2(20.0f, 20.0f);
    }

    //创建索引缓冲区
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
    // 还是使用CopyMemory以及CreateDefaultBuffer函数将缓冲区数据传至GPU.
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

void GameDevelopApp::UpdateWaves(const _GameTimer::GameTimer& gt)
{
    // 每四分之一秒，生成一个随机波
    static float t_base = 0.0f;
    if ((gt.TotalTime() - t_base) >= 0.25f)
    {
        t_base += 0.25f;//0.25秒生成一个波浪

        //随机生成横坐标
        int i = MathHelper::Rand(4, mWaves->RowCount() - 5);
        //随机生成纵坐标
        int j = MathHelper::Rand(4, mWaves->ColumnCount() - 5);
        //随机生成波的半径
        float r = MathHelper::RandF(0.2f, 0.5f);//float用RandF函数
        //使用波动方程函数生成波纹
        mWaves->Disturb(i, j, r);
    }
    //每帧更新波浪模拟（即更新顶点坐标）
    mWaves->Update(gt.DeltaTime());

    //将更新的顶点坐标存入GPU上传堆中
    auto currWavesVB = mCurrFrameResource->WavesVB.get();
    for (int i = 0; i < mWaves->VertexCount(); ++i)
    {
        Vertex v;

        v.Pos = mWaves->Position(i);
        v.Normal = mWaves->Normal(i);

        //将顶点坐标转换成UV坐标，从[-w/2, w/2]映射到[0, 1]
        v.TexC.x = 0.5f + v.Pos.x / mWaves->Width();
        v.TexC.y = 0.5f - v.Pos.z / mWaves->Depth();
 
        // 子缓冲区一一对应 有多少个顶点就有多少个子缓冲区
        currWavesVB->CopyData(i, v);
    }
    //赋值湖泊的GPU上的顶点缓存
    mWavesRitem->Geo->VertexBufferGPU = currWavesVB->Resource();
}

void GameDevelopApp::LoadTextures()
{
    /*板条箱纹理*/
    auto fenceTex = std::make_unique<Texture>();
    fenceTex->Name = "fenceTex";
    fenceTex->Filename = L"Textures\\fire.dds";
    // 读取DDS文件
    // CreateDDSTextureFromFile12函数，它加载了DDS文件，赋值resource和uploadHeap
    // resource相当于返回的默认堆资源，即最终的纹理缓冲区资源，而uploadHeap则是返回的上传堆资源
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(d3dDevice.Get(),
        cmdList.Get(), fenceTex->Filename.c_str(),
        fenceTex->Resource, fenceTex->UploadHeap));

    /*草地纹理*/
    auto grassTex = std::make_unique<Texture>();
    grassTex->Name = "grassTex";
    grassTex->Filename = L"Textures\\grass.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(d3dDevice.Get(),
        cmdList.Get(), grassTex->Filename.c_str(),
        grassTex->Resource, grassTex->UploadHeap));

    /*湖水纹理*/
    auto waterTex = std::make_unique<Texture>();
    waterTex->Name = "waterTex";
    waterTex->Filename = L"Textures\\water1.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(d3dDevice.Get(),
        cmdList.Get(), waterTex->Filename.c_str(),
        waterTex->Resource, waterTex->UploadHeap));

    /*篱笆纹理*/
    auto fenceTexA = std::make_unique<Texture>();
    fenceTexA->Name = "fenceTexA";
    fenceTexA->Filename = L"Textures\\WireFence.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(d3dDevice.Get(),
        cmdList.Get(), fenceTexA->Filename.c_str(),
        fenceTexA->Resource, fenceTexA->UploadHeap));

    /*树纹理*/
    auto treeArrayTex = std::make_unique<Texture>();
    treeArrayTex->Name = "treeArrayTex";
    treeArrayTex->Filename = L"Textures\\treeArray2.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(d3dDevice.Get(),
        cmdList.Get(), treeArrayTex->Filename.c_str(),//将wstring转成wChar_t
        treeArrayTex->Resource, treeArrayTex->UploadHeap));

    /*骨骼纹理*/
    auto white1x1Tex = std::make_unique<Texture>();
    white1x1Tex->Name = "white1x1Tex";
    white1x1Tex->Filename = L"Textures\\white1x1.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(d3dDevice.Get(),
        cmdList.Get(), white1x1Tex->Filename.c_str(),
        white1x1Tex->Resource, white1x1Tex->UploadHeap));

    //装入总的纹理映射表
    mTextures[grassTex->Name] = std::move(grassTex);
    mTextures[waterTex->Name] = std::move(waterTex);
    mTextures[fenceTex->Name] = std::move(fenceTex);
    mTextures[fenceTexA->Name] = std::move(fenceTexA);
    mTextures[treeArrayTex->Name] = std::move(treeArrayTex);
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
/*构建后处理的根签名*/
// 因为CS是不属于渲染流水线某一阶段，所以根签名的创建过程与传统根签名也不同
void GameDevelopApp::BuildPostProcessRootSignature()
{
    //创建SRV描述符表作为根参数0
    CD3DX12_DESCRIPTOR_RANGE srvTable;
    srvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,//描述符类型SRV
                  1,//描述符表数量
                  0);//描述符所绑定的寄存器槽号
    //创建UAV描述符表作为根参数1
    CD3DX12_DESCRIPTOR_RANGE uavTable;
    uavTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

    //根参数可以是描述符表、根描述符、根常量
    CD3DX12_ROOT_PARAMETER slotRootParameter[3];

    slotRootParameter[0].InitAsConstants(12, 0);//12个常量，寄存器槽号为0
    slotRootParameter[1].InitAsDescriptorTable(1, &srvTable);//Range数量为1
    slotRootParameter[2].InitAsDescriptorTable(1, &uavTable);//Range数量为1

    auto staticSamplers = GetStaticSamplers();//获得静态采样器集合
    //根签名由一组根参数构成
    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(3,//根参数的数量
        slotRootParameter,//根参数指针
        0, //静态采样器的数量0
        nullptr,//静态采样器指针为空
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    //用单个寄存器槽来创建一个根签名，该槽位指向一个仅含有单个常量缓冲区的描述符区域
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

    // 用一个shader来绘制它
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
    //编译成可移植的字节码
    /*mVsByteCode = d3dUtil::CompileShader(L"Shaders\\color.hlsl", nullptr, "VS", "vs_5_0");
    mPsByteCode = d3dUtil::CompileShader(L"Shaders\\color.hlsl", nullptr, "PS", "ps_5_0");*/

    mShaders["tessVS"] = d3dUtil::CompileShader(L"Shaders\\Tessellation.hlsl", nullptr, "VS", "vs_5_0");
    mShaders["tessHS"] = d3dUtil::CompileShader(L"Shaders\\Tessellation.hlsl", nullptr, "HS", "hs_5_0");
    mShaders["tessDS"] = d3dUtil::CompileShader(L"Shaders\\Tessellation.hlsl", nullptr, "DS", "ds_5_0");
    mShaders["tessPS"] = d3dUtil::CompileShader(L"Shaders\\Tessellation.hlsl", nullptr, "PS", "ps_5_0");
    mShaders["fogtessPS"] = d3dUtil::CompileShader(L"Shaders\\Tessellation.hlsl", defines, "PS", "ps_5_0");

    mInputLayout =
    {
        //第一个0指的是 例如相同语义的不同纹理集0 1 第二个0指的是只有一个输入缓冲区 大多数程序都只有一个输入缓冲区
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        // 如何缩减顶点颜色精度，从128位减少到32位
        // { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        // 因为DXGI_FORMAT符号所列的值在内存中是用小端字节序来表示的，所以从最低有效字节写至最高有效字节，格式ARGB被表示为BGRA。
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

    //
    //树木公告板的PSO（更换shader）
    //
    D3D12_GRAPHICS_PIPELINE_STATE_DESC treeSpritePsoDesc = opaquePsoDesc;
    //替换树木公告板的InputLayout
    treeSpritePsoDesc.InputLayout = { mTreeSpriteInputLayout.data(), (UINT)mTreeSpriteInputLayout.size() };// 输入布局描述  
    //替换编译后的shader字节码
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
    treeSpritePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;//点列表
    treeSpritePsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;//双面显示

    ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&treeSpritePsoDesc, IID_PPV_ARGS(&mPSOs["treeSprites"])));

    // 不加雾效
    D3D12_GRAPHICS_PIPELINE_STATE_DESC unfogtreeSpritePsoDesc = treeSpritePsoDesc;
    unfogtreeSpritePsoDesc.PS =
    {
        reinterpret_cast<BYTE*>(mShaders["unfogtreeSpritePS"]->GetBufferPointer()),
        mShaders["unfogtreeSpritePS"]->GetBufferSize()
    };
    ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&unfogtreeSpritePsoDesc, IID_PPV_ARGS(&mPSOs["unfogtreeSprites"])));

    // 20面体PSO
    D3D12_GRAPHICS_PIPELINE_STATE_DESC geoSpherePsoDesc = {};
    ZeroMemory(&geoSpherePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
    geoSpherePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
    geoSpherePsoDesc.pRootSignature = mRootSignature.Get();// 与此PSO绑定的根签名指针

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
    geoSpherePsoDesc.SampleMask = UINT_MAX;	//0xffffffff,全部采样，没有遮罩
    geoSpherePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    geoSpherePsoDesc.NumRenderTargets = 1;
    geoSpherePsoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;	//归一化的无符号整型
    geoSpherePsoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    geoSpherePsoDesc.SampleDesc.Count = 1;	//不使用4XMSAA
    geoSpherePsoDesc.SampleDesc.Quality = 0;	//不使用4XMSAA

    ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&geoSpherePsoDesc, IID_PPV_ARGS(&mPSOs["LOD"])));
    // 不加雾效
    D3D12_GRAPHICS_PIPELINE_STATE_DESC unfoggeoSpherePsoDesc = geoSpherePsoDesc;
    unfoggeoSpherePsoDesc.PS =
    {
        reinterpret_cast<BYTE*>(mShaders["unfogGeospherePS"]->GetBufferPointer()),
        mShaders["unfogGeospherePS"]->GetBufferSize()
    };
    ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&unfoggeoSpherePsoDesc, IID_PPV_ARGS(&mPSOs["unfogLOD"])));

    //
    //水平方向模糊的“计算PSO”
    //
    D3D12_COMPUTE_PIPELINE_STATE_DESC horzBlurPSO = {};
    horzBlurPSO.pRootSignature = mPostProcessRootSignature.Get();// 与此PSO绑定的根签名指针
    horzBlurPSO.CS =
    {
        reinterpret_cast<BYTE*>(mShaders["horzBlurCS"]->GetBufferPointer()),
        mShaders["horzBlurCS"]->GetBufferSize()
    };
    horzBlurPSO.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    ThrowIfFailed(d3dDevice->CreateComputePipelineState(&horzBlurPSO, IID_PPV_ARGS(&mPSOs["horzBlur"])));

    //
    //竖直方向模糊的“计算PSO”
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
    quadPatchPsoDesc.SampleMask = UINT_MAX;	//0xffffffff,全部采样，没有遮罩
    quadPatchPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;//Patch类型的图元拓扑
    quadPatchPsoDesc.NumRenderTargets = 1;
    quadPatchPsoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;	//归一化的无符号整型
    quadPatchPsoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    quadPatchPsoDesc.SampleDesc.Count = 1;	//不使用4XMSAA
    quadPatchPsoDesc.SampleDesc.Quality = 0;	//不使用4XMSAA
    ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&quadPatchPsoDesc, IID_PPV_ARGS(&mPSOs["subdivision"])));

    // 加入雾效
    D3D12_GRAPHICS_PIPELINE_STATE_DESC fogquadPatchPsoDesc = quadPatchPsoDesc;
    fogquadPatchPsoDesc.PS =
    { reinterpret_cast<BYTE*>(mShaders["fogtessPS"]->GetBufferPointer()), mShaders["fogtessPS"]->GetBufferSize() };
    ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&fogquadPatchPsoDesc, IID_PPV_ARGS(&mPSOs["fogsubdivision"])));
}

void GameDevelopApp::BuildFrameResources()
{
    for (int i = 0; i < gNumFrameResources; ++i)
    {
        // FrameResource构造函数最后加了wave的顶点数
        mFrameResources.push_back(std::make_unique<FrameResource>(d3dDevice.Get(), 1, (UINT)mAllRitems.size(),(UINT)mMaterials.size(), mWaves->VertexCount()));
    }
}

void GameDevelopApp::BuildMaterials()
{
    //定义陆地的材质
    auto grass = std::make_unique<Material>();
    grass->Name = "grass";
    grass->MatCBIndex = 0;
    // 根据设置的SRV堆偏移关系，0为草地，1为湖水，2为BOX 
    grass->DiffuseSrvHeapIndex = 0;
    // 有了贴图，所以以贴图纹理的颜色作为漫反射反照率即可，因此将材质中的反照率修改为1，即不影响总体反照率
    grass->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);//陆地的反照率（颜色）
    grass->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);//陆地的R0
    grass->Roughness = 0.125f;//陆地的粗糙度（归一化后的）

    //定义湖水的材质
    auto water = std::make_unique<Material>();
    water->Name = "water";
    water->MatCBIndex = 1;
    water->DiffuseSrvHeapIndex = 1;
    water->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    water->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);;//湖水的R0（因为没有透明度和折射率，所以这里给0.1）
    water->Roughness = 0.0f;

    //定义BOX的材质
    auto wood = std::make_unique<Material>();
    wood->Name = "wood";
    wood->MatCBIndex = 2;
    wood->DiffuseSrvHeapIndex = 2;
    wood->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);//木头的反照率（颜色）
    wood->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);//木头的R0
    wood->Roughness = 0.25f;//木头的粗糙度（归一化后的）

    auto wirefence = std::make_unique<Material>();
    wirefence->Name = "wirefence";
    wirefence->MatCBIndex = 3;
    wirefence->DiffuseSrvHeapIndex = 3;
    wirefence->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    wirefence->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
    wirefence->Roughness = 0.25f;

    //定义treeBillboard的材质
    auto treeSprites = std::make_unique<Material>();
    treeSprites->Name = "treeSprites";
    treeSprites->MatCBIndex = 5;
    treeSprites->DiffuseSrvHeapIndex = 5;
    treeSprites->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    treeSprites->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
    treeSprites->Roughness = 0.125f;

    //定义骨头的材质
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
    //// 创建box渲染项
    //auto boxRitem = std::make_unique<RenderItem>();
    //// 设置世界矩阵
    //XMStoreFloat4x4(&boxRitem->World, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(0.0f, 0.5f, 0.0f));
    //// 设置世界矩阵在常量数据中的索引
    //boxRitem->ObjCBIndex = 0;//BOX常量数据（world矩阵）在objConstantBuffer索引0上
    //boxRitem->Geo = mGeometries["shapeGeo"].get();
    //boxRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    //boxRitem->IndexCount = boxRitem->Geo->DrawArgs["box"].IndexCount;
    //boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;
    //boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;
    //mAllRitems.push_back(std::move(boxRitem));

    //// 创建grid渲染项
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

    //UINT fllowObjCBIndex = 3;//接下去的几何体常量数据在CB中的索引从2开始
    ////将圆柱和圆的实例模型存入渲染项中
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

    //    //左边5个圆柱
    //    XMStoreFloat4x4(&leftCylRitem->World, leftCylWorld);
    //    leftCylRitem->ObjCBIndex = fllowObjCBIndex++;
    //    leftCylRitem->Geo = mGeometries["shapeGeo"].get();
    //    leftCylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    //    leftCylRitem->IndexCount = leftCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
    //    leftCylRitem->StartIndexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
    //    leftCylRitem->BaseVertexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
    //    //右边5个圆柱
    //    XMStoreFloat4x4(&rightCylRitem->World, rightCylWorld);
    //    rightCylRitem->ObjCBIndex = fllowObjCBIndex++;
    //    rightCylRitem->Geo = mGeometries["shapeGeo"].get();
    //    rightCylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    //    rightCylRitem->IndexCount = rightCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
    //    rightCylRitem->StartIndexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
    //    rightCylRitem->BaseVertexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
    //    //左边5个球
    //    XMStoreFloat4x4(&leftSphereRitem->World, leftSphereWorld);
    //    leftSphereRitem->ObjCBIndex = fllowObjCBIndex++;
    //    leftSphereRitem->Geo = mGeometries["shapeGeo"].get();
    //    leftSphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    //    leftSphereRitem->IndexCount = leftSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
    //    leftSphereRitem->StartIndexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
    //    leftSphereRitem->BaseVertexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;
    //    //右边5个球
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
    //// 填充传递给pso的容器
    //for (auto& e : mAllRitems)
    //    mOpaqueRitems.push_back(e.get());

    //山川渲染项
    auto gridRitem = std::make_unique<RenderItem>();
    gridRitem->World = MathHelper::Identity4x4();
    XMStoreFloat4x4(&gridRitem->TexTransform, XMMatrixScaling(5.0f, 5.0f, 1.0f));
    gridRitem->ObjCBIndex = 1;//常量数据（world矩阵）在objConstantBuffer索引0上
    gridRitem->Mat = mMaterials["grass"].get();
    gridRitem->Geo = mGeometries["landGeo"].get();
    gridRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
    gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
    gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;

    mRitemLayer[(int)RenderLayer::Opaque].push_back(gridRitem.get());


    //湖泊渲染项
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

    //BOX渲染项
    auto boxRitem = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&boxRitem->World, XMMatrixTranslation(-15.0f, 2.0f, 15.0f));
    boxRitem->TexTransform = MathHelper::Identity4x4();
    boxRitem->ObjCBIndex = 2;//BOX的常量数据（world矩阵）在objConstantBuffer索引2上
    boxRitem->Mat = mMaterials["wood"].get();
    boxRitem->Geo = mGeometries["boxGeo"].get();	//赋值当前的“BOX”MeshGeoetry
    boxRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    boxRitem->IndexCount = boxRitem->Geo->DrawArgs["box"].IndexCount;
    boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;
    boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;

    mRitemLayer[(int)RenderLayer::Opaque].push_back(boxRitem.get());

    //篱笆渲染项
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

    //公告板渲染项
    auto treeSpritesRitem = std::make_unique<RenderItem>();
    treeSpritesRitem->World = MathHelper::Identity4x4();
    treeSpritesRitem->ObjCBIndex = 4;
    treeSpritesRitem->Mat = mMaterials["treeSprites"].get();
    treeSpritesRitem->Geo = mGeometries["treeSpritesGeo"].get();//赋值当前的“treeSpritesGeo”MeshGeoetry
    treeSpritesRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;//几何着色器只接受点列表和线列表
    treeSpritesRitem->IndexCount = treeSpritesRitem->Geo->DrawArgs["points"].IndexCount;
    treeSpritesRitem->StartIndexLocation = treeSpritesRitem->Geo->DrawArgs["points"].StartIndexLocation;
    treeSpritesRitem->BaseVertexLocation = treeSpritesRitem->Geo->DrawArgs["points"].BaseVertexLocation;

    mRitemLayer[(int)RenderLayer::AlphaTestedTreeSprites].push_back(treeSpritesRitem.get());

    //20面体渲染项
    auto geoSphereRitem = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&geoSphereRitem->World, XMMatrixTranslation(0.0f, 5.0f, 0.0f)* XMMatrixScaling(5.0f, 5.0f, 5.0f));
    geoSphereRitem->ObjCBIndex = 5;
    geoSphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    geoSphereRitem->Geo = mGeometries["geoSphereGeo"].get();
    geoSphereRitem->Mat = mMaterials["skullMat"].get();//赋予骨头材质给骷髅头
    geoSphereRitem->IndexCount = geoSphereRitem->Geo->DrawArgs["geoSphere"].IndexCount;
    geoSphereRitem->BaseVertexLocation = geoSphereRitem->Geo->DrawArgs["geoSphere"].BaseVertexLocation;
    geoSphereRitem->StartIndexLocation = geoSphereRitem->Geo->DrawArgs["geoSphere"].StartIndexLocation;
    mRitemLayer[(int)RenderLayer::geoSphere].push_back(geoSphereRitem.get());
    // 面片渲染项
    auto quadPatchRitem = std::make_unique<RenderItem>();
    quadPatchRitem->World = MathHelper::Identity4x4();
    quadPatchRitem->TexTransform = MathHelper::Identity4x4();
    quadPatchRitem->ObjCBIndex = 6;
    quadPatchRitem->Mat = mMaterials["skullMat"].get();
    quadPatchRitem->Geo = mGeometries["quadpatchGeo"].get();
    quadPatchRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_16_CONTROL_POINT_PATCHLIST;//16个控制点的patch列表
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