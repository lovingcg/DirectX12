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

//����������Դ��������������ģ������������
void GaussianBlurFilter::BuildResources()
{
	D3D12_RESOURCE_DESC texDesc;
	ZeroMemory(&texDesc, sizeof(D3D12_RESOURCE_DESC));
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Alignment = 0;
	texDesc.Width = mWidth;//ָ��Ϊ��̨��������Ŀ�
	texDesc.Height = mHeight;//ָ��Ϊ��̨��������ĸ�
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.Format = mFormat;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	// UAV����д�룩
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;//�����ô˱�־���ܺ�UAV��
	//����һ����Դ��һ���ѣ�������Դ�ύ�����У���������ԴblurMap0�ύ��GPU�Դ��У�
	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),//������ΪĬ�϶ѣ����ܱ�CPU��д��ֻ�ܱ�GPU��д��
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_COMMON,//��Դ��״̬Ϊ��ʼ״̬
		nullptr,
		IID_PPV_ARGS(&mBlurMap0)));//����blurMap0������Դ
	//����һ����Դ��һ���ѣ�������Դ�ύ�����У���������ԴblurMap1�ύ��GPU�Դ��У�
	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),//������ΪĬ�϶ѣ����ܱ�CPU��д��ֻ�ܱ�GPU��д��
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_COMMON,//��Դ��״̬Ϊ��ʼ״̬
		nullptr,
		IID_PPV_ARGS(&mBlurMap1)));//����blurMap1������Դ
}
//������������ģ����Ҫ�õ���������������
void GaussianBlurFilter::BuildDescriptors(CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuDescriptor,
										  CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuDescriptor,
									      UINT descriptorSize)
{
	//���������������ĵ�ַ��CPU�ϣ�
	mBlur0CpuSrv = hCpuDescriptor;
	mBlur0CpuUav = hCpuDescriptor.Offset(1, descriptorSize);
	mBlur1CpuSrv = hCpuDescriptor.Offset(1, descriptorSize);
	mBlur1CpuUav = hCpuDescriptor.Offset(1, descriptorSize);

	//���������������ĵ�ַ��GPU�ϣ�
	mBlur0GpuSrv = hGpuDescriptor;
	mBlur0GpuUav = hGpuDescriptor.Offset(1, descriptorSize);
	mBlur1GpuSrv = hGpuDescriptor.Offset(1, descriptorSize);
	mBlur1GpuUav = hGpuDescriptor.Offset(1, descriptorSize);

	BuildDescriptors();
}

void GaussianBlurFilter::BuildDescriptors()
{
	//
	//�ֱ𴴽����������SRV ��������Դ���ص��Դ�Ĵ���ʵ��һ��
	//
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;//���������˳�򲻸ı�
	srvDesc.Format = mFormat;//32λ4ͨ��
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;//2D��ͼ
	srvDesc.Texture2D.MostDetailedMip = 0;//ϸ�����꾡��mipmap�㼶Ϊ0
	srvDesc.Texture2D.MipLevels = 1;//mipmap�㼶����Ϊ1

	md3dDevice->CreateShaderResourceView(mBlurMap0.Get(), &srvDesc, mBlur0CpuSrv);
	md3dDevice->CreateShaderResourceView(mBlurMap1.Get(), &srvDesc, mBlur1CpuSrv);

	//
	//�ֱ𴴽����������UAV
	//
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};

	uavDesc.Format = mFormat;//32λ4ͨ��
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;//2D��ͼ
	uavDesc.Texture2D.MipSlice = 0;

	md3dDevice->CreateUnorderedAccessView(mBlurMap0.Get(), nullptr, &uavDesc, mBlur0CpuUav);
	md3dDevice->CreateUnorderedAccessView(mBlurMap1.Get(), nullptr, &uavDesc, mBlur1CpuUav);
}

std::vector<float> GaussianBlurFilter::CalcGaussWeights(float sigma)
{
	std::vector<float> weights;//�����˹��Ȩ�ؾ���
	int blurRadius = (int)ceil(2.0f * sigma);//ͨ��sigma����ģ���뾶
	assert(blurRadius <= MaxBlurRadius);
	weights.resize(2 * blurRadius + 1);//��˹���ܳ���
	float twoSigma2 = 2.0f * sigma * sigma;//��˹�˼��㹫ʽ�ķ�ĸ

	float weightSum = 0.0f;//Ȩ�غ�

	//����ÿ��Ȩ�أ�δ��һ����
	for (int i = -blurRadius; i <= blurRadius; ++i)
	{
		float x = (float)i;

		weights[i + blurRadius] = expf(-x * x / twoSigma2);//����ÿ��Ȩ��

		weightSum += weights[i + blurRadius];//����Ȩ�غ�
	}
	//��һ��Ȩ��
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

		//���µĴ�С���ؽ�����������Դ
		BuildResources();

		//��Ȼ�������µ���Դ��ҲӦ��Ϊ�䴴���µ�������
		BuildDescriptors();
	}
}

