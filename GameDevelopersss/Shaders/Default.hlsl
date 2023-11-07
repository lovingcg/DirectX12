#ifndef NUM_DIR_LIGHTS
    #define NUM_DIR_LIGHTS 3
#endif

#ifndef NUM_POINT_LIGHTS
    #define NUM_POINT_LIGHTS 0
#endif

#ifndef NUM_SPOT_LIGHTS
    #define NUM_SPOT_LIGHTS 0
#endif

#include "Common.hlsl"

struct VertexIn
{
	float3 PosL    : POSITION;
    float3 NormalL : NORMAL;
	float2 TexC    : TEXCOORD;
    float3 TangentU : TANGENT;
#ifdef SKINNED
    float3 BoneWeights : WEIGHTS;
    uint4 BoneIndices  : BONEINDICES;
#endif
};

struct VertexOut
{
	float4 PosH    : SV_POSITION;
    // 用了多组POSITION语义
    float4 ShadowPosH : POSITION0;
    float3 PosW    : POSITION1;

    float3 NormalW : NORMAL;
    float3 TangentW : TANGENT;
	float2 TexC    : TEXCOORD;
};

VertexOut VS(VertexIn vin)
{
	VertexOut vout = (VertexOut)0.0f;

	// Fetch the material data.
	MaterialData matData = gMaterialData[gMaterialIndex];
	
#ifdef SKINNED
    float weights[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    weights[0] = vin.BoneWeights.x;
    weights[1] = vin.BoneWeights.y;
    weights[2] = vin.BoneWeights.z;
    weights[3] = 1.0f - weights[0] - weights[1] - weights[2];

    float3 posL = float3(0.0f, 0.0f, 0.0f);
    float3 normalL = float3(0.0f, 0.0f, 0.0f);
    float3 tangentL = float3(0.0f, 0.0f, 0.0f);
    for (int i = 0; i < 4; ++i)
    {
        // Assume no nonuniform scaling when transforming normals, so 
        // that we do not have to use the inverse-transpose.

        posL += weights[i] * mul(float4(vin.PosL, 1.0f), gBoneTransforms[vin.BoneIndices[i]]).xyz;
        normalL += weights[i] * mul(vin.NormalL, (float3x3)gBoneTransforms[vin.BoneIndices[i]]);
        tangentL += weights[i] * mul(vin.TangentU.xyz, (float3x3)gBoneTransforms[vin.BoneIndices[i]]);

        posL.z -= 5.0f;

    }

    vin.PosL = posL;
    vin.NormalL = normalL;
    vin.TangentU.xyz = tangentL;
#endif

    // Transform to world space.
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.PosW = posW.xyz;

    // Assumes nonuniform scaling; otherwise, need to use inverse-transpose of world matrix.
    vout.NormalW = mul(vin.NormalL, (float3x3)gWorld);

    //将顶点切线从物体空间转至世界空间
    vout.TangentW = mul(vin.TangentU, (float3x3)gWorld);

    // Transform to homogeneous clip space.
    vout.PosH = mul(posW, gViewProj);
	
	// Output vertex attributes for interpolation across triangle.
	float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), gTexTransform);
	vout.TexC = mul(texC, matData.MatTransform).xy;

    // 生成投影坐标以将阴影贴图投影到场景上
    vout.ShadowPosH = mul(posW, gShadowTransform);
	
    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
    //获取材质数据(需要点出来，和CB使用不太一样)
	MaterialData matData = gMaterialData[gMaterialIndex];
	float4 diffuseAlbedo = matData.DiffuseAlbedo;
	float3 fresnelR0 = matData.FresnelR0;
	float  roughness = matData.Roughness;
	uint diffuseTexIndex = matData.DiffuseMapIndex;
    float eta = matData.geta;
    uint normalMapIndex = matData.NormalMapIndex;

    //在数组中动态地查找纹理
	diffuseAlbedo *= gTextureMaps[diffuseTexIndex].Sample(gsamAnisotropicWrap, pin.TexC);
	
    // Interpolating normal can unnormalize it, so renormalize it.
    pin.NormalW = normalize(pin.NormalW);
    float4 normalMapSample = gTextureMaps[normalMapIndex].Sample(gsamAnisotropicWrap, pin.TexC);
    //得到世界空间的法线
    float3 bumpedNormalW = NormalSampleToWorldSpace(normalMapSample.rgb, pin.NormalW, pin.TangentW);

    // Vector from point being lit to eye. 
    float3 toEyeW = normalize(gEyePosW - pin.PosW);

    // Light terms.
    float4 ambient = gAmbientLight*diffuseAlbedo;
    
    //主光产生阴影
    float3 shadowFactor = float3(1.0f, 1.0f, 1.0f);
    shadowFactor[0] = CalcShadowFactor(pin.ShadowPosH);

    const float shininess = (1.0f - roughness);
    Material mat = { diffuseAlbedo, fresnelR0, shininess };
    //float3 shadowFactor = 1.0f;
    float4 directLight = ComputeLighting(gLights, mat, pin.PosW,
        bumpedNormalW, toEyeW, shadowFactor);

    float4 litColor = ambient + directLight;

	// Add in specular reflections.
    // 当eta越接近1，折射越弱，这是符合自然规律的，比值趋于1，说明两介质折射率越接近
    if (grerf == 1)
    {
        float3 r = refract(-toEyeW, bumpedNormalW, eta);
       
        float4 refractionColor = gCubeMap.Sample(gsamLinearWrap, r);
        float3 fresnelFactor = SchlickFresnel(fresnelR0, bumpedNormalW, r);
        litColor.rgb += shininess * fresnelFactor * refractionColor.rgb;

        // Common convention to take alpha from diffuse albedo.
        litColor.a = diffuseAlbedo.a;
    }
        
    if (grerf == 0)
    {
        float3 r0 = reflect(-toEyeW, bumpedNormalW);
        int blurLayer = 1;
        if (shininess > 0.95f) blurLayer = 1;
        else if (shininess > 0.8f) blurLayer = 4;
        else if (shininess > 0.6f) blurLayer = 8;
        else if (shininess > 0.4f) blurLayer = 12;
        else if (shininess > 0.2f) blurLayer = 16;
        else blurLayer = 20;
        //blurLayer = 18;
        // 计算模糊
        const float smoothness = 0.15f;
        float3 r[20];
        r[0] = r0;
       
        for (int i = 1; i < blurLayer; ++i)
        {
            if (i % 6==0)
            {
                r[i] = r0 + float3(smoothness, smoothness, 0);
            }
            else if(i % 6 == 1)
            {
                r[i] = r0 - float3(smoothness, smoothness, 0);
            }
            else if (i % 6 == 2)
            {
                r[i] = r0 + float3(smoothness, 0, smoothness);
            }
            else if (i % 6 == 3)
            {
                r[i] = r0 - float3(smoothness, 0, smoothness);
            }
            else if (i % 6 == 4)
            {
                r[i] = r0 + float3(0, smoothness, smoothness);
            }
            else
            {
                r[i] = r0 - float3(0, smoothness, smoothness);
            }
        } 
        float4 reflCol[20];
        float4 reflectionColor;

        for (int i = 0; i < blurLayer; ++i)
        {
            reflCol[i] = gCubeMap.Sample(gsamLinearWrap, r[i]);

            reflectionColor += reflCol[i];
        }
        reflectionColor = reflectionColor / (blurLayer);
        float3 fresnelFactor = SchlickFresnel(fresnelR0, bumpedNormalW, r0);
        //颜色叠加环境贴图镜面反射
        litColor.rgb += shininess * fresnelFactor * reflectionColor.rgb;

        litColor.a = diffuseAlbedo.a;
    }

    return litColor;
}