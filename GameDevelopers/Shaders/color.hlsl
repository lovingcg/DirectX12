// 3个方向灯
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

struct InstanceData
{
	float4x4 World;
	float4x4 TexTransform;
	uint     MaterialIndex;
	uint     InstPad0;
	uint     InstPad1;
	uint     InstPad2;
};

// 因为纹理资源和结构化缓冲区都是通过SRV传入的，所以都是t寄存器，后面的数值要和根签名创建时的一一对应
// 用来接收matCB中数据
struct MaterialData
{
	float4   DiffuseAlbedo;//材质反照率
	float3   FresnelR0;//RF(0)值，即材质的反射属性
	float    Roughness;//材质的粗糙度
	float4x4 MatTransform;//UV动画变换矩阵
	uint     DiffuseMapIndex;//纹理数组索引
	uint     MatPad0;
	uint     MatPad1;
	uint     MatPad2;
};

// 纹理来自寄存器t[n]，采样器来自s[n]，常量缓冲区来自b[n]
//纹理数组，仅在着色器模型5.1+中支持。与Texture2DArray不同，纹理
//在这个数组中可以有不同的大小和格式，使它比一般纹理数组更灵活。
Texture2D gDiffuseMap[7] : register(t0);//所有漫反射贴图

//实例数据的结构化缓冲区，使用t0的space1空间
StructuredBuffer<InstanceData> gInstanceData : register(t0, space1);
//材质数据的结构化缓冲区，使用t0的space1空间
StructuredBuffer<MaterialData> gMaterialData : register(t1, space1);

//6个不同类型的采样器
SamplerState gsamPointWrap        : register(s0);
SamplerState gsamPointClamp       : register(s1);
SamplerState gsamLinearWrap       : register(s2);
SamplerState gsamLinearClamp      : register(s3);
SamplerState gsamAnisotropicWrap  : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);

//从寄存器槽号为0的地址去访问常量缓冲区中的常量数据
//cbuffer cbPerObject : register(b0)
//{
//	float4x4 gWorld; 
//	float4x4 gTexTransform;//UV缩放矩阵
//	uint gMaterialIndex;
//	uint gObjPad0;
//	uint gObjPad1;
//	uint gObjPad2;
//};
//寄存器槽号要和根签名(SetGraphicsRootConstantBufferView)设置时的一一对应
cbuffer cbPass : register(b0)
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

//cbuffer cbMaterial : register(b2)
//{
//	float4 gDiffuseAlbedo;
//	float3 gFresnelR0;
//	float  gRoughness;
//	float4x4 gMatTransform; //UV动画变换矩阵
//};

//数据冒号后面的POSITION和COLOR即为顶点输入布局描述中的定义，由此对应数据才能传入着色器的顶点函数和片元函数。
struct VertexIn
{
	float3 PosL  : POSITION;
    float3 NormalL : NORMAL;
	float2 TexC : TEXCOORD;
};

struct VertexOut
{
	float4 PosH  : SV_POSITION;
	float3 PosW    : POSITION;
	float3 NormalW : NORMAL;
	float2 TexC : TEXCOORD;

