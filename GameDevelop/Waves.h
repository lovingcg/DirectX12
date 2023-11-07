#pragma once

#ifndef WAVES_H
#define WAVES_H

#include <vector>
#include <DirectXMath.h>

class Waves
{
public:
	Waves(int m, int n, float dx, float dt, float speed, float damping);
	Waves(const Waves& rhs) = delete;
	Waves& operator=(const Waves& rhs) = delete;
	~Waves();

	int RowCount()const;
	int ColumnCount()const;
	int VertexCount()const;
	int TriangleCount()const;
	float Width()const;
	float Depth()const;

	// 返回计算后的网格顶点坐标
	const DirectX::XMFLOAT3& Position(int i) const
	{
		return mCurrSolution[i];
	}
	// 返回计算后的网格顶点法线
	const DirectX::XMFLOAT3& Normal(int i)	const
	{
		return mNormals[i];
	}
	// 返回计算后的网格顶点切线
	const DirectX::XMFLOAT3& TangentX(int i)	const
	{
		return mTangentX[i];
	}
	// Update函数则是每帧更新波动方程计算出来的顶点坐标
	void Update(float dt);
	// Disturb函数就是波动方程函数
	void Disturb(int i, int j, float magnitude);

private:
	int mNumRows = 0;
	int mNumCols = 0;

	int mVertexCount = 0;
	int mTriangleCount = 0;

	// 预先计算模拟常数
	float mK1 = 0.0f;
	float mK2 = 0.0f;
	float mK3 = 0.0f;

	float mTimeStep = 0.0f;
	float mSpatialStep = 0.0f;

	std::vector<DirectX::XMFLOAT3> mPrevSolution;
	std::vector<DirectX::XMFLOAT3> mCurrSolution;
	std::vector<DirectX::XMFLOAT3> mNormals;
	std::vector<DirectX::XMFLOAT3> mTangentX;
};

#endif