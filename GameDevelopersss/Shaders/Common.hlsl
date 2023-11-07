#ifndef NUM_DIR_LIGHTS
    #define NUM_DIR_LIGHTS 3
#endif

#ifndef NUM_POINT_LIGHTS
    #define NUM_POINT_LIGHTS 0
#endif

#ifndef NUM_SPOT_LIGHTS
    #define NUM_SPOT_LIGHTS 0
#endif

// Include structures and functions for lighting.
#include "LightingUtil.hlsl"

struct MaterialData
{
	float4   DiffuseAlbedo;
	float3   FresnelR0;
	float    Roughness;
	float4x4 MatTransform;
	uint     DiffuseMapIndex;
    float    geta;
	uint     NormalMapIndex;
	uint     MatPad2;
};

TextureCube gCubeMap : register(t0);
Texture2D gShadowMap : register(t1);

Texture2D gTextureMaps[58] : register(t2);
 
StructuredBuffer<MaterialData> gMaterialData : register(t0, space1);


SamplerState gsamPointWrap        : register(s0);
SamplerState gsamPointClamp       : register(s1);
SamplerState gsamLinearWrap       : register(s2);
SamplerState gsamLinearClamp      : register(s3);
SamplerState gsamAnisotropicWrap  : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);
SamplerComparisonState gsamShadow : register(s6);

// Constant data that varies per frame.
cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
	float4x4 gTexTransform;
	uint gMaterialIndex;
    uint gObjPad0;
	uint gObjPad1;
	uint gObjPad2;
};

cbuffer cbSkinned : register(b1)
{
    float4x4 gBoneTransforms[96];
};

// Constant data that varies per material.
cbuffer cbPass : register(b2)
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float4x4 gShadowTransform;
    float3 gEyePosW;
    int grerf;
    float2 gRenderTargetSize;
    float2 gInvRenderTargetSize;
    float gNearZ;
    float gFarZ;
    float gTotalTime;
    float gDeltaTime;
    float4 gAmbientLight;

    Light gLights[MaxLights];
};

float3 NormalSampleToWorldSpace(float3 normalMapSample, float3 unitNormalW, float3 tangentW)
{
    //将每个像素的法线值从[0, 1]映射到[-1 ,1]
    float3 normalT = 2.0f * normalMapSample - 1.0f;

    // 构建TBN矩阵
    float3 N = unitNormalW;
    float3 T = normalize(tangentW - dot(tangentW, N) * N);
    float3 B = cross(N, T);

    float3x3 TBN = float3x3(T, B, N);

    //将法线从切线空间转至世界空间
    float3 bumpedNormalW = mul(normalT, TBN);

    return bumpedNormalW;
}

// 为阴影图构建PCF,返回阴影因子
float CalcShadowFactor(float4 shadowPosH)
{
    // 将顶点变换到NDC空间（如果是正交投影，则W=1）
    shadowPosH.xyz /= shadowPosH.w;

    // NDC空间中的深度值
    float depth = shadowPosH.z;

    // 读取ShadowMap的宽高及mip级数
    uint width, height, numMips;
    gShadowMap.GetDimensions(0, width, height, numMips);

    // 纹素尺寸
    float dx = 1.0f / (float)width;

    float percentLit = 0.0f;
    const float2 offsets[9] =
    {
        float2(-dx,  -dx), float2(0.0f,  -dx), float2(dx,  -dx),
        float2(-dx, 0.0f), float2(0.0f, 0.0f), float2(dx, 0.0f),
        float2(-dx,  +dx), float2(0.0f,  +dx), float2(dx,  +dx)
    };

    [unroll]
    for (int i = 0; i < 9; ++i)
    {
        percentLit += gShadowMap.SampleCmpLevelZero(gsamShadow,
            shadowPosH.xy + offsets[i], depth).r;
    }

    return percentLit / 9.0f;
}
