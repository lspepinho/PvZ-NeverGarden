#include "WidescreenPatcher.h"
#if !defined(__SWITCH__) && !defined(__3DS__) && !defined(__IPHONEOS__)

#include "SexyAppFramework/Common.h"

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cstdarg>

#ifdef __EMSCRIPTEN__
#include <emscripten/fetch.h>
#else
#include <curl/curl.h>
#endif
#include <minizip/unzip.h>
#include <SDL.h>

namespace fs = std::filesystem;

// ============================================================================
// Constants
// ============================================================================
static const char* kWidescreenZipUrl =
	"https://github.com/HenryJk/PvZWidescreen/archive/refs/heads/main.zip";

static const char* kFlagFileName = "patched.flag";
static const char* kLogFileName = "patcher.log";

// Files to skip from the widescreen ZIP
static const char* kSkipFiles[] = {
	"bass.dll",
	"PlantsVsZombies.exe",
	"eula.txt",
	"properties/resources.xml",
	"properties/default.xml",
	"resources.xml",
	"default.xml"
};

// ============================================================================
// Helpers
// ============================================================================

static void LogPatch(const std::string& msg)
{
	std::string aAppData = Sexy::GetAppDataFolder();
	if (aAppData.empty()) return;
	fs::path logPath = fs::path(aAppData) / "cache64" / kLogFileName;

	fs::create_directories(logPath.parent_path());
	std::ofstream ofs(logPath, std::ios::app);
	if (ofs) {
		ofs << msg << std::endl;
	}
}

static void LogPrintf(const char* fmt, ...)
{
	char buf[1024];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	LogPatch(buf);
}

static bool ShouldSkipFile(const std::string& filename)
{
	std::string lowerName = filename;
	std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

	for (const auto* skip : kSkipFiles)
	{
		std::string lowerSkip = skip;
		std::transform(lowerSkip.begin(), lowerSkip.end(), lowerSkip.begin(), ::tolower);

		if (lowerName == lowerSkip)
			return true;
	}
	return false;
}

// Get just the filename from a path
static std::string GetBaseName(const std::string& path)
{
	auto pos = path.find_last_of("/\\");
	return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

#ifndef __EMSCRIPTEN__
// libcurl write callback
static size_t CurlWriteCallback(void* ptr, size_t size, size_t nmemb, void* userdata)
{
	auto* vec = static_cast<std::vector<uint8_t>*>(userdata);
	size_t total = size * nmemb;
	auto* bytes = static_cast<uint8_t*>(ptr);
	vec->insert(vec->end(), bytes, bytes + total);
	return total;
}
#endif

// ============================================================================
// Download
// ============================================================================

static bool DownloadFile(const char* url, const fs::path& outPath)
{
	LogPrintf("[Widescreen] Downloading %s ...", url);

#ifdef __EMSCRIPTEN__
	emscripten_fetch_attr_t attr;
	emscripten_fetch_attr_init(&attr);
	strcpy(attr.requestMethod, "GET");
	attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY | EMSCRIPTEN_FETCH_SYNCHRONOUS;
	
	emscripten_fetch_t *fetch = emscripten_fetch(&attr, url);
	if (!fetch)
	{
		LogPrintf("[Widescreen] emscripten_fetch failed to start");
		return false;
	}

	if (fetch->status != 200)
	{
		LogPrintf("[Widescreen] HTTP error: %d", fetch->status);
		emscripten_fetch_close(fetch);
		return false;
	}

	LogPrintf("[Widescreen] Downloaded %llu bytes", fetch->numBytes);

	fs::create_directories(outPath.parent_path());
	std::ofstream ofs(outPath, std::ios::binary);
	if (!ofs)
	{
		LogPrintf("[Widescreen] Failed to write %s", outPath.string().c_str());
		emscripten_fetch_close(fetch);
		return false;
	}
	ofs.write(fetch->data, fetch->numBytes);
	emscripten_fetch_close(fetch);
	return true;

#else
	CURL* curl = curl_easy_init();
	if (!curl)
	{
		LogPrintf("[Widescreen] Failed to init curl");
		return false;
	}

	std::vector<uint8_t> data;
	data.reserve(50 * 1024 * 1024); // Reserve 50MB

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "PvZ-Portable/1.0");

	CURLcode res = curl_easy_perform(curl);
	long httpCode = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
	curl_easy_cleanup(curl);

	if (res != CURLE_OK)
	{
		LogPrintf("[Widescreen] Download failed: %s", curl_easy_strerror(res));
		return false;
	}

	if (httpCode != 200)
	{
		LogPrintf("[Widescreen] HTTP error: %ld", httpCode);
		return false;
	}

	LogPrintf("[Widescreen] Downloaded %zu bytes", data.size());

	fs::create_directories(outPath.parent_path());
	std::ofstream ofs(outPath, std::ios::binary);
	if (!ofs)
	{
		LogPrintf("[Widescreen] Failed to write %s", outPath.string().c_str());
		return false;
	}
	ofs.write(reinterpret_cast<const char*>(data.data()), data.size());
	return true;
#endif
}

