#pragma once
#include <cstdint>
#include <climits>
#include <limits>
#include <stdexcept>
#include <vector>
#include <filesystem>
#include <string_view>
#include <istream>
#include <ostream>
#include <PatchData.hpp>
#include <Escape.hpp>

std::vector<std::uint8_t> getNewFileContent(const std::vector<std::uint8_t>& escapedOldFile, const PatchData& patchData)
{
	auto newFileData = std::vector<std::uint8_t>{};

	for (const auto& chunk : patchData.dataChunks)
	{
		auto copyBegin = newFileData.size();
		newFileData.resize(copyBegin + chunk.length);
		if (chunk.sourcePosition == static_cast<std::uint32_t>(-1))
		{
			std::copy_n(chunk.data.begin(), chunk.length, newFileData.begin() + copyBegin);
		}
		else
		{
			if (chunk.sourcePosition + chunk.length > escapedOldFile.size())
			{
				throw std::out_of_range{ "chunk.sourcePosition + chunk.length > escapedOldFile.size(), corrupted index file / old file?" };
			}
			std::copy_n(escapedOldFile.begin() + chunk.sourcePosition, chunk.length, newFileData.begin() + copyBegin);
		}

	}
	return unescape(newFileData, patchData.escapeData);
}

auto voidNoOperation = [](...) {};
template<typename ShowProgress = decltype(voidNoOperation)>
void writeNewFileContent(std::ostream& out, 
	const std::vector<std::uint8_t>& escapedOldFile, 
	const PatchData& patchData, 
	std::size_t maxBufferSize,
	ShowProgress showProgress = voidNoOperation)
{
	out.exceptions(out.exceptions() | out.badbit | out.failbit);
	auto newFileData = std::vector<std::uint8_t>{};
	for (const auto& chunk : patchData.dataChunks)
	{
		auto copyBegin = newFileData.size();
		newFileData.resize(copyBegin + chunk.length);
		if (chunk.sourcePosition == static_cast<std::uint32_t>(-1))
		{
			std::copy_n(chunk.data.begin(), chunk.length, newFileData.begin() + copyBegin);
		}
		else
		{
			if (chunk.sourcePosition + chunk.length > escapedOldFile.size())
			{
				throw std::out_of_range{ "chunk.sourcePosition + chunk.length > escapedOldFile.size(), corrupted index file / old file?" };
			}
			std::copy_n(escapedOldFile.begin() + chunk.sourcePosition, chunk.length, newFileData.begin() + copyBegin);
		}

		if (newFileData.size() > maxBufferSize && newFileData.back() != patchData.escapeData.escape)
		{
			showProgress(newFileData.size());
			newFileData = unescape(newFileData, patchData.escapeData);
			out.write(reinterpret_cast<const char*>(newFileData.data()), newFileData.size());
			newFileData.clear();
		}
	}

	if (newFileData.empty() == false)
	{
		newFileData = unescape(newFileData, patchData.escapeData);
		out.write(reinterpret_cast<const char*>(newFileData.data()), newFileData.size());
		newFileData.clear();
	}
}