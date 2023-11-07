#include "d3dUtil.h"
#include <d3dcompiler.h>

//返回默认堆
ComPtr<ID3D12Resource> d3dUtil::CreateDefaultBuffer(
	ID3D12Device* device,
	ID3D12GraphicsCommandList* cmdList,
	const void* initData,
	UINT64 byteSize,
	ComPtr<ID3D12Resource>& uploadBuffer)
{
	//创建上传堆，作用是：写入CPU内存数据，并传输给默认堆
	// 为了将CPU端内存中的数据复制到默认缓冲区，我们还需要创建一个处于中介位置的上传堆
	ThrowIfFailed(device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),//创建上传堆类型的堆
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(byteSize),//变体的构造函数，传入byteSize，其他均为默认值
		D3D12_RESOURCE_STATE_GENERIC_READ,//上传堆里的资源需要复制给默认堆，所以是可读状态
		nullptr,//不是深度模板资源，不用指定优化值
		IID_PPV_ARGS(uploadBuffer.GetAddressOf())
	));

	//创建默认堆，作为上传堆的数据传输对象
	ComPtr<ID3D12Resource> defaultBuffer;
	// 创建实际的默认缓冲区资源
	ThrowIfFailed(device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),// 指定堆类型为默认堆
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(byteSize),
		D3D12_RESOURCE_STATE_COMMON,//默认堆为最终存储数据的地方，所以暂时初始化为普通状态
		nullptr,
		IID_PPV_ARGS(defaultBuffer.GetAddressOf())// 指定资源COM ID
	));

	// 将数据复制到默认缓冲区资源的流程
	// 先将默认堆状态从common改成copy_dest状态（默认堆此时作为接收数据的目标）
	cmdList->ResourceBarrier(1,
		&CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer.Get(),
			D3D12_RESOURCE_STATE_COMMON,
			D3D12_RESOURCE_STATE_COPY_DEST));

	// 描述复制到默认缓冲区的数据
	//将数据从CPU内存拷贝到GPU缓存
	D3D12_SUBRESOURCE_DATA subResourceData = {};
	subResourceData.pData = initData;
	subResourceData.RowPitch = byteSize;
	subResourceData.SlicePitch = subResourceData.RowPitch;

	//核心函数UpdateSubresources，将数据从CPU内存拷贝至上传堆，再从上传堆拷贝至默认堆。
	//1是最大的子资源的下标（模板中定义，意为有2个子资源）
	UpdateSubresources<1>(// 1个子资源
		cmdList,
		defaultBuffer.Get(),
		uploadBuffer.Get(),
		0,// 中间资源的偏移量(byte)
		0,// 资源中第一个子资源的索引
		1,// 资源中子资源数
		&subResourceData);
	//再次将资源从COPY_DEST状态转换到GENERIC_READ状态(现在只提供给着色器访问)
	cmdList->ResourceBarrier(1,
	&CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_GENERIC_READ));

	return defaultBuffer;
}

// 编译Shader
//在DX中，着色器程序必须先被编译成一种可移植的字节码，这样图形驱动程序才能将其继续编译成针对当前系统GPU所优化的本地指令。
ComPtr<ID3DBlob> d3dUtil::CompileShader(
	const std::wstring& filename,
	const D3D_SHADER_MACRO* defines,
	const std::string& entrypoint,
	const std::string& target) {
	UINT compileFlags = 0;
#if defined(DEBUG) || defined(_DEBUG)  
	compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

	HRESULT hr = S_OK;

	ComPtr<ID3DBlob> byteCode = nullptr;
	ComPtr<ID3DBlob> errors;
	hr = D3DCompileFromFile(filename.c_str(),// hlsl源文件名
		defines,// 高级选项 指定为空指针
		D3D_COMPILE_STANDARD_FILE_INCLUDE,// 高级选项 可以指定为空指针
		entrypoint.c_str(),// 着色器的入口点函数
		target.c_str(),// 指定所用着色器类型和版本的字符串
		compileFlags,// 指定对着色器断代码应当如何编译的标指
		0,// 高级选项
		&byteCode,// 编译好的字节码
		&errors);// 错误信息

	if (errors != nullptr)
		OutputDebugStringA((char*)errors->GetBufferPointer());

	ThrowIfFailed(hr);

	return byteCode;
}