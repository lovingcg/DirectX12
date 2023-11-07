#include "Camera.h"

using namespace DirectX;

Camera::Camera()
{
	SetLens(0.25f * MathHelper::Pi, 1.0f, 1.0f, 1000.0f);
}

Camera::~Camera()
{
}

XMVECTOR Camera::GetPosition()const
{
	return XMLoadFloat3(&mPosition);
}

XMFLOAT3 Camera::GetPosition3f()const
{
	return mPosition;
}

void Camera::SetPosition(float x, float y, float z)
{
	mPosition = XMFLOAT3(x, y, z);
	mViewDirty = true;
}

void Camera::SetPosition(const XMFLOAT3& v)
{
	mPosition = v;
	mViewDirty = true;
}

XMVECTOR Camera::GetRight()const
{
	return XMLoadFloat3(&mRight);
}

XMFLOAT3 Camera::GetRight3f()const
{
	return mRight;
}

XMVECTOR Camera::GetUp()const
{
	return XMLoadFloat3(&mUp);
}

XMFLOAT3 Camera::GetUp3f()const
{
	return mUp;
}

XMVECTOR Camera::GetLook()const
{
	return XMLoadFloat3(&mLook);
}

XMFLOAT3 Camera::GetLook3f()const
{
	return mLook;
}

float Camera::GetNearZ()const
{
	return mNearZ;
}

float Camera::GetFarZ()const
{
	return mFarZ;
}

float Camera::GetAspect()const
{
	return mAspect;
}

float Camera::GetFovY()const
{
	return mFovY;
}

float Camera::GetFovX()const
{
	float halfWidth = 0.5f * GetNearWindowWidth();
	return 2.0f * atan(halfWidth / mNearZ);
}

float Camera::GetNearWindowWidth()const
{
	return mAspect * mNearWindowHeight;
}

float Camera::GetNearWindowHeight()const
{
	return mNearWindowHeight;
}

float Camera::GetFarWindowWidth()const
{
	return mAspect * mFarWindowHeight;
}

float Camera::GetFarWindowHeight()const
{
	return mFarWindowHeight;
}

void Camera::SetLens(float fovY, float aspect, float zn, float zf)
{
	mFovY = fovY;
	mAspect = aspect;
	mNearZ = zn;
	mFarZ = zf;

	mNearWindowHeight = 2.0f * mNearZ * tanf(0.5f * mFovY);
	mFarWindowHeight = 2.0f * mFarZ * tanf(0.5f * mFovY);
	// 使用XMMatrixPerspectiveFovLH函数构建投影矩阵，这个内置函数就是DX12用来构建投影矩阵的
	XMMATRIX P = XMMatrixPerspectiveFovLH(mFovY, mAspect, mNearZ, mFarZ);
	XMStoreFloat4x4(&mProj, P);
}

void Camera::LookAt(FXMVECTOR pos, FXMVECTOR target, FXMVECTOR worldUp)
{
	XMVECTOR L = XMVector3Normalize(XMVectorSubtract(target, pos));
	XMVECTOR R = XMVector3Normalize(XMVector3Cross(worldUp, L));
	XMVECTOR U = XMVector3Cross(L, R);

	XMStoreFloat3(&mPosition, pos);
	XMStoreFloat3(&mLook, L);
	XMStoreFloat3(&mRight, R);
	XMStoreFloat3(&mUp, U);

	mViewDirty = true;
}

void Camera::LookAt(const XMFLOAT3& pos, const XMFLOAT3& target, const XMFLOAT3& up)
{
	XMVECTOR P = XMLoadFloat3(&pos);
	XMVECTOR T = XMLoadFloat3(&target);
	XMVECTOR U = XMLoadFloat3(&up);

	LookAt(P, T, U);

	mViewDirty = true;
}

XMMATRIX Camera::GetView()const
{
	assert(!mViewDirty);
	return XMLoadFloat4x4(&mView);
}

XMMATRIX Camera::GetProj()const
{
	return XMLoadFloat4x4(&mProj);
}


XMFLOAT4X4 Camera::GetView4x4f()const
{
	assert(!mViewDirty);
	return mView;
}

XMFLOAT4X4 Camera::GetProj4x4f()const
{
	return mProj;
}

//左右平移摄像机（在right向量方向做平移）
void Camera::Strafe(float d)
{
	//计算公式：position += right * d
	XMVECTOR s = XMVectorReplicate(d);//将float的填充进XMVECTOR的每个分量
	XMVECTOR r = XMLoadFloat3(&mRight);
	XMVECTOR p = XMLoadFloat3(&mPosition);
	XMStoreFloat3(&mPosition, XMVectorMultiplyAdd(s, r, p));

	mViewDirty = true;
}

