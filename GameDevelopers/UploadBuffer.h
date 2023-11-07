#pragma once

#include "d3dUtil.h"

using namespace Microsoft::WRL;
//T���Ǹ�����Դ��ģ�� ���ݲ�ͬ���͵��ϴ�������
//���ǵ�����ᾭ���õ��ϴ�����Դ�����Է�װ����
template<typename T>
class UploadBuffer
{
public:
	// ͨ�����캯������ģ������T���ϴ���
	UploadBuffer(ID3D12Device* device, UINT elementCount, bool isConstantBuffer) :mIsConstantBuffer(isConstantBuffer)
	{
		// �ж���Դbyte
		mElementByteSize = sizeof(T);
		if (isConstantBuffer)
			mElementByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(T));
		// �����ϴ���
		ThrowIfFailed(device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(mElementByteSize * elementCount),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&mUploaderBuffer)));
		// ��GPU�ϴ����е���Դӳ�䵽CPU�ڴ���
		ThrowIfFailed(mUploaderBuffer->Map(0,// ����Դ����
			nullptr,// �ڴ�ӳ�䷶Χ��nullptr��������Դ����ӳ��
			reinterpret_cast<void**>(&mMappedData)));//����˫��ָ�룬����ӳ����Դ���ݵ�Ŀ���ڴ��
	}
	//ά������
	UploadBuffer(const UploadBuffer& rhs) = delete;
	UploadBuffer& operator=(const UploadBuffer& rhs) = delete;

	// ����ʱȡ��ӳ�䲢�ͷ��ڴ�
	~UploadBuffer()
	{
		if (mUploaderBuffer != nullptr)
		{
			mUploaderBuffer->Unmap(0,// ����Դ����
				nullptr);// ȡ��ӳ�䷶Χ
		}
		mMappedData = nullptr;
	}
	// ��CPU�ϵ����ݸ��Ƶ�GPU�ϵĻ�������
	void CopyData(int elementIndex, const T& data)
	{
		//mappedData����Ԫ�صĵ�ַ���Ǹ�������Դ�ڻ������ϵ��ֽ�ƫ�Ƶó���
		memcpy(&mMappedData[elementIndex * mElementByteSize], &data, sizeof(T));
	}
	//���ش������ϴ���ָ�루uploadBuffer��
	ID3D12Resource* Resource() const {
		return mUploaderBuffer.Get();
	}

private:
	// �ϴ�����Դ
	ComPtr<ID3D12Resource> mUploaderBuffer;
	// GPU��Դ��CPU�ڴ��е�ӳ��
	BYTE* mMappedData = nullptr;

	UINT mElementByteSize = 0;
	bool mIsConstantBuffer = false;
};