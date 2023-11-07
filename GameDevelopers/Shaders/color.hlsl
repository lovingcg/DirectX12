// 3�������
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

// ��Ϊ������Դ�ͽṹ������������ͨ��SRV����ģ����Զ���t�Ĵ������������ֵҪ�͸�ǩ������ʱ��һһ��Ӧ
// ��������matCB������
struct MaterialData
{
	float4   DiffuseAlbedo;//���ʷ�����
	float3   FresnelR0;//RF(0)ֵ�������ʵķ�������
	float    Roughness;//���ʵĴֲڶ�
	float4x4 MatTransform;//UV�����任����
	uint     DiffuseMapIndex;//������������
	uint     MatPad0;
	uint     MatPad1;
	uint     MatPad2;
};

// �������ԼĴ���t[n]������������s[n]����������������b[n]
//�������飬������ɫ��ģ��5.1+��֧�֡���Texture2DArray��ͬ������
//����������п����в�ͬ�Ĵ�С�͸�ʽ��ʹ����һ�������������
Texture2D gDiffuseMap[7] : register(t0);//������������ͼ

//ʵ�����ݵĽṹ����������ʹ��t0��space1�ռ�
StructuredBuffer<InstanceData> gInstanceData : register(t0, space1);
//�������ݵĽṹ����������ʹ��t0��space1�ռ�
StructuredBuffer<MaterialData> gMaterialData : register(t1, space1);

//6����ͬ���͵Ĳ�����
SamplerState gsamPointWrap        : register(s0);
SamplerState gsamPointClamp       : register(s1);
SamplerState gsamLinearWrap       : register(s2);
SamplerState gsamLinearClamp      : register(s3);
SamplerState gsamAnisotropicWrap  : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);

//�ӼĴ����ۺ�Ϊ0�ĵ�ַȥ���ʳ����������еĳ�������
//cbuffer cbPerObject : register(b0)
//{
//	float4x4 gWorld; 
//	float4x4 gTexTransform;//UV���ž���
//	uint gMaterialIndex;
//	uint gObjPad0;
//	uint gObjPad1;
//	uint gObjPad2;
//};
//�Ĵ����ۺ�Ҫ�͸�ǩ��(SetGraphicsRootConstantBufferView)����ʱ��һһ��Ӧ
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
//	float4x4 gMatTransform; //UV�����任����
//};

//����ð�ź����POSITION��COLOR��Ϊ�������벼�������еĶ��壬�ɴ˶�Ӧ���ݲ��ܴ�����ɫ���Ķ��㺯����ƬԪ������
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

	nointerpolation uint MatIndex  : MATINDEX;//instanceID����PS
};
// ʹ��SV_InstanceID��������ȡ��ʵ������ �ܸ��ݲ�ͬ��ʵ����������ͬ���������Ͳ�������Щ����ͨ��StructuredBuffer��������̬���õ�
VertexOut VS(VertexIn vin, uint instanceID : SV_InstanceID)
{
	VertexOut vout;

	//��ȡʵ������Ԫ��
	InstanceData instData = gInstanceData[instanceID];
	float4x4 world = instData.World;
	float4x4 texTransform = instData.TexTransform;
	uint matIndex = instData.MaterialIndex;

	vout.MatIndex = matIndex;
	//ʹ�ýṹ�����������飨�ṹ����������������������������ɵ����飩
	//������������ʵ������
	MaterialData matData = gMaterialData[matIndex];

	//MaterialData matData = gMaterialData[gMaterialIndex];
	
	float4 PosW = mul(float4(vin.PosL, 1.0f), world);
	vout.PosW = PosW.xyz;

	vout.NormalW = mul(vin.NormalL, (float3x3)world);

	vout.PosH = mul(PosW, gViewProj);
    

	//����UV����ľ�̬ƫ��
	float4 texCoord = mul(float4(vin.TexC, 0.0f, 1.0f), texTransform);
	//���ʱ�亯������UV����Ķ�̬ƫ�ƣ�UV������
	// hlsl��Ϊ�������ҳ˾���
	vout.TexC = mul(texCoord, matData.MatTransform).xy;

    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
	// diffuseAlbedo, fresnelR0, roughness��̬���ݸ����պ���
	MaterialData matData = gMaterialData[pin.MatIndex];
	//MaterialData matData = gMaterialData[gMaterialIndex];
	float4 diffuseAlbedo = matData.DiffuseAlbedo;
	float3 fresnelR0 = matData.FresnelR0;
	float  roughness = matData.Roughness;
	uint diffuseTexIndex = matData.DiffuseMapIndex;

	//�������ж�̬�ز�������
	diffuseAlbedo *= gDiffuseMap[diffuseTexIndex].Sample(gsamLinearWrap, pin.TexC);

	// Sample�����������ֱ��ǲ��������ͺ�UV����
	// float4 diffuseAlbedo = gDiffuseMap.Sample(gsamAnisotropicWrap, pin.TexC) * gDiffuseAlbedo;

 //��ɫ����
#ifdef ALPHA_TEST
	clip(diffuseAlbedo.a - 0.1f);
#endif

	pin.NormalW= normalize(pin.NormalW);

	float3 toEyeW = gEyePosW - pin.PosW;
	float distPosToEye = length(toEyeW);
	toEyeW /= distPosToEye;

	const float shininess = 1.0f - roughness;
	Material mat = { diffuseAlbedo, fresnelR0, shininess };
	float3 shadowFactor = 1.0f;//��ʱʹ��1.0�����Լ������Ӱ�� 

	float4 finalCol;

	//��ͨ��ɫ
	if (gCartoonShader)
	{
		float3 lightVec = -gLights[0].Direction;
		float m = mat.Shininess * 256.0f; //�ֲڶ��������mֵ
		float3 halfVec = normalize(lightVec + toEyeW); //�������

		float ks = pow(max(dot(pin.NormalW, halfVec), 0.0f), m);
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

		float kd = max(dot(pin.NormalW, lightVec), 0.0f);
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