//前后推拉摄像机（在look向量方向做平移）
void Camera::Walk(float d)
{
	//计算公式：position += look * d
	XMVECTOR s = XMVectorReplicate(d);
	XMVECTOR l = XMLoadFloat3(&mLook);
	XMVECTOR p = XMLoadFloat3(&mPosition);
	XMStoreFloat3(&mPosition, XMVectorMultiplyAdd(s, l, p));

	mViewDirty = true;
}

void Camera::UpDown(float d)
{
	XMVECTOR s = XMVectorReplicate(d);
	XMVECTOR l = XMLoadFloat3(&mUp);
	XMVECTOR p = XMLoadFloat3(&mPosition);
	XMStoreFloat3(&mPosition, XMVectorMultiplyAdd(s, l, p));

	mViewDirty = true;
}

//旋转摄像机Pitch方向(俯仰角)
void Camera::Pitch(float angle)
{
	XMMATRIX R = XMMatrixRotationAxis(XMLoadFloat3(&mRight), angle);//旋转轴为right轴，得到旋转矩阵

	XMStoreFloat3(&mUp, XMVector3TransformNormal(XMLoadFloat3(&mUp), R));//矩阵变换后的up向量
	XMStoreFloat3(&mLook, XMVector3TransformNormal(XMLoadFloat3(&mLook), R));//矩阵变换后的look向量

	mViewDirty = true;
}

//旋转摄像机Yaw方向（航向角）
void Camera::RotateY(float angle)
{
	XMMATRIX R = XMMatrixRotationY(angle);//旋转轴为Y轴，得到旋转矩阵

	XMStoreFloat3(&mRight, XMVector3TransformNormal(XMLoadFloat3(&mRight), R));//矩阵变换后的right向量
	//XMStoreFloat3(&mUp, XMVector3TransformNormal(XMLoadFloat3(&mUp), R));
	XMStoreFloat3(&mLook, XMVector3TransformNormal(XMLoadFloat3(&mLook), R));//矩阵变换后的look向量

	mViewDirty = true;
}

//旋转摄像机Roll方向（翻滚角）
void Camera::Roll(float angle)
{
	XMVECTOR right = XMLoadFloat3(&mRight);
	XMVECTOR up = XMLoadFloat3(&mUp);
	XMVECTOR look = XMLoadFloat3(&mLook);

	XMMATRIX R = XMMatrixRotationAxis(look, angle);//旋转轴为look，得到旋转矩阵

	right = XMVector3TransformNormal(right, R);//变换后的up向量
	up = XMVector3TransformNormal(up, R);//变换后的up向量

	XMStoreFloat3(&mRight, right);
	XMStoreFloat3(&mUp, up);

	mViewDirty = true;
}

//修正函数，经过一系列的旋转操作以及累积的数值误差会使摄像机的3个基向量变成非正交归一化向量，所以必须修正他们
//只需将3个基向量重新正交化，并代入观察矩阵行列式，最终得到修正后的观察矩阵
void Camera::UpdateViewMatrix()
{
	if (mViewDirty)
	{
		XMVECTOR R = XMLoadFloat3(&mRight);
		XMVECTOR U = XMLoadFloat3(&mUp);
		XMVECTOR L = XMLoadFloat3(&mLook);
		XMVECTOR P = XMLoadFloat3(&mPosition);

		//以Look向量为基准，修正up和right向量
		L = XMVector3Normalize(L);
		U = XMVector3Normalize(XMVector3Cross(L, R));
		R = XMVector3Cross(U, L);

		// 填写观察矩阵中的数据元素
		float x = -XMVectorGetX(XMVector3Dot(P, R));
		float y = -XMVectorGetX(XMVector3Dot(P, U));
		float z = -XMVectorGetX(XMVector3Dot(P, L));

		XMStoreFloat3(&mRight, R);
		XMStoreFloat3(&mUp, U);
		XMStoreFloat3(&mLook, L);

		// DirectX使用行向量 OpenGL则使用列向量 
		// 若用^表示转置，则(AB)^=B^A^ =一个向量
		// 对于同一个变换，DirectX中的矩阵和OpenGL中的矩阵互为转置矩阵
		mView(0, 0) = mRight.x;
		mView(1, 0) = mRight.y;
		mView(2, 0) = mRight.z;
		mView(3, 0) = x;

		mView(0, 1) = mUp.x;
		mView(1, 1) = mUp.y;
		mView(2, 1) = mUp.z;
		mView(3, 1) = y;

		mView(0, 2) = mLook.x;
		mView(1, 2) = mLook.y;
		mView(2, 2) = mLook.z;
		mView(3, 2) = z;

		mView(0, 3) = 0.0f;
		mView(1, 3) = 0.0f;
		mView(2, 3) = 0.0f;
		mView(3, 3) = 1.0f;

		mViewDirty = false;
	}
}