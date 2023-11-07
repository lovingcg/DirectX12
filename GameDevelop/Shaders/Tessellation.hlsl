// 一个方向灯
#ifndef NUM_DIR_LIGHTS
#define NUM_DIR_LIGHTS 3
#endif

#ifndef NUM_POINT_LIGHTS
#define NUM_POINT_LIGHTS 0
#endif

#ifndef NUM_SPOT_LIGHTS
#define NUM_SPOT_LIGHTS 0
#endif

#include "LightingTools.hlsl"

// 纹理来自寄存器t[n]，采样器来自s[n]，常量缓冲区来自b[n]
Texture2D gDiffuseMap : register(t0);//所有漫反射贴图

//6个不同类型的采样器
SamplerState gsamPointWrap        : register(s0);
SamplerState gsamPointClamp       : register(s1);
SamplerState gsamLinearWrap       : register(s2);
SamplerState gsamLinearClamp      : register(s3);
SamplerState gsamAnisotropicWrap  : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);

//从寄存器槽号为0的地址去访问常量缓冲区中的常量数据
cbuffer cbPerObject : register(b0)
{
	float4x4 gWorld;
	float4x4 gTexTransform;//UV缩放矩阵
};
//寄存器槽号要和根签名(SetGraphicsRootConstantBufferView)设置时的一一对应
cbuffer cbPass : register(b1)
{
	float4x4 gViewProj;
	float3 gEyePosW;
	//float gTotalTime;
	bool gCartoonShader;
	float4 gFogColor;
	float gFogStart;
	float gFogRange;
	float2 pad2;
	float galpha;
	float3 cbPerObjectPad3;
	float4 gAmbientLight;
	Light gLights[MaxLights];
};

cbuffer cbMaterial : register(b2)
{
	float4 gDiffuseAlbedo;
	float3 gFresnelR0;
	float  gRoughness;
	float4x4 gMatTransform; //UV动画变换矩阵
};

//数据冒号后面的POSITION和COLOR即为顶点输入布局描述中的定义，由此对应数据才能传入着色器的顶点函数和片元函数。
struct VertexIn
{
	float3 PosL    : POSITION;
};

struct VertexOut
{
	float3 PosL    : POSITION;
};
// 开启曲面细分，顶点着色器就变成了“处理控制点的着色器”。只需将控制点传入，直接输出即可
VertexOut VS(VertexIn vin)
{
	VertexOut vout;

	vout.PosL = vin.PosL;

	return vout;
}

//曲面细分因子
struct PatchTess
{
	float EdgeTess[4]   : SV_TessFactor;//4条边的细分因子
	float InsideTess[2] : SV_InsideTessFactor;//内部的细分因子（u和v两个方向）
};
//外壳着色器部分，它实际由两部分组成：常量外壳着色器和控制点外壳着色器
//常量外壳着色器 逐面片进行处理 任务是输出网格的曲面细分因子
PatchTess ConstantHS(InputPatch<VertexOut, 16> patch, //输入的面片控制点
					 uint patchID : SV_PrimitiveID)//面片图元ID
{
	PatchTess pt;

	//计算出面片的中点
	float3 centerL = 0.25f * (patch[0].PosL + patch[1].PosL + patch[2].PosL + patch[3].PosL);
	//将中心点从模型空间转到世界空间下
	float3 centerW = mul(float4(centerL, 1.0f), gWorld).xyz;

	//计算摄像机和面片的距离
	float d = distance(centerW, gEyePosW);

	//根据与眼睛的距离细分贴片，以便
	//如果d>=d1则镶嵌为0，如果d<=d0则镶嵌为64。间隔
	//[d0，d1]定义了我们镶嵌的范围。

	//LOD的变化区间（最大和最小区间）
	const float d0 = 20.0f;
	const float d1 = 100.0f;
	float tess = 64.0f * saturate((d1 - d) / (d1 - d0));

	//赋值所有边和内部的细分因子
	pt.EdgeTess[0] = tess;
	pt.EdgeTess[1] = tess;
	pt.EdgeTess[2] = tess;
	pt.EdgeTess[3] = tess;

	pt.InsideTess[0] = tess;
	pt.InsideTess[1] = tess;

	return pt;
}

// 外壳着色器的输出
// 控制点外壳着色器
// 作用是改变曲面的表示方式，例如将4个控制点的面片，转化成16个控制点的曲面，这篇我们不改变控制点数量，仅将控制点输入并输出
struct HullOut
{
	float3 PosL : POSITION;
};