// ============================================================================
// ZIP Extraction
// ============================================================================

static bool ExtractZip(const fs::path& zipPath, const fs::path& outDir)
{
	LogPrintf("[Widescreen] Extracting %s ...", zipPath.string().c_str());

	unzFile zf = unzOpen(zipPath.string().c_str());
	if (!zf)
	{
		LogPrintf("[Widescreen] Failed to open ZIP");
		return false;
	}

	fs::create_directories(outDir);

	int ret = unzGoToFirstFile(zf);
	while (ret == UNZ_OK)
	{
		unz_file_info fileInfo;
		char filename[512];
		unzGetCurrentFileInfo(zf, &fileInfo, filename, sizeof(filename), nullptr, 0, nullptr, 0);

		std::string name(filename);

		// Skip directories
		if (!name.empty() && name.back() == '/')
		{
			fs::create_directories(outDir / name);
			ret = unzGoToNextFile(zf);
			continue;
		}

		fs::path outFile = outDir / name;
		fs::create_directories(outFile.parent_path());

		if (unzOpenCurrentFile(zf) != UNZ_OK)
		{
			ret = unzGoToNextFile(zf);
			continue;
		}

		std::ofstream ofs(outFile, std::ios::binary);
		if (ofs)
		{
			char buf[8192];
			int bytesRead;
			while ((bytesRead = unzReadCurrentFile(zf, buf, sizeof(buf))) > 0)
			{
				ofs.write(buf, bytesRead);
			}
		}

		unzCloseCurrentFile(zf);
		ret = unzGoToNextFile(zf);
	}

	unzClose(zf);
	LogPrintf("[Widescreen] Extraction complete");
	return true;
}

// ============================================================================
// PAK Extraction (XOR 0xF7 + PopCap format)
// ============================================================================

struct PakEntry
{
	std::string name;
	int64_t fileTime;
	std::vector<uint8_t> data;
};

static bool ExtractPak(const fs::path& pakPath, std::vector<PakEntry>& entries)
{
	LogPrintf("[Widescreen] Extracting PAK %s ...", pakPath.string().c_str());

	std::ifstream ifs(pakPath, std::ios::binary | std::ios::ate);
	if (!ifs)
	{
		LogPrintf("[Widescreen] Failed to open PAK");
		return false;
	}

	size_t fileSize = ifs.tellg();
	ifs.seekg(0);

	std::vector<uint8_t> raw(fileSize);
	ifs.read(reinterpret_cast<char*>(raw.data()), fileSize);
	ifs.close();

	// XOR decode
	for (auto& b : raw)
		b ^= 0xF7;

	// Parse header
	if (fileSize < 8)
		return false;

	uint32_t magic = *reinterpret_cast<uint32_t*>(raw.data());
	uint32_t version = *reinterpret_cast<uint32_t*>(raw.data() + 4);

	// Handle endianness (stored as LE)
	// On little-endian systems this is a no-op
	if (magic != 0xBAC04AC0)
	{
		LogPrintf("[Widescreen] Bad PAK magic: 0x%08X", magic);
		return false;
	}
	if (version > 0)
	{
		LogPrintf("[Widescreen] Unsupported PAK version: %u", version);
		return false;
	}

	// Parse file table
	struct FileTableEntry
	{
		std::string name;
		int srcSize;
		int64_t fileTime;
	};

	std::vector<FileTableEntry> table;
	size_t pos = 8;

	while (pos < fileSize)
	{
		uint8_t flags = raw[pos++];
		if (flags & 0x80)
			break;

		uint8_t nameWidth = raw[pos++];
		std::string name(reinterpret_cast<char*>(raw.data() + pos), nameWidth);
		pos += nameWidth;

		int srcSize = *reinterpret_cast<int32_t*>(raw.data() + pos);
		pos += 4;
		int64_t fileTime = *reinterpret_cast<int64_t*>(raw.data() + pos);
		pos += 8;

		// Normalize slashes
		for (auto& c : name)
			if (c == '\\') c = '/';

		table.push_back({name, srcSize, fileTime});
	}

	// pos now points to the start of file data
	size_t dataStart = pos;

	entries.clear();
	entries.reserve(table.size());

	size_t dataPos = dataStart;
	for (const auto& entry : table)
	{
		PakEntry pe;
		pe.name = entry.name;
		pe.fileTime = entry.fileTime;

		if (dataPos + entry.srcSize <= fileSize)
		{
			pe.data.assign(raw.begin() + dataPos, raw.begin() + dataPos + entry.srcSize);
		}
		dataPos += entry.srcSize;
		entries.push_back(std::move(pe));
	}

	LogPrintf("[Widescreen] Extracted %zu files from PAK", entries.size());
	return true;
}

