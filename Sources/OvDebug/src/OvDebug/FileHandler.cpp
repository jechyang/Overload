/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#define _CRT_SECURE_NO_WARNINGS // Enable getenv

#include <OvTools/Time/Date.h>
#include <OvTools/Utils/SystemCalls.h>

#include "OvDebug/FileHandler.h"

#include <fstream>
#include <iostream>
#include <mutex>
#include <filesystem>

namespace { std::mutex fileMutex; }

std::string const OvDebug::FileHandler::__DEFAULT_HEADER;
std::string const OvDebug::FileHandler::__INFO_HEADER		= "[INFO] ";
std::string const OvDebug::FileHandler::__WARNING_HEADER	= "[WARNING] ";
std::string const OvDebug::FileHandler::__ERROR_HEADER		= "[ERROR] ";
std::string OvDebug::FileHandler::__APP_LAUNCH_DATE			= OvTools::Time::Date::GetDateAsString();
std::string const OvDebug::FileHandler::__LOG_EXTENSION		= ".ovlog";

std::ofstream OvDebug::FileHandler::OUTPUT_FILE;
std::string OvDebug::FileHandler::LOG_FILE_PATH = OvTools::Utils::SystemCalls::GetPathToAppdata() + std::string("\\OverloadTech\\OvEditor\\Log\\") + __APP_LAUNCH_DATE + __LOG_EXTENSION;

void OvDebug::FileHandler::Log(const LogData& p_logData)
{
	std::lock_guard lock(fileMutex);
	if (!OUTPUT_FILE.is_open())
	{
		OUTPUT_FILE.open(LOG_FILE_PATH, std::ios_base::app);
	}

	if (OUTPUT_FILE.is_open())
		OUTPUT_FILE << GetLogHeader(p_logData.logLevel) << p_logData.date << " " << p_logData.message << std::endl;
	else
		std::cout << "Unable to create log file" << std::endl;
}

std::string OvDebug::FileHandler::GetLogFilePath()
{
	std::lock_guard lock(fileMutex);
	return LOG_FILE_PATH;
}

void OvDebug::FileHandler::SetLogFilePath(const std::string& p_path)
{
	const std::filesystem::path path(p_path);
	if (path.filename().empty()) throw std::invalid_argument("Log file name is empty");
	const auto destination = (path.parent_path() / (__APP_LAUNCH_DATE + path.filename().string())).string();
	std::lock_guard lock(fileMutex);
	OUTPUT_FILE.close(); OUTPUT_FILE.clear();
	LOG_FILE_PATH = destination;
}

std::string OvDebug::FileHandler::GetLogHeader(ELogLevel p_logLevel)
{
	switch (p_logLevel)
	{
	case ELogLevel::LOG_DEFAULT:	return __DEFAULT_HEADER;
	case ELogLevel::LOG_INFO:		return __INFO_HEADER;
	case ELogLevel::LOG_WARNING:	return __WARNING_HEADER;
	case ELogLevel::LOG_ERROR:		return __ERROR_HEADER;
	}

	return "";
}
