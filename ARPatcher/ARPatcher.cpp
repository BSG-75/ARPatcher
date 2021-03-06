// ARPatcher.cpp: 定义控制台应用程序的入口点。
//

#include "stdafx.h"

#include <filesystem>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <algorithm>
#include <execution>
#include <chrono>

#pragma warning(push)  
#pragma warning(disable: 4146)  
#pragma warning(disable: 4244)
#pragma warning(disable: 4267)
#include <sdsl/suffix_trees.hpp>
#pragma warning(pop)   

#pragma comment(lib, "sdsl.lib")
#pragma comment(lib, "divsufsort.lib")
#pragma comment(lib, "divsufsort64.lib")

#include "Utilities.hpp"
#include "Escape.hpp"
#include "Patch.hpp"

namespace filesystem = std::filesystem;
namespace chrono = std::chrono;

template<typename ForwardIterator>
std::pair<sdsl::int_vector<8>, EscapeData> getEscapedIntVector(ForwardIterator begin, ForwardIterator end, EscapeData escapeData)
{
	auto buffer = std::vector<std::uint8_t>{ begin, end };
	if (buffer.size() != std::distance(begin, end))
	{
		throw std::logic_error{ "buffer.size() != std::distance(begin, end)" };
	}
	escapeData.recalculateEstimatedNewSize(buffer);
	auto escaped = escape(buffer, escapeData);
	auto result = sdsl::int_vector<8>{};
	result.resize(escaped.size());
	std::copy(escaped.begin(), escaped.end(), result.begin());
	return { result, escapeData };
}

template<typename ForwardIterator>
std::pair<std::size_t, std::size_t> bestMatch(const sdsl::cst_sct3<>& cst, const sdsl::int_vector<8>& str, ForwardIterator substringBegin, ForwardIterator substringEnd)
{
	auto begin = std::size_t{ 0 };
	auto node = cst.root();
	auto iterator = substringBegin;
	while (iterator < substringEnd)
	{
		node = cst.child(node, *iterator);
		if (node == cst.root())
		{
			break;
		}
		begin = cst.csa[cst.lb(node)];

		auto depth = cst.depth(node);

		for (auto i = static_cast<std::size_t>(iterator - substringBegin);
			i < std::min(depth, str.size() - begin) && iterator < substringEnd;
			++i, ++iterator)
		{
			if (*iterator != str[begin + i])
			{
				goto endLoop;
			}
		}
	}
endLoop:
	return std::make_pair(begin, begin + std::distance(substringBegin, iterator));
};


bool verify(std::istream& indexFileContent)
{
	auto patchData = readChunks(indexFileContent);
	auto newFile = getNewFileContent(escape(readEntireFile<std::uint8_t>(patchData.oldFileName), patchData.escapeData), patchData);
	std::cerr << "Generated new file content size: " << newFile.size() << std::endl;
	auto newFileAgain = readEntireFile<std::uint8_t>(patchData.newFileName);
	std::cerr << "FINAL RESULT: " << std::boolalpha << (newFileAgain == newFile) << std::endl;
	return newFileAgain == newFile;
}

struct Section {
	std::size_t index;
	std::size_t offset;
	sdsl::int_vector<8> data;
	sdsl::cst_sct3<> cst;
};

struct CSTs {
	EscapeData escapeData;
	std::vector<Section> sections;
};

CSTs treesFromEscapedFile(const filesystem::path& fileName, std::size_t maxSingleBufferSize)
{
	auto result = CSTs{};

	{
		const auto oldFile = readEntireFile<std::uint8_t>(fileName);
		std::cerr << "Old file read, calculating escaped size...\r";
		result.escapeData = findBestEscape(oldFile, 0);
		std::cerr << "Estimated file size after escaping the null character: " << result.escapeData.estimatedNewSize << std::endl;
		if (result.escapeData.estimatedNewSize > std::numeric_limits<std::uint32_t>::max())
		{
			throw std::length_error{ "File too large!" };
		}

		{
			auto increment = maxSingleBufferSize;
			for (auto begin = oldFile.begin(); begin < oldFile.end(); begin += increment)
			{
				increment = std::min(maxSingleBufferSize, static_cast<std::size_t>(oldFile.end() - begin));
				auto offset = std::size_t{ 0 };
				if (result.sections.empty() == false)
				{
					offset = result.sections.back().offset + result.sections.back().data.size();
				}

				result.sections.emplace_back();
				auto& section = result.sections.back();
				section.offset = offset;
				section.index = result.sections.size() - 1;
				section.data = getEscapedIntVector(begin, begin + increment, result.escapeData).first;
				std::cerr << "Constructing CST for old file section #" << result.sections.size() << " (" << makeMetricPrefix(section.data.size()) << "B)...\r";
				sdsl::construct_im(section.cst, section.data);
				std::cerr << "Constructed CST for old file section #" << result.sections.size() << " (" << makeMetricPrefix(section.data.size()) << "B).   " << std::endl;
			}
		}
	}

	return result;
}