// ============================================================================
// PAK Repacking
// ============================================================================

static bool RepackPak(const fs::path& pakPath, const std::vector<PakEntry>& entries)
{
	LogPrintf("[Widescreen] Repacking PAK with %zu files...", entries.size());

	// Build file table + data
	std::vector<uint8_t> output;
	output.reserve(200 * 1024 * 1024); // Reserve 200MB

	// Magic + Version
	uint32_t magic = 0xBAC04AC0;
	uint32_t version = 0;
	output.insert(output.end(), reinterpret_cast<uint8_t*>(&magic), reinterpret_cast<uint8_t*>(&magic) + 4);
	output.insert(output.end(), reinterpret_cast<uint8_t*>(&version), reinterpret_cast<uint8_t*>(&version) + 4);

	// File table entries
	for (const auto& entry : entries)
	{
		// Use backslashes in PAK (original format)
		std::string pakName = entry.name;
		for (auto& c : pakName)
			if (c == '/') c = '\\';

		uint8_t flags = 0;
		output.push_back(flags);

		uint8_t nameWidth = static_cast<uint8_t>(pakName.size());
		output.push_back(nameWidth);
		output.insert(output.end(), pakName.begin(), pakName.end());

		int32_t srcSize = static_cast<int32_t>(entry.data.size());
		output.insert(output.end(), reinterpret_cast<uint8_t*>(&srcSize), reinterpret_cast<uint8_t*>(&srcSize) + 4);

		int64_t fileTime = entry.fileTime;
		output.insert(output.end(), reinterpret_cast<uint8_t*>(&fileTime), reinterpret_cast<uint8_t*>(&fileTime) + 8);
	}

	// End of file table
	uint8_t endFlag = 0x80;
	output.push_back(endFlag);

	// File data
	for (const auto& entry : entries)
	{
		output.insert(output.end(), entry.data.begin(), entry.data.end());
	}

	// XOR encode
	for (auto& b : output)
		b ^= 0xF7;

	// Write
	std::ofstream ofs(pakPath, std::ios::binary);
	if (!ofs)
	{
		LogPrintf("[Widescreen] Failed to write PAK");
		return false;
	}
	ofs.write(reinterpret_cast<const char*>(output.data()), output.size());
	LogPrintf("[Widescreen] PAK written: %zu bytes", output.size());
	return true;
}

// ============================================================================
// Main Entry Point
// ============================================================================

