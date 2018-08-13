#include <filesystem>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <cstdint>
#include "../ARPatcher/Escape.hpp"
#include "../ARPatcher/Patch.hpp"

namespace filesystem = std::filesystem;

template<typename ValueType>
std::vector<ValueType> readEntireFile(const filesystem::path& path)
{
	auto fileSize = filesystem::file_size(path);
	std::cerr << path << " file size: " << fileSize << std::endl;
	if (fileSize % sizeof(ValueType) != 0)
	{
		throw std::runtime_error{ "fileSize % sizeof(ValueType) != 0" };
	}

	auto buffer = std::vector<ValueType>{};
	buffer.resize(static_cast<std::size_t>(fileSize) / sizeof(ValueType));

	auto input = std::ifstream{ path, std::ios::binary };

	input.read(reinterpret_cast<char*>(buffer.data()), buffer.size() * sizeof(ValueType));
	auto bytesRead = input.gcount();
	if (bytesRead != fileSize)
	{
		throw std::runtime_error{ "bytesRead != fileSize" };
	}
	std::cerr << "File read, number of elements = " << buffer.size() << std::endl;
	buffer.shrink_to_fit();
	return buffer;
};


void printUsage()
{
	std::cerr << "Usage: " << std::endl;
	std::cerr << "Build new file from old file and index file:\n"
		<< "ARPatchApplier <index file name>\n\n";
	std::cerr << "Build multiple new files from old files and index files:\n"
		<< "ARPatchApplier <index file name 1> <index file name 2> <index file name 3>...\n\n";
	std::cerr << std::endl;
	std::cerr << "Surround file names with quotes (\") when they contain spaces." << std::endl;
	std::cerr << "Press Enter to exit." << std::endl;
	std::cin.get();
}

int main(int argc, char* argv[])
{
	try
	{
		std::cerr << "Patch tool based on SDSL Compressed Suffix Tree - Test version" << std::endl;
		std::cerr << "Contact lanyi <lanyi@ra3.moe>, or post a thread on RA3Bar <https://tieba.baidu.com/ra3> for any questions" << std::endl;
		if (argc < 2)
		{
			printUsage();
			return 1;
		}
		auto arguments = std::vector<std::string>{ argv + 1, argv + argc };
		std::cerr << "Arguments: " << std::endl;
		for (const auto& argument : arguments)
		{
			std::cerr << "\"" << argument << "\" ";
		}
		std::cerr << std::endl;


		constexpr auto maxBufferSize = 32 * 1024 * 1024;
		for (const auto& argument : arguments)
		{
			try
			{
				std::cerr << "Reading index file " << argument << "..." << std::endl;
				auto indexFileName = filesystem::path{ argument };
				auto patchData = readChunks(std::ifstream{ indexFileName, std::ifstream::binary });
				auto indexFileDirectory = indexFileName.parent_path();
				auto inputFileName = indexFileDirectory / patchData.oldFileName;
				auto outputFileName = indexFileDirectory / patchData.newFileName;
				std::cerr << "Index file read, trying to create new file "
					<< "[" << outputFileName << "] from [" << inputFileName << "]..." << std::endl;

				auto output = std::ofstream{ outputFileName, std::ofstream::binary };
				output.exceptions(output.exceptions() | output.badbit | output.failbit);
				writeNewFileContent(output, escape(readEntireFile<std::uint8_t>(inputFileName), patchData.escapeData), patchData, maxBufferSize);
				std::cerr << "Successfully created file " << outputFileName << "." << std::endl;
			}
			catch (const std::exception& e)
			{
				std::cerr << "ERROR: " << e.what() << std::endl << "This index file will be skipped." << std::endl;
			}
		}
	}
	catch (const std::exception& e)
	{
		std::cerr << "ERROR: " << e.what() << std::endl << "ARPatchApplier will now exit." << std::endl;
		throw;
	}

	return 0;
}