void generateIndexFile(const filesystem::path& oldFileName,
	const filesystem::path& newFileName,
	const filesystem::path& indexFileName,
	std::size_t maxSingleBufferSize, double minumChunkFactor)
{
	constexpr auto initialPessimisticCounter = -3;

	auto indexFileContent = std::stringstream{};
	indexFileContent.exceptions(indexFileContent.exceptions() | indexFileContent.badbit | indexFileContent.failbit);

	auto chunks = std::vector<DataChunk>{};
	auto newBytesCount = std::size_t{ 0 };

	{
		auto trees = treesFromEscapedFile(oldFileName, maxSingleBufferSize);

		auto escapedNewFile = escape(readEntireFile<std::uint8_t>(newFileName), trees.escapeData);
		std::cerr << "New file escaped size = " << escapedNewFile.size() << std::endl;
		std::cerr << "New file processed, starting to search for common substrings..." << std::endl;

		auto iterator = escapedNewFile.begin();
		auto minimumChunkSize = std::max(DataChunk::lowestReferencedBytesCount, static_cast<std::size_t>(escapedNewFile.size() * minumChunkFactor));
		auto pessimisticCounter = initialPessimisticCounter;
		auto previousTime = chrono::system_clock::now();
		while (iterator < escapedNewFile.end())
		{
			auto results = std::vector<std::pair<std::size_t, std::size_t>>{ trees.sections.size() };

			std::for_each(std::execution::par_unseq, trees.sections.begin(), trees.sections.end(),
				[&results, iterator, endFile = escapedNewFile.end()](const Section& section) {
				auto[localBegin, localEnd] = bestMatch(section.cst, section.data, iterator, endFile);
				results.at(section.index) = std::make_pair(localBegin + section.offset, localEnd + section.offset);
			});

			auto[begin, end] = *std::max_element(results.begin(), results.end(), [](const auto& pair1, const auto& pair2) {
				return (pair1.second - pair1.first) < (pair2.second - pair2.first);
			});
			auto length = end - begin;

			auto data = std::vector<std::uint8_t>{};
			if (length < minimumChunkSize)
			{
				pessimisticCounter += std::max(1, pessimisticCounter / 2);
				end += std::max(1, pessimisticCounter) * minimumChunkSize;
				if (end - begin > static_cast<std::size_t>(escapedNewFile.end() - iterator))
				{
					end = begin + (escapedNewFile.end() - iterator);
				}
				length = end - begin;
				data.resize(length);
				std::copy(iterator, iterator + length, data.begin());
				newBytesCount += length;
				begin = static_cast<std::size_t>(-1);
			}
			else
			{
				pessimisticCounter = initialPessimisticCounter;
			}

			chunks.emplace_back(length, begin, std::move(data));

			iterator += length;

			//print progress
			if (chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now() - previousTime).count() > 300)
			{
				previousTime = chrono::system_clock::now();
				auto bytes = iterator - escapedNewFile.begin();
				std::cerr << "Processed " << makeMetricPrefix(bytes) << "B, (" << makePercent(bytes, escapedNewFile.size()) << ")......\r";
			}
		}

		auto estimatedIndexFileSize = chunks.size() * 2 * sizeof(std::uint32_t) + newBytesCount;
		std::cerr << "Search ended, index file size = " << makeMetricPrefix(estimatedIndexFileSize) << 'B' << std::endl
			<< "Of which new data = " << makeMetricPrefix(newBytesCount) << "B; "
			<< "indexes = " << makeMetricPrefix(estimatedIndexFileSize - newBytesCount) << 'B' << std::endl;

		writeChunks(indexFileContent, PatchData{ latestPatchDataVersion, oldFileName, newFileName, trees.escapeData, std::move(chunks) });
		std::cerr << "Index data generated..." << std::endl;
	}

	std::cerr << "Verifying generated index data..." << std::endl;
	if (verify(indexFileContent) == false)
	{
		throw std::runtime_error{ "Failed to reconstruct the new file from the index file!" };
	}

	std::cerr << "Index data has been verified. Writing index data to disk..." << std::endl;
	auto indexFile = std::ofstream{ indexFileName, std::ofstream::binary };
	indexFile.exceptions(indexFile.exceptions() | indexFile.badbit | indexFile.failbit);
	indexFile << indexFileContent.str();
	std::cerr << "Successfully created index file " << indexFileName << std::endl;
}


