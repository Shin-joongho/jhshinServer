#pragma once
#include "JobObject.h"

class JobQueue
{
public:
	bool PopAll( queue<JobObjectRef>& jobqueue );
	void Push( JobObjectRef jobObject );

	void Stop();

private:
	mutex m_Lock;
	queue<JobObjectRef> m_JobQueue;
	condition_variable m_cv;

	bool m_Stop = false;
};

