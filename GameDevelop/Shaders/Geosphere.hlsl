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
	float3 PosL  : POSITION;
    float3 NormalL : NORMAL;
	float2 TexC : TEXCOORD;
};

struct VertexOut
{
	float3 Normal : NORMAL;
	float3 Vertex  : POSITION;
	float2 TexCoord : TEXCOORD;
};

struct GeoOut
{
	float4 Pos    : SV_POSITION;
	float4 WorldPos    : POSITION;
	float3 WorldNormal : NORMAL;
	float2 UV : TEXCOORD;
};

VertexOut VS(VertexIn vin)
{
	VertexOut vout;
	
	vout.Vertex = vin.PosL;
	vout.Normal = vin.NormalL;
	float4 texCoord = mul(float4(vin.TexC, 0.0f, 1.0f), gTexTransform);
	vout.TexCoord = texCoord.xy;

    return vout;
}

// 根据位置计算后输出到输出带
void AppendVertex(float3 p, inout TriangleStream<GeoOut> triStream)
{
	const float r = 0.8f;

	VertexOut v;
	v.Vertex = p;
	v.Normal = normalize(v.Vertex);
	v.TexCoord = float2(0.0f, 0.0f);
	v.Vertex = v.Normal * r;

	GeoOut geoOut;
	float4 PosW = mul(float4(v.Vertex, 1.0f), gWorld);
	geoOut.WorldPos = PosW;
	geoOut.WorldNormal = mul(v.Normal, (float3x3) gWorld);
	geoOut.Pos = mul(PosW, gViewProj);
	geoOut.UV = v.TexCoord;

	triStream.Append(geoOut);
}

void Subdivide(int cnt, VertexOut gin[3], inout TriangleStream<GeoOut> triStream)
{
	//const float r = 0.8f;

	uint depth = pow(2, cnt);//三角形细分层数
	uint vcnt = depth * 2 + 1;// 最下面一层顶点数
	float len = distance(gin[0].Vertex, gin[1].Vertex) / (float)depth; // 每个小三角形边长
	float3 rightup = gin[1].Vertex - gin[0].Vertex;// 右上方
	rightup = normalize(rightup);
	float3 right = gin[2].Vertex - gin[0].Vertex;// 右方
	right = normalize(right);

	// 用于确定每一层三角形的位置
	float3 down = gin[0].Vertex;
	float3 up = down + rightup * len;

	[unroll]//用for循环用unroll
	for (uint i = 1; i <= depth; i++)
	{
		for (int j = 0; j < vcnt; j++)
		{
			if (j % 2 == 0)
			{
				AppendVertex(down + right * len * (j / 2), triStream);
			}
			else
			{
				AppendVertex(up + right * len * (j / 2), triStream);
			}
		}
		vcnt -= 2;//往上一层顶点数少2
		down = up;
		up += rightup * len;
		triStream.RestartStrip();//输出完一层后把输出带重置
	}
}

//这里我最多只能给到78
[maxvertexcount(78)]//17+15+13+....+5+3=80
void GS(triangle VertexOut gin[3], //传入三角形图元，所以顶点数组为3
	inout TriangleStream<GeoOut> triStream)//输出三角带
{
	// 物体的世界坐标 写死了
	float dis = distance(float3(0.0f, 5.0f, 0.0f), gEyePosW.xyz);
	uint subCnt = 0;
	if (dis < 45)
		subCnt = 3;
	else if (dis < 55)
		subCnt = 2;
	else if (dis < 65)
		subCnt = 1;
	else if (dis < 75)
		subCnt = 0;
	Subdivide(subCnt, gin, triStream);
}

//[maxvertexcount(6)]//最多6个顶点
//void GS(triangle VertexOut gin[3], //传入三角形图元，所以顶点数组为3
//	inout TriangleStream<GeoOut> triStream)//输出三角带
//{
//	VertexOut m[3];
//	//计算中点坐标
//	m[0].Vertex = (gin[0].Vertex + gin[1].Vertex) * 0.5f;
//	m[1].Vertex = (gin[1].Vertex + gin[2].Vertex) * 0.5f;
//	m[2].Vertex = (gin[0].Vertex + gin[2].Vertex) * 0.5f;
//	//归一化法线 20面体的中心在（0，0，0）
//	m[0].Normal = normalize(m[0].Vertex);
//	m[1].Normal = normalize(m[1].Vertex);
//	m[2].Normal = normalize(m[2].Vertex);
//	//定义UV
//	m[0].TexCoord = float2(0.0f, 0.0f);
//	m[1].TexCoord = float2(0.0f, 0.0f);
//	m[2].TexCoord = float2(0.0f, 0.0f);
//	//将顶点投影到半径为r的球面上
//	//这里的r要和CPU中创建二十面体时的r一致（这里其实应该将CPU的r值传过来的，为了方便，所以直接写死了）
//	float r = 0.8f;
//	m[0].Vertex = m[0].Normal * r;
//	m[1].Vertex = m[1].Normal * r;
//	m[2].Vertex = m[2].Normal * r;
//
//	VertexOut geoOutVert[6];
//	// 按照三角带图元的索引序号排序
//	geoOutVert[0] = gin[0];
//	geoOutVert[1] = m[0];
//	geoOutVert[2] = m[2];
//	geoOutVert[3] = m[1];
//	geoOutVert[4] = gin[2];
//	geoOutVert[5] = gin[1];
//
//	GeoOut geoOut[6];
//	[unroll]
//	for (uint i = 0; i < 6; i++)
//	{
//		float4 PosW = mul(float4(geoOutVert[i].Vertex, 1.0f), gWorld);
//		geoOut[i].WorldPos = PosW;
//		geoOut[i].WorldNormal = mul(geoOutVert[i].Normal, (float3x3) gWorld);
//		geoOut[i].Pos = mul(PosW, gViewProj);
//		geoOut[i].UV = geoOutVert[i].TexCoord;
//
//		triStream.Append(geoOut[i]);
//	}
//}

float4 PS(GeoOut pin) : SV_Target
{
	// Sample的两个参数分别是采样器类型和UV坐标
	float4 diffuseAlbedo = gDiffuseMap.Sample(gsamAnisotropicWrap, pin.UV) * gDiffuseAlbedo;

// 着色器宏
#ifdef ALPHA_TEST
	clip(diffuseAlbedo.a - 0.1f);
#endif

	pin.WorldNormal = normalize(pin.WorldNormal);

	float3 toEyeW = gEyePosW - pin.WorldPos;
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

		float ks = pow(max(dot(pin.WorldNormal, halfVec), 0.0f), m);
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

		float kd = max(dot(pin.WorldNormal, lightVec), 0.0f);
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
		float4 directLight = ComputeLighting(gLights, mat, pin.WorldPos, pin.WorldNormal, toEyeW, shadowFactor);

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


