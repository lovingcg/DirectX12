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

// �������ԼĴ���t[n]������������s[n]����������������b[n]
// Texture2D gDiffuseMap : register(t0);//������������ͼ
Texture2DArray gTreeMapArray : register(t0);//������������

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
	float3 PosW  : POSITION;
	float2 SizeW : SIZE;
};

struct VertexOut
{
	float3 CenterW : POSITION;
	float2 SizeW   : SIZE;
};

// ����һ��GeoOut�ļ�����ɫ������ṹ��
// ��Ϊ��Ҫ����PS�������ռ��㣬��������PosW��NormalW�Լ�TexCoord��
// ������γ���ҲҪ��������, ���Լ���ü��ռ�Ķ�������
struct GeoOut
{
	float4 PosH    : SV_POSITION;//�ü��ռ�Ķ�������
	float3 PosW    : POSITION;//����ռ�Ķ�������
	float3 NormalW : NORMAL;//����ռ��·�����
	float2 TexC    : TEXCOORD;//��������
	// �ò���������ΪSV_PrimitiveID����ָ���˸����壬�����װ�����׶λ��Զ�Ϊÿ��ͼԪ����ͼԪID
	// ÿ�ε���һ�����ƺ�����ͼԪID�ļ����ᱻ����Ϊ0
	uint   PrimID  : SV_PrimitiveID;//�Զ�����ͼԪID
};

VertexOut VS(VertexIn vin)
{
	VertexOut vout;

	// ֻ�������ݵ�geometry shader.
	vout.CenterW = vin.PosW;
	vout.SizeW = vin.SizeW;

	return vout;
}
// ������ɫ��
// �������������������ֵ���������4�����㣬��������4
// ����ͼԪΪһ���㣬����gin[]���鳤��Ϊ1
// �������ؿգ������ķ���ֵ�ڲ����з��أ��������η�����Ϊinout��Ȼ����д���ص������
[maxvertexcount(4)]
void GS(point VertexOut gin[1],//����ͼԪ��һ�����㡱������gin����Ԫ������Ϊ1
	uint primID : SV_PrimitiveID,//����ͼԪID���������������ɫ��
	inout TriangleStream<GeoOut> triStream)//����������Ǵ�
{
	//���㹫���Ƶ��������� look up right
	float3 up = float3(0.0f, 1.0f, 0.0f);//������ķ���
	float3 look = gEyePosW - gin[0].CenterW;//���������ָ�������������������ռ䣩
	look.y = 0.0f; // y����룬���ͶӰ��xzƽ�� ʹ�ù����ʼ�մ�ֱ�ڵ���
	look = normalize(look);//��һ��look����
	float3 right = cross(up, look);//DX����������ϵ������Ҫup���look
	//up = cross(look,right);

	//���㹫���İ��Ͱ��
	float halfWidth = 0.5f * gin[0].SizeW.x;
	float halfHeight = 0.5f * gin[0].SizeW.y;

	//���㹫���4���������������(��ά�������)
	float4 v[4];
	v[0] = float4(gin[0].CenterW + halfWidth * right - halfHeight * up, 1.0f);
	v[1] = float4(gin[0].CenterW + halfWidth * right + halfHeight * up, 1.0f);
	v[2] = float4(gin[0].CenterW - halfWidth * right - halfHeight * up, 1.0f);
	v[3] = float4(gin[0].CenterW - halfWidth * right + halfHeight * up, 1.0f);

	//���㶥��UV����
	//��������Ͻ� opengl��������½�
	float2 texC[4] =
	{
		float2(0.0f, 1.0f),
		float2(0.0f, 0.0f),
		float2(1.0f, 1.0f),
		float2(1.0f, 0.0f)
	};

	//���������ݸ�������ɫ��
	GeoOut gout;
	[unroll]
	for (int i = 0; i < 4; ++i)
	{
		gout.PosH = mul(v[i], gViewProj);//�������4���������ռ�ת�����ü��ռ�
		gout.PosW = v[i].xyz;
		gout.NormalW = up;
		gout.TexC = texC[i];
		gout.PrimID = primID;//ͼԪID���

		triStream.Append(gout);//��������ݺϲ��������
	}
}

float4 PS(GeoOut pin) : SV_Target
{
	// �����Զ�����ͼԪ�����ӣ�ģ3���ֵ��0��1��2��ѭ��
	float3 uvw = float3(pin.TexC, pin.PrimID % 3);
	// Sample�����������ֱ��ǲ��������ͺ�UV����
	float4 diffuseAlbedo = gTreeMapArray.Sample(gsamAnisotropicWrap, uvw) * gDiffuseAlbedo;

// ��ɫ����
#ifdef ALPHA_TEST
	clip(diffuseAlbedo.a - 0.1f);
#endif

	pin.NormalW= normalize(pin.NormalW);

	float3 toEyeW = gEyePosW - pin.PosW;
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


