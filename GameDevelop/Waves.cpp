#include "Waves.h"
#include <ppl.h>
#include <cassert>
#include <algorithm>
#include <vector>

using namespace DirectX;

Waves::Waves(int m, int n, float dx, float dt, float speed, float damping)
{
	mNumRows = m;
	mNumCols = n;

	mVertexCount = m * n;
	mTriangleCount = (m - 1) * (n - 1) * 2;

	mTimeStep = dt;
	mSpatialStep = dx;

	float d = damping * dt + 2.0f;
	float e = (speed * speed) * (dt * dt) / (dx * dx);

	mK1 = (damping * dt - 2.0f) / d;
	mK2 = (4.0f - 8.0f * e) / d;
	mK3 = (2.0f * e) / d;

	mPrevSolution.resize(m * n);
	mCurrSolution.resize(m * n);
	mNormals.resize(m * n);
	mTangentX.resize(m * n);

	// 在系统内存中生成网格顶点

	float halfWidth = (n - 1) * dx * 0.5f;
	float halfDepth = (m - 1) * dx * 0.5f;
	for (int i = 0; i < m; ++i)
	{
		float z = halfDepth - i * dx;
		for (int j = 0; j < n; ++j)
		{
			float x = -halfWidth + j * dx;

			mPrevSolution[i * n + j] = XMFLOAT3(x, 0.0f, z);
			mCurrSolution[i * n + j] = XMFLOAT3(x, 0.0f, z);
			mNormals[i * n + j] = XMFLOAT3(0.0f, 1.0f, 0.0f);
			mTangentX[i * n + j] = XMFLOAT3(1.0f, 0.0f, 0.0f);
		}
	}
}

Waves::~Waves()
{
}

int Waves::RowCount()const
{
	return mNumRows;
}

int Waves::ColumnCount()const
{
	return mNumCols;
}

int Waves::VertexCount()const
{
	return mVertexCount;
}

int Waves::TriangleCount()const
{
	return mTriangleCount;
}

float Waves::Width()const
{
	return mNumCols * mSpatialStep;
}

float Waves::Depth()const
{
	return mNumRows * mSpatialStep;
}

void Waves::Update(float dt)
{
	static float t = 0;

	t += dt;
	// 仅在指定的时间步长更新模拟
	if (t >= mTimeStep)
	{
		// 仅更新内部点；使用零边界条件
		concurrency::parallel_for(1, mNumRows - 1, [this](int i)
		{
				for (int j = 1; j < mNumCols - 1; ++j)
				{
					mPrevSolution[i * mNumCols + j].y =
						mK1 * mPrevSolution[i * mNumCols + j].y +
						mK2 * mCurrSolution[i * mNumCols + j].y +
						mK3 * (mCurrSolution[(i + 1) * mNumCols + j].y +
							mCurrSolution[(i - 1) * mNumCols + j].y +
							mCurrSolution[i * mNumCols + j + 1].y +
							mCurrSolution[i * mNumCols + j - 1].y);
				}
		});

		std::swap(mPrevSolution, mCurrSolution);

		t = 0.0f;//重置时间

		// 使用有限差分方案计算法线
		concurrency::parallel_for(1, mNumRows - 1, [this](int i)
		{
				for (int j = 1; j < mNumCols - 1; ++j)
				{
					float l = mCurrSolution[i * mNumCols + j - 1].y;
					float r = mCurrSolution[i * mNumCols + j + 1].y;
					float t = mCurrSolution[(i - 1) * mNumCols + j].y;
					float b = mCurrSolution[(i + 1) * mNumCols + j].y;
					mNormals[i * mNumCols + j].x = -r + l;
					mNormals[i * mNumCols + j].y = 2.0f * mSpatialStep;
					mNormals[i * mNumCols + j].z = b - t;

					XMVECTOR n = XMVector3Normalize(XMLoadFloat3(&mNormals[i * mNumCols + j]));
					XMStoreFloat3(&mNormals[i * mNumCols + j], n);

					mTangentX[i * mNumCols + j] = XMFLOAT3(2.0f * mSpatialStep, r - l, 0.0f);
					XMVECTOR T = XMVector3Normalize(XMLoadFloat3(&mTangentX[i * mNumCols + j]));
					XMStoreFloat3(&mTangentX[i * mNumCols + j], T);
				}
		});
	}
}

void Waves::Disturb(int i, int j, float magnitude)
{
	// 不扰乱边界.
	assert(i > 1 && i < mNumRows - 2);
	assert(j > 1 && j < mNumCols - 2);

	float halfMag = 0.5f * magnitude;

	// 扰乱第i个顶点的高度及其邻居
	mCurrSolution[i * mNumCols + j].y += magnitude;
	mCurrSolution[i * mNumCols + j + 1].y += halfMag;
	mCurrSolution[i * mNumCols + j - 1].y += halfMag;
	mCurrSolution[(i + 1) * mNumCols + j].y += halfMag;
	mCurrSolution[(i - 1) * mNumCols + j].y += halfMag;
}