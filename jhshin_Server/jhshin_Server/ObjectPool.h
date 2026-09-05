#pragma once

#include "RSDefine.h"

template<typename T>
class ObjectPool
{
public:
	ObjectPool();
	ObjectPool( int PoolSize );
	~ObjectPool() {}

	void InitObjectPool( int PoolSize );

	T* Pop();
	bool Push( T* Object );

private:
	mutex m_Lock;
	vector<T> m_Storage;
	stack<T*> m_Pools;
	vector<bool> m_IsUse;
	int m_FreeCount = 0;
	int m_MaxSize = 0;
};

template<typename T>
inline ObjectPool<T>::ObjectPool()
{
}

template<typename T>
inline ObjectPool<T>::ObjectPool( int PoolSize )
{
	InitObjectPool( PoolSize );
}

template<typename T>
inline void ObjectPool<T>::InitObjectPool( int PoolSize )
{
	if( 0 < m_MaxSize )
	{
		return;
	}

	m_MaxSize = PoolSize;
	m_FreeCount = PoolSize;
	m_Storage.resize( PoolSize );
	m_IsUse.resize( PoolSize, false );

	for( int i = 0; i < PoolSize; ++i )
	{
		m_Pools.push( &m_Storage[i] );
	}

}

template<typename T>
inline T* ObjectPool<T>::Pop()
{
	lock_guard<mutex> lock( m_Lock );

	if( !m_Pools.empty() )
	{
		T* Object = m_Pools.top();

		int index = Object - &m_Storage[0];
		if( 0 <= index && index < m_MaxSize )
		{
			if( false == m_IsUse[index] )
			{
				m_IsUse[index] = true;
				--m_FreeCount;

				m_Pools.pop();

				return Object;
			}
		}
	}
	
	return nullptr;
}

template<typename T>
inline bool ObjectPool<T>::Push( T* Object )
{
	bool Result = false;
	lock_guard<mutex> lock( m_Lock );

	// 범위 체크
	if( &m_Storage[0] > Object || Object > &m_Storage[m_MaxSize - 1] )
	{
		return Result;
	}

	// 중복 반환 체크
	int index = Object - &m_Storage[0];
	if( 0 <= index && index < m_MaxSize )
	{
		if( m_IsUse[index] )
		{
			m_IsUse[index] = false;

			Object->Reset();

			m_Pools.push( Object );
			++m_FreeCount;

			Result = true;
		}
	}

	return Result;
}
