cbuffer cbSettings : register(b0)
{
	int gBlurRadius;

	//11��Ȩ�أ������Ǹ������������ģ����Բ��������飩 weights
	float w0;
	float w1;
	float w2;
	float w3;
	float w4;
	float w5;
	float w6;
	float w7;
	float w8;
	float w9;
	float w10;
}

static const int gMaxBlurRadius = 5;

Texture2D gInput            : register(t0);//�����SRV����
RWTexture2D<float4> gOutput : register(u0);//�����UAV����ע��ģ���е���������

#define N 256  //һ���߳�����256���߳�
// ÿ���߳��鶼��һ�鹲���ڴ�
// ��ҪƵ����ȡ�����ϵ����������������������ǽ�����ֱ�Ӽ��ص������ڴ��ϣ�������ȡ����Ͳ����Ϊ���ǳ���Ч�ʵ�ƿ��
// ����ģ���뾶��ԭ��������Ҫ�洢������Ҫ��ʵ�������2���뾶
#define CacheSize (N + 2 * gMaxBlurRadius)     //�����ڴ��С

groupshared float4 gCache[CacheSize];//���������ڴ�

[numthreads(N,1,1)]//�߳������壨����
void HorzBlurCS(int3 groupThreadID : SV_GroupThreadID, //�����߳�ID
	int3 dispatchThreadID : SV_DispatchThreadID)//����ID
{
	float weights[11] = { w0, w1, w2, w3, w4, w5, w6, w7, w8, w9, w10 };

	//��ͼ������Խ���������ǯλ����
	if (groupThreadID.x < gBlurRadius)//����������5������
	{
		//ǯλ��ߵ�ģ���뾶����5��������,x����0
		int x = max(dispatchThreadID.x - gBlurRadius, 0);
		//��input��λ�õ����أ�ǯλ����ߵ����أ������빲���ڴ���
		gCache[groupThreadID.x] = gInput[int2(x, dispatchThreadID.y)];
	}

	//��ͼ���Ҳ��Խ���������ǯλ����
	if (groupThreadID.x >= N - gBlurRadius)//��������Ҳ�5������
	{
		//ǯλ�ұߵ�ģ���뾶����5��������,x����gInput.Length.x - 1,�����һ����������
		int x = min(dispatchThreadID.x + gBlurRadius, gInput.Length.x - 1);
		//��inputβλ�õ����أ�ǯλ���ұߵ����أ������빲���ڴ���
		gCache[groupThreadID.x + 2 * gBlurRadius] = gInput[int2(x, dispatchThreadID.y)];
	}

	//�м�����ش��빲���ڴ棬ע�⣬�Ҳ���߳̿�����ʣ�࣬����Ҫ��ǯλ
	gCache[groupThreadID.x + gBlurRadius] = gInput[min(dispatchThreadID.xy, gInput.Length.xy - 1)];

	//�ȴ������߳��������
	GroupMemoryBarrierWithGroupSync();

	//
	//��ÿ�����ؽ���ģ������
	//
	float4 blurColor = float4(0, 0, 0, 0);//��ʼ����ɫΪ��ɫ

	for (int i = -gBlurRadius; i <= gBlurRadius; ++i)//��-5��5ѭ������
	{
		int k = groupThreadID.x + gBlurRadius + i;//ƽ������

		blurColor += weights[i + gBlurRadius] * gCache[k];//�ԡ�Ȩ��*���ء���ͣ������
	}

	gOutput[dispatchThreadID.xy] = blurColor;
}

[numthreads(1, N, 1)]//�߳������壨����
void VertBlurCS(int3 groupThreadID : SV_GroupThreadID,//�����߳�ID
	int3 dispatchThreadID : SV_DispatchThreadID)//����ID
{
	float weights[11] = { w0, w1, w2, w3, w4, w5, w6, w7, w8, w9, w10 };

	//��ͼ���ϲ��Խ���������ǯλ����
	if (groupThreadID.y < gBlurRadius)
	{
		int y = max(dispatchThreadID.y - gBlurRadius, 0);
		gCache[groupThreadID.y] = gInput[int2(dispatchThreadID.x, y)];
	}
	if (groupThreadID.y >= N - gBlurRadius)
	{
		int y = min(dispatchThreadID.y + gBlurRadius, gInput.Length.y - 1);
		gCache[groupThreadID.y + 2 * gBlurRadius] = gInput[int2(dispatchThreadID.x, y)];
	}

	gCache[groupThreadID.y + gBlurRadius] = gInput[min(dispatchThreadID.xy, gInput.Length.xy - 1)];

	GroupMemoryBarrierWithGroupSync();

	float4 blurColor = float4(0, 0, 0, 0);

	for (int i = -gBlurRadius; i <= gBlurRadius; ++i)
	{
		int k = groupThreadID.y + gBlurRadius + i;

		blurColor += weights[i + gBlurRadius] * gCache[k];
	}

	gOutput[dispatchThreadID.xy] = blurColor;
}