[domain("quad")]//传入的patch为四边形面片
[partitioning("integer")]//细分模式为整型
[outputtopology("triangle_cw")] //三角形绕序为顺时针
[outputcontrolpoints(16)]//HS执行次数
[patchconstantfunc("ConstantHS")]//所执行的“常量外壳着色器名”
[maxtessfactor(64.0f)]//细分因子最大值
HullOut HS(InputPatch<VertexOut, 16> p,//控制点
	uint i : SV_OutputControlPointID,//正被执行的控制点索引
	uint patchId : SV_PrimitiveID)//图元索引
{
	HullOut hout;

	hout.PosL = p[i].PosL;//仅传值，控制点数量不变

	return hout;
}

// 镶嵌器会基于常量外壳着色器所输出的细分因子，对面片进行镶嵌化处理。这个处理由硬件完成

// 域着色器是逐面片调用的，它的功能是根据细分因子和镶嵌处理后的顶点UV，计算出实际顶点坐标，并将其转换到齐次裁剪空间
struct DomainOut
{
	float4 PosH : SV_POSITION;
	float3 PosWorld : POSITION;
};

//计算伯恩斯坦基函数的4个系数（三阶）
float4 BernsteinBasis(float t)
{
	float invT = 1.0f - t;
	//计算伯恩斯坦基函数的4个系数（三阶）
	return float4(invT * invT * invT,
		3.0f * t * invT * invT,
		3.0f * t * t * invT,
		t * t * t);
}

//通过伯恩斯坦系数计算控制点坐标
// 实现贝塞尔曲面 返回顶点坐标
float3 CubicBezierSum(const OutputPatch<HullOut, 16> bezpatch, float4 basisU, float4 basisV)
{
	float3 sum = float3(0.0f, 0.0f, 0.0f);
	sum = basisV.x * (basisU.x * bezpatch[0].PosL + basisU.y * bezpatch[1].PosL + basisU.z * bezpatch[2].PosL + basisU.w * bezpatch[3].PosL);
	sum += basisV.y * (basisU.x * bezpatch[4].PosL + basisU.y * bezpatch[5].PosL + basisU.z * bezpatch[6].PosL + basisU.w * bezpatch[7].PosL);
	sum += basisV.z * (basisU.x * bezpatch[8].PosL + basisU.y * bezpatch[9].PosL + basisU.z * bezpatch[10].PosL + basisU.w * bezpatch[11].PosL);
	sum += basisV.w * (basisU.x * bezpatch[12].PosL + basisU.y * bezpatch[13].PosL + basisU.z * bezpatch[14].PosL + basisU.w * bezpatch[15].PosL);

	return sum;
}

//域着色器
[domain("quad")]
DomainOut DS(PatchTess patchTess,//细分因子
	float2 uv : SV_DomainLocation,//细分后顶点UV（位置UV，非纹理UV）
	const OutputPatch<HullOut, 16> bezPatch)//patch的16个控制点
{
	DomainOut dout;

	////使用双线性插值(先算横向，再算纵向)算出控制点坐标
	//float3 v1 = lerp(quad[0].PosL, quad[1].PosL, uv.x);
	//float3 v2 = lerp(quad[2].PosL, quad[3].PosL, uv.x);
	//float3 p = lerp(v1, v2, uv.y);

	////计算控制点高度（Y值）
	//p.y = 0.3f * (p.z * sin(p.x) + p.x * cos(p.z));

	//p.y += 20.0f;

	////将控制点坐标转换到裁剪空间
	//float4 posW = mul(float4(p, 1.0f), gWorld);
	//dout.PosWorld = posW.xyz;
	//dout.PosH = mul(posW, gViewProj);

	//return dout;
	//计算伯恩斯坦系数
	float4 basisU = BernsteinBasis(uv.x);
	float4 basisV = BernsteinBasis(uv.y);
	//计算控制点坐标
	float3 p = CubicBezierSum(bezPatch, basisU, basisV);

	p.y += 10.f;

	float4 posW = mul(float4(p, 1.0f), gWorld);
	dout.PosWorld = posW.xyz;
	dout.PosH = mul(posW, gViewProj);

	return dout;
}

float4 PS(DomainOut pin) : SV_Target
{
	float3 toEyeW = gEyePosW - pin.PosWorld;
	float distPosToEye = length(toEyeW);
		
	float4 finalCol = float4(pin.PosWorld, 1.0f);

#ifdef FOG
	float s = saturate((distPosToEye - gFogStart) / gFogRange);
	finalCol = lerp(finalCol, gFogColor, s);
#endif

	return finalCol;
	return float4(0.0f, 1.0f, 0.0f, 1.0f);
}