#pragma once
#include <stdexcept>
#include <limits>
#include <vector>
#include <filesystem>
#include <string_view>
#include <istream>
#include <ostream>
#include "Escape.hpp"

struct DataChunk {
	DataChunk() : length{ 0 }, sourcePosition{ 0 }, data{} {}

	DataChunk(std::size_t length, std::size_t sourcePosition, std::vector<std::uint8_t> data)
	{
		if (length > std::numeric_limits<std::uint32_t>::max())
		{
			throw std::length_error{ "length too large!" };
		}
		if (sourcePosition > std::numeric_limits<std::uint32_t>::max() && sourcePosition != static_cast<std::size_t>(-1))
		{
			throw std::length_error{ "sourcePosition too large!" };
		}
		this->length = static_cast<std::uint32_t>(length);
		this->sourcePosition = static_cast<std::uint32_t>(sourcePosition);
		this->data = std::move(data);
	}

	std::uint32_t length;
	std::uint32_t sourcePosition;
	std::vector<std::uint8_t> data;
	static constexpr std::size_t lowestReferencedBytesCount = 32;
};

template<typename T>
void writeAsCharArray(std::ostream& out, const T& data)
{
	out.write(reinterpret_cast<const char*>(&data), sizeof(data) / sizeof(char));
};

template<typename T>
T readFromCharArray(std::istream& in)
{
	auto data = T{};
	in.read(reinterpret_cast<char*>(&data), sizeof(data) / sizeof(char));
	return data;
};

struct PatchData {
	int version;
	std::filesystem::path oldFileName;
	std::filesystem::path newFileName;
	EscapeData escapeData;
	std::vector<DataChunk> dataChunks;
};

using namespace std::string_view_literals;

constexpr auto latestPatchDataVersion = 1000;
constexpr auto patchFileHeader = u8"ºì¾¯3°É×°¼×³å»÷¸üÐÂÃèÊöÎÄ¼þ"sv;
constexpr auto delimiter = u8"\r\n"sv;

/*
	Patch File structure:
	[plain text] utf8 patchFileHeader;
	[plain text] latestPatchDataVersion
	\r\n
	(Other contents)

	Patch File Version 1000 structure:

	[plain text] patchFileHeader;
	[plain text] latestPatchDataVersion
	\r\n
	[plain text] oldFileName.length()
	\r\n
	[plain text] utf8 oldFileName
	\r\n
	[plain text] newFileName.length()
	\r\n
	[plain text] utf8 newFileName
	\r\n
	[plain text] numerical value of escaped char
	\r\n
	[plain text] numerical value of substitue char
	\r\n
	[plain text] numerical value of escape char
	\r\n
	[plain text] numerical value of escape2 char
	\r\n
	[plain text] DataChunks array length
	DataChunks[DataChunks array length]

	Layout of DataChunk:
	char[4] chunk length (32bit) in little endian
	char[4] source position (32bit) in little endian
#if soucePosition == -1
    bytes[chunk length]
#endif

*/

void writeChunks(std::ostream& out, const PatchData& patchData)
{
	constexpr auto supportedVersion = 1000;
	if (patchData.version != supportedVersion)
	{
		throw std::invalid_argument{ "Unsupported patch data version!" };
	}

	out.exceptions(out.exceptions() | out.badbit | out.failbit);

	out.write(patchFileHeader.data(), patchFileHeader.size());
	out << patchData.version;
	out.write(delimiter.data(), delimiter.size());

	auto utf8OldFileName = patchData.oldFileName.u8string();
	out << utf8OldFileName.size();
	out.write(delimiter.data(), delimiter.size());
	out.write(utf8OldFileName.data(), utf8OldFileName.size());
	out.write(delimiter.data(), delimiter.size());

	auto utf8NewFileName = patchData.newFileName.u8string();
	out << utf8NewFileName.size();
	out.write(delimiter.data(), delimiter.size());
	out.write(utf8NewFileName.data(), utf8NewFileName.size());
	out.write(delimiter.data(), delimiter.size());

	out << +patchData.escapeData.toBeEscaped;
	out.write(delimiter.data(), delimiter.size());
	out << +patchData.escapeData.substituteCharacter;
	out.write(delimiter.data(), delimiter.size());
	out << +patchData.escapeData.escape;
	out.write(delimiter.data(), delimiter.size());
	out << +patchData.escapeData.escape2;
	out.write(delimiter.data(), delimiter.size());

	out << patchData.dataChunks.size();
	out.write(delimiter.data(), delimiter.size());
	for (const auto& chunk : patchData.dataChunks)
	{
		writeAsCharArray(out, chunk.length);
		writeAsCharArray(out, chunk.sourcePosition);
		if (chunk.sourcePosition == static_cast<decltype(chunk.sourcePosition)>(-1))
		{
			out.write(reinterpret_cast<const char*>(chunk.data.data()), chunk.length);
		}
	}
}

