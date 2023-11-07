#define MaxLights 16

struct Light
{
    float3 Strength;    // 光源颜色（三光通用）
    float FalloffStart; // 点光灯和聚光灯的开始衰减距离
    float3 Direction;   // 方向光和聚光灯的方向向量
    float FalloffEnd;   // 点光和聚光灯的衰减结束距离
    float3 Position;    // 点光和聚光灯的坐标
    float SpotPower;    // 聚光灯因子中的幂参数
};

struct Material
{
    float4 DiffuseAlbedo;   //材质反照率
    float3 FresnelR0;       //RF(0)值，即材质的反射属性
    float Shininess;        //材质的粗糙度
};

float CalcAttenuation(float d, float falloffStart, float falloffEnd)
{
    // 线性衰减
    // d是离灯光的距离
    return saturate((falloffEnd - d) / (falloffEnd - falloffStart));
}
// 用石克里近似方程
float3 SchlickFresnel(float3 R0, float3 normal, float3 lightVec)
{
    float cosIncidentAngle = saturate(dot(normal, lightVec));

    float f0 = 1.0f - cosIncidentAngle;
    float3 reflectPercent = R0 + (1.0f - R0) * (f0 * f0 * f0 * f0 * f0);

    return reflectPercent;
}

float3 BlinnPhong(float3 lightStrength, float3 lightVec, float3 normal, float3 toEye, Material mat)
{
    const float m = mat.Shininess * 256.0f; //粗糙度因子里的m值
    float3 halfVec = normalize(toEye + lightVec);//半角向量

    float roughnessFactor = (m + 8.0f) * pow(max(dot(halfVec, normal), 0.0f), m) / 8.0f;//粗糙度因子
    float3 fresnelFactor = SchlickFresnel(mat.FresnelR0, halfVec, lightVec);

    float3 specAlbedo = fresnelFactor * roughnessFactor;//镜面反射反照率=菲尼尔因子*粗糙度因子

    specAlbedo = specAlbedo / (specAlbedo + 1.0f);//将镜面反射反照率缩放到[0，1]

    return (mat.DiffuseAlbedo.rgb + specAlbedo) * lightStrength;//返回漫反射+高光反射
}
// 平行光
float3 ComputeDirectionalLight(Light L, Material mat, float3 normal, float3 toEye)
{
    float3 lightVec = -L.Direction;//光向量和光源指向顶点的向量相反

    float ndotl = max(dot(lightVec, normal), 0.0f);
    float3 lightStrength = L.Strength * ndotl; //方向光单位面积上的辐照度

     //平行光的漫反射+高光反射
    return BlinnPhong(lightStrength, lightVec, normal, toEye, mat);
}
// 点光源
float3 ComputePointLight(Light L, Material mat, float3 pos, float3 normal, float3 toEye)
{
    float3 lightVec = L.Position - pos;//顶点指向点光源的光向量

    float d = length(lightVec);

    if (d > L.FalloffEnd)
        return 0.0f;

    lightVec /= d;

    float ndotl = max(dot(lightVec, normal), 0.0f);
    float3 lightStrength = L.Strength * ndotl;

    // 考虑衰减
    float att = CalcAttenuation(d, L.FalloffStart, L.FalloffEnd);
    lightStrength *= att;

    return BlinnPhong(lightStrength, lightVec, normal, toEye, mat);
}
// 聚光灯
float3 ComputeSpotLight(Light L, Material mat, float3 pos, float3 normal, float3 toEye)
{
    float3 lightVec = L.Position - pos;

    float d = length(lightVec);
    if (d > L.FalloffEnd)
        return 0.0f;
    
    lightVec /= d;
    float ndotl = max(dot(lightVec, normal), 0.0f);
    float3 lightStrength = L.Strength * ndotl;
    // 考虑衰减
    float att = CalcAttenuation(d, L.FalloffStart, L.FalloffEnd);
    lightStrength *= att;

    //计算聚光灯衰减因子
    float spotFactor = pow(max(dot(-lightVec, L.Direction), 0.0f), L.SpotPower);
    lightStrength *= spotFactor;

    return BlinnPhong(lightStrength, lightVec, normal, toEye, mat);
}

// 计算多种光源对像素的贡献之和
float4 ComputeLighting(Light gLights[MaxLights], Material mat, float3 pos, float3 normal, float3 toEye, float3 shadowFactor)
{
    float3 result = 0.0f;

    int i = 0;

#if (NUM_DIR_LIGHTS > 0)
    for (i = 0; i < NUM_DIR_LIGHTS; ++i)
    {
        // 参数shadowFactor在后面章节才会用到，这里暂且将它设置成(1, 1, 1)，这样它就不会对计算结果产生影响。
        result += shadowFactor[i] * ComputeDirectionalLight(gLights[i], mat, normal, toEye);
    }
#endif

#if (NUM_POINT_LIGHTS > 0)
    for (i = NUM_DIR_LIGHTS; i < NUM_DIR_LIGHTS + NUM_POINT_LIGHTS; ++i)
    {
        result += ComputePointLight(gLights[i], mat, pos, normal, toEye);
    }
#endif

#if (NUM_SPOT_LIGHTS > 0)
    for (i = NUM_DIR_LIGHTS + NUM_POINT_LIGHTS; i < NUM_DIR_LIGHTS + NUM_POINT_LIGHTS + NUM_SPOT_LIGHTS; ++i)
    {
        result += ComputeSpotLight(gLights[i], mat, pos, normal, toEye);
    }
#endif 

    return float4(result, 0.0f);
}