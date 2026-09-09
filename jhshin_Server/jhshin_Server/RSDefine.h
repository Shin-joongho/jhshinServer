#pragma once

#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <stack>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <queue>
#include <cstring>

using namespace std;


using uint16 = unsigned __int16;

using SessionDataRef = shared_ptr<class SessionData>;
using SendBufferRef = shared_ptr<class SendBuffer>;
using JobObjectRef = shared_ptr<class JobObject>;

const int PACKET_SIZE = 4096;
