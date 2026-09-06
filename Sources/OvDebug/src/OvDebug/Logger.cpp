/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <cstdint>
#include <mutex>

#include "OvDebug/Logger.h"
#include "OvTools/Time/Date.h"

OvTools::Eventing::MainThreadEvent<OvDebug::LogData> OvDebug::Logger::LogEvent;

namespace { std::mutex logMutex; }

std::map<std::string, OvDebug::ConsoleHandler>	OvDebug::Logger::CONSOLE_HANDLER_MAP;
std::map<std::string, OvDebug::FileHandler>		OvDebug::Logger::FILE_HANDLER_MAP;
std::map<std::string, OvDebug::HistoryHandler>	OvDebug::Logger::HISTORY_HANDLER_MAP;

void OvDebug::Logger::Log(const std::string& p_message, ELogLevel p_logLevel, ELogMode p_logMode, std::string p_handlerId)
{
	LogData logData{ p_message, p_logLevel, OvTools::Time::Date::GetDateAsString() };
	{
		std::lock_guard lock(logMutex);

		switch (p_logMode)
		{
		case ELogMode::DEFAULT:
		case ELogMode::CONSOLE: LogToHandlerMap<ConsoleHandler>(CONSOLE_HANDLER_MAP, logData, p_handlerId); break;
		case ELogMode::FILE:	LogToHandlerMap<FileHandler>(FILE_HANDLER_MAP, logData, p_handlerId);		break;
		case ELogMode::HISTORY: LogToHandlerMap<HistoryHandler>(HISTORY_HANDLER_MAP, logData, p_handlerId);	break;
		case ELogMode::ALL:
			LogToHandlerMap<ConsoleHandler>(CONSOLE_HANDLER_MAP, logData, p_handlerId);
			LogToHandlerMap<FileHandler>(FILE_HANDLER_MAP, logData, p_handlerId);
			LogToHandlerMap<HistoryHandler>(HISTORY_HANDLER_MAP, logData, p_handlerId);
			break;
		}

		LogEvent.Post(logData);
	}
	if (LogEvent.IsOwnerThread()) DispatchPendingEvents();
}

void OvDebug::Logger::DispatchPendingEvents()
{
	// A subscriber may log while processing a message. Keep each pump finite.
	static thread_local bool dispatching = false;
	if (dispatching) return;
	dispatching = true;
	struct Reset { bool& value; ~Reset() { value = false; } } reset{dispatching};
	LogEvent.DispatchPending();
	if (const auto dropped = LogEvent.TakeDroppedCount())
	{
		Log("UI log notification queue skipped " + std::to_string(dropped) +
			" older messages; log output destinations are unchanged.", ELogLevel::LOG_WARNING);
		LogEvent.DispatchPending();
	}
}

OvDebug::ConsoleHandler& OvDebug::Logger::CreateConsoleHandler(std::string p_id)
{
	std::lock_guard lock(logMutex);
	CONSOLE_HANDLER_MAP.emplace(p_id, OvDebug::ConsoleHandler());
	return CONSOLE_HANDLER_MAP[p_id];
}

OvDebug::FileHandler& OvDebug::Logger::CreateFileHandler(std::string p_id)
{
	std::lock_guard lock(logMutex);
	FILE_HANDLER_MAP.emplace(p_id, OvDebug::FileHandler());
	return FILE_HANDLER_MAP[p_id];
}

OvDebug::HistoryHandler& OvDebug::Logger::CreateHistoryHandler(std::string p_id)
{
	std::lock_guard lock(logMutex);
	HISTORY_HANDLER_MAP.emplace(p_id, OvDebug::HistoryHandler());
	return HISTORY_HANDLER_MAP[p_id];
}

OvDebug::ConsoleHandler& OvDebug::Logger::GetConsoleHandler(std::string p_id)
{
	std::lock_guard lock(logMutex);
	return CONSOLE_HANDLER_MAP[p_id];
}

OvDebug::FileHandler& OvDebug::Logger::GetFileHandler(std::string p_id)
{
	std::lock_guard lock(logMutex);
	return FILE_HANDLER_MAP[p_id];
}

OvDebug::HistoryHandler& OvDebug::Logger::GetHistoryHandler(std::string p_id)
{
	std::lock_guard lock(logMutex);
	return HISTORY_HANDLER_MAP[p_id];
}
