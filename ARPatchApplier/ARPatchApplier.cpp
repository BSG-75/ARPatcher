#include <filesystem>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <cstdint>

#include "../ARPatcher/Utilities.hpp"
#include "../ARPatcher/Escape.hpp"
#include "../ARPatcher/Patch.hpp"

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
		std::cerr << "Patch tool based on SDSL Compressed Suffix Tree - Test version" << std::endl
			<< "Contact lanyi <lanyi@ra3.moe>, or post a thread on RA3Bar <https://tieba.baidu.com/ra3> for any questions" << std::endl
			<< "The source code is available on Github: https://github.com/BSG-75/ARPatcher/" << std::endl;
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
				auto indexFileName = std::filesystem::path{ argument };
				auto patchData = readChunks(std::ifstream{ indexFileName, std::ifstream::binary });
				auto indexFileDirectory = indexFileName.parent_path();
				auto inputFileName = indexFileDirectory / patchData.oldFileName;
				auto outputFileName = indexFileDirectory / patchData.newFileName;
				std::cerr << "Index file read, trying to create new file "
					<< "[" << outputFileName << "] from [" << inputFileName << "]..." << std::endl;

				auto output = std::ofstream{ outputFileName, std::ofstream::binary };
				output.exceptions(output.exceptions() | output.badbit | output.failbit);
				auto expectedSum = std::accumulate(patchData.dataChunks.begin(), patchData.dataChunks.end(), std::size_t{ 0 }, 
					[](std::size_t sum, const DataChunk& chunk) {
					return sum += chunk.length;
				});
				writeNewFileContent(output, 
					escape(readEntireFile<std::uint8_t>(inputFileName), patchData.escapeData), patchData, maxBufferSize,
					[expectedSum, progress = 0](std::size_t delta) mutable {
					progress += delta;
					std::cerr << makeMetricPrefix(progress) << "B (" << makePercent(progress, expectedSum) << ")          \r";
				});
				std::cerr << "Successfully created file " << outputFileName << ".      " << std::endl;
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