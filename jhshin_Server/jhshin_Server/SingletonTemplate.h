#pragma once

// main에서 매니저 미리 생성해서 쓰기
template< typename T >
class SingleT
{
public:
	static T* This()
	{
		if( instance == nullptr )
		{
			instance = Create();
		}

		return instance;
	};

	static T* Create()
	{
		if( instance == nullptr )
		{
			instance = new T();
		}

		return instance;
	}

	static void Release()
	{
		if( instance )
		{
			delete instance;
			instance = nullptr;
		}
	}

protected:
	SingleT() = default;
	~SingleT() = default;

	SingleT( const SingleT& ) = delete;
	SingleT& operator=( const SingleT& ) = delete;

private:
	inline static T* instance = nullptr;
};