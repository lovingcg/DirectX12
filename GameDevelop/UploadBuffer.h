#pragma once

#include "d3dUtil.h"

using namespace Microsoft::WRL;
//T就是各类资源的模板 兼容不同类型的上传堆数据
//考虑到后面会经常用到上传堆资源，所以封装个类
template<typename T>
class UploadBuffer
{
public:
	// 通过构造函数创建模板类型T的上传堆
	UploadBuffer(ID3D12Device* device, UINT elementCount, bool isConstantBuffer) :mIsConstantBuffer(isConstantBuffer)
	{
		// 判断资源byte
		mElementByteSize = sizeof(T);
		if (isConstantBuffer)
			mElementByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(T));
		// 创建上传堆
		ThrowIfFailed(device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(mElementByteSize * elementCount),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&mUploaderBuffer)));
		// 将GPU上传堆中的资源映射到CPU内存中
		ThrowIfFailed(mUploaderBuffer->Map(0,// 子资源索引
			nullptr,// 内存映射范围，nullptr对整个资源进行映射
			reinterpret_cast<void**>(&mMappedData)));//借助双重指针，返回映射资源数据的目标内存块
	}
	//维护单例
	UploadBuffer(const UploadBuffer& rhs) = delete;
	UploadBuffer& operator=(const UploadBuffer& rhs) = delete;

	// 析构时取消映射并释放内存
	~UploadBuffer()
	{
		if (mUploaderBuffer != nullptr)
		{
			mUploaderBuffer->Unmap(0,// 子资源索引
				nullptr);// 取消映射范围
		}
		mMappedData = nullptr;
	}
	// 将CPU上的数据复制到GPU上的缓冲区中
	void CopyData(int elementIndex, const T& data)
	{
		//mappedData数组元素的地址，是根据子资源在缓冲区上的字节偏移得出的
		memcpy(&mMappedData[elementIndex * mElementByteSize], &data, sizeof(T));
	}
	//返回创建的上传堆指针（uploadBuffer）
	ID3D12Resource* Resource() const {
		return mUploaderBuffer.Get();
	}

private:
	// 上传堆资源
	ComPtr<ID3D12Resource> mUploaderBuffer;
	// GPU资源在CPU内存中的映射
	BYTE* mMappedData = nullptr;

	UINT mElementByteSize = 0;
	bool mIsConstantBuffer = false;
};