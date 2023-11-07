#include "GaussianBlurFilter.h"

GaussianBlurFilter::GaussianBlurFilter(ID3D12Device* device,
									   UINT width, UINT height,
									   DXGI_FORMAT format)
{
	md3dDevice = device;

	mWidth = width;
	mHeight = height;
	mFormat = format;

	BuildResources();
}

ID3D12Resource* GaussianBlurFilter::Output()
{
	return mBlurMap0.Get();
}

//构建纹理资源（计算横向和纵向模糊的两个纹理）
void GaussianBlurFilter::BuildResources()
{
	D3D12_RESOURCE_DESC texDesc;
	ZeroMemory(&texDesc, sizeof(D3D12_RESOURCE_DESC));
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Alignment = 0;
	texDesc.Width = mWidth;//指定为后台缓冲纹理的宽
	texDesc.Height = mHeight;//指定为后台缓冲纹理的高
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.Format = mFormat;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	// UAV（可写入）
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;//必须用此标志才能和UAV绑定
	//创建一个资源和一个堆，并将资源提交至堆中（将纹理资源blurMap0提交至GPU显存中）
	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),//堆类型为默认堆（不能被CPU读写，只能被GPU读写）
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_COMMON,//资源的状态为初始状态
		nullptr,
		IID_PPV_ARGS(&mBlurMap0)));//返回blurMap0纹理资源
	//创建一个资源和一个堆，并将资源提交至堆中（将纹理资源blurMap1提交至GPU显存中）
	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),//堆类型为默认堆（不能被CPU读写，只能被GPU读写）
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_COMMON,//资源的状态为初始状态
		nullptr,
		IID_PPV_ARGS(&mBlurMap1)));//返回blurMap1纹理资源
}
//构建描述符（模糊需要用到的所有描述符）
void GaussianBlurFilter::BuildDescriptors(CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuDescriptor,
										  CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuDescriptor,
									      UINT descriptorSize)
{
	//计算所有描述符的地址（CPU上）
	mBlur0CpuSrv = hCpuDescriptor;
	mBlur0CpuUav = hCpuDescriptor.Offset(1, descriptorSize);
	mBlur1CpuSrv = hCpuDescriptor.Offset(1, descriptorSize);
	mBlur1CpuUav = hCpuDescriptor.Offset(1, descriptorSize);

	//计算所有描述符的地址（GPU上）
	mBlur0GpuSrv = hGpuDescriptor;
	mBlur0GpuUav = hGpuDescriptor.Offset(1, descriptorSize);
	mBlur1GpuSrv = hGpuDescriptor.Offset(1, descriptorSize);
	mBlur1GpuUav = hGpuDescriptor.Offset(1, descriptorSize);

	BuildDescriptors();
}

void GaussianBlurFilter::BuildDescriptors()
{
	//
	//分别创建两个纹理的SRV 与纹理资源加载到显存的代码实现一样
	//
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;//采样后分量顺序不改变
	srvDesc.Format = mFormat;//32位4通道
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;//2D贴图
	srvDesc.Texture2D.MostDetailedMip = 0;//细节最详尽的mipmap层级为0
	srvDesc.Texture2D.MipLevels = 1;//mipmap层级数量为1

	md3dDevice->CreateShaderResourceView(mBlurMap0.Get(), &srvDesc, mBlur0CpuSrv);
	md3dDevice->CreateShaderResourceView(mBlurMap1.Get(), &srvDesc, mBlur1CpuSrv);

	//
	//分别创建两个纹理的UAV
	//
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};

	uavDesc.Format = mFormat;//32位4通道
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;//2D贴图
	uavDesc.Texture2D.MipSlice = 0;

	md3dDevice->CreateUnorderedAccessView(mBlurMap0.Get(), nullptr, &uavDesc, mBlur0CpuUav);
	md3dDevice->CreateUnorderedAccessView(mBlurMap1.Get(), nullptr, &uavDesc, mBlur1CpuUav);
}

std::vector<float> GaussianBlurFilter::CalcGaussWeights(float sigma)
{
	std::vector<float> weights;//定义高斯核权重矩阵
	int blurRadius = (int)ceil(2.0f * sigma);//通过sigma计算模糊半径
	assert(blurRadius <= MaxBlurRadius);
	weights.resize(2 * blurRadius + 1);//高斯核总长度
	float twoSigma2 = 2.0f * sigma * sigma;//高斯核计算公式的分母

	float weightSum = 0.0f;//权重和

	//计算每个权重（未归一化）
	for (int i = -blurRadius; i <= blurRadius; ++i)
	{
		float x = (float)i;

		weights[i + blurRadius] = expf(-x * x / twoSigma2);//计算每个权重

		weightSum += weights[i + blurRadius];//计算权重和
	}
	//归一化权重
	for (int i = 0; i < weights.size(); ++i)
	{
		weights[i] /= weightSum;
	}

	return weights;
}