bool PatchWidescreenPak(const std::filesystem::path& theResourceDir)
{
    try {
#ifndef __EMSCRIPTEN__
        curl_global_init(CURL_GLOBAL_DEFAULT);
#endif
        fs::path aSaveDir;
        std::string aAppData = Sexy::GetAppDataFolder();
        if (!aAppData.empty())
        {
            aSaveDir = fs::path(aAppData);
        }

        if (aSaveDir.empty())
        {
            LogPrintf("[Widescreen] Could not determine save directory");
            return false;
        }

	fs::path aCacheDir = aSaveDir / "cache64";
	fs::path aFlagFile = aCacheDir / kFlagFileName;

	LogPatch("--- Starting PatchWidescreenPak ---");

	// Already patched?
	if (fs::exists(aFlagFile))
	{
		LogPrintf("[Widescreen] Already patched (flag found)");
		return true;
	}

	fs::create_directories(aCacheDir);

	// Step 1: Download the widescreen ZIP
	fs::path aZipPath = aCacheDir / "widescreen.zip";
	if (!fs::exists(aZipPath))
	{
		if (!DownloadFile(kWidescreenZipUrl, aZipPath))
			return false;
	}

	// Step 2: Extract ZIP
	fs::path aWsTemp = aCacheDir / "ws_temp";
	if (!fs::exists(aWsTemp))
	{
		if (!ExtractZip(aZipPath, aWsTemp))
			return false;
	}

	// Find the resources directory inside the extracted ZIP
	// GitHub ZIP structure: PvZWidescreen-main/resources/...
	fs::path aWsResources;
	for (auto& p : fs::recursive_directory_iterator(aWsTemp))
	{
		if (p.is_directory() && p.path().filename() == "resources")
		{
			aWsResources = p.path();
			break;
		}
	}

	if (aWsResources.empty() || !fs::exists(aWsResources))
	{
		LogPrintf("[Widescreen] Could not find resources/ in ZIP");
		return false;
	}

	LogPrintf("[Widescreen] Found widescreen resources at: %s", aWsResources.string().c_str());

	// Step 3: Extract original main.pak
	fs::path aPakPath = theResourceDir / "main.pak";
	if (!fs::exists(aPakPath))
	{
		LogPrintf("[Widescreen] main.pak not found at: %s", aPakPath.string().c_str());
		return false;
	}

	std::vector<PakEntry> pakEntries;
	if (!ExtractPak(aPakPath, pakEntries))
		return false;

	// Step 4: Overlay widescreen resources
	// Build a map of PAK entries by name for quick lookup
	std::map<std::string, size_t> pakMap;
	for (size_t i = 0; i < pakEntries.size(); ++i)
	{
		std::string key = pakEntries[i].name;
		std::transform(key.begin(), key.end(), key.begin(), ::tolower);
		pakMap[key] = i;
	}

	int replaced = 0, added = 0;

	for (auto& p : fs::recursive_directory_iterator(aWsResources))
	{
		if (!p.is_regular_file())
			continue;

		fs::path relPath = fs::relative(p.path(), aWsResources);
		std::string relStr = relPath.generic_string();

		// Skip excluded files
		if (ShouldSkipFile(relStr))
			continue;

		// Read the file
		std::ifstream fIn(p.path(), std::ios::binary | std::ios::ate);
		if (!fIn)
			continue;

		size_t fSize = fIn.tellg();
		fIn.seekg(0);
		std::vector<uint8_t> fData(fSize);
		fIn.read(reinterpret_cast<char*>(fData.data()), fSize);

		// Check if this replaces an existing entry
		std::string keyLower = relStr;
		std::transform(keyLower.begin(), keyLower.end(), keyLower.begin(), ::tolower);

		auto it = pakMap.find(keyLower);
		if (it != pakMap.end())
		{
			pakEntries[it->second].data = std::move(fData);
			replaced++;
		}
		else
		{
			PakEntry newEntry;
			newEntry.name = relStr;
			newEntry.fileTime = 0;
			newEntry.data = std::move(fData);
			pakMap[keyLower] = pakEntries.size();
			pakEntries.push_back(std::move(newEntry));
			added++;
		}
	}

	LogPrintf("[Widescreen] Replaced %d files, added %d new files from ZIP", replaced, added);
	
	// Step 4.5: Overlay local assets
	fs::path aLocalAssets = theResourceDir / "assets";
	if (!fs::exists(aLocalAssets)) {
		// Try relative to current working directory as fallback
		aLocalAssets = fs::current_path() / "assets";
	}

	if (fs::exists(aLocalAssets))
	{
		LogPrintf("[Widescreen] Found local assets at: %s", aLocalAssets.string().c_str());
		int localReplaced = 0, localAdded = 0;

		for (auto& p : fs::recursive_directory_iterator(aLocalAssets))
		{
			if (!p.is_regular_file())
				continue;

			// We want to keep the "assets/" prefix in the PAK
			fs::path relPath = fs::path("assets") / fs::relative(p.path(), aLocalAssets);
			std::string relStr = relPath.generic_string();

			// Read the file
			std::ifstream fIn(p.path(), std::ios::binary | std::ios::ate);
			if (!fIn)
				continue;

			size_t fSize = fIn.tellg();
			fIn.seekg(0);
			std::vector<uint8_t> fData(fSize);
			fIn.read(reinterpret_cast<char*>(fData.data()), fSize);

			// Check if this replaces an existing entry
			std::string keyLower = relStr;
			std::transform(keyLower.begin(), keyLower.end(), keyLower.begin(), ::tolower);

			auto it = pakMap.find(keyLower);
			if (it != pakMap.end())
			{
				pakEntries[it->second].data = std::move(fData);
				localReplaced++;
			}
			else
			{
				PakEntry newEntry;
				newEntry.name = relStr;
				newEntry.fileTime = 0;
				newEntry.data = std::move(fData);
				pakMap[keyLower] = pakEntries.size();
				pakEntries.push_back(std::move(newEntry));
				localAdded++;
			}
		}
		LogPrintf("[Widescreen] Local assets: Replaced %d files, added %d new files", localReplaced, localAdded);
	}
	else
	{
		LogPrintf("[Widescreen] Local assets directory not found at %s", aLocalAssets.string().c_str());
	}

	// Step 5: Repack
	if (!RepackPak(aPakPath, pakEntries))
		return false;

	// Step 6: Write flag
	{
		std::ofstream flagOut(aFlagFile);
		flagOut << "patched";
	}

	// Cleanup temp files
	std::error_code ec;
	fs::remove_all(aWsTemp, ec);
	fs::remove(aZipPath, ec);

	LogPrintf("[Widescreen] Patching complete!");
	return true;
    } catch (const std::exception& e) {
        LogPrintf("[Widescreen] Exception during patching: %s", e.what());
        return false;
    } catch (...) {
        LogPrintf("[Widescreen] Unknown exception during patching");
        return false;
    }
}

#else

bool PatchWidescreenPak(const std::filesystem::path& theResourceDir)
{
	return true;
}

#endif
