#pragma once
#include <cstdint>
#include <ratio>
#include <string>
#include <ostream>
#include <fstream>
#include <filesystem>

template<typename ValueType>
std::vector<ValueType> readEntireFile(const std::filesystem::path& path)
{
	auto fileSize = std::filesystem::file_size(path);
	std::cerr << "Reading file " << path << "; file size: " << fileSize << std::endl;
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


template<typename N>
struct Suffix {

	friend std::ostream& operator<<(std::ostream& out, const Suffix& prefix)
	{
		return out << prefix.value << prefix.suffix;
	}

	N value;
	std::string suffix;
};

template<typename X, typename... Y>
Suffix(X, Y...)->Suffix<X>;

template<typename RatioType, typename ResultType = std::intmax_t>
constexpr ResultType evaluateRatio()
{
	return static_cast<ResultType>(RatioType::num) / RatioType::den;
}

template<typename T>
Suffix<double> makeMetricPrefix(T size)
{
	if (size >= evaluateRatio<std::giga, T>())
	{
		return Suffix{ size / evaluateRatio<std::giga, double>(), "G" };
	}
	else if (size >= evaluateRatio<std::mega, T>())
	{
		return Suffix{ size / evaluateRatio<std::mega, double>(), "M" };
	}
	else if (size >= evaluateRatio<std::kilo, T>())
	{
		return Suffix{ size / evaluateRatio<std::kilo, double>(), "k" };
	}
	return Suffix{ static_cast<double>(size) };
};

template<typename T, typename U>
Suffix<double> makePercent(T numerator, U denominator)
{
	return Suffix{ numerator / evaluateRatio<std::centi, double>() / denominator, "%" };
};