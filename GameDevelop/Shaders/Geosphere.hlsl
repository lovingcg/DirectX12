// һ�������
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

// �������ԼĴ���t[n]������������s[n]����������������b[n]
Texture2D gDiffuseMap : register(t0);//������������ͼ

//6����ͬ���͵Ĳ�����
SamplerState gsamPointWrap        : register(s0);
SamplerState gsamPointClamp       : register(s1);
SamplerState gsamLinearWrap       : register(s2);
SamplerState gsamLinearClamp      : register(s3);
SamplerState gsamAnisotropicWrap  : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);

//�ӼĴ����ۺ�Ϊ0�ĵ�ַȥ���ʳ����������еĳ�������
cbuffer cbPerObject : register(b0)
{
	float4x4 gWorld; 
	float4x4 gTexTransform;//UV���ž���
};
//�Ĵ����ۺ�Ҫ�͸�ǩ��(SetGraphicsRootConstantBufferView)����ʱ��һһ��Ӧ
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
	float4x4 gMatTransform; //UV�����任����
};

//����ð�ź����POSITION��COLOR��Ϊ�������벼�������еĶ��壬�ɴ˶�Ӧ���ݲ��ܴ�����ɫ���Ķ��㺯����ƬԪ������
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

// ����λ�ü��������������
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

	uint depth = pow(2, cnt);//������ϸ�ֲ���
	uint vcnt = depth * 2 + 1;// ������һ�㶥����
	float len = distance(gin[0].Vertex, gin[1].Vertex) / (float)depth; // ÿ��С�����α߳�
	float3 rightup = gin[1].Vertex - gin[0].Vertex;// ���Ϸ�
	rightup = normalize(rightup);
	float3 right = gin[2].Vertex - gin[0].Vertex;// �ҷ�
	right = normalize(right);

	// ����ȷ��ÿһ�������ε�λ��
	float3 down = gin[0].Vertex;
	float3 up = down + rightup * len;

	[unroll]//��forѭ����unroll
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
		vcnt -= 2;//����һ�㶥������2
		down = up;
		up += rightup * len;
		triStream.RestartStrip();//�����һ�������������
	}
}

//���������ֻ�ܸ���78
[maxvertexcount(78)]//17+15+13+....+5+3=80
void GS(triangle VertexOut gin[3], //����������ͼԪ�����Զ�������Ϊ3
	inout TriangleStream<GeoOut> triStream)//������Ǵ�
{
	// ������������� д����
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

//[maxvertexcount(6)]//���6������
//void GS(triangle VertexOut gin[3], //����������ͼԪ�����Զ�������Ϊ3
//	inout TriangleStream<GeoOut> triStream)//������Ǵ�
//{
//	VertexOut m[3];
//	//�����е�����
//	m[0].Vertex = (gin[0].Vertex + gin[1].Vertex) * 0.5f;
//	m[1].Vertex = (gin[1].Vertex + gin[2].Vertex) * 0.5f;
//	m[2].Vertex = (gin[0].Vertex + gin[2].Vertex) * 0.5f;
//	//��һ������ 20����������ڣ�0��0��0��
//	m[0].Normal = normalize(m[0].Vertex);
//	m[1].Normal = normalize(m[1].Vertex);
//	m[2].Normal = normalize(m[2].Vertex);
//	//����UV
//	m[0].TexCoord = float2(0.0f, 0.0f);
//	m[1].TexCoord = float2(0.0f, 0.0f);
//	m[2].TexCoord = float2(0.0f, 0.0f);
//	//������ͶӰ���뾶Ϊr��������
//	//�����rҪ��CPU�д�����ʮ����ʱ��rһ�£�������ʵӦ�ý�CPU��rֵ�������ģ�Ϊ�˷��㣬����ֱ��д���ˣ�
//	float r = 0.8f;
//	m[0].Vertex = m[0].Normal * r;
//	m[1].Vertex = m[1].Normal * r;
//	m[2].Vertex = m[2].Normal * r;
//
//	VertexOut geoOutVert[6];
//	// �������Ǵ�ͼԪ�������������
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
	// Sample�����������ֱ��ǲ��������ͺ�UV����
	float4 diffuseAlbedo = gDiffuseMap.Sample(gsamAnisotropicWrap, pin.UV) * gDiffuseAlbedo;

// ��ɫ����
#ifdef ALPHA_TEST
	clip(diffuseAlbedo.a - 0.1f);
#endif

	pin.WorldNormal = normalize(pin.WorldNormal);

	float3 toEyeW = gEyePosW - pin.WorldPos;
	float distPosToEye = length(toEyeW);
	toEyeW /= distPosToEye;

	const float shininess = 1.0f - gRoughness;
	Material mat = { diffuseAlbedo, gFresnelR0, shininess };
	float3 shadowFactor = 1.0f;//��ʱʹ��1.0�����Լ������Ӱ�� 

	float4 finalCol;

	//��ͨ��ɫ
	if (gCartoonShader)
	{
		float3 lightVec = -gLights[0].Direction;
		float m = mat.Shininess * 256.0f; //�ֲڶ��������mֵ
		float3 halfVec = normalize(lightVec + toEyeW); //�������

		float ks = pow(max(dot(pin.WorldNormal, halfVec), 0.0f), m);
		if (ks >= 0.0f && ks <= 0.1f)
			ks = 0.0f;
		if (ks > 0.1f && ks <= 0.8f)
			ks = 0.5f;
		if (ks > 0.8f && ks <= 1.0f)
			ks = 0.8f;

		float roughnessFactor = (m + 8.0f) / 8.0f * ks; //�ֲڶ�����
		float3 fresnelFactor = SchlickFresnel(mat.FresnelR0, halfVec, lightVec); //���������

		float3 specAlbedo = fresnelFactor * roughnessFactor; //���淴�䷴����=���������*�ֲڶ�����
		specAlbedo = specAlbedo / (specAlbedo + 1.0f); //�����淴�����ŵ�[0��1]

		float kd = max(dot(pin.WorldNormal, lightVec), 0.0f);
		if (kd <= 0.1f)
			kd = 0.1f;
		if (kd > 0.1f && kd <= 0.5f)
			kd = 0.15f;
		if (kd > 0.5f && kd <= 1.0f)
			kd = 0.25f;
		float3 lightStrength = kd * 5 * gLights[0].Strength; //����ⵥλ����ϵķ��ն� 

		float3 directLight = lightStrength * (mat.DiffuseAlbedo.rgb + specAlbedo);

		//��������
		float4 ambient = gAmbientLight * diffuseAlbedo;
		//���չ���
		finalCol = ambient + float4(directLight, 1.0f);
	}
	else
	{
		float4 ambient = gAmbientLight * diffuseAlbedo;
		float4 directLight = ComputeLighting(gLights, mat, pin.WorldPos, pin.WorldNormal, toEyeW, shadowFactor);

		finalCol = ambient + directLight;

		//float4 litColor = ambient + directLight;
		//����������ʻ�ȡalpha�ĳ���Լ��
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


