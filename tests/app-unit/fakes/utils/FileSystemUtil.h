#pragma once
// Double for es-core/src/utils/FileSystemUtil.h: no file exists.
#include <string>
namespace Utils { namespace FileSystem {
	bool exists(const std::string& path, bool useCache = true);
	std::string readAllText(const std::string& path);
}}
