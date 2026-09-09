#include "JobQueue.h"

bool JobQueue::PopAll( queue<JobObjectRef>& jobqueue )
{
    unique_lock<mutex> lg( m_Lock );
    m_cv.wait( lg, [this]() { return m_Stop || m_JobQueue.empty() == false; } );

    // 가짜 일어남 확인
    if( m_JobQueue.empty() )
    {
        return m_Stop;
    }

    m_JobQueue.swap( jobqueue );

    return m_Stop;
}

void JobQueue::Push( JobObjectRef jobObject )
{
    {
        unique_lock<mutex> lg( m_Lock );
        m_JobQueue.push( jobObject );
    }

    m_cv.notify_one();
}

void JobQueue::Stop()
{
    {
        unique_lock<mutex> lg( m_Lock );
        m_Stop = true;
    }

    m_cv.notify_all();
}