void GaussianBlurFilter::OnResize(UINT newWidth, UINT newHeight)
{
	if ((mWidth != newWidth) || (mHeight != newHeight))
	{
		mWidth = newWidth;
		mHeight = newHeight;

		//以新的大小来重建离屏纹理资源
		BuildResources();

		//既然创建了新的资源，也应当为其创建新的描述符
		BuildDescriptors();
	}
}

// 为计算着色器绑定根签名、常量数据、资源描述符，以供其使用
// 之前一直是将光栅化后的图像传递给后台缓冲区并呈现在屏幕上，而渲染到纹理技术是将光栅化后的图像存在一张离屏纹理中，
// 可以对这张离屏纹理做计算，并最终将计算结果再传递给后台缓冲区呈现屏幕上
// 这个计算就可以放到计算着色器中去做，因为计算着色器是可以读取像素着色器的输出数据的。
void GaussianBlurFilter::Execute(ID3D12GraphicsCommandList* cmdList,
	ID3D12RootSignature* rootSig,
	ID3D12PipelineState* horzBlurPSO,
	ID3D12PipelineState* vertBlurPSO,
	ID3D12Resource* input,
	int blurCount)
{
	auto weights = CalcGaussWeights(2.5f);//计算高斯核
	int blurRadius = (int)weights.size() / 2;//反向计算模糊长度

	//设置根签名和根常量
	cmdList->SetComputeRootSignature(rootSig);//绑定传至计算着色器的根签名

	//设置“模糊半径”的根常量
	cmdList->SetComputeRoot32BitConstants(0, 1, &blurRadius, 0);

	//设置“高斯核权重”的根常量
	cmdList->SetComputeRoot32BitConstants(0,  //根参数索引
		(UINT)weights.size(), //根常量数组元素个数
		weights.data(), //根常量数组
		1);//首元素位于根常量中位置

	//将后台缓冲区资源转换成“复制源”状态
	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(input,
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE));

	//将blurMap0离屏纹理资源转换成“复制目标”状态
	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mBlurMap0.Get(),
		D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST));

	//将后台缓冲纹理拷贝至blurMap0离屏纹理中
	cmdList->CopyResource(mBlurMap0.Get(), input);

	// 将blurMap0离屏纹理资源转换成“可读”状态
	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mBlurMap0.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ));

	// 将blurMap1离屏纹理资源转换成“可写入”状态
	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mBlurMap1.Get(),
		D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));

	for (int i = 0; i < blurCount; ++i)
	{
		//
		//执行横向模糊
		//
		cmdList->SetPipelineState(horzBlurPSO);//执行横向计算着色器

		cmdList->SetComputeRootDescriptorTable(1, mBlur0GpuSrv);//blurMap0作为输入
		cmdList->SetComputeRootDescriptorTable(2, mBlur1GpuUav);//blurMap1作为输出
		//需要调度多少组来覆盖一行像素
		//每组覆盖256个像素（256在ComputeShader中定义）
		UINT numGroupsX = (UINT)ceilf(mWidth / 256.0f);//X方向的线程组数量
		// D3D12通过Dispatch函数启动线程组
		cmdList->Dispatch(numGroupsX, mHeight, 1);//分派线程组(之后便执行计算着色器)

		// 将blurMap0离屏纹理资源转换成“可写入”状态
		cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mBlurMap0.Get(),
			D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));

		// 将blurMap1离屏纹理资源转换成“可读”状态
		cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mBlurMap1.Get(),
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ));

		//
		//执行纵向模糊
		//
		cmdList->SetPipelineState(vertBlurPSO);//执行纵向计算着色器

		cmdList->SetComputeRootDescriptorTable(1, mBlur1GpuSrv);//blurMap1作为输入
		cmdList->SetComputeRootDescriptorTable(2, mBlur0GpuUav);//blurMap0作为输出

		//需要调度多少组来覆盖一列像素
		//每组覆盖256个像素（256在ComputeShader中定义）
		UINT numGroupsY = (UINT)ceilf(mHeight / 256.0f);//Y方向的线程组数量
		cmdList->Dispatch(mWidth, numGroupsY, 1);//分派线程组(之后便执行计算着色器)

		// 将blurMap0离屏纹理资源转换成“可写入”状态
		cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mBlurMap0.Get(),
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ));

		// 将blurMap1离屏纹理资源转换成“可读”状态
		cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mBlurMap1.Get(),
			D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
	}
}