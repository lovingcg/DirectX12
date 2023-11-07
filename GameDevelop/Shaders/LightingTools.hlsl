#define MaxLights 16

struct Light
{
    float3 Strength;    // ��Դ��ɫ������ͨ�ã�
    float FalloffStart; // ���ƺ;۹�ƵĿ�ʼ˥������
    float3 Direction;   // �����;۹�Ƶķ�������
    float FalloffEnd;   // ���;۹�Ƶ�˥����������
    float3 Position;    // ���;۹�Ƶ�����
    float SpotPower;    // �۹�������е��ݲ���
};

struct Material
{
    float4 DiffuseAlbedo;   //���ʷ�����
    float3 FresnelR0;       //RF(0)ֵ�������ʵķ�������
    float Shininess;        //���ʵĴֲڶ�
};

float CalcAttenuation(float d, float falloffStart, float falloffEnd)
{
    // ����˥��
    // d����ƹ�ľ���
    return saturate((falloffEnd - d) / (falloffEnd - falloffStart));
}
// ��ʯ������Ʒ���
float3 SchlickFresnel(float3 R0, float3 normal, float3 lightVec)
{
    float cosIncidentAngle = saturate(dot(normal, lightVec));

    float f0 = 1.0f - cosIncidentAngle;
    float3 reflectPercent = R0 + (1.0f - R0) * (f0 * f0 * f0 * f0 * f0);

    return reflectPercent;
}

float3 BlinnPhong(float3 lightStrength, float3 lightVec, float3 normal, float3 toEye, Material mat)
{
    const float m = mat.Shininess * 256.0f; //�ֲڶ��������mֵ
    float3 halfVec = normalize(toEye + lightVec);//�������

    float roughnessFactor = (m + 8.0f) * pow(max(dot(halfVec, normal), 0.0f), m) / 8.0f;//�ֲڶ�����
    float3 fresnelFactor = SchlickFresnel(mat.FresnelR0, halfVec, lightVec);

    float3 specAlbedo = fresnelFactor * roughnessFactor;//���淴�䷴����=���������*�ֲڶ�����

    specAlbedo = specAlbedo / (specAlbedo + 1.0f);//�����淴�䷴�������ŵ�[0��1]

    return (mat.DiffuseAlbedo.rgb + specAlbedo) * lightStrength;//����������+�߹ⷴ��
}
// ƽ�й�
float3 ComputeDirectionalLight(Light L, Material mat, float3 normal, float3 toEye)
{
    float3 lightVec = -L.Direction;//�������͹�Դָ�򶥵�������෴

    float ndotl = max(dot(lightVec, normal), 0.0f);
    float3 lightStrength = L.Strength * ndotl; //����ⵥλ����ϵķ��ն�

     //ƽ�й��������+�߹ⷴ��
    return BlinnPhong(lightStrength, lightVec, normal, toEye, mat);
}
// ���Դ
float3 ComputePointLight(Light L, Material mat, float3 pos, float3 normal, float3 toEye)
{
    float3 lightVec = L.Position - pos;//����ָ����Դ�Ĺ�����

    float d = length(lightVec);

    if (d > L.FalloffEnd)
        return 0.0f;

    lightVec /= d;

    float ndotl = max(dot(lightVec, normal), 0.0f);
    float3 lightStrength = L.Strength * ndotl;

    // ����˥��
    float att = CalcAttenuation(d, L.FalloffStart, L.FalloffEnd);
    lightStrength *= att;

    return BlinnPhong(lightStrength, lightVec, normal, toEye, mat);
}
// �۹��
float3 ComputeSpotLight(Light L, Material mat, float3 pos, float3 normal, float3 toEye)
{
    float3 lightVec = L.Position - pos;

    float d = length(lightVec);
    if (d > L.FalloffEnd)
        return 0.0f;
    
    lightVec /= d;
    float ndotl = max(dot(lightVec, normal), 0.0f);
    float3 lightStrength = L.Strength * ndotl;
    // ����˥��
    float att = CalcAttenuation(d, L.FalloffStart, L.FalloffEnd);
    lightStrength *= att;

    //����۹��˥������
    float spotFactor = pow(max(dot(-lightVec, L.Direction), 0.0f), L.SpotPower);
    lightStrength *= spotFactor;

    return BlinnPhong(lightStrength, lightVec, normal, toEye, mat);
}

// ������ֹ�Դ�����صĹ���֮��
float4 ComputeLighting(Light gLights[MaxLights], Material mat, float3 pos, float3 normal, float3 toEye, float3 shadowFactor)
{
    float3 result = 0.0f;

    int i = 0;

#if (NUM_DIR_LIGHTS > 0)
    for (i = 0; i < NUM_DIR_LIGHTS; ++i)
    {
        // ����shadowFactor�ں����½ڲŻ��õ����������ҽ������ó�(1, 1, 1)���������Ͳ���Լ���������Ӱ�졣
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