// Ϊ������ɫ���󶨸�ǩ�����������ݡ���Դ���������Թ���ʹ��
// ֮ǰһֱ�ǽ���դ�����ͼ�񴫵ݸ���̨����������������Ļ�ϣ�����Ⱦ���������ǽ���դ�����ͼ�����һ�����������У�
// ���Զ������������������㣬�����ս��������ٴ��ݸ���̨������������Ļ��
// �������Ϳ��Էŵ�������ɫ����ȥ������Ϊ������ɫ���ǿ��Զ�ȡ������ɫ����������ݵġ�
void GaussianBlurFilter::Execute(ID3D12GraphicsCommandList* cmdList,
	ID3D12RootSignature* rootSig,
	ID3D12PipelineState* horzBlurPSO,
	ID3D12PipelineState* vertBlurPSO,
	ID3D12Resource* input,
	int blurCount)
{
	auto weights = CalcGaussWeights(2.5f);//�����˹��
	int blurRadius = (int)weights.size() / 2;//�������ģ������

	//���ø�ǩ���͸�����
	cmdList->SetComputeRootSignature(rootSig);//�󶨴���������ɫ���ĸ�ǩ��

	//���á�ģ���뾶���ĸ�����
	cmdList->SetComputeRoot32BitConstants(0, 1, &blurRadius, 0);

	//���á���˹��Ȩ�ء��ĸ�����
	cmdList->SetComputeRoot32BitConstants(0,  //����������
		(UINT)weights.size(), //����������Ԫ�ظ���
		weights.data(), //����������
		1);//��Ԫ��λ�ڸ�������λ��

	//����̨��������Դת���ɡ�����Դ��״̬
	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(input,
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE));

	//��blurMap0����������Դת���ɡ�����Ŀ�ꡱ״̬
	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mBlurMap0.Get(),
		D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST));

	//����̨������������blurMap0����������
	cmdList->CopyResource(mBlurMap0.Get(), input);

	// ��blurMap0����������Դת���ɡ��ɶ���״̬
	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mBlurMap0.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ));

	// ��blurMap1����������Դת���ɡ���д�롱״̬
	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mBlurMap1.Get(),
		D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));

	for (int i = 0; i < blurCount; ++i)
	{
		//
		//ִ�к���ģ��
		//
		cmdList->SetPipelineState(horzBlurPSO);//ִ�к��������ɫ��

		cmdList->SetComputeRootDescriptorTable(1, mBlur0GpuSrv);//blurMap0��Ϊ����
		cmdList->SetComputeRootDescriptorTable(2, mBlur1GpuUav);//blurMap1��Ϊ���
		//��Ҫ���ȶ�����������һ������
		//ÿ�鸲��256�����أ�256��ComputeShader�ж��壩
		UINT numGroupsX = (UINT)ceilf(mWidth / 256.0f);//X������߳�������
		// D3D12ͨ��Dispatch���������߳���
		cmdList->Dispatch(numGroupsX, mHeight, 1);//�����߳���(֮���ִ�м�����ɫ��)

		// ��blurMap0����������Դת���ɡ���д�롱״̬
		cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mBlurMap0.Get(),
			D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));

		// ��blurMap1����������Դת���ɡ��ɶ���״̬
		cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mBlurMap1.Get(),
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ));

		//
		//ִ������ģ��
		//
		cmdList->SetPipelineState(vertBlurPSO);//ִ�����������ɫ��

		cmdList->SetComputeRootDescriptorTable(1, mBlur1GpuSrv);//blurMap1��Ϊ����
		cmdList->SetComputeRootDescriptorTable(2, mBlur0GpuUav);//blurMap0��Ϊ���

		//��Ҫ���ȶ�����������һ������
		//ÿ�鸲��256�����أ�256��ComputeShader�ж��壩
		UINT numGroupsY = (UINT)ceilf(mHeight / 256.0f);//Y������߳�������
		cmdList->Dispatch(mWidth, numGroupsY, 1);//�����߳���(֮���ִ�м�����ɫ��)

		// ��blurMap0����������Դת���ɡ���д�롱״̬
		cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mBlurMap0.Get(),
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ));

		// ��blurMap1����������Դת���ɡ��ɶ���״̬
		cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mBlurMap1.Get(),
			D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
	}
}