template<typename IStream>
PatchData readChunks(IStream&& in)
{
	constexpr auto supportedVersion = 1000;
	in.exceptions(in.exceptions() | in.badbit | in.failbit);

	auto patchData = PatchData{};
	auto checkHeader = [](std::istream& in, std::string_view check) {
		for (auto character : check)
		{
			if (static_cast<char>(in.get()) != character)
			{
				throw std::invalid_argument{ "Required patch file header not found!" };
			}
		}
	};
	checkHeader(in, patchFileHeader);
	in >> patchData.version;
	if (patchData.version != supportedVersion)
	{
		throw std::invalid_argument{ "Unsupported patch data version! You may need to get a newer version of this program." };
	}
	checkHeader(in, delimiter);

	auto readSizeType = [](std::istream& in) {
		auto result = std::size_t{};
		in >> result;
		return result;
	};


	auto oldFileNameString = std::string{ readSizeType(in),{}, std::string::allocator_type{} };
	checkHeader(in, delimiter);
	in.read(oldFileNameString.data(), oldFileNameString.size());
	patchData.oldFileName = filesystem::u8path(oldFileNameString);
	checkHeader(in, delimiter);

	auto newFileNameString = std::string{ readSizeType(in),{}, std::string::allocator_type{} };
	checkHeader(in, delimiter);
	in.read(newFileNameString.data(), newFileNameString.size());
	patchData.newFileName = filesystem::u8path(newFileNameString);
	checkHeader(in, delimiter);

	auto readUnsignedInt8Bit = [](std::istream& in) {
		auto result = unsigned{};
		in >> result;
		if (result > std::numeric_limits<std::uint8_t>::max())
		{
			throw std::domain_error{ "Input value too large" };
		}
		return static_cast<std::uint8_t>(result);
	};

	patchData.escapeData.toBeEscaped = readUnsignedInt8Bit(in);
	checkHeader(in, delimiter);
	patchData.escapeData.substituteCharacter = readUnsignedInt8Bit(in);
	checkHeader(in, delimiter);
	patchData.escapeData.escape = readUnsignedInt8Bit(in);
	checkHeader(in, delimiter);
	patchData.escapeData.escape2 = readUnsignedInt8Bit(in);
	checkHeader(in, delimiter);

	patchData.dataChunks.resize(readSizeType(in));
	checkHeader(in, delimiter);
	for (auto& chunk : patchData.dataChunks)
	{
		chunk.length = readFromCharArray<decltype(chunk.length)>(in);
		chunk.sourcePosition = readFromCharArray<decltype(chunk.sourcePosition)>(in);
		if (chunk.sourcePosition == static_cast<decltype(chunk.sourcePosition)>(-1))
		{
			chunk.data.resize(chunk.length);
			in.read(reinterpret_cast<char*>(chunk.data.data()), chunk.length);
		}
	}

	return patchData;
}

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

void writeNewFileContent(std::ostream& out, const std::vector<std::uint8_t>& escapedOldFile, const PatchData& patchData, std::size_t maxBufferSize)
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