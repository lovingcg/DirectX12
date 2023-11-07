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
	float3 PosL    : POSITION;
};

struct VertexOut
{
	float3 PosL    : POSITION;
};
// ��������ϸ�֣�������ɫ���ͱ���ˡ�������Ƶ����ɫ������ֻ�轫���Ƶ㴫�룬ֱ���������
VertexOut VS(VertexIn vin)
{
	VertexOut vout;

	vout.PosL = vin.PosL;

	return vout;
}

//����ϸ������
struct PatchTess
{
	float EdgeTess[4]   : SV_TessFactor;//4���ߵ�ϸ������
	float InsideTess[2] : SV_InsideTessFactor;//�ڲ���ϸ�����ӣ�u��v��������
};
//�����ɫ�����֣���ʵ������������ɣ����������ɫ���Ϳ��Ƶ������ɫ��
//���������ɫ�� ����Ƭ���д��� ������������������ϸ������
PatchTess ConstantHS(InputPatch<VertexOut, 16> patch, //�������Ƭ���Ƶ�
					 uint patchID : SV_PrimitiveID)//��ƬͼԪID
{
	PatchTess pt;

	//�������Ƭ���е�
	float3 centerL = 0.25f * (patch[0].PosL + patch[1].PosL + patch[2].PosL + patch[3].PosL);
	//�����ĵ��ģ�Ϳռ�ת������ռ���
	float3 centerW = mul(float4(centerL, 1.0f), gWorld).xyz;

	//�������������Ƭ�ľ���
	float d = distance(centerW, gEyePosW);

	//�������۾��ľ���ϸ����Ƭ���Ա�
	//���d>=d1����ǶΪ0�����d<=d0����ǶΪ64�����
	//[d0��d1]������������Ƕ�ķ�Χ��

	//LOD�ı仯���䣨������С���䣩
	const float d0 = 20.0f;
	const float d1 = 100.0f;
	float tess = 64.0f * saturate((d1 - d) / (d1 - d0));

	//��ֵ���бߺ��ڲ���ϸ������
	pt.EdgeTess[0] = tess;
	pt.EdgeTess[1] = tess;
	pt.EdgeTess[2] = tess;
	pt.EdgeTess[3] = tess;

	pt.InsideTess[0] = tess;
	pt.InsideTess[1] = tess;

	return pt;
}

// �����ɫ�������
// ���Ƶ������ɫ��
// �����Ǹı�����ı�ʾ��ʽ�����罫4�����Ƶ����Ƭ��ת����16�����Ƶ�����棬��ƪ���ǲ��ı���Ƶ��������������Ƶ����벢���
struct HullOut
{
	float3 PosL : POSITION;
};

[domain("quad")]//�����patchΪ�ı�����Ƭ
[partitioning("integer")]//ϸ��ģʽΪ����
[outputtopology("triangle_cw")] //����������Ϊ˳ʱ��
[outputcontrolpoints(16)]//HSִ�д���
[patchconstantfunc("ConstantHS")]//��ִ�еġ����������ɫ������
[maxtessfactor(64.0f)]//ϸ���������ֵ
HullOut HS(InputPatch<VertexOut, 16> p,//���Ƶ�
	uint i : SV_OutputControlPointID,//����ִ�еĿ��Ƶ�����
	uint patchId : SV_PrimitiveID)//ͼԪ����
{
	HullOut hout;

	hout.PosL = p[i].PosL;//����ֵ�����Ƶ���������

	return hout;
}

// ��Ƕ������ڳ��������ɫ���������ϸ�����ӣ�����Ƭ������Ƕ���������������Ӳ�����

// ����ɫ��������Ƭ���õģ����Ĺ����Ǹ���ϸ�����Ӻ���Ƕ�����Ķ���UV�������ʵ�ʶ������꣬������ת������βü��ռ�
struct DomainOut
{
	float4 PosH : SV_POSITION;
	float3 PosWorld : POSITION;
};

//���㲮��˹̹��������4��ϵ�������ף�
float4 BernsteinBasis(float t)
{
	float invT = 1.0f - t;
	//���㲮��˹̹��������4��ϵ�������ף�
	return float4(invT * invT * invT,
		3.0f * t * invT * invT,
		3.0f * t * t * invT,
		t * t * t);
}

//ͨ������˹̹ϵ��������Ƶ�����
// ʵ�ֱ��������� ���ض�������
float3 CubicBezierSum(const OutputPatch<HullOut, 16> bezpatch, float4 basisU, float4 basisV)
{
	float3 sum = float3(0.0f, 0.0f, 0.0f);
	sum = basisV.x * (basisU.x * bezpatch[0].PosL + basisU.y * bezpatch[1].PosL + basisU.z * bezpatch[2].PosL + basisU.w * bezpatch[3].PosL);
	sum += basisV.y * (basisU.x * bezpatch[4].PosL + basisU.y * bezpatch[5].PosL + basisU.z * bezpatch[6].PosL + basisU.w * bezpatch[7].PosL);
	sum += basisV.z * (basisU.x * bezpatch[8].PosL + basisU.y * bezpatch[9].PosL + basisU.z * bezpatch[10].PosL + basisU.w * bezpatch[11].PosL);
	sum += basisV.w * (basisU.x * bezpatch[12].PosL + basisU.y * bezpatch[13].PosL + basisU.z * bezpatch[14].PosL + basisU.w * bezpatch[15].PosL);

	return sum;
}

//����ɫ��
[domain("quad")]
DomainOut DS(PatchTess patchTess,//ϸ������
	float2 uv : SV_DomainLocation,//ϸ�ֺ󶥵�UV��λ��UV��������UV��
	const OutputPatch<HullOut, 16> bezPatch)//patch��16�����Ƶ�
{
	DomainOut dout;

	////ʹ��˫���Բ�ֵ(���������������)������Ƶ�����
	//float3 v1 = lerp(quad[0].PosL, quad[1].PosL, uv.x);
	//float3 v2 = lerp(quad[2].PosL, quad[3].PosL, uv.x);
	//float3 p = lerp(v1, v2, uv.y);

	////������Ƶ�߶ȣ�Yֵ��
	//p.y = 0.3f * (p.z * sin(p.x) + p.x * cos(p.z));

	//p.y += 20.0f;

	////�����Ƶ�����ת�����ü��ռ�
	//float4 posW = mul(float4(p, 1.0f), gWorld);
	//dout.PosWorld = posW.xyz;
	//dout.PosH = mul(posW, gViewProj);

	//return dout;
	//���㲮��˹̹ϵ��
	float4 basisU = BernsteinBasis(uv.x);
	float4 basisV = BernsteinBasis(uv.y);
	//������Ƶ�����
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