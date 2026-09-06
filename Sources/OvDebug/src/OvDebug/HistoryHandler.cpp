/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include "OvDebug/HistoryHandler.h"
#include <mutex>

namespace { std::mutex historyMutex; }

std::queue<OvDebug::LogData> OvDebug::HistoryHandler::LOG_QUEUE;

void OvDebug::HistoryHandler::Log(const LogData& p_logData)
{
	std::lock_guard lock(historyMutex);
	LOG_QUEUE.push(p_logData);
}

std::queue<OvDebug::LogData> OvDebug::HistoryHandler::GetLogQueue()
{
	std::lock_guard lock(historyMutex);
	return LOG_QUEUE;
}
