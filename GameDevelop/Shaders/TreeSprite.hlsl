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

// 纹理来自寄存器t[n]，采样器来自s[n]，常量缓冲区来自b[n]
// Texture2D gDiffuseMap : register(t0);//所有漫反射贴图
Texture2DArray gTreeMapArray : register(t0);//定义数组纹理

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
	float3 PosW  : POSITION;
	float2 SizeW : SIZE;
};

struct VertexOut
{
	float3 CenterW : POSITION;
	float2 SizeW   : SIZE;
};

// 新增一个GeoOut的几何着色器输出结构体
// 因为需要传入PS中做光照计算，所以新增PosW和NormalW以及TexCoord，
// 并且齐次除法也要在这里做, 所以加入裁剪空间的顶点坐标
struct GeoOut
{
	float4 PosH    : SV_POSITION;//裁剪空间的顶点坐标
	float3 PosW    : POSITION;//世界空间的顶点坐标
	float3 NormalW : NORMAL;//世界空间下法向量
	float2 TexC    : TEXCOORD;//纹理坐标
	// 该参数的语义为SV_PrimitiveID，若指定了该语义，则输出装配器阶段会自动为每个图元生成图元ID
	// 每次调用一个绘制函数，图元ID的计数会被重置为0
	uint   PrimID  : SV_PrimitiveID;//自动生成图元ID
};

VertexOut VS(VertexIn vin)
{
	VertexOut vout;

	// 只传递数据到geometry shader.
	vout.CenterW = vin.PosW;
	vout.SizeW = vin.SizeW;

	return vout;
}
// 几何着色器
// 最上面是输出顶点的最大值，公告板是4个顶点，所以设置4
// 输入图元为一个点，所以gin[]数组长度为1
// 函数返回空，真正的返回值在参数中返回，所以修饰符必须为inout，然后书写返回的输出流
[maxvertexcount(4)]
void GS(point VertexOut gin[1],//输入图元是一个“点”，所以gin数组元素数量为1
	uint primID : SV_PrimitiveID,//传入图元ID，并输出至像素着色器
	inout TriangleStream<GeoOut> triStream)//输出流是三角带
{
	//计算公告牌的三轴向量 look up right
	float3 up = float3(0.0f, 1.0f, 0.0f);//向上轴的方向
	float3 look = gEyePosW - gin[0].CenterW;//公告板中心指向摄像机的向量（世界空间）
	look.y = 0.0f; // y轴对齐，因此投影到xz平面 使得公告板始终垂直于地面
	look = normalize(look);//归一化look向量
	float3 right = cross(up, look);//DX是左手坐标系，所以要up叉乘look
	//up = cross(look,right);

	//计算公告板的半宽和半高
	float halfWidth = 0.5f * gin[0].SizeW.x;
	float halfHeight = 0.5f * gin[0].SizeW.y;

	//计算公告板4个顶点的世界坐标(四维齐次坐标)
	float4 v[4];
	v[0] = float4(gin[0].CenterW + halfWidth * right - halfHeight * up, 1.0f);
	v[1] = float4(gin[0].CenterW + halfWidth * right + halfHeight * up, 1.0f);
	v[2] = float4(gin[0].CenterW - halfWidth * right - halfHeight * up, 1.0f);
	v[3] = float4(gin[0].CenterW - halfWidth * right + halfHeight * up, 1.0f);

	//计算顶点UV坐标
	//起点在左上角 opengl起点在左下角
	float2 texC[4] =
	{
		float2(0.0f, 1.0f),
		float2(0.0f, 0.0f),
		float2(1.0f, 1.0f),
		float2(1.0f, 0.0f)
	};

	//最后输出数据给像素着色器
	GeoOut gout;
	[unroll]
	for (int i = 0; i < 4; ++i)
	{
		gout.PosH = mul(v[i], gViewProj);//将公告板4顶点从世界空间转换到裁剪空间
		gout.PosW = v[i].xyz;
		gout.NormalW = up;
		gout.TexC = texC[i];
		gout.PrimID = primID;//图元ID输出

		triStream.Append(gout);//将输出数据合并至输出流
	}
}

float4 PS(GeoOut pin) : SV_Target
{
	// 随着自动生成图元的增加，模3后的值在0、1、2中循环
	float3 uvw = float3(pin.TexC, pin.PrimID % 3);
	// Sample的两个参数分别是采样器类型和UV坐标
	float4 diffuseAlbedo = gTreeMapArray.Sample(gsamAnisotropicWrap, uvw) * gDiffuseAlbedo;

// 着色器宏
#ifdef ALPHA_TEST
	clip(diffuseAlbedo.a - 0.1f);
#endif

	pin.NormalW= normalize(pin.NormalW);

	float3 toEyeW = gEyePosW - pin.PosW;
	float distPosToEye = length(toEyeW);
	toEyeW /= distPosToEye;

	const float shininess = 1.0f - gRoughness;
	Material mat = { diffuseAlbedo, gFresnelR0, shininess };
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

		//float4 litColor = ambient + directLight;
		//从漫反射材质获取alpha的常见约定
		//litColor.a = diffuseAlbedo.a * galpha;

		//return litColor;
	}


#ifdef FOG
	float s = saturate((distPosToEye - gFogStart) / gFogRange);
	finalCol = lerp(finalCol, gFogColor, s);
#endif

	finalCol.a = diffuseAlbedo.a * galpha;

	return finalCol;
}