	nointerpolation uint MatIndex  : MATINDEX;//instanceID传入PS
};
// 使用SV_InstanceID语义来获取逐实例数据 能根据不同的实例索引到不同的世界矩阵和材质球，这些都是通过StructuredBuffer数组来动态调用的
VertexOut VS(VertexIn vin, uint instanceID : SV_InstanceID)
{
	VertexOut vout;

	//获取实例数组元素
	InstanceData instData = gInstanceData[instanceID];
	float4x4 world = instData.World;
	float4x4 texTransform = instData.TexTransform;
	uint matIndex = instData.MaterialIndex;

	vout.MatIndex = matIndex;
	//使用结构化缓冲区数组（结构化缓冲区是由若干类型数据所组成的数组）
	//材质索引来自实例数据
	MaterialData matData = gMaterialData[matIndex];

	//MaterialData matData = gMaterialData[gMaterialIndex];
	
	float4 PosW = mul(float4(vin.PosL, 1.0f), world);
	vout.PosW = PosW.xyz;

	vout.NormalW = mul(vin.NormalL, (float3x3)world);

	vout.PosH = mul(PosW, gViewProj);
    

	//计算UV坐标的静态偏移
	float4 texCoord = mul(float4(vin.TexC, 0.0f, 1.0f), texTransform);
	//配合时间函数计算UV坐标的动态偏移（UV动画）
	// hlsl中为行向量右乘矩阵
	vout.TexC = mul(texCoord, matData.MatTransform).xy;

    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
	// diffuseAlbedo, fresnelR0, roughness动态传递给光照函数
	MaterialData matData = gMaterialData[pin.MatIndex];
	//MaterialData matData = gMaterialData[gMaterialIndex];
	float4 diffuseAlbedo = matData.DiffuseAlbedo;
	float3 fresnelR0 = matData.FresnelR0;
	float  roughness = matData.Roughness;
	uint diffuseTexIndex = matData.DiffuseMapIndex;

	//在数组中动态地查找纹理
	diffuseAlbedo *= gDiffuseMap[diffuseTexIndex].Sample(gsamLinearWrap, pin.TexC);

	// Sample的两个参数分别是采样器类型和UV坐标
	// float4 diffuseAlbedo = gDiffuseMap.Sample(gsamAnisotropicWrap, pin.TexC) * gDiffuseAlbedo;

 //着色器宏
#ifdef ALPHA_TEST
	clip(diffuseAlbedo.a - 0.1f);
#endif

	pin.NormalW= normalize(pin.NormalW);

	float3 toEyeW = gEyePosW - pin.PosW;
	float distPosToEye = length(toEyeW);
	toEyeW /= distPosToEye;

	const float shininess = 1.0f - roughness;
	Material mat = { diffuseAlbedo, fresnelR0, shininess };
	float3 shadowFactor = 1.0f;//暂时使用1.0，不对计算产生影响 

	float4 finalCol;

	//卡通着色
	if (gCartoonShader)
	{
		float3 lightVec = -gLights[0].Direction;
		float m = mat.Shininess * 256.0f; //粗糙度因子里的m值
		float3 halfVec = normalize(lightVec + toEyeW); //半角向量

		float ks = pow(max(dot(pin.NormalW, halfVec), 0.0f), m);
		if (ks >= 0.0f && ks <= 0.1f)
			ks = 0.0f;
		if (ks > 0.1f && ks <= 0.8f)
			ks = 0.5f;
		if (ks > 0.8f && ks <= 1.0f)
			ks = 0.8f;

		float roughnessFactor = (m + 8.0f) / 8.0f * ks; //粗糙度因子
		float3 fresnelFactor = SchlickFresnel(mat.FresnelR0, halfVec, lightVec); //菲尼尔因子

		float3 specAlbedo = fresnelFactor * roughnessFactor; //镜面反射反照率=菲尼尔因子*粗糙度因子
		specAlbedo = specAlbedo / (specAlbedo + 1.0f); //将镜面反射缩放到[0，1]

		float kd = max(dot(pin.NormalW, lightVec), 0.0f);
		if (kd <= 0.1f)
			kd = 0.1f;
		if (kd > 0.1f && kd <= 0.5f)
			kd = 0.15f;
		if (kd > 0.5f && kd <= 1.0f)
			kd = 0.25f;
		float3 lightStrength = kd * 5 * gLights[0].Strength; //方向光单位面积上的辐照度 

		float3 directLight = lightStrength * (mat.DiffuseAlbedo.rgb + specAlbedo);

		//环境光照
		float4 ambient = gAmbientLight * diffuseAlbedo;
		//最终光照
		finalCol = ambient + float4(directLight, 1.0f);
	}
	else
	{
		float4 ambient = gAmbientLight * diffuseAlbedo;
		float4 directLight = ComputeLighting(gLights, mat, pin.PosW, pin.NormalW, toEyeW, shadowFactor);

		finalCol = ambient + directLight;
	}


#ifdef FOG
	float s = saturate((distPosToEye - gFogStart) / gFogRange);
	finalCol = lerp(finalCol, gFogColor, s);
#endif

	finalCol.a = diffuseAlbedo.a ;

	return finalCol;
}