void printUsage()
{
	std::cerr << "Usage: " << std::endl;
	std::cerr << "Generate index file:\n"
		<< "ARPatcher -generateIndexFile <old file name> <new file name> <output index file name> <max single buffer size> <minumum chunk factor>\n"
		<< "- max single buffer size: positive integer (0 or negative value indicates 'no limit') in MiB\n"
		<< "\t(for example '2' means 2 MiB = 2048 KiB), higher is better (smaller index file),\n"
		<< "\tbut requires more RAM to generate index.\n"
		<< "- minumum chunk factor: decimal number, usually 0.000001, lower is better (smaller index file)\n"
		<< "\tbut it will take more time to generate index.\n\n";
	std::cerr << "Build new file from old file and index file:\n"
		<< "ARPatcher -buildNewFile <index file name>\n\n";
	std::cerr << "Build new file from old file and index file (low memory usage version):\n"
		<< "ARPatcher -buildNewFileLow <index file name> <max buffer size (in bytes)>\n\n";
	std::cerr << std::endl;
	std::cerr << "Surround file names with quotes (\") when they contain spaces." << std::endl;
	std::cerr << "Press Enter to exit." << std::endl;
	std::cin.get();
}

int main(int argc, char* argv[])
{
	try
	{
		std::cerr << "Patch tool based on SDSL Compressed Suffix Tree - Test version" << std::endl
			<< "Contact lanyi <lanyi@ra3.moe>, or post a thread on RA3Bar <https://tieba.baidu.com/ra3> for any quesitons" << std::endl
			<< "The source code is available on Github: https://github.com/BSG-75/ARPatcher/" << std::endl;
		auto arguments = std::vector<std::string>{ argv, argv + argc };
		if (arguments.size() < 3)
		{
			printUsage();
			return 1;
		}
		auto mode = arguments.at(1);
		std::transform(mode.begin(), mode.end(), mode.begin(), ::tolower);
		if (mode == "-generateindexfile")
		{
			auto oldFileName = filesystem::path{};
			auto newFileName = filesystem::path{};
			auto indexFileName = filesystem::path{};
			auto maxSingleBufferSize = std::size_t{};
			auto minimunChunkFactor = double{};
			try
			{
				oldFileName = arguments.at(2);
				newFileName = arguments.at(3);
				indexFileName = arguments.at(4);
				auto inputBufferSize = std::stoi(arguments.at(5));
				if (inputBufferSize > 0)
				{
					maxSingleBufferSize = static_cast<std::size_t>(inputBufferSize) * 1024 * 1024;
				}
				else
				{
					maxSingleBufferSize = static_cast<std::size_t>(filesystem::file_size(oldFileName));
				}
				minimunChunkFactor = std::stod(arguments.at(6));
			}
			catch (const std::exception&)
			{
				printUsage();
				return 1;
			}
			std::cerr << "Parameters: oldFile " << oldFileName << "; newFile " << newFileName << "; outputIndexFile " << indexFileName << '\n'
				<< "\tMax single buffer size " << makeMetricPrefix(maxSingleBufferSize) << "B; minimum chunk factor " << minimunChunkFactor << std::endl;
			generateIndexFile(oldFileName, newFileName, indexFileName, maxSingleBufferSize, minimunChunkFactor);
		}
		else if (mode == "-buildnewfile")
		{
			auto indexFileName = filesystem::path{};
			try
			{
				indexFileName = arguments.at(2);
			}
			catch (const std::exception&)
			{
				printUsage();
				return 1;
			}
			auto patchData = readChunks(std::ifstream{ indexFileName, std::ifstream::binary });
			std::cerr << "Index file read, new file name = " << patchData.newFileName << std::endl;
			auto buffer = getNewFileContent(escape(readEntireFile<std::uint8_t>(patchData.oldFileName), patchData.escapeData), patchData);
			std::cerr << "New file content successfully created, writing new file content to disk..." << std::endl;
			auto output = std::ofstream{ patchData.newFileName, std::ofstream::binary };
			output.exceptions(output.exceptions() | output.badbit | output.failbit);
			output.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
			std::cerr << "New file content successfully written to disk." << std::endl;
		}
		else if (mode == "-buildnewfilelow")
		{
			auto indexFileName = filesystem::path{};
			auto maxBufferSize = std::size_t{};
			try
			{
				indexFileName = arguments.at(2);
				maxBufferSize = std::stoul(arguments.at(3));
			}
			catch (const std::exception&)
			{
				printUsage();
				return 1;
			}
			auto patchData = readChunks(std::ifstream{ indexFileName, std::ifstream::binary });
			std::cerr << "Index file read, new file name = " << patchData.newFileName << std::endl;
			std::cerr << "Creating new file content from old file..." << std::endl;
			auto output = std::ofstream{ patchData.newFileName, std::ofstream::binary };
			output.exceptions(output.exceptions() | output.badbit | output.failbit);
			writeNewFileContent(output, escape(readEntireFile<std::uint8_t>(patchData.oldFileName), patchData.escapeData), patchData, maxBufferSize);
			std::cerr << "New file content successfully written to disk." << std::endl;
		}
		else
		{
			printUsage();
			return 1;
		}
		//constexpr auto minimumChunkPercent = 0.000001;
		//constexpr auto maxSingleBufferSize = 20'000'000;
	}
	catch (const std::exception& e)
	{
		std::cerr << "Error: " << e.what() << std::endl;
		return -1;
	}
	return 0;
}